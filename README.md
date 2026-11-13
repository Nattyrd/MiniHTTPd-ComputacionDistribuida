# MiniHTTPd

Servidor HTTP/1.1 basico desarrollado en C para Linux, sin bibliotecas HTTP externas.

## Estado actual

- Fase 1: servidor TCP funcional.
- Fase 2: primera solicitud HTTP.
  - Lee una solicitud HTTP completa por conexion.
  - Reconoce el metodo GET, la URI y la version HTTP/1.0 o HTTP/1.1.
  - Valida la estructura basica de los encabezados y guarda Host y User-Agent.
  - Responde con 200 OK, 400 Bad Request o 405 Method Not Allowed.
  - Cierra la conexion despues de enviar la respuesta.
- La atencion es secuencial: se procesa un cliente a la vez. La concurrencia con epoll se agregara en una fase posterior.
- Todavia no se sirven archivos estaticos; eso corresponde a una fase posterior.

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

## Prueba HTTP

En otra terminal, enviar una solicitud GET con curl:

```sh
curl -v http://127.0.0.1:8080/
```

Tambien se puede enviar una solicitud manual con nc:

```sh
printf 'GET /index.html HTTP/1.1\r\nHost: localhost\r\nUser-Agent: nc\r\n\r\n' | nc 127.0.0.1 8080
```

El servidor debe responder con `200 OK` y mostrar el metodo, la URI, la version y los encabezados Host y User-Agent recibidos.

Para comprobar el rechazo de metodos distintos de GET:

```sh
printf 'POST / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc 127.0.0.1 8080
```

La respuesta debe ser `405 Method Not Allowed` con el encabezado `Allow: GET`.

Tambien se puede comprobar que el puerto esta escuchando con:

```sh
ss -ltnp | grep 8080
```

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
    └── image.png
```
