CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2

.PHONY: all clean

all: select_echo_server

select_echo_server: select_echo_server.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f select_echo_server
