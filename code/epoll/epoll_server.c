#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <time.h>
#include <sys/epoll.h>

#define ENGINE_NAME "epoll"
#define DEFAULT_PORT 9090
#define DEFAULT_MAX_CLIENTS 10000
#define LISTEN_BACKLOG 128
#define BUFFER_SIZE 4096
#define MAX_TRANSFER (64ULL * 1024 * 1024)
#define WAIT_MS 100
#define ACCEPT_BATCH 64

/* client_t state and server counters. */
typedef enum { ECHO, UPLOAD, DOWNLOAD } app_t;
typedef enum { HEADER, RECEIVE, SEND, ACK } stage_t;
typedef struct server_t server_t;

typedef struct client_t {
    int fd;
    stage_t stage;
    unsigned char buffer[BUFFER_SIZE];
    size_t done, want;    /* Progress through the current buffer. */
    uint64_t total, left; /* Payload bytes in this request. */
    server_t *server;
    struct client_t *prev, *next; /* Shutdown list, not scanned for I/O. */
    struct client_t *ready_next;  /* Used only by io_uring's submission queue. */
    int pending;                  /* io_uring owns the buffer while this is true. */
} client_t;

struct server_t {
    int listener, port;
    size_t max_clients;
    app_t app;
    client_t *clients;
    uint64_t accepted, closed, active, peak, rejected, errors, incomplete;
    uint64_t messages, bytes_read, bytes_written, loop_calls;
};

/* Application and connection helpers: identical behavior in all servers. */
static volatile sig_atomic_t stop_requested = 0;
static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void next_header(client_t *c)
{
    c->stage = HEADER;
    c->done = 0;
    c->want = 8;
    c->total = c->left = 0;
}

static client_t *client_add(server_t *s, int fd)
{
    if (s->active >= s->max_clients) {
        s->rejected++;
        close(fd);
        return NULL;
    }
    client_t *c = calloc(1, sizeof(*c));
    if (!c) {
        s->rejected++;
        close(fd);
        return NULL;
    }
    c->fd = fd;
    c->server = s;
    next_header(c);
    c->next = s->clients;
    if (c->next) {
        c->next->prev = c;
    }
    s->clients = c;
    s->accepted++;
    if (++s->active > s->peak) {
        s->peak = s->active;
    }
    return c;
}

static void client_close(client_t *c, int error)
{
    server_t *s = c->server;
    if (error < 0) {
        s->errors++;
    }
    if (c->stage != HEADER || c->done != 0) {
        s->incomplete++;
    }
    if (c->prev) {
        c->prev->next = c->next;
    } else {
        s->clients = c->next;
    }
    if (c->next) {
        c->next->prev = c->prev;
    }
    close(c->fd);
    s->active--;
    s->closed++;
    free(c);
}

static int accept_client(server_t *s)
{
    int fd = accept4(s->listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    int enabled = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) < 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    return fd;
}

static int client_is_reading(const client_t *c)
{
    return c->stage == HEADER || c->stage == RECEIVE;
}
static unsigned char *client_buffer(client_t *c)
{
    return c->buffer + c->done;
}
static size_t client_length(const client_t *c)
{
    return c->want - c->done;
}

static void prepare_chunk(client_t *c, stage_t stage)
{
    c->stage = stage;
    c->done = 0;
    c->want = c->left < BUFFER_SIZE ? (size_t)c->left : BUFFER_SIZE;
    if (stage == SEND && c->server->app == DOWNLOAD) {
        uint64_t offset = c->total - c->left;
        for (size_t i = 0; i < c->want; i++) {
            c->buffer[i] = (unsigned char)((offset + i) % 251);
        }
    }
}

static int client_complete(client_t *c, ssize_t result)
{
    if (result < 0) {
        if (result == -EAGAIN || result == -EWOULDBLOCK || result == -EINTR) {
            return 0;
        }
        return (int)result;
    }
    if (result == 0) {
        return c->stage == HEADER && c->done == 0 ? 1 : -ECONNRESET;
    }
    if ((size_t)result > client_length(c)) {
        return -EIO;
    }

    if (client_is_reading(c)) {
        c->server->bytes_read += (uint64_t)result;
    } else {
        c->server->bytes_written += (uint64_t)result;
    }

    /* Uploads use a known pattern so a final acknowledgement means the data
     * was verified, not merely read. Verification cost is shared by engines. */
    if (c->stage == RECEIVE && c->server->app == UPLOAD) {
        uint64_t offset = c->total - c->left + c->done;
        for (size_t i = 0; i < (size_t)result; i++) {
            if (c->buffer[c->done + i] != (unsigned char)((offset + i) % 251)) {
                return -EBADMSG;
            }
        }
    }
    c->done += (size_t)result;
    if (c->done != c->want) {
        return 0; /* Partial header/body/send: keep going. */
    }

    if (c->stage == HEADER) {
        uint64_t length = 0;
        for (size_t i = 0; i < 8; i++) {
            length = (length << 8) | c->buffer[i];
        }
        if (!length || length > MAX_TRANSFER || (c->server->app == ECHO && length > BUFFER_SIZE)) {
            return -EMSGSIZE;
        }
        c->total = c->left = length;
        prepare_chunk(c, c->server->app == DOWNLOAD ? SEND : RECEIVE);
    } else if (c->stage == RECEIVE) {
        if (c->server->app == ECHO) {
            c->stage = SEND;
            c->done = 0;
        } else {
            c->left -= c->want;
            if (c->left) {
                prepare_chunk(c, RECEIVE);
            } else {
                c->stage = ACK;
                c->buffer[0] = 'K';
                c->done = 0;
                c->want = 1;
            }
        }
    } else if (c->stage == SEND) {
        c->left -= c->want;
        if (c->left) {
            prepare_chunk(c, SEND);
        } else {
            c->server->messages++;
            next_header(c);
        }
    } else { /* Complete upload acknowledgement. */
        c->server->messages++;
        next_header(c);
    }
    return 0;
}

static int client_step(client_t *c)
{
    ssize_t n;
    if (client_is_reading(c)) {
        n = recv(c->fd, client_buffer(c), client_length(c), 0);
    } else {
        n = send(c->fd, client_buffer(c), client_length(c), MSG_NOSIGNAL);
    }
    return client_complete(c, n < 0 ? -errno : n);
}

static int number(const char *text, unsigned long limit, unsigned long *out)
{
    char *end;
    errno = 0;
    if (!text[0] || text[0] == '-') {
        return -1;
    }
    unsigned long value = strtoul(text, &end, 10);
    if (errno || *end || value > limit) {
        return -1;
    }
    *out = value;
    return 0;
}

static double seconds(struct timeval value)
{
    return value.tv_sec + value.tv_usec / 1e6;
}

static long peak_rss_kib(void)
{
    /* Current executable's memory high-water mark. getrusage's process-lifetime
     * maximum may include the launcher's pre-exec image, so retain it separately. */
    FILE *file = fopen("/proc/self/status", "r");
    if (!file) {
        return -1;
    }
    char line[256];
    long value = -1;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "VmHWM: %ld kB", &value) == 1) {
            break;
        }
    }
    fclose(file);
    return value;
}

/* I/O event loop: this is the mechanism-specific part. */
static int run_event_loop(server_t *s)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        return -errno;
    }
    struct epoll_event interest = {.events = EPOLLIN, .data.ptr = NULL};
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, s->listener, &interest)) {
        int error = errno;
        close(epfd);
        return -error;
    }
    int result = 0;
    printf("READY %d\n", s->port);
    fflush(stdout);
    while (!stop_requested) {
        struct epoll_event events[256];
        s->loop_calls++;
        int n = epoll_wait(epfd, events, 256, WAIT_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            result = -errno;
            break;
        }
        for (int i = 0; i < n; i++) {
            client_t *c = events[i].data.ptr;
            if (!c) { /* NULL identifies the listening socket. */
                for (int j = 0; j < ACCEPT_BATCH; j++) {
                    int fd = accept_client(s);
                    if (fd == -EAGAIN || fd == -EWOULDBLOCK) {
                        break;
                    }
                    if (fd == -EINTR) {
                        continue;
                    }
                    if (fd < 0) {
                        result = fd;
                        goto finished;
                    }
                    c = client_add(s, fd);
                    if (!c) {
                        continue;
                    }
                    interest = (struct epoll_event){.events = EPOLLIN, .data.ptr = c};
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &interest)) {
                        client_close(c, -errno);
                    }
                }
                continue;
            }
            int was_reading = client_is_reading(c);
            int rc = client_step(c);
            if (rc) {
                epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                client_close(c, rc);
            } else if (was_reading != client_is_reading(c)) {
                interest.events = client_is_reading(c) ? EPOLLIN : EPOLLOUT;
                interest.data.ptr = c;
                if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &interest)) {
                    int error = errno;
                    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                    client_close(c, -error);
                }
            }
        }
    }
finished:
    close(epfd);
    return result;
}

/* Program entry: arguments, listener, signals, cleanup, and final results. */
int main(int argc, char **argv)
{
    server_t s = {
        .listener = -1, .port = DEFAULT_PORT, .max_clients = DEFAULT_MAX_CLIENTS, .app = ECHO};
    const char *address = "127.0.0.1";
    const char *app_name = "echo";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            printf("Usage: %s [--port 9090] [--bind 127.0.0.1] [--app echo|upload|download] "
                   "[--max-clients 10000]\n",
                   argv[0]);
            return 0;
        }
        if (i + 1 == argc) {
            fprintf(stderr, "Missing option value\n");
            return 2;
        }
        const char *key = argv[i], *value = argv[++i];
        unsigned long n;
        if (!strcmp(key, "--port") && !number(value, 65535, &n)) {
            s.port = (int)n;
        } else if (!strcmp(key, "--max-clients") && !number(value, 100000, &n) && n) {
            s.max_clients = n;
        } else if (!strcmp(key, "--bind")) {
            address = value;
        } else if (!strcmp(key, "--app") && !strcmp(value, "echo")) {
            s.app = ECHO;
            app_name = value;
        } else if (!strcmp(key, "--app") && !strcmp(value, "upload")) {
            s.app = UPLOAD;
            app_name = value;
        } else if (!strcmp(key, "--app") && !strcmp(value, "download")) {
            s.app = DOWNLOAD;
            app_name = value;
        } else {
            fprintf(stderr, "Invalid option: %s %s\n", key, value);
            return 2;
        }
    }
    struct sigaction action = {0};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        return 1;
    }
    s.listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (s.listener < 0) {
        perror("socket");
        return 1;
    }
    int enabled = 1;
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)s.port)};
    if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) {
        fprintf(stderr, "--bind requires a numeric IPv4 address\n");
        close(s.listener);
        return 2;
    }
    if (setsockopt(s.listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ||
        bind(s.listener, (struct sockaddr *)&addr, sizeof(addr)) ||
        listen(s.listener, LISTEN_BACKLOG)) {
        perror("listen setup");
        close(s.listener);
        return 1;
    }
    socklen_t length = sizeof(addr);
    if (getsockname(s.listener, (struct sockaddr *)&addr, &length)) {
        perror("getsockname");
        close(s.listener);
        return 1;
    }
    s.port = ntohs(addr.sin_port);
    struct timespec start, finish;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) {
        perror("clock");
        close(s.listener);
        return 1;
    }
    /* The event loop prints READY only after its own initialization succeeds. */
    int rc = run_event_loop(&s);
    while (s.clients) {
        client_close(s.clients, 0);
    }
    close(s.listener);
    if (clock_gettime(CLOCK_MONOTONIC, &finish)) {
        perror("clock");
        return 1;
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)) {
        perror("getrusage");
        return 1;
    }
    double elapsed = finish.tv_sec - start.tv_sec + (finish.tv_nsec - start.tv_nsec) / 1e9;
    printf("{\"engine\":\"%s\",\"app\":\"%s\",\"server_ok\":%s,"
           "\"accepted\":%" PRIu64 ",\"closed\":%" PRIu64 ",\"peak\":%" PRIu64 ","
           "\"rejected\":%" PRIu64 ",\"errors\":%" PRIu64 ",\"incomplete\":%" PRIu64 ","
           "\"messages\":%" PRIu64 ",\"wire_bytes_read\":%" PRIu64
           ",\"wire_bytes_written\":%" PRIu64 ","
           "\"loop_calls\":%" PRIu64 ",\"elapsed_s\":%.6f,\"user_cpu_s\":%.6f,"
           "\"system_cpu_s\":%.6f,\"max_rss_kib\":%ld,\"rusage_maxrss_kib\":%ld,"
           "\"rss_source\":\"proc_self_status_VmHWM\"}\n",
           ENGINE_NAME, app_name, rc == 0 ? "true" : "false", s.accepted, s.closed, s.peak,
           s.rejected, s.errors, s.incomplete, s.messages, s.bytes_read, s.bytes_written,
           s.loop_calls, elapsed, seconds(usage.ru_utime), seconds(usage.ru_stime), peak_rss_kib(),
           usage.ru_maxrss);
    if (rc) {
        fprintf(stderr, "%s: %s\n", ENGINE_NAME, strerror(-rc));
    }
    return rc ? 1 : 0;
}
