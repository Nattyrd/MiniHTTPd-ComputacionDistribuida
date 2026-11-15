#ifndef MINIHTTPD_MIME_H
#define MINIHTTPD_MIME_H

#include <stddef.h>

#define MIME_DEFAULT_TYPE "application/octet-stream"

/*
 * Devuelve el tipo MIME correspondiente a la extension del archivo.
 * La comparacion de la extension no distingue mayusculas de minusculas.
 * Si la extension no esta registrada, retorna MIME_DEFAULT_TYPE.
 */
const char *mime_type_from_path(const char *path);

#endif
