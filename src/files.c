#define _XOPEN_SOURCE 700

#include "files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int path_is_inside_root(const char *root, const char *path);
static int uri_has_parent_segment(const char *uri, size_t length);

files_result_t files_open(const char *uri, files_file_t *file)
{
    if (uri == NULL || file == NULL) {
        return FILES_BAD_REQUEST;
    }

    file->fd = -1;
    file->size = 0;
    file->modified = 0;
    file->path[0] = '\0';

    if (uri[0] != '/') {
        return FILES_BAD_REQUEST;
    }

    const char *query = strchr(uri, '?');
    size_t uri_length = query != NULL
                            ? (size_t)(query - uri)
                            : strlen(uri);

    if (uri_length == 0) {
        return FILES_BAD_REQUEST;
    }

    /*
     * Rechaza cualquier intento de retroceder con "..". El chequeo con
     * realpath() sigue siendo la defensa principal, pero este rechazo
     * temprano deja claro que el cliente intento salir de www.
     */
    if (uri_has_parent_segment(uri, uri_length)) {
        return FILES_FORBIDDEN;
    }

    char candidate[FILES_MAX_PATH];
    int written;

    if (uri_length == 1 && uri[0] == '/') {
        written = snprintf(candidate, sizeof(candidate),
                           "%s/index.html", FILES_WWW_ROOT);
    } else if (uri[uri_length - 1] == '/') {
        written = snprintf(candidate, sizeof(candidate),
                           "%s%.*sindex.html",
                           FILES_WWW_ROOT,
                           (int)uri_length,
                           uri);
    } else {
        written = snprintf(candidate, sizeof(candidate),
                           "%s%.*s",
                           FILES_WWW_ROOT,
                           (int)uri_length,
                           uri);
    }

    if (written < 0 || (size_t)written >= sizeof(candidate)) {
        return FILES_BAD_REQUEST;
    }

    char root[FILES_MAX_PATH];
    if (realpath(FILES_WWW_ROOT, root) == NULL) {
        if (errno == ENOENT) {
            return FILES_ERROR;
        }
        if (errno == EACCES) {
            return FILES_FORBIDDEN;
        }

        return FILES_ERROR;
    }

    char resolved[FILES_MAX_PATH];
    if (realpath(candidate, resolved) == NULL) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return FILES_NOT_FOUND;
        }
        if (errno == EACCES) {
            return FILES_FORBIDDEN;
        }

        return FILES_ERROR;
    }

    if (!path_is_inside_root(root, resolved)) {
        return FILES_FORBIDDEN;
    }

    int fd = open(resolved, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return FILES_NOT_FOUND;
        }
        if (errno == EACCES) {
            return FILES_FORBIDDEN;
        }

        return FILES_ERROR;
    }

    struct stat status;
    if (fstat(fd, &status) < 0) {
        close(fd);
        return FILES_ERROR;
    }

    if (!S_ISREG(status.st_mode) || status.st_size < 0) {
        close(fd);
        return FILES_FORBIDDEN;
    }

    size_t path_length = strlen(resolved);
    if (path_length >= sizeof(file->path)) {
        close(fd);
        return FILES_ERROR;
    }

    memcpy(file->path, resolved, path_length + 1);
    file->fd = fd;
    file->size = (size_t)status.st_size;
    file->modified = status.st_mtime;

    return FILES_OK;
}

ssize_t files_read(files_file_t *file, void *buffer, size_t size)
{
    if (file == NULL || file->fd < 0 || buffer == NULL) {
        return -1;
    }

    return read(file->fd, buffer, size);
}

void files_close(files_file_t *file)
{
    if (file != NULL && file->fd >= 0) {
        close(file->fd);
        file->fd = -1;
    }
}

static int path_is_inside_root(const char *root, const char *path)
{
    size_t root_length = strlen(root);

    if (strncmp(root, path, root_length) != 0) {
        return 0;
    }

    if (path[root_length] == '\0' || path[root_length] == '/') {
        return 1;
    }

    return 0;
}

static int uri_has_parent_segment(const char *uri, size_t length)
{
    size_t i = 0;

    while (i < length) {
        while (i < length && uri[i] == '/') {
            i++;
        }

        size_t segment_start = i;

        while (i < length && uri[i] != '/') {
            i++;
        }

        size_t segment_length = i - segment_start;

        if (segment_length == 2 &&
            uri[segment_start] == '.' &&
            uri[segment_start + 1] == '.') {
            return 1;
        }
    }

    return 0;
}
