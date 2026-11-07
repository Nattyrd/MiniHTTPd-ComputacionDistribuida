#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int create_listening_socket(uint16_t port);
static void handle_client(int client_fd, const struct sockaddr_in *client_addr);
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

    char buffer[SERVER_BUFFER_SIZE];

    for (;;) {
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (received == 0) {
            break;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("recv");
            break;
        }

        if (send_all(client_fd, buffer, (size_t)received) < 0) {
            perror("send");
            break;
        }
    }

    printf("Cliente desconectado: %s:%u\n",
           client_ip,
           (unsigned)ntohs(client_addr->sin_port));
    fflush(stdout);
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
