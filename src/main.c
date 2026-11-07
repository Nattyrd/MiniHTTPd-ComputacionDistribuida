#include "server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static int parse_port(const char *text, uint16_t *port);

int main(int argc, char **argv)
{
    uint16_t port = SERVER_DEFAULT_PORT;

    if (argc > 2) {
        fprintf(stderr, "Uso: %s [puerto]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2) {
        if (parse_port(argv[1], &port) != 0) {
            fprintf(stderr, "Puerto invalido: %s\n", argv[1]);
            fprintf(stderr, "Uso: %s [puerto]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (server_run(port) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int parse_port(const char *text, uint16_t *port)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' ||
        value < 1 || value > 65535) {
        return -1;
    }

    *port = (uint16_t)value;
    return 0;
}
