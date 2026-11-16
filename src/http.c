#define _POSIX_C_SOURCE 200809L

#include "http.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int is_token_char(unsigned char c);
static int is_token(const char *data, size_t length);
static int is_valid_uri(const char *data, size_t length);
static int is_valid_version(const char *data, size_t length);
static int is_valid_header_value(const char *data, size_t length);
static int header_name_equals(const char *name, size_t length,
                              const char *expected);
static int copy_header_value(char *dest, size_t dest_size,
                             const char *value, size_t value_length);
static const char *http_reason_phrase(http_status_t status);
static int http_format_date(time_t when, char *buffer, size_t capacity);
static int http_build_head(char *buffer, size_t capacity,
                           http_status_t status,
                           const char *content_type,
                           size_t content_length,
                           time_t last_modified,
                           const char *extra_headers);

http_status_t http_parse_request(const char *data, size_t length,
                                 http_request_t *request)
{
    if (data == NULL || request == NULL || length == 0) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    memset(request, 0, sizeof(*request));

    const char *line_end = memchr(data, '\r', length);
    if (line_end == NULL || line_end + 1 >= data + length ||
        line_end[1] != '\n') {
        return HTTP_STATUS_BAD_REQUEST;
    }

    size_t request_line_length = (size_t)(line_end - data);
    if (request_line_length == 0 ||
        request_line_length > HTTP_MAX_REQUEST_LINE) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    const char *method_end = memchr(data, ' ', request_line_length);
    if (method_end == NULL || method_end == data) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    size_t method_length = (size_t)(method_end - data);
    if (method_length >= sizeof(request->method) ||
        !is_token(data, method_length)) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    memcpy(request->method, data, method_length);
    request->method[method_length] = '\0';

    const char *uri_start = method_end + 1;
    size_t uri_search_length = (size_t)(line_end - uri_start);
    const char *uri_end = memchr(uri_start, ' ', uri_search_length);

    if (uri_end == NULL || uri_end == uri_start) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    size_t uri_length = (size_t)(uri_end - uri_start);
    if (uri_length >= sizeof(request->uri) ||
        !is_valid_uri(uri_start, uri_length)) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    memcpy(request->uri, uri_start, uri_length);
    request->uri[uri_length] = '\0';

    const char *version_start = uri_end + 1;
    size_t version_length = (size_t)(line_end - version_start);
    if (version_length == 0 ||
        version_length >= sizeof(request->version) ||
        !is_valid_version(version_start, version_length)) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    memcpy(request->version, version_start, version_length);
    request->version[version_length] = '\0';

    const char *cursor = line_end + 2;
    const char *end = data + length;
    int header_count = 0;
    int host_seen = 0;

    while (cursor < end) {
        const char *header_end = memchr(cursor, '\r',
                                        (size_t)(end - cursor));

        if (header_end == NULL || header_end + 1 >= end ||
            header_end[1] != '\n') {
            return HTTP_STATUS_BAD_REQUEST;
        }

        size_t header_length = (size_t)(header_end - cursor);

        if (header_length == 0) {
            if (strcmp(request->method, "GET") != 0) {
                return HTTP_STATUS_METHOD_NOT_ALLOWED;
            }

            /*
             * HTTP/1.1 exige el encabezado Host. Rechazar su ausencia
             * evita solicitudes ambiguas.
             */
            if (strcmp(request->version, "HTTP/1.1") == 0 &&
                request->host[0] == '\0') {
                return HTTP_STATUS_BAD_REQUEST;
            }

            return HTTP_STATUS_OK;
        }

        if (header_length > HTTP_MAX_HEADER_LINE ||
            cursor[0] == ' ' || cursor[0] == '\t') {
            return HTTP_STATUS_BAD_REQUEST;
        }

        const char *colon = memchr(cursor, ':', header_length);
        if (colon == NULL || colon == cursor) {
            return HTTP_STATUS_BAD_REQUEST;
        }

        size_t name_length = (size_t)(colon - cursor);
        if (!is_token(cursor, name_length)) {
            return HTTP_STATUS_BAD_REQUEST;
        }

        const char *value = colon + 1;
        size_t value_length = header_length - name_length - 1;
        if (!is_valid_header_value(value, value_length)) {
            return HTTP_STATUS_BAD_REQUEST;
        }

        if (header_name_equals(cursor, name_length, "Host")) {
            if (host_seen) {
                return HTTP_STATUS_BAD_REQUEST;
            }

            host_seen = 1;

            if (copy_header_value(request->host, sizeof(request->host),
                                  value, value_length) != 0) {
                return HTTP_STATUS_BAD_REQUEST;
            }
        } else if (header_name_equals(cursor, name_length, "User-Agent")) {
            if (copy_header_value(request->user_agent,
                                  sizeof(request->user_agent),
                                  value, value_length) != 0) {
                return HTTP_STATUS_BAD_REQUEST;
            }
        } else if (header_name_equals(cursor, name_length, "Connection")) {
            if (copy_header_value(request->connection,
                                  sizeof(request->connection),
                                  value, value_length) != 0) {
                return HTTP_STATUS_BAD_REQUEST;
            }
        } else if (header_name_equals(cursor, name_length,
                                      "Transfer-Encoding")) {
            /*
             * El servidor no procesa cuerpos de solicitud, por lo que
             * no admite Transfer-Encoding.
             */
            return HTTP_STATUS_BAD_REQUEST;
        }

        header_count++;
        if (header_count > HTTP_MAX_HEADERS) {
            return HTTP_STATUS_BAD_REQUEST;
        }

        cursor = header_end + 2;
    }

    return HTTP_STATUS_BAD_REQUEST;
}

static int is_token_char(unsigned char c)
{
    if (c <= 0x20 || c == 0x7f || c >= 0x80) {
        return 0;
    }

    switch (c) {
        case '(':
        case ')':
        case '<':
        case '>':
        case '@':
        case ',':
        case ';':
        case ':':
        case '\\':
        case '"':
        case '/':
        case '[':
        case ']':
        case '?':
        case '=':
        case '{':
        case '}':
            return 0;
        default:
            return 1;
    }
}

static int is_token(const char *data, size_t length)
{
    if (length == 0) {
        return 0;
    }

    for (size_t i = 0; i < length; i++) {
        if (!is_token_char((unsigned char)data[i])) {
            return 0;
        }
    }

    return 1;
}

static int is_valid_uri(const char *data, size_t length)
{
    if (length == 0 || data[0] != '/') {
        return 0;
    }

    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];

        if (c <= 0x20 || c == 0x7f || c == '#') {
            return 0;
        }
    }

    return 1;
}

static int is_valid_version(const char *data, size_t length)
{
    return (length == 8 && memcmp(data, "HTTP/1.1", 8) == 0) ||
           (length == 8 && memcmp(data, "HTTP/1.0", 8) == 0);
}

static int is_valid_header_value(const char *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];

        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return 0;
        }
    }

    return 1;
}

static int header_name_equals(const char *name, size_t length,
                              const char *expected)
{
    size_t expected_length = strlen(expected);

    if (length != expected_length) {
        return 0;
    }

    for (size_t i = 0; i < length; i++) {
        unsigned char a = (unsigned char)name[i];
        unsigned char b = (unsigned char)expected[i];

        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b - 'A' + 'a');
        }

        if (a != b) {
            return 0;
        }
    }

    return 1;
}

static int copy_header_value(char *dest, size_t dest_size,
                             const char *value, size_t value_length)
{
    while (value_length > 0 &&
           (*value == ' ' || *value == '\t')) {
        value++;
        value_length--;
    }

    while (value_length > 0 &&
           (value[value_length - 1] == ' ' ||
            value[value_length - 1] == '\t')) {
        value_length--;
    }

    if (value_length >= dest_size) {
        return -1;
    }

    memcpy(dest, value, value_length);
    dest[value_length] = '\0';

    return 0;
}

static const char *http_reason_phrase(http_status_t status)
{
    switch (status) {
        case HTTP_STATUS_OK:
            return "OK";
        case HTTP_STATUS_BAD_REQUEST:
            return "Bad Request";
        case HTTP_STATUS_FORBIDDEN:
            return "Forbidden";
        case HTTP_STATUS_NOT_FOUND:
            return "Not Found";
        case HTTP_STATUS_METHOD_NOT_ALLOWED:
            return "Method Not Allowed";
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
            return "Internal Server Error";
        default:
            return "Error";
    }
}

static int http_format_date(time_t when, char *buffer, size_t capacity)
{
    struct tm value;

    if (buffer == NULL || capacity == 0) {
        return -1;
    }

    if (gmtime_r(&when, &value) == NULL) {
        return -1;
    }

    if (strftime(buffer, capacity, "%a, %d %b %Y %H:%M:%S GMT",
                 &value) == 0) {
        return -1;
    }

    return 0;
}

static int http_build_head(char *buffer, size_t capacity,
                           http_status_t status,
                           const char *content_type,
                           size_t content_length,
                           time_t last_modified,
                           const char *extra_headers)
{
    if (buffer == NULL || capacity == 0 || content_type == NULL) {
        return -1;
    }

    char date[HTTP_DATE_SIZE];
    if (http_format_date(time(NULL), date, sizeof(date)) != 0) {
        return -1;
    }

    char last_modified_header[HTTP_DATE_SIZE + 24];
    last_modified_header[0] = '\0';

    if (last_modified > 0) {
        char modified[HTTP_DATE_SIZE];

        if (http_format_date(last_modified, modified,
                             sizeof(modified)) != 0) {
            return -1;
        }

        int written = snprintf(last_modified_header,
                               sizeof(last_modified_header),
                               "Last-Modified: %s\r\n", modified);
        if (written < 0 ||
            (size_t)written >= sizeof(last_modified_header)) {
            return -1;
        }
    }

    int written = snprintf(
        buffer, capacity,
        "HTTP/1.1 %d %s\r\n"
        "Server: %s\r\n"
        "Date: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "%s"
        "\r\n",
        (int)status,
        http_reason_phrase(status),
        HTTP_SERVER_NAME,
        date,
        content_type,
        content_length,
        last_modified_header,
        extra_headers != NULL ? extra_headers : "");

    if (written < 0 || (size_t)written >= capacity) {
        return -1;
    }

    return written;
}

int http_build_error_response(char *buffer, size_t capacity,
                              http_status_t status)
{
    if (buffer == NULL || capacity == 0) {
        return -1;
    }

    const char *reason = http_reason_phrase(status);
    char body[2048];

    int body_length = snprintf(
        body, sizeof(body),
        "<!DOCTYPE html>\n"
        "<html lang=\"es\">\n"
        "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<title>%d %s</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>%d %s</h1>\n"
        "<p>MiniHTTPd no pudo procesar la solicitud.</p>\n"
        "<hr>\n"
        "<p>%s</p>\n"
        "</body>\n"
        "</html>\n",
        (int)status, reason,
        (int)status, reason,
        HTTP_SERVER_NAME);

    if (body_length < 0 || (size_t)body_length >= sizeof(body)) {
        return -1;
    }

    const char *extra_headers =
        status == HTTP_STATUS_METHOD_NOT_ALLOWED ? "Allow: GET\r\n" : "";

    int head_length = http_build_head(buffer, capacity, status,
                                      "text/html; charset=utf-8",
                                      (size_t)body_length, 0,
                                      extra_headers);
    if (head_length < 0) {
        return -1;
    }

    if ((size_t)head_length + (size_t)body_length >= capacity) {
        return -1;
    }

    memcpy(buffer + head_length, body, (size_t)body_length);

    return head_length + body_length;
}

int http_build_file_response_header(char *buffer, size_t capacity,
                                    http_status_t status,
                                    const char *content_type,
                                    size_t content_length,
                                    time_t last_modified)
{
    return http_build_head(buffer, capacity, status, content_type,
                           content_length, last_modified, NULL);
}
