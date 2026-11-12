CC ?= cc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -pthread
URING_LIBS ?= $(shell pkg-config --libs liburing 2>/dev/null || echo -luring)
# This Makefile lives in code/benchmark. The engines live in their owners'
# folders next to it (code/select, code/poll, code/epoll, code/io_uring).
ENGINES = ..
CPPFLAGS += -Icommon
OBJ = build/framework.o build/apps.o
BIN = bin/select_server bin/poll_server bin/epoll_server bin/io_uring_server bin/loadgen

.PHONY: all clean
all: $(BIN)

build/%.o: common/%.c common/framework.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

bin/select_server: $(ENGINES)/select/select_server.c $(OBJ)
	@mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@
bin/poll_server: $(ENGINES)/poll/poll_server.c $(OBJ)
	@mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@
bin/epoll_server: $(ENGINES)/epoll/epoll_server.c $(OBJ)
	@mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@
bin/io_uring_server: $(ENGINES)/io_uring/io_uring_server.c $(OBJ)
	@mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(URING_LIBS) -o $@
bin/loadgen: loadgen.c
	@mkdir -p bin
	$(CC) $(CFLAGS) $< -lm -o $@

clean:
	rm -rf bin build
