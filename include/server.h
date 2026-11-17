#ifndef MINIHTTPD_SERVER_H
#define MINIHTTPD_SERVER_H

#include <stdint.h>

#define SERVER_DEFAULT_PORT 8080
#define SERVER_BACKLOG 128
#define SERVER_BUFFER_SIZE 4096

#define SERVER_MAX_EVENTS 64
#define SERVER_MAX_CONNECTIONS 512
#define SERVER_EPOLL_TIMEOUT_MS 1000

/*
 * Tiempo maximo (en segundos) que una conexion puede estar inactiva antes
 * de cerrarse. Evita que clientes lentos ocupen recursos indefinidamente.
 */
#ifndef SERVER_CLIENT_TIMEOUT_SEC
#define SERVER_CLIENT_TIMEOUT_SEC 10
#endif

/*
 * Tiempo maximo (en segundos) que una conexion persistente puede estar
 * ociosa esperando la siguiente solicitud. Es menor que el tiempo de
 * recepcion para liberar rapidamente las conexiones keep-alive inactivas.
 */
#ifndef SERVER_KEEPALIVE_TIMEOUT_SEC
#define SERVER_KEEPALIVE_TIMEOUT_SEC 5
#endif

/*
 * Cantidad maxima de solicitudes atendidas en una misma conexion
 * persistente. Al llegar al limite, la ultima respuesta anuncia
 * "Connection: close" y la conexion se cierra ordenadamente.
 */
#ifndef SERVER_MAX_REQUESTS_PER_CONNECTION
#define SERVER_MAX_REQUESTS_PER_CONNECTION 100
#endif

/*
 * Tiempo maximo (en segundos) para descartar los datos que el cliente
 * siga enviando despues de una respuesta de error. Evita que el cierre
 * con datos sin leer genere un RST y el cliente pierda la respuesta.
 */
#ifndef SERVER_DRAIN_TIMEOUT_SEC
#define SERVER_DRAIN_TIMEOUT_SEC 1
#endif

/*
 * Inicia el servidor TCP, registra el socket de escucha en epoll y atiende
 * varios clientes de forma concurrente con sockets no bloqueantes.
 * Retorna 0 si el servidor termina correctamente y 1 si ocurre un error.
 */
int server_run(uint16_t port);

#endif
