/*
 * epoll_echo_server.c - Single-threaded TCP echo server using epoll()
 *
 * Course: CS331 - Scalable Network I/O (Project ID: 13, Team ID: T016)
 * Author: Patil Nachiket Kiran (Roll No: 24110250)
 *
 * - Level-triggered epoll, non-blocking fds, EPOLLOUT registered only
 *   while a pending echo write exists (EPOLL_CTL_MOD)
 * - accept4() combines accept + set-nonblocking into one syscall
 * - Accept loop capped per wakeup so one busy listener can't starve
 *   already-connected clients in the same epoll_wait cycle
 * - Reserved idle_fd is closed/reopened on EMFILE/ENFILE so the process
 *   can still accept-and-drop instead of spinning when out of fds
 * - Doubly-linked connection list for O(1) insert/remove on churn
 * - peer_closed is set ONLY when read() returns 0. EPOLLRDHUP fires as
 *   soon as the peer's FIN arrives even if data is still buffered, so
 *   it is never used to infer close: level-triggered epoll keeps the
 *   fd readable until the buffer is actually drained down to EOF.
 */
 
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#define DEFAULT_PORT 9090
#define BACKLOG 128
#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define MAX_ACCEPTS_PER_CYCLE 64

static volatile sig_atomic_t running = 1;

typedef struct conn {
    int fd;
    int is_listener;
    int peer_closed;
    char buffer[BUFFER_SIZE];
    size_t bytes_to_write;
    size_t write_pos;
    uint32_t registered_events;
    struct conn *prev;
    struct conn *next;
} conn_t;

static conn_t *conn_list_head = NULL;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void list_add(conn_t *c)
{
    c -> prev = NULL;
    c -> next = conn_list_head;
    if (conn_list_head)
        conn_list_head -> prev = c;
    conn_list_head = c;
}

static void list_remove(conn_t *c)
{
    if (c -> prev) c -> prev -> next = c -> next;
    else conn_list_head = c -> next;
    if (c -> next) c -> next -> prev = c -> prev;
}

int main(int argc, char *argv[])
{
    int server_fd, epfd, idle_fd;
    int port = DEFAULT_PORT;
    struct sockaddr_in server_addr;
    struct epoll_event ev, events[MAX_EVENTS];
    conn_t listener_conn;

    uint64_t total_bytes = 0;
    uint64_t total_reads = 0;
    int active_connections = 0;

    /* raw syscall counters, for the benchmarker's syscalls/message metric */
    uint64_t epoll_wait_calls = 0;
    uint64_t epoll_ctl_calls = 0;
    uint64_t read_calls = 0;
    uint64_t write_calls = 0;

    if (argc > 1) {
        char *endptr;
        long p = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || p <= 0 || p > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 1;
        }
        port = (int)p;
    }

    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* reserved fd: closed and reused to accept-and-drop under EMFILE */
    idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);

    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd == -1) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }

    memset(&listener_conn, 0, sizeof(listener_conn));
    listener_conn.fd = server_fd;
    listener_conn.is_listener = 1;
    listener_conn.registered_events = EPOLLIN;

    ev.events = EPOLLIN;
    ev.data.ptr = &listener_conn;
    epoll_ctl_calls++;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        close(server_fd);
        close(epfd);
        return 1;
    }

    printf("Epoll server listening on port %d\n", port);

    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        epoll_wait_calls++;

        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            conn_t *c = events[i].data.ptr;

            /* --- listener: drain pending connections --- */
            if (c -> is_listener) {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    running = 0;
                    break;
                }

                int accepts = 0;
                while (accepts++ < MAX_ACCEPTS_PER_CYCLE) {
                    int client_fd = accept4(server_fd, NULL, NULL,
                                             SOCK_NONBLOCK | SOCK_CLOEXEC);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;

                        if (errno == EMFILE || errno == ENFILE) {
                            if (idle_fd != -1) close(idle_fd);
                            int discard = accept(server_fd, NULL, NULL);
                            if (discard != -1) close(discard);
                            idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
                            break;
                        }
                        perror("accept4");
                        break;
                    }

                    conn_t *nc = calloc(1, sizeof(conn_t));
                    if (!nc) { perror("calloc"); close(client_fd); continue; }

                    nc -> fd = client_fd;
                    nc -> registered_events = EPOLLIN | EPOLLRDHUP;

                    struct epoll_event cev;
                    cev.events = nc -> registered_events;
                    cev.data.ptr = nc;

                    epoll_ctl_calls++;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev) == -1) {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                        free(nc);
                        continue;
                    }

                    list_add(nc);
                    active_connections++;
                }
                continue;
            }

            /* --- client: read (only if no write pending), then write --- */
            if ((events[i].events & EPOLLIN) &&
                !c -> peer_closed && c -> bytes_to_write == 0) {
                ssize_t r;
                do {
                    r = read(c -> fd, c -> buffer, BUFFER_SIZE);
                    read_calls++;
                } while (r == -1 && errno == EINTR);

                if (r > 0) {
                    c -> bytes_to_write = (size_t)r;
                    c -> write_pos = 0;
                    total_reads++;
                } else if (r == 0) {
                    c -> peer_closed = 1;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    goto cleanup_conn;
                }
            }

            if (c -> bytes_to_write > 0) {
                while (c -> write_pos < c -> bytes_to_write) {
                    ssize_t w = write(c -> fd, c -> buffer + c -> write_pos,
                                      c -> bytes_to_write - c -> write_pos);
                    write_calls++;

                    if (w > 0) {
                        c -> write_pos += (size_t)w;
                        total_bytes += (uint64_t)w;
                    } else if (w == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else if (w == -1 && errno == EINTR) {
                        continue;
                    } else {
                        goto cleanup_conn;
                    }
                }

                if (c -> write_pos == c -> bytes_to_write) {
                    c -> bytes_to_write = 0;
                    c -> write_pos = 0;
                }
            }

            if (c -> peer_closed && c -> bytes_to_write == 0)
                goto cleanup_conn;

            if (events[i].events & (EPOLLERR | EPOLLHUP))
                goto cleanup_conn;

            /* re-arm interest set only if it actually changed */
            {
                uint32_t desired = (c -> bytes_to_write > 0)
                                    ? (EPOLLOUT | EPOLLRDHUP)
                                    : (EPOLLIN | EPOLLRDHUP);

                if (desired != c -> registered_events) {
                    struct epoll_event mev;
                    mev.events = desired;
                    mev.data.ptr = c;

                    epoll_ctl_calls++;
                    if (epoll_ctl(epfd, EPOLL_CTL_MOD, c -> fd, &mev) == -1) {
                        perror("epoll_ctl: MOD");
                        goto cleanup_conn;
                    }
                    c -> registered_events = desired;
                }
            }
            continue;

        cleanup_conn:
            list_remove(c);
            epoll_ctl_calls++;
            epoll_ctl(epfd, EPOLL_CTL_DEL, c -> fd, NULL);
            close(c -> fd);
            free(c);
            active_connections--;
        }
    }

    printf("\nShutting down...\n");
    printf("Active connections: %d\n", active_connections);
    printf("Total read operations: %" PRIu64 "\n", total_reads);
    printf("Total bytes echoed: %" PRIu64 "\n", total_bytes);

    conn_t *cur = conn_list_head;
    while (cur) {
        conn_t *next = cur -> next;
        epoll_ctl_calls++;
        epoll_ctl(epfd, EPOLL_CTL_DEL, cur -> fd, NULL);
        close(cur -> fd);
        free(cur);
        cur = next;
    }

    /* CSV row for the benchmarker: same schema across all four engines.
     * engine,port,active_connections,messages,read_calls,write_calls,epoll_wait_calls,epoll_ctl_calls,total_bytes */
    printf("CSV,epoll,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
           port, active_connections, total_reads, read_calls, write_calls,
           epoll_wait_calls, epoll_ctl_calls, total_bytes);

    if (idle_fd != -1) close(idle_fd);
    close(server_fd);
    close(epfd);
    return 0;
}