#include "mime.h"

#include <string.h>

typedef struct {
    const char *extension;
    const char *type;
} mime_entry_t;

static const mime_entry_t mime_table[] = {
    {".html", "text/html; charset=utf-8"},
    {".htm",  "text/html; charset=utf-8"},
    {".css",  "text/css; charset=utf-8"},
    {".js",   "application/javascript; charset=utf-8"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"},
    {".svg",  "image/svg+xml"},
    {".ico",  "image/x-icon"},
    {".txt",  "text/plain; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".pdf",  "application/pdf"}
};

static int equals_ignore_case(const char *a, const char *b);

const char *mime_type_from_path(const char *path)
{
    if (path == NULL) {
        return MIME_DEFAULT_TYPE;
    }

    const char *name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;

    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0') {
        return MIME_DEFAULT_TYPE;
    }

    size_t count = sizeof(mime_table) / sizeof(mime_table[0]);

    for (size_t i = 0; i < count; i++) {
        if (equals_ignore_case(dot, mime_table[i].extension)) {
            return mime_table[i].type;
        }
    }

    return MIME_DEFAULT_TYPE;
}

static int equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;

        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }

        if (ca != cb) {
            return 0;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}
