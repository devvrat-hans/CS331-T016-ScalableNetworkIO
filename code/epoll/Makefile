CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2

.PHONY: all clean

all: epoll_echo_server

epoll_echo_server: epoll_echo_server.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f epoll_echo_server