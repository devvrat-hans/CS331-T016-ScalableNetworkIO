/*
 * framework.c - everything that is the same for all four engines: command line,
 * sockets, signals, the TCP request state machine, UDP handling, worker threads
 * and the final statistics.
 */
#define _GNU_SOURCE
#include "framework.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 9090
#define DEFAULT_MAX_CLIENTS 10000
#define LISTEN_BACKLOG 1024
#define UDP_SOCKET_BUFFER (8 * 1024 * 1024)

volatile sig_atomic_t stop_requested = 0;

static atomic_size_t live_clients;
static pthread_barrier_t ready_barrier;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

/* ---- TCP connection state machine ---- */

static void next_header(conn_t *c)
{
    c->stage = HEADER;
    c->done = 0;
    c->want = 8;
    c->length = c->body_left = c->response_total = c->response_left = 0;
}

conn_t *conn_add(server_t *s, int fd)
{
    if (atomic_fetch_add(&live_clients, 1) >= s->config->max_clients) {
        atomic_fetch_sub(&live_clients, 1);
        s->rejected++;
        close(fd);
        return NULL;
    }
    conn_t *c = calloc(1, sizeof(*c) + s->config->buffer_size);
    if (!c) {
        atomic_fetch_sub(&live_clients, 1);
        s->rejected++;
        close(fd);
        return NULL;
    }
    c->fd = fd;
    c->server = s;
    if (s->config->app->on_open && s->config->app->on_open(c)) {
        free(c);
        atomic_fetch_sub(&live_clients, 1);
        s->rejected++;
        close(fd);
        return NULL;
    }
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

void conn_close(conn_t *c, int error)
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
    if (s->config->app->on_close) {
        s->config->app->on_close(c);
    }
    close(c->fd);
    s->active--;
    s->closed++;
    atomic_fetch_sub(&live_clients, 1);
    free(c);
}

int accept_client(server_t *s)
{
    s->sc_accept++;
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

int accept_ready(server_t *s, int (*add_cb)(server_t *, conn_t *, void *), void *context)
{
    for (int i = 0; i < ACCEPT_BATCH; i++) {
        int fd = accept_client(s);
        if (fd == -EAGAIN || fd == -EWOULDBLOCK) {
            break;
        }
        if (fd == -EINTR || fd == -ECONNABORTED) {
            continue;
        }
        if (fd < 0) {
            return fd;
        }
        conn_t *c = conn_add(s, fd);
        if (c) {
            int rc = add_cb(s, c, context);
            if (rc < 0) {
                return rc;
            }
        }
    }
    return 0;
}

int conn_is_reading(const conn_t *c)
{
    return c->stage == HEADER || c->stage == RECEIVE;
}

unsigned char *conn_buffer(conn_t *c)
{
    return c->buffer + c->done;
}

size_t conn_length(const conn_t *c)
{
    return c->want - c->done;
}

static void prepare_receive(conn_t *c)
{
    c->stage = RECEIVE;
    c->done = 0;
    size_t chunk = c->server->config->buffer_size;
    c->want = c->body_left < chunk ? (size_t)c->body_left : chunk;
}

static void prepare_send(conn_t *c)
{
    const app_ops_t *app = c->server->config->app;
    c->stage = SEND;
    c->done = 0;
    size_t chunk = c->server->config->buffer_size;
    c->want = c->response_left < chunk ? (size_t)c->response_left : chunk;
    if (app->fill) {
        app->fill(c, c->response_total - c->response_left, c->buffer, c->want);
    }
    /* A NULL fill (echo) sends the request bytes already in the buffer. */
}

int conn_complete(conn_t *c, ssize_t result)
{
    const app_ops_t *app = c->server->config->app;
    if (result < 0) {
        if (result == -EAGAIN || result == -EWOULDBLOCK || result == -EINTR) {
            return 0;
        }
        return (int)result;
    }
    if (result == 0) {
        return c->stage == HEADER && c->done == 0 ? 1 : -ECONNRESET;
    }
    if ((size_t)result > conn_length(c)) {
        return -EIO;
    }
    if (conn_is_reading(c)) {
        c->server->bytes_read += (uint64_t)result;
    } else {
        c->server->bytes_written += (uint64_t)result;
    }
    if (c->stage == RECEIVE && app->on_body) {
        uint64_t received = c->length - c->body_left + c->done;
        int rc = app->on_body(c, received, c->buffer + c->done, (size_t)result);
        if (rc) {
            return rc;
        }
    }
    c->done += (size_t)result;
    if (c->done != c->want) {
        return 0; /* Partial header, body or send: keep going. */
    }

    if (c->stage == HEADER) {
        uint64_t length = 0;
        for (size_t i = 0; i < 8; i++) {
            length = (length << 8) | c->buffer[i];
        }
        int rc = app->validate(length);
        if (rc) {
            return rc;
        }
        c->length = length;
        c->body_left = app->body_size(length);
        c->response_total = c->response_left = app->response_size(length);
        if (c->body_left) {
            prepare_receive(c);
        } else {
            prepare_send(c);
        }
    } else if (c->stage == RECEIVE) {
        c->body_left -= c->want;
        if (c->body_left) {
            prepare_receive(c);
        } else {
            prepare_send(c);
        }
    } else {
        c->response_left -= c->want;
        if (c->response_left) {
            prepare_send(c);
        } else {
            c->server->messages++;
            next_header(c);
        }
    }
    return 0;
}

int conn_step(conn_t *c, int *would_block)
{
    ssize_t n;
    if (conn_is_reading(c)) {
        c->server->sc_recv++;
        n = recv(c->fd, conn_buffer(c), conn_length(c), 0);
    } else {
        c->server->sc_send++;
        n = send(c->fd, conn_buffer(c), conn_length(c), MSG_NOSIGNAL);
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && would_block) {
        *would_block = 1;
    }
    return conn_complete(c, n < 0 ? -errno : n);
}

/* --send-mode one (default): one recv or send per readiness event, so a 64 KiB
 * reply through a 4 KiB buffer costs 16 waits. --send-mode full: on a writable
 * event keep sending until the response is finished or the kernel says EAGAIN
 * (socket buffer full), as many servers do. Receives are unchanged. */
int conn_progress(conn_t *c)
{
    if (!c->server->config->send_full || conn_is_reading(c)) {
        return conn_step(c, NULL);
    }
    for (;;) {
        int would_block = 0;
        int rc = conn_step(c, &would_block);
        if (rc || would_block || conn_is_reading(c)) {
            return rc;
        }
    }
}

/* ---- UDP ---- */

static uint64_t read_u64(const unsigned char *p)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | p[i];
    }
    return value;
}

/* Datagram layout: 8-byte client sequence, 8-byte requested reply length, body.
 * Every datagram is a complete request; UDP keeps the message boundary for us. */
ssize_t udp_handle(server_t *s, unsigned char *in, size_t n, unsigned char *out)
{
    s->bytes_read += n;
    if (n < DGRAM_HEADER) {
        s->errors++;
        return -1;
    }
    long reply = s->config->app->on_datagram(read_u64(in + 8), in + DGRAM_HEADER, n - DGRAM_HEADER,
                                             out + DGRAM_HEADER, MAX_DATAGRAM - DGRAM_HEADER - 64);
    if (reply < 0) {
        s->errors++;
        return -1;
    }
    memcpy(out, in, DGRAM_HEADER);
    s->messages++;
    return (ssize_t)reply + DGRAM_HEADER;
}

int udp_drain(server_t *s)
{
    for (int i = 0; i < UDP_BATCH; i++) {
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        s->sc_recv++;
        ssize_t n = recvfrom(s->listener, s->dgram_in, MAX_DATAGRAM, 0, (struct sockaddr *)&peer,
                             &peer_length);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 1;
            }
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        ssize_t reply = udp_handle(s, s->dgram_in, (size_t)n, s->dgram_out);
        if (reply < 0) {
            continue;
        }
        s->sc_send++;
        ssize_t sent = sendto(s->listener, s->dgram_out, (size_t)reply, MSG_NOSIGNAL,
                              (struct sockaddr *)&peer, peer_length);
        if (sent < 0) {
            /* UDP has no flow control: a full queue means the reply is lost. */
            s->datagrams_dropped++;
        } else {
            s->bytes_written += (uint64_t)sent;
        }
    }
    return 0;
}

/* ---- Start-up, threads, final report ---- */

void server_ready(server_t *s)
{
    pthread_barrier_wait(&ready_barrier);
    if (s->index == 0) {
        printf("READY %d\n", s->port);
        fflush(stdout);
    }
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

static int parse_cpus(const char *text, config_t *config)
{
    char copy[512];
    if (strlen(text) >= sizeof(copy)) {
        return -1;
    }
    strcpy(copy, text);
    config->cpu_count = 0;
    for (char *item = strtok(copy, ","); item; item = strtok(NULL, ",")) {
        unsigned long cpu;
        if (number(item, 4095, &cpu) || config->cpu_count == MAX_THREADS) {
            return -1;
        }
        config->cpus[config->cpu_count++] = (int)cpu;
    }
    return config->cpu_count ? 0 : -1;
}

static double seconds(struct timeval value)
{
    return value.tv_sec + value.tv_usec / 1e6;
}

static long peak_rss_kib(void)
{
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

static int open_socket(server_t *s, const struct sockaddr_in *base, int reuse_port)
{
    int is_udp = s->config->transport == TRANSPORT_UDP;
    int fd = socket(AF_INET, (is_udp ? SOCK_DGRAM : SOCK_STREAM) | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -errno;
    }
    int enabled = 1;
    struct sockaddr_in addr = *base;
    if (is_udp) {
        /* UDP has no flow control: a full receive queue silently drops datagrams.
         * The kernel caps this at net.core.rmem_max/wmem_max; run_all.sh checks them. */
        int size = UDP_SOCKET_BUFFER;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ||
        (reuse_port && setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled))) ||
        bind(fd, (struct sockaddr *)&addr, sizeof(addr)) ||
        (!is_udp && listen(fd, LISTEN_BACKLOG))) {
        int error = errno;
        close(fd);
        return -error;
    }
    return fd;
}

typedef struct thread_slot_t {
    server_t server;
    pthread_t thread;
    int result;
} thread_slot_t;

static void *thread_main(void *argument)
{
    thread_slot_t *slot = argument;
    server_t *s = &slot->server;
    if (s->config->cpu_count) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(s->config->cpus[s->index % s->config->cpu_count], &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set)) {
            slot->result = -EINVAL;
        }
    }
    int rc = engine_run(s);
    if (!slot->result) {
        slot->result = rc;
    }
    if (rc) {
        /* One failed thread ends the run instead of leaving a partial server. */
        stop_requested = 1;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    static config_t config = {.threads = 1, .sqpoll_cpu = -1, .udp_depth = 32,
                              .buffer_size = DEFAULT_BUFFER, .max_clients = DEFAULT_MAX_CLIENTS};
    config.app = app_find("echo");
    int port = DEFAULT_PORT;
    const char *address = "127.0.0.1";
    int is_epoll = !strcmp(ENGINE_NAME, "epoll");
    int is_uring = !strcmp(ENGINE_NAME, "io_uring");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            printf("Usage: %s [--port 9090] [--bind 127.0.0.1] "
                   "[--app noop|echo|upload|download|stream|memory] [--transport tcp|udp] "
                   "[--threads N] [--cpus a,b,...] [--max-clients N] "
                   "[--buffer 4096-1048576] [--epoll-mode lt|et|lt-direct] [--sqpoll 0|1] "
                   "[--sqpoll-busy 0|1] [--sqpoll-cpu N] [--udp-depth 1-256] "
                   "[--send-mode one|full]\n",
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
            port = (int)n;
        } else if (!strcmp(key, "--max-clients") && !number(value, 100000, &n) && n) {
            config.max_clients = n;
        } else if (!strcmp(key, "--bind")) {
            address = value;
        } else if (!strcmp(key, "--app") && app_find(value)) {
            config.app = app_find(value);
        } else if (!strcmp(key, "--transport") && !strcmp(value, "tcp")) {
            config.transport = TRANSPORT_TCP;
        } else if (!strcmp(key, "--transport") && !strcmp(value, "udp")) {
            config.transport = TRANSPORT_UDP;
        } else if (!strcmp(key, "--threads") && !number(value, MAX_THREADS, &n) && n) {
            config.threads = (int)n;
        } else if (!strcmp(key, "--cpus") && !parse_cpus(value, &config)) {
            continue;
        } else if (!strcmp(key, "--buffer") && !number(value, MAX_BUFFER, &n) && n >= MIN_BUFFER) {
            config.buffer_size = n;
        } else if (is_epoll && !strcmp(key, "--epoll-mode") &&
                   (!strcmp(value, "lt") || !strcmp(value, "et") || !strcmp(value, "lt-direct"))) {
            config.edge_triggered = !strcmp(value, "et");
            config.epoll_direct = !strcmp(value, "lt-direct");
        } else if (!is_uring && !strcmp(key, "--send-mode") &&
                   (!strcmp(value, "one") || !strcmp(value, "full"))) {
            /* io_uring submits the next send as soon as one completes, without
             * waiting for readiness, so the option means nothing there. */
            config.send_full = !strcmp(value, "full");
        } else if (is_uring && !strcmp(key, "--sqpoll-busy") && !number(value, 1, &n)) {
            config.sqpoll_busy = (int)n;
        } else if (is_uring && !strcmp(key, "--sqpoll") && !number(value, 1, &n)) {
            config.sqpoll = (int)n;
        } else if (is_uring && !strcmp(key, "--sqpoll-cpu") && !number(value, 4095, &n)) {
            config.sqpoll_cpu = (int)n;
        } else if (is_uring && !strcmp(key, "--udp-depth") && !number(value, 256, &n) && n) {
            config.udp_depth = (int)n;
        } else {
            fprintf(stderr, "Invalid option: %s %s\n", key, value);
            return 2;
        }
    }
    if (config.send_full && config.edge_triggered) {
        fprintf(stderr, "--send-mode full is already what edge-triggered epoll does\n");
        return 2;
    }
    if (config.sqpoll_busy && !config.sqpoll) {
        fprintf(stderr, "--sqpoll-busy requires --sqpoll 1\n");
        return 2;
    }
    if (config.transport == TRANSPORT_UDP && !config.app->supports_udp) {
        fprintf(stderr, "Application %s does not define a datagram mode\n", config.app->name);
        return 2;
    }

    pattern_init();
    struct sigaction action = {0};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    signal(SIGPIPE, SIG_IGN);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        return 1;
    }

    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) {
        fprintf(stderr, "--bind requires a numeric IPv4 address\n");
        return 2;
    }

    thread_slot_t *slots = calloc((size_t)config.threads, sizeof(*slots));
    if (!slots || pthread_barrier_init(&ready_barrier, NULL, (unsigned)config.threads)) {
        perror("thread setup");
        return 1;
    }
    /* Multithreading is one independent event loop per thread. SO_REUSEPORT lets
     * the kernel spread connections, so no lock is needed on the request path. */
    int reuse_port = config.threads > 1;
    for (int i = 0; i < config.threads; i++) {
        server_t *s = &slots[i].server;
        s->config = &config;
        s->index = i;
        s->listener = open_socket(s, &addr, reuse_port);
        if (s->listener < 0) {
            fprintf(stderr, "listen setup: %s\n", strerror(-s->listener));
            return 1;
        }
        if (config.transport == TRANSPORT_UDP) {
            s->dgram_in = malloc(MAX_DATAGRAM);
            s->dgram_out = malloc(MAX_DATAGRAM);
            if (!s->dgram_in || !s->dgram_out) {
                perror("malloc");
                return 1;
            }
        }
        if (i == 0) {
            socklen_t length = sizeof(addr);
            if (getsockname(s->listener, (struct sockaddr *)&addr, &length)) {
                perror("getsockname");
                return 1;
            }
        }
        s->port = ntohs(addr.sin_port);
    }

    struct timespec start, finish;
    clock_gettime(CLOCK_MONOTONIC, &start);
    /* Workers before the main thread's own loop would leave it unpinned, so the
     * main thread only supervises and every event loop has identical treatment. */
    sigset_t block, previous;
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &block, &previous);
    int single = config.threads == 1;
    for (int i = single ? 1 : 0; i < config.threads; i++) {
        if (pthread_create(&slots[i].thread, NULL, thread_main, &slots[i])) {
            perror("pthread_create");
            return 1;
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (single) {
        /* --threads 1 stays strictly one process with one thread. */
        thread_main(&slots[0]);
    } else {
        /* A signal or a failed worker sets the flag; the workers then exit.
         * The main thread is not pinned, so it sleeps in sigtimedwait (woken at
         * once by SIGINT/SIGTERM) and checks for a failed worker only every
         * 200 ms, instead of waking 50 times a second on whatever core is free. */
        pthread_sigmask(SIG_BLOCK, &block, NULL);
        struct timespec check = {.tv_sec = 0, .tv_nsec = 200000000L};
        while (!stop_requested) {
            if (sigtimedwait(&block, NULL, &check) > 0) {
                stop_requested = 1;
            }
        }
        for (int i = 0; i < config.threads; i++) {
            pthread_join(slots[i].thread, NULL);
        }
    }

    server_t total = {0};
    int rc = 0;
    for (int i = 0; i < config.threads; i++) {
        server_t *s = &slots[i].server;
        while (s->clients) {
            conn_close(s->clients, 0);
        }
        close(s->listener);
        free(s->dgram_in);
        free(s->dgram_out);
        if (slots[i].result && !rc) {
            rc = slots[i].result;
        }
        total.accepted += s->accepted;
        total.closed += s->closed;
        total.peak += s->peak;
        total.rejected += s->rejected;
        total.errors += s->errors;
        total.incomplete += s->incomplete;
        total.messages += s->messages;
        total.bytes_read += s->bytes_read;
        total.bytes_written += s->bytes_written;
        total.loop_calls += s->loop_calls;
        total.datagrams_dropped += s->datagrams_dropped;
        total.sc_recv += s->sc_recv;
        total.sc_send += s->sc_send;
        total.sc_wait += s->sc_wait;
        total.sc_ctl += s->sc_ctl;
        total.sc_accept += s->sc_accept;
        total.sc_enter += s->sc_enter;
    }
    clock_gettime(CLOCK_MONOTONIC, &finish);
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    double elapsed = finish.tv_sec - start.tv_sec + (finish.tv_nsec - start.tv_nsec) / 1e9;
    printf("{\"engine\":\"%s\",\"app\":\"%s\",\"transport\":\"%s\",\"threads\":%d,"
           "\"edge_triggered\":%d,\"epoll_direct\":%d,\"send_full\":%d,\"sqpoll\":%d,\"sqpoll_busy\":%d,"
           "\"buffer\":%zu,\"server_ok\":%s,"
           "\"accepted\":%" PRIu64 ",\"closed\":%" PRIu64 ",\"peak\":%" PRIu64 ","
           "\"rejected\":%" PRIu64 ",\"errors\":%" PRIu64 ",\"incomplete\":%" PRIu64 ","
           "\"messages\":%" PRIu64 ",\"wire_bytes_read\":%" PRIu64
           ",\"wire_bytes_written\":%" PRIu64 ",\"loop_calls\":%" PRIu64
           ",\"datagrams_dropped\":%" PRIu64 ",\"sc_recv\":%" PRIu64 ",\"sc_send\":%" PRIu64
           ",\"sc_wait\":%" PRIu64 ",\"sc_ctl\":%" PRIu64 ",\"sc_accept\":%" PRIu64
           ",\"sc_enter\":%" PRIu64 ",\"elapsed_s\":%.6f,\"user_cpu_s\":%.6f,"
           "\"system_cpu_s\":%.6f,\"max_rss_kib\":%ld,\"rusage_maxrss_kib\":%ld,"
           "\"rss_source\":\"proc_self_status_VmHWM\"}\n",
           ENGINE_NAME, config.app->name, config.transport == TRANSPORT_UDP ? "udp" : "tcp",
           config.threads, config.edge_triggered, config.epoll_direct, config.send_full, config.sqpoll,
           config.sqpoll_busy, config.buffer_size, rc == 0 ? "true" : "false",
           total.accepted, total.closed, total.peak, total.rejected, total.errors,
           total.incomplete, total.messages, total.bytes_read, total.bytes_written,
           total.loop_calls, total.datagrams_dropped, total.sc_recv, total.sc_send, total.sc_wait,
           total.sc_ctl, total.sc_accept, total.sc_enter, elapsed, seconds(usage.ru_utime),
           seconds(usage.ru_stime), peak_rss_kib(), usage.ru_maxrss);
    if (rc) {
        fprintf(stderr, "%s: %s\n", ENGINE_NAME, strerror(-rc));
    }
    free(slots);
    return rc ? 1 : 0;
}
