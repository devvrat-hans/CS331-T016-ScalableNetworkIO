CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11

.PHONY: all clean
all: tcp_echo_load

tcp_echo_load: tcp_echo_load.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f tcp_echo_load
