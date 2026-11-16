#define _GNU_SOURCE

#include "server.h"
#include "http.h"
#include "files.h"
#include "mime.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    CONN_READING = 0,
    CONN_WRITING,
    CONN_DRAINING
} conn_state_t;

typedef struct {
    int fd;
    conn_state_t state;

    char in_buffer[HTTP_MAX_REQUEST_SIZE + 1];
    size_t in_length;

    char out_buffer[HTTP_RESPONSE_BUFFER_SIZE];
    size_t out_length;
    size_t out_sent;

    char file_buffer[SERVER_BUFFER_SIZE];
    size_t file_length;
    size_t file_sent;

    files_file_t file;
    int file_open;
    int is_file_response;
    int drain_after_write;
    http_status_t status;

    time_t last_activity;
    time_t drain_deadline;

    char client_ip[INET_ADDRSTRLEN];
    uint16_t client_port;
} connection_t;

static int create_listening_socket(uint16_t port);
static int set_nonblocking(int fd);
static int add_to_epoll(int epfd, int fd, uint32_t events,
                        connection_t *conn);
static int modify_epoll(int epfd, int fd, uint32_t events,
                        connection_t *conn);
static void accept_clients(int epfd, int listen_fd,
                           connection_t **table, size_t *count);
static int handle_readable(int epfd, connection_t *conn);
static int handle_writable(int epfd, connection_t *conn);
static int handle_draining(connection_t *conn);
static int prepare_response(int epfd, connection_t *conn);
static int prepare_error(int epfd, connection_t *conn, http_status_t status);
static int flush_connection(connection_t *conn);
static void close_connection(int epfd, connection_t **table, size_t *count,
                             connection_t *conn);
static void expire_connections(int epfd, connection_t **table, size_t *count);
static const char *find_header_end(const char *data, size_t length);
static http_status_t file_result_to_status(files_result_t result);
static int table_add(connection_t **table, size_t *count,
                     connection_t *conn);
static void table_remove(connection_t **table, size_t *count,
                         connection_t *conn);

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

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        return 1;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = NULL;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        perror("epoll_ctl ADD listen");
        close(epfd);
        close(listen_fd);
        return 1;
    }

    printf("MiniHTTPd escuchando en el puerto %u "
           "(epoll, max %d conexiones)\n",
           (unsigned)port, SERVER_MAX_CONNECTIONS);
    fflush(stdout);

    connection_t *connections[SERVER_MAX_CONNECTIONS];
    size_t connection_count = 0;
    struct epoll_event events[SERVER_MAX_EVENTS];
    int running = 1;

    while (running) {
        int ready = epoll_wait(epfd, events, SERVER_MAX_EVENTS,
                               SERVER_EPOLL_TIMEOUT_MS);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < ready; i++) {
            if (events[i].data.ptr == NULL) {
                if ((events[i].events & (EPOLLERR | EPOLLHUP)) != 0) {
                    fprintf(stderr, "Error en el socket de escucha\n");
                    running = 0;
                    break;
                }

                if ((events[i].events & EPOLLIN) != 0) {
                    accept_clients(epfd, listen_fd, connections,
                                   &connection_count);
                }
                continue;
            }

            connection_t *conn = events[i].data.ptr;
            uint32_t mask = events[i].events;

            if ((mask & (EPOLLERR | EPOLLHUP)) != 0) {
                close_connection(epfd, connections, &connection_count, conn);
                continue;
            }

            if (conn->state == CONN_READING &&
                (mask & (EPOLLIN | EPOLLRDHUP)) != 0) {
                if (handle_readable(epfd, conn) == 0) {
                    close_connection(epfd, connections,
                                     &connection_count, conn);
                    continue;
                }
            }

            if (conn->state == CONN_WRITING &&
                (mask & EPOLLOUT) != 0) {
                if (handle_writable(epfd, conn) == 0) {
                    close_connection(epfd, connections,
                                     &connection_count, conn);
                    continue;
                }
            }

            if (conn->state == CONN_DRAINING &&
                (mask & (EPOLLIN | EPOLLRDHUP)) != 0) {
                if (handle_draining(conn) == 0) {
                    close_connection(epfd, connections,
                                     &connection_count, conn);
                    continue;
                }
            }
        }

        if (running) {
            expire_connections(epfd, connections, &connection_count);
        }
    }

    while (connection_count > 0) {
        close_connection(epfd, connections, &connection_count,
                         connections[connection_count - 1]);
    }

    close(epfd);
    close(listen_fd);

    return 0;
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

    if (set_nonblocking(fd) != 0) {
        perror("fcntl listen");
        close(fd);
        return -1;
    }

    return fd;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    return 0;
}

static int add_to_epoll(int epfd, int fd, uint32_t events,
                        connection_t *conn)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = conn;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) < 0) {
        perror("epoll_ctl ADD");
        return -1;
    }

    return 0;
}

static int modify_epoll(int epfd, int fd, uint32_t events,
                        connection_t *conn)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = conn;

    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event) < 0) {
        perror("epoll_ctl MOD");
        return -1;
    }

    return 0;
}

static void accept_clients(int epfd, int listen_fd,
                           connection_t **table, size_t *count)
{
    for (;;) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);

        int fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            perror("accept");
            break;
        }

        if (set_nonblocking(fd) != 0) {
            perror("fcntl client");
            close(fd);
            continue;
        }

        if (*count >= SERVER_MAX_CONNECTIONS) {
            printf("Limite de conexiones alcanzado; conexion rechazada\n");
            fflush(stdout);
            close(fd);
            continue;
        }

        connection_t *conn = calloc(1, sizeof(*conn));
        if (conn == NULL) {
            perror("calloc");
            close(fd);
            continue;
        }

        conn->fd = fd;
        conn->state = CONN_READING;
        conn->file.fd = -1;
        conn->last_activity = time(NULL);
        conn->client_port = ntohs(addr.sin_port);

        if (inet_ntop(AF_INET, &addr.sin_addr,
                      conn->client_ip, sizeof(conn->client_ip)) == NULL) {
            snprintf(conn->client_ip, sizeof(conn->client_ip),
                     "%s", "desconocido");
        }

        if (add_to_epoll(epfd, fd, EPOLLIN | EPOLLRDHUP, conn) != 0) {
            close(fd);
            free(conn);
            continue;
        }

        if (table_add(table, count, conn) != 0) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            free(conn);
            continue;
        }

        printf("Cliente conectado: %s:%u\n",
               conn->client_ip, (unsigned)conn->client_port);
        fflush(stdout);
    }
}

static int handle_readable(int epfd, connection_t *conn)
{
    for (;;) {
        if (conn->in_length >= HTTP_MAX_REQUEST_SIZE) {
            printf("Solicitud rechazada -> %d (demasiado grande)\n",
                   (int)HTTP_STATUS_BAD_REQUEST);
            fflush(stdout);

            return prepare_error(epfd, conn,
                                 HTTP_STATUS_BAD_REQUEST) == 0;
        }

        ssize_t received = recv(conn->fd,
                                conn->in_buffer + conn->in_length,
                                HTTP_MAX_REQUEST_SIZE - conn->in_length, 0);

        if (received > 0) {
            conn->in_length += (size_t)received;
            conn->in_buffer[conn->in_length] = '\0';
            conn->last_activity = time(NULL);

            if (find_header_end(conn->in_buffer, conn->in_length) != NULL) {
                return prepare_response(epfd, conn) == 0;
            }

            continue;
        }

        if (received == 0) {
            if (conn->in_length == 0) {
                return 0;
            }

            printf("Solicitud rechazada -> %d (incompleta)\n",
                   (int)HTTP_STATUS_BAD_REQUEST);
            fflush(stdout);

            return prepare_error(epfd, conn,
                                 HTTP_STATUS_BAD_REQUEST) == 0;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }

        return 0;
    }
}

static int handle_writable(int epfd, connection_t *conn)
{
    int result = flush_connection(conn);

    if (result < 0) {
        return 0;
    }

    if (result == 0) {
        return 1;
    }

    if (conn->is_file_response) {
        printf("Archivo servido: %s (%s, %zu bytes)\n",
               conn->file.path,
               mime_type_from_path(conn->file.path),
               conn->file.size);
    } else {
        printf("Respuesta enviada -> %d\n", (int)conn->status);
    }
    fflush(stdout);

    /*
     * Tras una respuesta de error el cliente puede seguir enviando datos
     * (por ejemplo, el cuerpo de un POST o el resto de una solicitud
     * demasiado grande). Se cierra el lado de escritura y se descartan
     * esos datos para que el cierre no genere un RST que oculte la
     * respuesta al cliente.
     */
    if (conn->drain_after_write) {
        if (shutdown(conn->fd, SHUT_WR) < 0 &&
            errno != ENOTCONN) {
            return 0;
        }

        conn->state = CONN_DRAINING;
        conn->drain_deadline = time(NULL) + SERVER_DRAIN_TIMEOUT_SEC;

        return modify_epoll(epfd, conn->fd,
                            EPOLLIN | EPOLLRDHUP, conn) == 0;
    }

    return 0;
}

static int handle_draining(connection_t *conn)
{
    char buffer[1024];

    for (;;) {
        ssize_t received = recv(conn->fd, buffer, sizeof(buffer), 0);

        if (received > 0) {
            continue;
        }

        if (received == 0) {
            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }

        return 0;
    }
}

static int prepare_response(int epfd, connection_t *conn)
{
    http_request_t request;
    http_status_t status = http_parse_request(conn->in_buffer,
                                              conn->in_length,
                                              &request);

    if (status != HTTP_STATUS_OK) {
        if (status == HTTP_STATUS_METHOD_NOT_ALLOWED) {
            printf("Solicitud: %s %s %s -> %d\n",
                   request.method, request.uri, request.version,
                   (int)status);
        } else {
            printf("Solicitud rechazada -> %d\n", (int)status);
        }
        fflush(stdout);

        return prepare_error(epfd, conn, status);
    }

    files_result_t result = files_open(request.uri, &conn->file);
    if (result != FILES_OK) {
        status = file_result_to_status(result);

        printf("Solicitud: %s %s %s -> %d\n",
               request.method, request.uri, request.version, (int)status);
        fflush(stdout);

        return prepare_error(epfd, conn, status);
    }

    const char *content_type = mime_type_from_path(conn->file.path);

    int header_length = http_build_file_response_header(
        conn->out_buffer,
        sizeof(conn->out_buffer),
        HTTP_STATUS_OK,
        content_type,
        conn->file.size,
        conn->file.modified);

    if (header_length < 0) {
        files_close(&conn->file);

        printf("Solicitud: %s %s %s -> %d\n",
               request.method, request.uri, request.version,
               (int)HTTP_STATUS_INTERNAL_SERVER_ERROR);
        fflush(stdout);

        return prepare_error(epfd, conn,
                             HTTP_STATUS_INTERNAL_SERVER_ERROR);
    }

    conn->file_open = 1;
    conn->file_length = 0;
    conn->file_sent = 0;
    conn->out_length = (size_t)header_length;
    conn->out_sent = 0;
    conn->is_file_response = 1;
    conn->drain_after_write = 0;
    conn->status = HTTP_STATUS_OK;
    conn->state = CONN_WRITING;
    conn->last_activity = time(NULL);

    printf("Solicitud: %s %s %s -> %d\n",
           request.method, request.uri, request.version,
           (int)HTTP_STATUS_OK);
    fflush(stdout);

    return modify_epoll(epfd, conn->fd, EPOLLOUT, conn);
}

static int prepare_error(int epfd, connection_t *conn, http_status_t status)
{
    int length = http_build_error_response(conn->out_buffer,
                                           sizeof(conn->out_buffer),
                                           status);

    conn->out_length = length > 0 ? (size_t)length : 0;
    conn->out_sent = 0;
    conn->file_open = 0;
    conn->file_length = 0;
    conn->file_sent = 0;
    conn->is_file_response = 0;
    conn->drain_after_write = 1;
    conn->status = status;
    conn->state = CONN_WRITING;
    conn->last_activity = time(NULL);

    return modify_epoll(epfd, conn->fd, EPOLLOUT, conn);
}

static int flush_connection(connection_t *conn)
{
    while (conn->out_sent < conn->out_length) {
        ssize_t sent = send(conn->fd,
                            conn->out_buffer + conn->out_sent,
                            conn->out_length - conn->out_sent, 0);

        if (sent > 0) {
            conn->out_sent += (size_t)sent;
            conn->last_activity = time(NULL);
            continue;
        }

        if (sent < 0 && errno == EINTR) {
            continue;
        }

        if (sent < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }

        return -1;
    }

    while (conn->file_open) {
        if (conn->file_sent >= conn->file_length) {
            ssize_t received = files_read(&conn->file,
                                          conn->file_buffer,
                                          sizeof(conn->file_buffer));

            if (received == 0) {
                files_close(&conn->file);
                conn->file_open = 0;
                conn->file_length = 0;
                conn->file_sent = 0;
                break;
            }

            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }

                return -1;
            }

            conn->file_length = (size_t)received;
            conn->file_sent = 0;
        }

        while (conn->file_sent < conn->file_length) {
            ssize_t sent = send(conn->fd,
                                conn->file_buffer + conn->file_sent,
                                conn->file_length - conn->file_sent, 0);

            if (sent > 0) {
                conn->file_sent += (size_t)sent;
                conn->last_activity = time(NULL);
                continue;
            }

            if (sent < 0 && errno == EINTR) {
                continue;
            }

            if (sent < 0 &&
                (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return 0;
            }

            return -1;
        }
    }

    return 1;
}

static void close_connection(int epfd, connection_t **table, size_t *count,
                             connection_t *conn)
{
    if (conn == NULL) {
        return;
    }

    files_close(&conn->file);
    epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, NULL);
    close(conn->fd);

    printf("Cliente desconectado: %s:%u\n",
           conn->client_ip, (unsigned)conn->client_port);
    fflush(stdout);

    table_remove(table, count, conn);
    free(conn);
}

static void expire_connections(int epfd, connection_t **table, size_t *count)
{
    time_t now = time(NULL);
    size_t i = 0;

    while (i < *count) {
        connection_t *conn = table[i];

        if (conn->state == CONN_DRAINING) {
            if (now < conn->drain_deadline) {
                i++;
                continue;
            }

            printf("Conexion expirada: %s:%u\n",
                   conn->client_ip, (unsigned)conn->client_port);
            fflush(stdout);

            close_connection(epfd, table, count, conn);
            continue;
        }

        if (now - conn->last_activity < SERVER_CLIENT_TIMEOUT_SEC) {
            i++;
            continue;
        }

        /*
         * Si el cliente alcanzo a enviar parte de la solicitud, se le
         * responde 400 antes de cerrar. Si no envio nada o se quedo
         * bloqueado escribiendo, se cierra la conexion directamente.
         */
        if (conn->state == CONN_READING && conn->in_length > 0) {
            printf("Solicitud rechazada -> %d (tiempo de espera agotado)\n",
                   (int)HTTP_STATUS_BAD_REQUEST);
            fflush(stdout);

            if (prepare_error(epfd, conn,
                              HTTP_STATUS_BAD_REQUEST) == 0) {
                conn->last_activity = now;
                i++;
                continue;
            }
        }

        printf("Conexion expirada: %s:%u\n",
               conn->client_ip, (unsigned)conn->client_port);
        fflush(stdout);

        close_connection(epfd, table, count, conn);
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

static int table_add(connection_t **table, size_t *count,
                     connection_t *conn)
{
    if (*count >= SERVER_MAX_CONNECTIONS) {
        return -1;
    }

    table[*count] = conn;
    (*count)++;

    return 0;
}

static void table_remove(connection_t **table, size_t *count,
                         connection_t *conn)
{
    for (size_t i = 0; i < *count; i++) {
        if (table[i] == conn) {
            table[i] = table[*count - 1];
            (*count)--;
            return;
        }
    }
}
