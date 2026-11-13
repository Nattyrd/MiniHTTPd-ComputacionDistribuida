CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
LDLIBS ?=

TARGET := minihttpd

<<<<<<< HEAD
# Fase 2: main.c, server.c y http.c.
# Las fases posteriores agregaran files.c y mime.c.
SRCS := src/main.c src/server.c src/http.c
=======
# Fase 1: solo main.c y server.c.
# Las fases posteriores se le agregaran http.c, files.c y mime.c.
SRCS := src/main.c src/server.c
>>>>>>> a8a0894e985f9836f587342e1b4d32a390d0b5ab
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET) $(wildcard src/*.o) $(wildcard src/*.d)

-include $(DEPS)
