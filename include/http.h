#ifndef MINIHTTPD_HTTP_H
#define MINIHTTPD_HTTP_H

#include <stddef.h>

#define HTTP_MAX_REQUEST_SIZE 8192
#define HTTP_MAX_REQUEST_LINE 4096
#define HTTP_MAX_HEADER_LINE 1024
#define HTTP_MAX_HEADERS 64

#define HTTP_MAX_METHOD_SIZE 16
#define HTTP_MAX_URI_SIZE 2048
#define HTTP_MAX_VERSION_SIZE 16
#define HTTP_MAX_HOST_SIZE 256
#define HTTP_MAX_USER_AGENT_SIZE 512

#define HTTP_RESPONSE_BUFFER_SIZE 8192

typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500
} http_status_t;

typedef struct {
    char method[HTTP_MAX_METHOD_SIZE];
    char uri[HTTP_MAX_URI_SIZE];
    char version[HTTP_MAX_VERSION_SIZE];
    char host[HTTP_MAX_HOST_SIZE];
    char user_agent[HTTP_MAX_USER_AGENT_SIZE];
} http_request_t;

/*
 * Analiza una solicitud HTTP completa (cabecera incluida).
 * Retorna 200 si la solicitud es valida, 405 si el metodo no es GET
 * y 400 si la solicitud esta mal formada.
 */
http_status_t http_parse_request(const char *data, size_t length,
                                 http_request_t *request);

/*
 * Genera una respuesta HTTP en buffer.
 * Retorna la cantidad de bytes generados o -1 si no cabe en el buffer.
 */
int http_build_response(char *buffer, size_t capacity,
                        const http_request_t *request,
                        http_status_t status);

#endif
