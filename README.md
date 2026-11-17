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
- Fase 6: concurrencia con `epoll`.
  - El socket de escucha y cada conexion se configuran en modo no bloqueante.
  - El socket de escucha y las conexiones se registran en una instancia de
    `epoll`; el bucle de eventos usa `epoll_wait()`.
  - Cada conexion tiene una maquina de estados: lectura de la solicitud,
    escritura de la respuesta y descarte final de datos pendientes.
  - Las solicitudes y las respuestas se procesan por bloques, por lo que una
    transferencia grande o un cliente lento no bloquea a los demas.
  - Se admiten hasta `SERVER_MAX_CONNECTIONS` (512) conexiones simultaneas.
  - Las conexiones inactivas se cierran despues de `SERVER_CLIENT_TIMEOUT_SEC`
    (10 segundos).
- Fase 7: conexiones persistentes.
  - En HTTP/1.1 la conexion se reutiliza por defecto; el cliente puede pedir el
    cierre con `Connection: close`.
  - En HTTP/1.0 la conexion solo se mantiene si el cliente envia
    `Connection: keep-alive`.
  - El encabezado `Connection` se analiza como una lista de tokens separados
    por comas, sin distinguir mayusculas. Si aparece `close`, tiene prioridad.
  - Una misma conexion atiende varias solicitudes en secuencia y admite
    `pipelining`: si el cliente adelanta varias solicitudes, se responden en
    orden sin esperar a un nuevo evento de `epoll`.
  - Cada respuesta anuncia `Connection: keep-alive` o `Connection: close`
    segun corresponda.
  - Las conexiones persistentes ociosas se cierran tras
    `SERVER_KEEPALIVE_TIMEOUT_SEC` (5 segundos) para liberar recursos.
  - Una conexion atiende como maximo `SERVER_MAX_REQUESTS_PER_CONNECTION` (100)
    solicitudes; la ultima respuesta anuncia `Connection: close`.
  - Los errores de sintaxis (`400`, `405`) y las solicitudes con cuerpo cierran
    la conexion, porque la sincronizacion del flujo ya no es confiable. Los
    errores de recurso (`403`, `404`, `500`) conservan la conexion.
  - Las solicitudes `GET` con `Content-Length` mayor que cero se rechazan con
    `400` para que el cuerpo no se interprete como otra solicitud.

## Sitio web de demostracion

`www/index.html` y `www/style.css` forman una pagina de presentacion del
proyecto que el propio servidor entrega como archivos estaticos. Incluye:

- Diseno responsive con temas claro y oscuro segun `prefers-color-scheme`.
- Terminal de ejemplo, tarjetas de caracteristicas y linea de tiempo de fases.
- Enlaces de prueba a `/`, `/style.css`, `/image.jpg` y una ruta 404.
- Solo HTML y CSS, sin JavaScript ni recursos externos.

Para verla, se compila y ejecuta el servidor desde `minihttpd/` y abre
`http://127.0.0.1:8080/` en el navegador.

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
| `400 Bad Request` | Solicitud mal formada, URI invalida, version no soportada, `Host` ausente o duplicado en HTTP/1.1, `Content-Length` duplicado, cuerpo de solicitud, `Transfer-Encoding` o solicitud demasiado grande. |
| `403 Forbidden` | La ruta intenta salir de `www`, apunta a un directorio o no hay permisos. |
| `404 Not Found` | El archivo solicitado no existe. |
| `405 Method Not Allowed` | El metodo no es GET; incluye `Allow: GET`. |
| `500 Internal Server Error` | Error interno al abrir o leer el archivo. |

## Limites del servidor

| Elemento | Limite | Constante |
|----------|--------|-----------|
| Solicitud completa | 8192 bytes | `HTTP_MAX_REQUEST_SIZE` |
| Linea de solicitud | 4096 bytes | `HTTP_MAX_REQUEST_LINE` |
| URI | 2048 bytes | `HTTP_MAX_URI_SIZE` |
| Linea de encabezado | 1024 bytes | `HTTP_MAX_HEADER_LINE` |
| Cantidad de encabezados | 64 | `HTTP_MAX_HEADERS` |
| Metodo | 16 bytes | `HTTP_MAX_METHOD_SIZE` |
| Espera de recepcion/envio | 10 segundos | `SERVER_CLIENT_TIMEOUT_SEC` |
| Espera de una conexion keep-alive ociosa | 5 segundos | `SERVER_KEEPALIVE_TIMEOUT_SEC` |
| Solicitudes por conexion | 100 | `SERVER_MAX_REQUESTS_PER_CONNECTION` |
| Conexiones simultaneas | 512 | `SERVER_MAX_CONNECTIONS` |
| Eventos por `epoll_wait` | 64 | `SERVER_MAX_EVENTS` |
| Espera de `epoll_wait` | 1 segundo | `SERVER_EPOLL_TIMEOUT_MS` |
| Descarte tras un error | 1 segundo | `SERVER_DRAIN_TIMEOUT_SEC` |

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
  `Host` duplicados y no se admite `Transfer-Encoding`. Un `Content-Length`
  duplicado o un cuerpo de solicitud tambien se rechazan con `400`.
- **Contrabando de solicitudes**: como no se procesan cuerpos, un `GET` con
  `Content-Length` mayor que cero se rechaza y se cierra la conexion. Asi los
  bytes del cuerpo no pueden interpretarse como la siguiente solicitud de una
  conexion persistente.
- **Clientes lentos**: cada conexion tiene un tiempo maximo de inactividad
  (recepcion, escritura o descarte). Un cliente que no completa la solicitud,
  no lee la respuesta o no termina de enviar datos no bloquea al servidor ni
  ocupa una conexion indefinidamente. Las conexiones persistentes ociosas
  usan un tiempo menor (`SERVER_KEEPALIVE_TIMEOUT_SEC`).
- **Limite de conexiones**: si se alcanza `SERVER_MAX_CONNECTIONS`, las
  conexiones nuevas se cierran inmediatamente.
- **Limite de solicitudes**: una conexion persistente se cierra despues de
  `SERVER_MAX_REQUESTS_PER_CONNECTION` solicitudes para evitar que un cliente
  monopolice una conexion.
- **Limitaciones conocidas**: no usa TLS ni autenticacion y existe una ventana
  TOCTOU entre `realpath()` y `open()`.

## Concurrencia con epoll

- El socket de escucha se configura con `O_NONBLOCK` y se registra en `epoll`
  con `EPOLLIN`. El bucle de eventos llama a `epoll_wait()` con un tiempo maximo
  de 1 segundo para poder revisar las conexiones inactivas.
- Cada conexion aceptada tambien se configura con `O_NONBLOCK` y se registra
  con `EPOLLIN | EPOLLRDHUP`.
- Mientras se lee la solicitud, se llama a `recv()` hasta que llega el final de
  las cabeceras (`\r\n\r\n`) o hasta que no hay mas datos (`EAGAIN`).
- Cuando la solicitud esta completa, se prepara la respuesta y la conexion pasa
  a `EPOLLOUT`. El envio usa `send()` por bloques; si el socket no puede aceptar
  mas datos, la conexion espera el siguiente evento sin bloquear el bucle.
- Los archivos grandes se leen por bloques de `SERVER_BUFFER_SIZE` y se envian
  a medida que el cliente los acepta.
- Despues de una respuesta de error que obliga a cerrar, se cierra el lado de
  escritura (`shutdown(SHUT_WR)`) y se descartan los datos pendientes durante
  un maximo de 1 segundo. Asi el cliente recibe la respuesta completa aunque
  siga enviando datos (por ejemplo, el cuerpo de un `POST` o una solicitud
  demasiado grande).
- Tras una respuesta `keep-alive`, la conexion vuelve a `EPOLLIN` y descarta la
  solicitud ya atendida. Si el buffer conserva solicitudes adelantadas
  (`pipelining`), la siguiente se procesa de inmediato.
- Las conexiones sin actividad se cierran al cumplirse
  `SERVER_CLIENT_TIMEOUT_SEC`; una conexion keep-alive ociosa se cierra al
  cumplirse `SERVER_KEEPALIVE_TIMEOUT_SEC`.

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

## Prueba de concurrencia

Lanzar varias solicitudes al mismo tiempo y comprobar que todas responden `200`:

```sh
seq 20 | xargs -P20 -I{} curl -s -o /dev/null \
    -w '%{http_code}\n' http://127.0.0.1:8080/image.jpg | sort | uniq -c
```

La salida debe mostrar `20 200`.

Comprobar que un cliente lento no bloquea a los demas: abrir una conexion que
envia solo parte de la solicitud y, sin cerrarla, solicitar un archivo desde
otra terminal:

```sh
printf 'GET / HTT' | nc 127.0.0.1 8080 &
curl -i http://127.0.0.1:8080/image.jpg
```

La segunda solicitud debe responder de inmediato con `200 OK`. La conexion
incompleta se cierra automaticamente al cumplirse el tiempo de inactividad.

## Prueba de conexiones persistentes

Con `curl`, pedir dos recursos en una sola invocacion y comprobar que reutiliza
la conexion:

```sh
curl -v -o /dev/null http://127.0.0.1:8080/ \
     -o /dev/null http://127.0.0.1:8080/style.css 2>&1 |
    grep -E 'Re-using|Connection: keep-alive'
```

La salida debe incluir `Re-using existing connection` y
`Connection: keep-alive` en ambas respuestas. El contador `num_connects` de
`curl` confirma lo mismo:

```sh
curl -sS -o /dev/null -o /dev/null \
    -w '%{http_code} num_connects=%{num_connects}\n' \
    http://127.0.0.1:8080/ http://127.0.0.1:8080/style.css
```

La primera linea muestra `num_connects=1` y la segunda `num_connects=0`.

Pedir el cierre explicito con HTTP/1.1:

```sh
curl -i -H 'Connection: close' http://127.0.0.1:8080/
```

La respuesta debe incluir `Connection: close` y la conexion se cierra.

Comprobar HTTP/1.0 (sin persistencia salvo que se pida):

```sh
curl -i --http1.0 http://127.0.0.1:8080/ | grep -i '^Connection:'
curl -i --http1.0 -H 'Connection: keep-alive' \
    http://127.0.0.1:8080/ | grep -i '^Connection:'
```

La primera respuesta debe decir `close` y la segunda `keep-alive`.

Para probar el `pipelining` manualmente, enviar dos solicitudes en una sola
escritura y comprobar que llegan las dos respuestas en orden:

```sh
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' |
    nc 127.0.0.1 8080 | grep -a '^HTTP/1.1'
```

Debe mostrar `HTTP/1.1 200 OK` dos veces.

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
