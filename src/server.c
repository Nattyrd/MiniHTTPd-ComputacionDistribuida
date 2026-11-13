#include "server.h"
#include "http.h"

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
static int read_http_request(int fd, char *buffer, size_t capacity,
                             size_t *length);
static const char *find_header_end(const char *data, size_t length);
static void send_simple_response(int client_fd, http_status_t status);
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
        char response[HTTP_RESPONSE_BUFFER_SIZE];
        int response_length = http_build_response(response,
                                                  sizeof(response),
                                                  &request,
                                                  status);

        if (response_length < 0) {
            static const char fallback[] =
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";

            (void)send_all(client_fd, fallback, sizeof(fallback) - 1);
            status = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        } else if (send_all(client_fd, response,
                            (size_t)response_length) < 0) {
            perror("send");
        }

        if (status == HTTP_STATUS_OK) {
            printf("Solicitud: %s %s %s -> %d\n",
                   request.method,
                   request.uri,
                   request.version,
                   (int)status);
        } else {
            printf("Solicitud rechazada -> %d\n", (int)status);
        }
        fflush(stdout);
    } else if (read_result == -2) {
        printf("Solicitud rechazada -> %d (demasiado grande)\n",
               (int)HTTP_STATUS_BAD_REQUEST);
        fflush(stdout);
        send_simple_response(client_fd, HTTP_STATUS_BAD_REQUEST);
        shutdown(client_fd, SHUT_WR);
        drain_client(client_fd);
    } else if (read_result == 0 && request_length > 0) {
        printf("Solicitud rechazada -> %d (incompleta)\n",
               (int)HTTP_STATUS_BAD_REQUEST);
        fflush(stdout);
        send_simple_response(client_fd, HTTP_STATUS_BAD_REQUEST);
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

static void send_simple_response(int client_fd, http_status_t status)
{
    char response[HTTP_RESPONSE_BUFFER_SIZE];
    int response_length = http_build_response(response, sizeof(response),
                                              NULL, status);

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
