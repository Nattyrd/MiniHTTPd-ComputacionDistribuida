#ifndef MINIHTTPD_FILES_H
#define MINIHTTPD_FILES_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#define FILES_WWW_ROOT "www"
#define FILES_MAX_PATH 4096

typedef enum {
    FILES_OK = 0,
    FILES_NOT_FOUND,
    FILES_FORBIDDEN,
    FILES_BAD_REQUEST,
    FILES_ERROR
} files_result_t;

typedef struct {
    int fd;
    size_t size;
    time_t modified;
    char path[FILES_MAX_PATH];
} files_file_t;

/*
 * Busca y abre un archivo dentro de www a partir de la URI solicitada.
 * La URI debe comenzar con '/'. La ruta "/" se resuelve como index.html.
 * Al abrirlo guarda su tamano y su fecha de modificacion.
 */
files_result_t files_open(const char *uri, files_file_t *file);

/*
 * Lee hasta size bytes del archivo abierto.
 * Retorna la cantidad leida, 0 al final o -1 si ocurre un error.
 */
ssize_t files_read(files_file_t *file, void *buffer, size_t size);

/* Cierra el archivo y deja el descriptor en -1. */
void files_close(files_file_t *file);

#endif
