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
 * - Blocks indefinitely in poll() (timeout -1) so the process never wakes up without
 *   work to do; syscall counts therefore reflect real I/O, not idle timer wakeups
 * - Prints a machine-readable CSV statistics line on shutdown (see README)
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

/*
 * Install a handler without SA_RESTART.
 *
 * poll() now blocks with an infinite timeout, so the only way out of the event
 * loop is an interrupted syscall. sigaction() with sa_flags = 0 guarantees that
 * poll() returns -1/EINTR on SIGINT or SIGTERM, which lets the loop exit and the
 * final statistics line get printed. signal() would request SA_RESTART on glibc
 * and BSD/macOS, so it is avoided here.
 */
static int install_handler(int sig, void (*handler)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handler;
    sa.sa_flags = 0;

    if (sigemptyset(&sa.sa_mask) == -1)
        return -1;

    return sigaction(sig, &sa, NULL);
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

    /* counters reported as a CSV line on shutdown */
    unsigned long long stat_poll_calls = 0;  /* poll() syscalls issued            */
    unsigned long long stat_accepts = 0;     /* connections accepted              */
    unsigned long long stat_reads = 0;       /* read() syscalls issued            */
    unsigned long long stat_writes = 0;      /* write() syscalls issued           */
    unsigned long long stat_messages = 0;    /* echo replies fully written back   */
    unsigned long long stat_bytes = 0;       /* bytes echoed back to clients      */

    int active_connections = 0;

    if (install_handler(SIGINT, handle_signal) == -1 ||
        install_handler(SIGTERM, handle_signal) == -1 ||
        install_handler(SIGPIPE, SIG_IGN) == -1) {
        perror("sigaction");
        return 1;
    }

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
        /*
         * timeout -1: block until at least one descriptor is ready or a signal
         * arrives. A finite timeout would add periodic wakeups that inflate the
         * poll() syscall count in proportion to uptime rather than to load, which
         * would make the comparison against the other engines unfair.
         *
         * Accepted tradeoff: if a signal lands between the loop condition above
         * and this call, the flag is already clear and poll() still blocks until
         * the next event. Closing that window needs the self-pipe trick (make
         * signal delivery a pollable event); it is left out to keep the event
         * loop readable, since every run here is terminated by a client
         * disconnect or a second signal.
         */
        int ret = poll(fds, nfds, -1);

        stat_poll_calls++;

        if (ret == -1) {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

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
                    stat_accepts++;
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
                    stat_reads++;
                } while (n == -1 && errno == EINTR);

                if (n > 0) {
                    clients[i].bytes_to_write = (size_t)n;
                    clients[i].write_pos = 0;

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

                    stat_writes++;

                    if (n > 0) {
                        clients[i].write_pos += (size_t)n;
                        stat_bytes += (unsigned long long)n;
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

                    /* a fully drained buffer is one completed echo */
                    if (clients[i].bytes_to_write > 0)
                        stat_messages++;

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
    printf("Connections still open: %d\n", active_connections);

    /*
     * Machine-readable summary for the benchmark/report tooling.
     * The header is printed alongside the values so a captured server log is
     * self-describing; grep '^STATS,' to extract just the values.
     */
    printf("STATS_HEADER,engine,poll_calls,accepts,reads,writes,messages,bytes\n");
    printf("STATS,poll,%llu,%llu,%llu,%llu,%llu,%llu\n",
           stat_poll_calls,
           stat_accepts,
           stat_reads,
           stat_writes,
           stat_messages,
           stat_bytes);

    fflush(stdout);

    for (int i = 0; i < nfds; i++)
        close(fds[i].fd);

    free(clients);

    return 0;
}
