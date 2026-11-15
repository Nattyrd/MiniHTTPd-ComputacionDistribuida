# MiniHTTPd

Servidor HTTP/1.1 basico desarrollado en C para Linux, sin bibliotecas HTTP externas.

## Estado actual

- Fase 1: servidor TCP funcional.
- Fase 2: primera solicitud HTTP.
  - Lee una solicitud HTTP completa por conexion.
  - Reconoce el metodo GET, la URI y la version HTTP/1.0 o HTTP/1.1.
  - Valida la estructura basica de los encabezados y guarda `Host`, `User-Agent` y `Connection`.
  - Responde con 200 OK, 400 Bad Request o 405 Method Not Allowed.
- Fase 3: servicio de archivos estaticos.
  - Busca los archivos dentro del directorio `www`.
  - La URI `/` se resuelve como `www/index.html`.
  - Ignora la cadena de consulta (lo que sigue a `?`) para buscar el archivo.
  - Rechaza cualquier segmento `..` en la URI.
  - Usa `realpath()` y verifica que el archivo resuelto permanezca dentro de `www`.
  - Responde 404 si el archivo no existe.
  - Responde 403 si la ruta intenta salir de `www` o si apunta a un directorio.
  - Envia las cabeceras y luego el contenido del archivo por bloques.
- Fase 4: respuestas completas.
  - Selecciona el `Content-Type` segun la extension del archivo mediante `mime.c`.
  - Envia los encabezados `Server`, `Date`, `Content-Type`, `Content-Length`,
    `Connection` y `Last-Modified` en las respuestas de archivos.
  - Genera paginas de error HTML con `Content-Type: text/html; charset=utf-8`
    y su `Content-Length` correcto.
  - Agrega `Allow: GET` en las respuestas 405.
  - Cubre los codigos 200, 400, 403, 404, 405 y 500.
- La atencion sigue siendo secuencial: se procesa un cliente a la vez y se
  responde una solicitud por conexion (`Connection: close`). La concurrencia con
  epoll y las conexiones persistentes se agregaran en una fase posterior.

## Tipos MIME

| Extension | Content-Type |
|-----------|--------------|
| `.html`, `.htm` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js` | `application/javascript; charset=utf-8` |
| `.png` | `image/png` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.gif` | `image/gif` |
| `.svg` | `image/svg+xml` |
| `.ico` | `image/x-icon` |
| `.txt` | `text/plain; charset=utf-8` |
| `.json` | `application/json; charset=utf-8` |
| `.pdf` | `application/pdf` |
| cualquier otra | `application/octet-stream` |

## Codigos de estado

| Codigo | Situacion |
|--------|-----------|
| `200 OK` | El archivo existe y se envia su contenido. |
| `400 Bad Request` | Solicitud mal formada, URI invalida, version no soportada o solicitud demasiado grande. |
| `403 Forbidden` | La ruta intenta salir de `www`, apunta a un directorio o no hay permisos. |
| `404 Not Found` | El archivo solicitado no existe. |
| `405 Method Not Allowed` | El metodo no es GET; incluye `Allow: GET`. |
| `500 Internal Server Error` | Error interno al abrir o leer el archivo. |

## Requisitos

- Linux
- gcc
- make

## Compilacion

```sh
make
```

## Ejecucion

```sh
./minihttpd [puerto]
```

Si no se indica el puerto, se usa el 8080 por defecto.

Ejemplo:

```sh
./minihttpd 8080
```

## Prueba de archivos y encabezados

En otra terminal, solicitar el documento por defecto y ver las cabeceras:

```sh
curl -i http://127.0.0.1:8080/
```

Solicitar una imagen y comparar su contenido:

```sh
curl -sS http://127.0.0.1:8080/image.jpg -o /tmp/image.jpg
sha256sum www/image.jpg /tmp/image.jpg
```

La respuesta debe incluir `Content-Type: image/jpeg`, el `Content-Length`
correcto y `Last-Modified`.

Comprobar un archivo inexistente:

```sh
curl -i http://127.0.0.1:8080/no-existe
```

Debe responder `404 Not Found` con una pagina HTML.

Comprobar un intento de directory traversal:

```sh
curl -i --path-as-is http://127.0.0.1:8080/../../etc/passwd
```

Debe responder `403 Forbidden`.

## Prueba de metodos

Para comprobar el rechazo de metodos distintos de GET:

```sh
printf 'POST / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc 127.0.0.1 8080
```

La respuesta debe ser `405 Method Not Allowed` con el encabezado `Allow: GET`.

## Estructura

```text
minihttpd/
├── Makefile
├── README.md
├── include/
│   ├── http.h
│   ├── server.h
│   ├── mime.h
│   └── files.h
├── src/
│   ├── main.c
│   ├── server.c
│   ├── http.c
│   ├── mime.c
│   └── files.c
└── www/
    ├── index.html
    ├── style.css
    └── image.jpg
```
