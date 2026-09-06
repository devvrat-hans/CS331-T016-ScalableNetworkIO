CC = gcc

CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags liburing)
LDLIBS = $(shell pkg-config --libs liburing)

TARGET = io_uring_echo_server
SOURCE = io_uring_echo_server.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)