/*
 * epoll_echo_server.c - Single-threaded TCP echo server using epoll()
 *
 * Course: CS331 - Scalable Network I/O (Project ID: 13, Team ID: T016)
 * Author: Patil Nachiket Kiran (Roll No: 24110250)
 *
 * Implementation details:
 * - Uses epoll (level-triggered) for I/O multiplexing in a single thread
 * - Sets listening and client sockets to non-blocking mode (O_NONBLOCK)
 * - Registers EPOLLOUT only when pending echo data exists (EPOLL_CTL_MOD)
 * - Drains new incoming connections using a non-blocking accept() loop
 * - Per-connection state is heap-allocated; epoll_event.data.ptr points
 *   directly at it, avoiding a separate fd-to-state lookup table
 * - A singly-linked list of live connections exists only to allow a clean
 *   walk-and-close on shutdown (epoll itself does not expose this)
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
#include <sys/epoll.h>

#define PORT 9003
#define BACKLOG 128
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096

static volatile sig_atomic_t running = 1;

typedef struct conn {
    int fd;
    int is_listener;
    char buffer[BUFFER_SIZE];
    size_t bytes_to_write;
    size_t write_pos;
    struct conn *next;
} conn_t;

static conn_t *conn_list_head = NULL;

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

int main(void)
{
    int server_fd, epfd;
    struct sockaddr_in server_addr;
    struct epoll_event ev, events[MAX_EVENTS];
    conn_t listener_conn;

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

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    /* create epoll instance and register the listener */
    epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }

    listener_conn.fd = server_fd;
    listener_conn.is_listener = 1;

    ev.events = EPOLLIN;
    ev.data.ptr = &listener_conn;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        close(server_fd);
        close(epfd);
        return 1;
    }

    printf("Epoll server listening on port %d\n", PORT);

    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 1000);

        if (n == -1) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        if (n == 0)
            continue;

        for (int i = 0; i < n; i++) {
            conn_t *c = events[i].data.ptr;

            /* new connection(s) on listener */
            if (c->is_listener) {
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

                    if (set_nonblocking(client_fd) == -1) {
                        perror("fcntl");
                        close(client_fd);
                        continue;
                    }

                    conn_t *nc = malloc(sizeof(conn_t));
                    if (!nc) {
                        perror("malloc");
                        close(client_fd);
                        continue;
                    }
                    nc->fd = client_fd;
                    nc->is_listener = 0;
                    nc->bytes_to_write = 0;
                    nc->write_pos = 0;

                    struct epoll_event cev;
                    cev.events = EPOLLIN;
                    cev.data.ptr = nc;

                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev) == -1) {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                        free(nc);
                        continue;
                    }

                    nc->next = conn_list_head;
                    conn_list_head = nc;

                    active_connections++;
                }

                continue;
            }

            /* error or hangup */
            if (events[i].events & (EPOLLERR | EPOLLHUP))
                goto cleanup_conn;

            /* read data from client */
            if (events[i].events & EPOLLIN) {
                ssize_t r;

                do {
                    r = read(c->fd, c->buffer, BUFFER_SIZE);
                } while (r == -1 && errno == EINTR);

                if (r > 0) {
                    c->bytes_to_write = (size_t)r;
                    c->write_pos = 0;
                    total_bytes += r;
                    total_reads++;
                    goto try_write;
                }
                else if (r == 0) {
                    goto cleanup_conn;
                }
                else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    goto cleanup_conn;
                }
            }

            /* write pending data back to client */
            if (events[i].events & EPOLLOUT) {
            try_write:
                while (c->write_pos < c->bytes_to_write) {
                    ssize_t w = write(c->fd,
                                      c->buffer + c->write_pos,
                                      c->bytes_to_write - c->write_pos);

                    if (w > 0) {
                        c->write_pos += (size_t)w;
                    }
                    else if (w == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    else if (w == -1 && errno == EINTR) {
                        continue;
                    }
                    else {
                        goto cleanup_conn;
                    }
                }

                struct epoll_event mev;
                mev.data.ptr = c;

                if (c->write_pos == c->bytes_to_write) {
                    c->bytes_to_write = 0;
                    c->write_pos = 0;
                    mev.events = EPOLLIN;
                } else {
                    mev.events = EPOLLIN | EPOLLOUT;
                }
                epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &mev);
            }

            continue;

        cleanup_conn: {
            conn_t **pp = &conn_list_head;
            while (*pp && *pp != c) pp = &(*pp)->next;
            if (*pp) *pp = c->next;

            epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
            close(c->fd);
            free(c);
            active_connections--;
        }
        }
    }

    printf("\nShutting down...\n");
    printf("Active connections: %d\n", active_connections);
    printf("Total read operations: %ld\n", total_reads);
    printf("Total bytes echoed: %ld\n", total_bytes);

    conn_t *cur = conn_list_head;
    while (cur) {
        conn_t *next = cur->next;
        epoll_ctl(epfd, EPOLL_CTL_DEL, cur->fd, NULL);
        close(cur->fd);
        free(cur);
        cur = next;
    }

    close(server_fd);
    close(epfd);

    return 0;
}