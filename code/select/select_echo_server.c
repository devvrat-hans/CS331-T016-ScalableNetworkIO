/*
 * select_echo_server.c - Single-threaded TCP echo server using select()
 *
 * Course: CS331 - Scalable Network I/O (Project ID: 13, Team ID: T016)
 * Author: Parth Kale (Roll No: 24110242)
 *
 * Implementation details:
 * - Uses select() for I/O multiplexing in a single thread.
 * - Client state is indexed directly by file descriptor. select() reports
 *   readiness as a bitmap over fd values and never says *which* fds are ready,
 *   so there is no compact slot array to maintain and no compaction step.
 * - Every loop iteration makes three linear passes:
 *       1. rebuild both fd_sets from local state   (userspace, O(max_fd))
 *       2. select() itself                         (kernel,    O(max_fd))
 *       3. FD_ISSET scan to discover ready fds      (userspace, O(max_fd))
 *   Cost therefore tracks the highest live fd NUMBER, not the connection
 *   count. Ten connections, one of which happens to be fd 900, costs ~901
 *   slots of work per iteration; poll() would cost 11.
 * - Read and write readiness are mutually exclusive per connection: a
 *   connection is either accepting input or flushing it, never both. That
 *   gives backpressure for free and makes it structurally impossible to
 *   overwrite a buffer still holding unsent data.
 * - Hard ceiling is FD_SETSIZE (1024 with glibc). FD_SET() on an fd at or
 *   above that limit writes past the end of fd_set, so accept() rejects such
 *   descriptors explicitly rather than corrupting memory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define DEFAULT_PORT   9090
#define BUFFER_SIZE    4096
#define LISTEN_BACKLOG 128
#define MAX_FDS        FD_SETSIZE  /* the mechanism's limit, not an arbitrary cap */

static volatile sig_atomic_t running = 1;

typedef struct {
    int    in_use;
    size_t bytes_to_write;
    size_t write_pos;
    char   buffer[BUFFER_SIZE];
} client_t;

typedef struct {
    unsigned long      select_calls;
    unsigned long      read_calls;
    unsigned long      write_calls;
    unsigned long      accept_calls;
    unsigned long      messages;
    unsigned long      accepts_total;
    unsigned long      rejected_fd_too_high;
    unsigned long      maxfd_recomputes;
    unsigned long long bytes_read;
    unsigned long long bytes_written;
    long               live_connections;
    int                peak_max_fd;
} stats_t;

static stats_t stats;

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

/*
 * select() scans 0..nfds-1 on every call, so a stale high max_fd keeps charging
 * us for a walk across dead descriptors. Shrinking it is itself a downward
 * scan: an O(N) cost on disconnect that poll() has no equivalent of.
 */
static int recompute_max_fd(const client_t *clients, int listen_fd, int old_max)
{
    int fd;

    for (fd = old_max - 1; fd >= 0; fd--)
        if (fd != listen_fd && clients[fd].in_use)
            break;

    return (fd > listen_fd) ? fd : listen_fd;
}

static void close_client(client_t *clients, int fd, int listen_fd, int *max_fd)
{
    close(fd);
    clients[fd].in_use = 0;
    clients[fd].bytes_to_write = 0;
    clients[fd].write_pos = 0;
    stats.live_connections--;

    if (fd == *max_fd) {
        *max_fd = recompute_max_fd(clients, listen_fd, *max_fd);
        stats.maxfd_recomputes++;
    }
}

static void accept_new(client_t *clients, int listen_fd, int *max_fd)
{
    for (;;) {
        int client_fd;

        stats.accept_calls++;
        client_fd = accept(listen_fd, NULL, NULL);

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;                     /* backlog drained */
            if (errno == EINTR)
                continue;
            perror("accept");
            return;
        }

        /*
         * The select() wall. Note this tests the fd VALUE, not a connection
         * count: fd numbers fragment as connections churn, so this can fire
         * with well under FD_SETSIZE connections actually open.
         */
        if (client_fd >= FD_SETSIZE) {
            stats.rejected_fd_too_high++;
            close(client_fd);
            continue;
        }

        if (set_nonblocking(client_fd) == -1) {
            perror("fcntl");
            close(client_fd);
            continue;
        }

        clients[client_fd].in_use = 1;
        clients[client_fd].bytes_to_write = 0;
        clients[client_fd].write_pos = 0;

        if (client_fd > *max_fd)
            *max_fd = client_fd;
        if (client_fd > stats.peak_max_fd)
            stats.peak_max_fd = client_fd;

        stats.accepts_total++;
        stats.live_connections++;
    }
}

static void handle_read(client_t *clients, int fd, int listen_fd, int *max_fd)
{
    ssize_t n;

    /*
     * One read() per readiness event, matching the poll engine. select() is
     * level-triggered, so anything left in the socket buffer simply re-notifies
     * on the next iteration: correct, just not maximally efficient.
     */
    do {
        stats.read_calls++;
        n = read(fd, clients[fd].buffer, BUFFER_SIZE);
    } while (n == -1 && errno == EINTR);

    if (n > 0) {
        clients[fd].bytes_to_write = (size_t)n;
        clients[fd].write_pos = 0;
        stats.bytes_read += (unsigned long long)n;
        stats.messages++;
        return;        /* non-zero bytes_to_write moves this fd to the write set */
    }

    if (n == 0) {                           /* orderly shutdown by the peer */
        close_client(clients, fd, listen_fd, max_fd);
        return;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;                             /* spurious readiness */

    close_client(clients, fd, listen_fd, max_fd);
}

static void handle_write(client_t *clients, int fd, int listen_fd, int *max_fd)
{
    while (clients[fd].write_pos < clients[fd].bytes_to_write) {
        ssize_t n;

        stats.write_calls++;
        n = write(fd,
                  clients[fd].buffer + clients[fd].write_pos,
                  clients[fd].bytes_to_write - clients[fd].write_pos);

        if (n > 0) {
            clients[fd].write_pos += (size_t)n;
            stats.bytes_written += (unsigned long long)n;
            continue;
        }
        if (n == -1 && errno == EINTR)
            continue;
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;                  /* stay in the write set, resume next pass */

        close_client(clients, fd, listen_fd, max_fd);
        return;
    }

    clients[fd].bytes_to_write = 0;   /* fully flushed: back to the read set */
    clients[fd].write_pos = 0;
}

static void print_stats(int port)
{
    unsigned long long syscalls = (unsigned long long)stats.select_calls
                                + stats.read_calls
                                + stats.write_calls
                                + stats.accept_calls;
    /* Two sets, copied in and back out again, per select() call. */
    long bitmap_bytes = (((long)stats.peak_max_fd + 1) + 7) / 8 * 4;

    printf("\nShutting down...\n");
    printf("Active connections: %ld\n", stats.live_connections);
    printf("Total read operations: %lu\n", stats.read_calls);
    printf("Total bytes echoed: %llu\n", stats.bytes_written);
    printf("select() calls: %lu\n", stats.select_calls);
    printf("write operations: %lu\n", stats.write_calls);
    printf("connections accepted: %lu\n", stats.accepts_total);
    printf("rejected (fd >= FD_SETSIZE): %lu\n", stats.rejected_fd_too_high);
    printf("max_fd recomputations: %lu\n", stats.maxfd_recomputes);
    printf("peak max_fd: %d (FD_SETSIZE = %d)\n",
           stats.peak_max_fd, FD_SETSIZE);
    printf("fd_set bytes crossing the syscall boundary per select(): %ld\n",
           bitmap_bytes);
    printf("bytes read vs echoed: %llu vs %llu%s\n",
           stats.bytes_read, stats.bytes_written,
           stats.bytes_read == stats.bytes_written ? " (balanced)"
                                                  : " (MISMATCH)");

    /* The final two lines are a one-row CSV for the benchmark harness. */
    printf("engine,port,select_calls,read_calls,write_calls,accept_calls,"
           "messages,accepts_total,rejected_fd_too_high,maxfd_recomputes,"
           "bytes_read,bytes_written,peak_max_fd,fdset_bytes_per_call,"
           "syscalls_per_msg\n");
    printf("select,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%llu,%llu,%d,%ld,%.3f\n",
           port,
           stats.select_calls, stats.read_calls, stats.write_calls,
           stats.accept_calls, stats.messages, stats.accepts_total,
           stats.rejected_fd_too_high, stats.maxfd_recomputes,
           stats.bytes_read, stats.bytes_written,
           stats.peak_max_fd, bitmap_bytes,
           stats.messages ? (double)syscalls / (double)stats.messages : 0.0);
}

int main(int argc, char **argv)
{
    struct sockaddr_in server_addr;
    client_t *clients;
    int listen_fd, max_fd, i, opt = 1;
    int port = DEFAULT_PORT;

    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "usage: %s [port]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return 1;
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    if (set_nonblocking(listen_fd) == -1) {
        perror("fcntl");
        close(listen_fd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, LISTEN_BACKLOG) == -1) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    /* ~4 MB of client state; heap rather than stack, as in the poll engine. */
    clients = calloc(MAX_FDS, sizeof(client_t));
    if (!clients) {
        perror("calloc");
        close(listen_fd);
        return 1;
    }

    max_fd = listen_fd;
    stats.peak_max_fd = listen_fd;
    printf("select server listening on port %d (FD_SETSIZE = %d)\n",
           port, FD_SETSIZE);
    fflush(stdout);

    while (running) {
        fd_set rfds, wfds;
        struct timeval tv;
        int ready, fd, scan_max;

        /* ---- PASS 1: rebuild. select() destroys the sets on every call. ---- */
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(listen_fd, &rfds);

        for (fd = 0; fd <= max_fd; fd++) {
            if (fd == listen_fd || !clients[fd].in_use)
                continue;
            if (clients[fd].bytes_to_write == 0)
                FD_SET(fd, &rfds);
            else
                FD_SET(fd, &wfds);
        }

        /*
         * Linux writes the remaining time back into tv, so it has to be reset
         * every iteration or the timeout decays to zero and this turns into a
         * busy-poll. poll() takes a plain int and has no such hazard.
         */
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        /* ---- PASS 2: nfds is max_fd + 1, NOT a connection count. ---- */
        stats.select_calls++;
        ready = select(max_fd + 1, &rfds, &wfds, NULL, &tv);

        if (ready == -1) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }
        if (ready == 0)
            continue;

        /* Snapshot before accept(): fds added below were not in the sets we
           handed to select(), so they must not be scanned this round. */
        scan_max = max_fd;

        if (FD_ISSET(listen_fd, &rfds))
            accept_new(clients, listen_fd, &max_fd);

        /* ---- PASS 3: select returned a count, not identities. Go find them. -- */
        for (fd = 0; fd <= scan_max; fd++) {
            if (fd == listen_fd || !clients[fd].in_use)
                continue;
            if (FD_ISSET(fd, &rfds))
                handle_read(clients, fd, listen_fd, &max_fd);
            if (clients[fd].in_use && FD_ISSET(fd, &wfds))
                handle_write(clients, fd, listen_fd, &max_fd);
        }
    }

    print_stats(port);

    for (i = 0; i < MAX_FDS; i++)
        if (clients[i].in_use)
            close(i);
    close(listen_fd);
    free(clients);
    return 0;
}