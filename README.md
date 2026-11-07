# MiniHTTPd

Servidor HTTP/1.1 basico desarrollado en C para Linux, sin bibliotecas HTTP externas.

## Estado actual

- Fase 1: servidor TCP funcional.
- El servidor inicia, escucha un puerto, acepta un cliente y devuelve (eco) los datos recibidos.
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

## Prueba de comunicacion

En otra terminal, conectarse al servidor:

```sh
nc 127.0.0.1 8080
```

Escribir cualquier texto y presionar Enter. El servidor debe devolver el mismo texto.

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
