# MiniHTTPd

Servidor HTTP/1.1 basico desarrollado en C para Linux, sin bibliotecas HTTP externas.

## Estado actual

- Fase 1: servidor TCP funcional.
- Fase 2: primera solicitud HTTP.
  - Lee una solicitud HTTP completa por conexion.
  - Reconoce el metodo GET, la URI y la version HTTP/1.0 o HTTP/1.1.
  - Valida la estructura basica de los encabezados y guarda Host y User-Agent.
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
  - Por ahora usa `Content-Type: application/octet-stream`; los tipos MIME se agregaran en la siguiente fase.
- La atencion es secuencial: se procesa un cliente a la vez. La concurrencia con epoll se agregara en una fase posterior.

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

## Prueba de archivos

En otra terminal, solicitar el documento por defecto:

```sh
curl -v http://127.0.0.1:8080/
```

Solicitar una imagen y comparar su contenido:

```sh
curl -sS http://127.0.0.1:8080/image.jpg -o /tmp/image.jpg
sha256sum www/image.jpg /tmp/image.jpg
```

Comprobar un archivo inexistente:

```sh
curl -i http://127.0.0.1:8080/no-existe
```

Debe responder `404 Not Found`.

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
