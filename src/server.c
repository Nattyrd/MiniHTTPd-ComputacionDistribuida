#include "server.h"
#include "http.h"
#include "files.h"
#include "mime.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int create_listening_socket(uint16_t port);
static void handle_client(int client_fd, const struct sockaddr_in *client_addr);
static void configure_client_timeouts(int client_fd);
static int read_http_request(int fd, char *buffer, size_t capacity,
                             size_t *length);
static const char *find_header_end(const char *data, size_t length);
static http_status_t serve_file(int client_fd,
                                const http_request_t *request);
static http_status_t file_result_to_status(files_result_t result);
static void send_error_response(int client_fd, http_status_t status);
static void drain_client(int client_fd);
static int send_all(int fd, const char *data, size_t length);

int server_run(uint16_t port)
{
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    int listen_fd = create_listening_socket(port);
    if (listen_fd < 0) {
        return 1;
    }

    printf("MiniHTTPd escuchando en el puerto %u\n", (unsigned)port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("accept");
            close(listen_fd);
            return 1;
        }

        handle_client(client_fd, &client_addr);
        close(client_fd);
    }
}

static int create_listening_socket(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, SERVER_BACKLOG) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

static void configure_client_timeouts(int client_fd)
{
    struct timeval timeout;

    timeout.tv_sec = SERVER_CLIENT_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
    }

    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_SNDTIMEO");
    }
}

static void handle_client(int client_fd, const struct sockaddr_in *client_addr)
{
    char client_ip[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &client_addr->sin_addr,
                  client_ip, sizeof(client_ip)) == NULL) {
        snprintf(client_ip, sizeof(client_ip), "%s", "desconocido");
    }

    printf("Cliente conectado: %s:%u\n",
           client_ip,
           (unsigned)ntohs(client_addr->sin_port));
    fflush(stdout);

    configure_client_timeouts(client_fd);

    char request_buffer[HTTP_MAX_REQUEST_SIZE + 1];
    size_t request_length = 0;
    int read_result = read_http_request(client_fd, request_buffer,
                                        sizeof(request_buffer),
                                        &request_length);

    if (read_result == 1) {
        http_request_t request;
        http_status_t status = http_parse_request(request_buffer,
                                                  request_length,
                                                  &request);

        if (status == HTTP_STATUS_OK) {
            status = serve_file(client_fd, &request);
            printf("Solicitud: %s %s %s -> %d\n",
                   request.method,
                   request.uri,
                   request.version,
                   (int)status);
        } else {
            send_error_response(client_fd, status);
            printf("Solicitud rechazada -> %d\n", (int)status);
        }
        fflush(stdout);
    } else if (read_result == -2) {
        printf("Solicitud rechazada -> %d (demasiado grande)\n",
               (int)HTTP_STATUS_BAD_REQUEST);
        fflush(stdout);
        send_error_response(client_fd, HTTP_STATUS_BAD_REQUEST);
        shutdown(client_fd, SHUT_WR);
        drain_client(client_fd);
    } else if (read_result == 0 && request_length > 0) {
        printf("Solicitud rechazada -> %d (incompleta)\n",
               (int)HTTP_STATUS_BAD_REQUEST);
        fflush(stdout);
        send_error_response(client_fd, HTTP_STATUS_BAD_REQUEST);
    } else if (read_result == -1) {
        perror("recv");
    }

    printf("Cliente desconectado: %s:%u\n",
           client_ip,
           (unsigned)ntohs(client_addr->sin_port));
    fflush(stdout);
}

static int read_http_request(int fd, char *buffer, size_t capacity,
                             size_t *length)
{
    if (buffer == NULL || length == NULL || capacity < 2) {
        return -1;
    }

    *length = 0;

    for (;;) {
        if (*length >= capacity - 1) {
            return -2;
        }

        ssize_t received = recv(fd, buffer + *length,
                                capacity - 1 - *length, 0);
        if (received == 0) {
            return 0;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            /*
             * Si se agota el tiempo de espera, se trata como una
             * solicitud incompleta: el llamador responde 400 o cierra.
             */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }

            return -1;
        }

        *length += (size_t)received;
        buffer[*length] = '\0';

        if (find_header_end(buffer, *length) != NULL) {
            return 1;
        }
    }
}

static const char *find_header_end(const char *data, size_t length)
{
    if (length < 4) {
        return NULL;
    }

    for (size_t i = 0; i + 3 < length; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i + 4;
        }
    }

    return NULL;
}

static http_status_t serve_file(int client_fd,
                                const http_request_t *request)
{
    files_file_t file;
    files_result_t result = files_open(request->uri, &file);

    if (result != FILES_OK) {
        http_status_t status = file_result_to_status(result);
        send_error_response(client_fd, status);
        return status;
    }

    const char *content_type = mime_type_from_path(file.path);

    char header[HTTP_RESPONSE_BUFFER_SIZE];
    int header_length = http_build_file_response_header(
        header,
        sizeof(header),
        HTTP_STATUS_OK,
        content_type,
        file.size,
        file.modified);

    if (header_length < 0) {
        files_close(&file);
        send_error_response(client_fd, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    if (send_all(client_fd, header, (size_t)header_length) < 0) {
        perror("send");
        files_close(&file);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    char buffer[4096];

    for (;;) {
        ssize_t received = files_read(&file, buffer, sizeof(buffer));

        if (received == 0) {
            break;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("read");
            files_close(&file);
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }

        if (send_all(client_fd, buffer, (size_t)received) < 0) {
            perror("send");
            files_close(&file);
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
    }

    files_close(&file);

    printf("Archivo servido: %s (%s, %zu bytes)\n",
           file.path, content_type, file.size);
    fflush(stdout);

    return HTTP_STATUS_OK;
}

static http_status_t file_result_to_status(files_result_t result)
{
    switch (result) {
        case FILES_OK:
            return HTTP_STATUS_OK;
        case FILES_NOT_FOUND:
            return HTTP_STATUS_NOT_FOUND;
        case FILES_FORBIDDEN:
            return HTTP_STATUS_FORBIDDEN;
        case FILES_BAD_REQUEST:
            return HTTP_STATUS_BAD_REQUEST;
        case FILES_ERROR:
        default:
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
}

static void send_error_response(int client_fd, http_status_t status)
{
    char response[HTTP_RESPONSE_BUFFER_SIZE];
    int response_length = http_build_error_response(response,
                                                    sizeof(response),
                                                    status);

    if (response_length < 0) {
        return;
    }

    if (send_all(client_fd, response, (size_t)response_length) < 0) {
        perror("send");
    }
}

static void drain_client(int client_fd)
{
    char buffer[1024];
    struct timeval timeout;

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, sizeof(timeout));

    for (;;) {
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (received == 0) {
            break;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            break;
        }
    }
}

static int send_all(int fd, const char *data, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t result = send(fd, data + sent, length - sent, 0);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        if (result == 0) {
            return -1;
        }

        sent += (size_t)result;
    }

    return 0;
}
