#ifndef MINIHTTPD_HTTP_H
#define MINIHTTPD_HTTP_H

#include <stddef.h>
#include <time.h>

#define HTTP_MAX_REQUEST_SIZE 8192
#define HTTP_MAX_REQUEST_LINE 4096
#define HTTP_MAX_HEADER_LINE 1024
#define HTTP_MAX_HEADERS 64

#define HTTP_MAX_METHOD_SIZE 16
#define HTTP_MAX_URI_SIZE 2048
#define HTTP_MAX_VERSION_SIZE 16
#define HTTP_MAX_HOST_SIZE 256
#define HTTP_MAX_USER_AGENT_SIZE 512
#define HTTP_MAX_CONNECTION_SIZE 128

#define HTTP_RESPONSE_BUFFER_SIZE 8192
#define HTTP_DATE_SIZE 64
#define HTTP_SERVER_NAME "MiniHTTPd/1.0"

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
    char connection[HTTP_MAX_CONNECTION_SIZE];
    size_t content_length;
} http_request_t;

/*
 * Analiza una solicitud HTTP completa (cabecera incluida).
 * Retorna 200 si la solicitud es valida, 405 si el metodo no es GET
 * y 400 si la solicitud esta mal formada.
 *
 * El servidor no procesa cuerpos de solicitud: una solicitud GET con
 * Content-Length mayor que cero se rechaza con 400 para no perder la
 * sincronizacion de la conexion.
 */
http_status_t http_parse_request(const char *data, size_t length,
                                 http_request_t *request);

/*
 * Indica si la conexion debe reutilizarse despues de esta solicitud.
 * En HTTP/1.1 la persistencia es el comportamiento por defecto salvo
 * "Connection: close". En HTTP/1.0 solo se mantiene si el cliente envia
 * "Connection: keep-alive". La comparacion no distingue mayusculas y
 * admite listas separadas por comas.
 */
int http_request_wants_keep_alive(const http_request_t *request);

/*
 * Genera una respuesta de error completa: cabeceras y cuerpo HTML.
 * Incluye Server, Date, Content-Type, Content-Length y Connection.
 * Si keep_alive es distinto de cero, Connection sera "keep-alive";
 * de lo contrario sera "close". Para 405 agrega el encabezado Allow.
 * Retorna la cantidad de bytes generados o -1 si no cabe en el buffer.
 */
int http_build_error_response(char *buffer, size_t capacity,
                              http_status_t status, int keep_alive);

/*
 * Genera solo las cabeceras de una respuesta con el contenido de un
 * archivo. El cuerpo se envia despues de estas cabeceras.
 * Si last_modified es 0 se omite el encabezado Last-Modified.
 * El valor de keep_alive decide el encabezado Connection.
 * Retorna la cantidad de bytes generados o -1 si no cabe en el buffer.
 */
int http_build_file_response_header(char *buffer, size_t capacity,
                                    http_status_t status,
                                    const char *content_type,
                                    size_t content_length,
                                    time_t last_modified,
                                    int keep_alive);

#endif
