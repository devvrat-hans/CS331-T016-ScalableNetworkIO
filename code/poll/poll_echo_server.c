/*
 * poll_echo_server.c - Single-threaded TCP echo server using poll()
 *
 * Course: CS331 - Scalable Network I/O (Project ID: 13, Team ID: T016)
 * Author: Devvrat Hans (Roll No: 23110094)
 *
 * Implementation details:
 * - Uses poll() for I/O multiplexing in a single thread
 * - Sets listening and client sockets to non-blocking mode (O_NONBLOCK)
 * - Separates read/write readiness: registers POLLOUT only when pending echo data exists
 * - Drains new incoming connections using a non-blocking accept() loop
 * - Handles client disconnects by swapping the last active descriptor slot (O(1) compaction)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

#define PORT 9090
#define MAX_FDS 1024
#define BUFFER_SIZE 4096

static volatile sig_atomic_t running = 1;

typedef struct {
    int fd;
    char buffer[BUFFER_SIZE];
    size_t bytes_to_write;
    size_t write_pos;
} client_t;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1)
        return -1;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_client(struct pollfd *fds, client_t *clients, int *nfds, int index)
{
    close(fds[index].fd);

    /* swap last client into this slot to avoid shifting */
    if (index != *nfds - 1) {
        fds[index] = fds[*nfds - 1];
        clients[index] = clients[*nfds - 1];
    }

    (*nfds)--;
}

int main(void)
{
    int server_fd;
    struct sockaddr_in server_addr;

    struct pollfd fds[MAX_FDS];
    client_t *clients = NULL;

    int nfds = 1;

    long total_bytes = 0;
    long total_reads = 0;
    int active_connections = 0;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    /* create listening socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    if (set_nonblocking(server_fd) == -1) {
        perror("fcntl");
        close(server_fd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 128) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    /* allocate client state buffer on heap to avoid excessive stack usage */
    clients = malloc(MAX_FDS * sizeof(client_t));
    if (!clients) {
        perror("malloc");
        close(server_fd);
        return 1;
    }

    printf("Poll server listening on port %d\n", PORT);

    /* first entry is always the listener */
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    while (running) {
        int ret = poll(fds, nfds, 1000);

        if (ret == -1) {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        if (ret == 0)
            continue;

        /* check each fd for events */
        for (int i = 0; i < nfds; i++) {

            if (fds[i].revents == 0)
                continue;

            /* new connection on listener */
            if (i == 0) {
                while (1) {
                    int client_fd = accept(server_fd, NULL, NULL);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;

                        if (errno == EINTR)
                            continue;

                        perror("accept");
                        break;
                    }

                    if (nfds >= MAX_FDS) {
                        fprintf(stderr, "Maximum connections reached\n");
                        close(client_fd);
                        continue;
                    }

                    if (set_nonblocking(client_fd) == -1) {
                        perror("fcntl");
                        close(client_fd);
                        continue;
                    }

                    fds[nfds].fd = client_fd;
                    fds[nfds].events = POLLIN;
                    fds[nfds].revents = 0;

                    clients[nfds].fd = client_fd;
                    clients[nfds].bytes_to_write = 0;
                    clients[nfds].write_pos = 0;

                    nfds++;
                    active_connections++;
                }

                continue;
            }

            /* error or hangup */
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_client(fds, clients, &nfds, i);
                active_connections--;
                i--;
                continue;
            }

            /* read data from client */
            if (fds[i].revents & POLLIN) {
                ssize_t n;

                do {
                    n = read(fds[i].fd,
                             clients[i].buffer,
                             BUFFER_SIZE);
                } while (n == -1 && errno == EINTR);

                if (n > 0) {
                    clients[i].bytes_to_write = (size_t)n;
                    clients[i].write_pos = 0;

                    total_bytes += n;
                    total_reads++;

                    /* got data, flag that we need to write it back */
                    fds[i].events |= POLLOUT;
                }
                else if (n == 0) {
                    close_client(fds, clients, &nfds, i);
                    active_connections--;
                    i--;
                    continue;
                }
                else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    close_client(fds, clients, &nfds, i);
                    active_connections--;
                    i--;
                    continue;
                }
            }

            /* write data back to client */
            if (fds[i].revents & POLLOUT) {
                while (clients[i].write_pos <
                       clients[i].bytes_to_write) {

                    ssize_t n = write(
                        fds[i].fd,
                        clients[i].buffer + clients[i].write_pos,
                        clients[i].bytes_to_write -
                        clients[i].write_pos
                    );

                    if (n > 0) {
                        clients[i].write_pos += (size_t)n;
                    }
                    else if (n == -1 &&
                             (errno == EAGAIN ||
                              errno == EWOULDBLOCK)) {
                        break;
                    }
                    else if (n == -1 && errno == EINTR) {
                        continue;
                    }
                    else {
                        close_client(fds, clients, &nfds, i);
                        active_connections--;
                        i--;
                        goto next_client;
                    }
                }

                /* done writing, stop watching for POLLOUT */
                if (clients[i].write_pos ==
                    clients[i].bytes_to_write) {

                    clients[i].bytes_to_write = 0;
                    clients[i].write_pos = 0;

                    fds[i].events &= ~POLLOUT;
                }
            }

        next_client:
            ;
        }
    }

    printf("\nShutting down...\n");
    printf("Active connections: %d\n", active_connections);
    printf("Total read operations: %ld\n", total_reads);
    printf("Total bytes echoed: %ld\n", total_bytes);

    for (int i = 0; i < nfds; i++)
        close(fds[i].fd);

    free(clients);

    return 0;
}
