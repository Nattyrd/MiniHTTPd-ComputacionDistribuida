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
- Fase 5: seguridad.
  - Valida la URI (debe iniciar con `/`, sin caracteres de control).
  - Aplica limites estrictos al tamano de la solicitud, la linea de solicitud,
    la URI, cada encabezado y la cantidad de encabezados.
  - Bloquea Directory Traversal con el rechazo de segmentos `..`, la
    canonicalizacion con `realpath()` y la verificacion de que el archivo
    resuelto permanezca dentro de `www`.
  - Bloquea enlaces simbolicos que apunten fuera de `www`.
  - Exige `Host` en HTTP/1.1 y rechaza `Host` duplicados.
  - Rechaza `Transfer-Encoding`, que el servidor no procesa.
  - Usa solo funciones acotadas (`snprintf`, `memcpy` con comprobaciones) y
    compila con `_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, `relro` y `now`.
  - Aplica tiempos maximos de recepcion y envio para evitar clientes lentos.
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
| `400 Bad Request` | Solicitud mal formada, URI invalida, version no soportada, `Host` ausente o duplicado en HTTP/1.1, `Transfer-Encoding` o solicitud demasiado grande. |
| `403 Forbidden` | La ruta intenta salir de `www`, apunta a un directorio o no hay permisos. |
| `404 Not Found` | El archivo solicitado no existe. |
| `405 Method Not Allowed` | El metodo no es GET; incluye `Allow: GET`. |
| `500 Internal Server Error` | Error interno al abrir o leer el archivo. |

## Limites de la solicitud

| Elemento | Limite | Constante |
|----------|--------|-----------|
| Solicitud completa | 8192 bytes | `HTTP_MAX_REQUEST_SIZE` |
| Linea de solicitud | 4096 bytes | `HTTP_MAX_REQUEST_LINE` |
| URI | 2048 bytes | `HTTP_MAX_URI_SIZE` |
| Linea de encabezado | 1024 bytes | `HTTP_MAX_HEADER_LINE` |
| Cantidad de encabezados | 64 | `HTTP_MAX_HEADERS` |
| Metodo | 16 bytes | `HTTP_MAX_METHOD_SIZE` |
| Espera de recepcion/envio | 10 segundos | `SERVER_CLIENT_TIMEOUT_SEC` |

## Seguridad

- **Directory Traversal**: la URI debe empezar con `/`; se rechaza cualquier
  segmento `..`; luego `realpath()` resuelve enlaces y `path_is_inside_root()`
  comprueba que el resultado siga dentro de `www`. Una URI con `%2e%2e` no se
  decodifica, por lo que se trata como un nombre literal y no como traversal.
- **Enlaces simbolicos**: un enlace dentro de `www` que apunte fuera de `www`
  se rechaza con 403. Un enlace circular produce un 500 controlado.
- **Desbordamientos**: no se usan `strcpy`, `strcat` ni `sprintf`; todas las
  copias usan `snprintf` o `memcpy` con verificacion previa de longitud.
- **Solicitudes ambiguas**: en HTTP/1.1 se exige `Host`, se rechazan los
  `Host` duplicados y no se admite `Transfer-Encoding`.
- **Clientes lentos**: cada conexion tiene un tiempo maximo de recepcion y de
  envio, de modo que un cliente que no completa la solicitud no bloquea al
  servidor indefinidamente.
- **Limitaciones conocidas**: el servidor es secuencial, no usa TLS ni
  autenticacion, y existe una ventana TOCTOU entre `realpath()` y `open()`.
  Las conexiones persistentes y `epoll` corresponden a la siguiente fase.

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

## Prueba de limites y seguridad

`Host` es obligatorio en HTTP/1.1:

```sh
printf 'GET / HTTP/1.1\r\n\r\n' | nc 127.0.0.1 8080
```

Debe responder `400 Bad Request`.

Una URI demasiado larga tambien se rechaza:

```sh
curl -i "http://127.0.0.1:8080/$(head -c 3000 /dev/zero | tr '\0' 'a')"
```

Debe responder `400 Bad Request`.

Un enlace simbolico dentro de `www` que apunte fuera del directorio se rechaza:

```sh
ln -s /etc/passwd www/escape
curl -i http://127.0.0.1:8080/escape
rm www/escape
```

Debe responder `403 Forbidden`.

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
