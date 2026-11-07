#ifndef MINIHTTPD_SERVER_H
#define MINIHTTPD_SERVER_H

#include <stdint.h>

#define SERVER_DEFAULT_PORT 8080
#define SERVER_BACKLOG 128
#define SERVER_BUFFER_SIZE 4096

/*
 * Inicia el servidor TCP y atiende clientes de forma secuencial.
 * Retorna 0 si el servidor termina correctamente y 1 si ocurre un error.
 */
int server_run(uint16_t port);

#endif
