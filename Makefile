CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 \
          -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong
CPPFLAGS ?= -Iinclude
LDFLAGS ?= -Wl,-z,relro,-z,now
LDLIBS ?=

TARGET := minihttpd

# Fase 4: main.c, server.c, http.c, files.c y mime.c.
SRCS := src/main.c src/server.c src/http.c src/files.c src/mime.c
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
