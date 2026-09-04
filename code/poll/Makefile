CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2

.PHONY: all clean

all: poll_echo_server

poll_echo_server: poll_echo_server.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f poll_echo_server
