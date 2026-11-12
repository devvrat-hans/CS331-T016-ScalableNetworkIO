/*
 * loadgen.c - multithreaded C load generator.
 *
 * The earlier Python client used 100% of its core while the servers were half
 * idle, so it, not the server, set the throughput. This replaces it.
 *
 * Two arrival models:
 *   --rate 0   closed loop: each active connection sends, waits, sends again.
 *   --rate R   paced: R requests/s in total on a fixed schedule. Latency is
 *              measured from the scheduled send time, so if the server falls
 *              behind the delay is counted instead of hidden (coordinated omission).
 *
 * Every response is verified before it counts as a completed request.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PATTERN_PERIOD 251
#define MAX_DGRAM 65536
#define CHUNK 65536
#define HIST_SIZE 2600
#define UDP_TIMEOUT_NS 200000000LL
#define MAX_THREADS 64
enum { PACE_SPIN, PACE_SLEEP, PACE_HYBRID };
#define HYBRID_SPIN_NS 50000LL /* Poll for the last 50 us before a send is due. */
#define WINDOW_NS 100000000LL /* Timeline resolution: 100 ms. */
#define MAX_WINDOWS 6000

static unsigned char pattern[PATTERN_PERIOD + 2 * MAX_DGRAM];

typedef enum { FREE_C, SENDING, RECEIVING } state_t;

typedef struct conn_t {
    int fd;
    state_t state;
    uint64_t sent, send_total, received, response_total;
    int64_t intended_ns, deadline_ns;
    uint64_t seq;
    struct conn_t *next_free;
    int writing;
} conn_t;

typedef struct options_t {
    const char *host, *app;
    int port, udp, threads, conns, active;
    uint64_t size;
    double duration, warmup, rate;
    /* Paced mode. spin: poll continuously. sleep: sleep until the next send is
     * due. hybrid: sleep until shortly before it, then poll, so sends stay on
     * time while the client core is free most of the time. */
    int pace;
    int cpus[MAX_THREADS], cpu_count;
} options_t;

typedef struct worker_t {
    int index, epfd;
    conn_t *conns;
    int count, active_count;
    int idle_fds_count;
    conn_t *free_list;
    uint64_t scheduled;
    uint64_t hist[HIST_SIZE];
    uint64_t measured, total_completed, errors, timeouts, outstanding, late_schedule;
    uint64_t bytes_measured;
    uint64_t window_max[MAX_WINDOWS]; /* Worst latency in each 100 ms window. */
    pthread_t thread;
    int failed;
} worker_t;

static options_t opt;
static int64_t t_start, t_measure, t_end;
static pthread_barrier_t barrier;
static int body_on_request, resp_is_ack;

static int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static int hist_index(uint64_t v)
{
    if (v < 128) {
        return (int)v;
    }
    int shift = 63 - __builtin_clzll(v) - 6;
    int index = (shift << 6) + (int)(v >> shift);
    return index < HIST_SIZE ? index : HIST_SIZE - 1;
}

static double hist_value(int index)
{
    if (index < 128) {
        return index;
    }
    int shift = (index >> 6) - 1;
    uint64_t mantissa = (uint64_t)index - ((uint64_t)shift << 6);
    return (double)(mantissa << shift) + (double)(1ULL << shift) / 2;
}

static uint64_t expected_response(void)
{
    if (opt.udp) {
        return 16 + (!strcmp(opt.app, "echo") || !strcmp(opt.app, "download") ||
                             !strcmp(opt.app, "stream")
                         ? opt.size
                         : 0);
    }
    return resp_is_ack ? 1 : opt.size;
}

static void set_interest(worker_t *w, conn_t *c, int writing)
{
    if (c->writing == writing) {
        return;
    }
    c->writing = writing;
    struct epoll_event e = {.events = writing ? EPOLLIN | EPOLLOUT : EPOLLIN, .data.ptr = c};
    epoll_ctl(w->epfd, EPOLL_CTL_MOD, c->fd, &e);
}

static void put_u64(unsigned char *p, uint64_t v)
{
    for (int i = 7; i >= 0; i--) {
        p[i] = (unsigned char)v;
        v >>= 8;
    }
}

static uint64_t get_u64(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

static void conn_fail(worker_t *w, conn_t *c)
{
    w->errors++;
    if (c->state != FREE_C) {
        w->outstanding--;
    }
    c->state = FREE_C;
    epoll_ctl(w->epfd, EPOLL_CTL_DEL, c->fd, NULL);
}

static void tcp_pump_send(worker_t *w, conn_t *c)
{
    static __thread unsigned char buffer[CHUNK + 8];
    while (c->sent < c->send_total) {
        size_t n = 0;
        if (c->sent < 8) {
            put_u64(buffer, opt.size);
            n = 8 - c->sent;
            memmove(buffer, buffer + c->sent, n);
        }
        uint64_t body_offset = c->sent > 8 ? c->sent - 8 : 0;
        uint64_t left = c->send_total - 8 - body_offset;
        size_t take = left < CHUNK ? (size_t)left : CHUNK;
        if (take) {
            memcpy(buffer + n, pattern + body_offset % PATTERN_PERIOD, take);
        }
        ssize_t r = send(c->fd, buffer, n + take, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                set_interest(w, c, 1);
                return;
            }
            conn_fail(w, c);
            return;
        }
        c->sent += (uint64_t)r;
    }
    set_interest(w, c, 0);
    c->state = RECEIVING;
}

static void udp_send(worker_t *w, conn_t *c)
{
    static __thread unsigned char buffer[MAX_DGRAM];
    put_u64(buffer, ++c->seq);
    int has_body = !strcmp(opt.app, "echo") || !strcmp(opt.app, "upload");
    put_u64(buffer + 8, opt.size);
    size_t body = has_body ? (size_t)opt.size : 0;
    if (body) {
        memcpy(buffer + 16, pattern, body);
    }
    c->deadline_ns = now_ns() + UDP_TIMEOUT_NS;
    c->state = RECEIVING;
    if (send(c->fd, buffer, 16 + body, MSG_NOSIGNAL) < 0 && errno != EAGAIN &&
        errno != ECONNREFUSED) {
        conn_fail(w, c);
    }
}

static void issue(worker_t *w, conn_t *c, int64_t intended)
{
    c->intended_ns = intended;
    c->sent = c->received = 0;
    c->state = SENDING;
    w->outstanding++;
    if (opt.udp) {
        udp_send(w, c);
    } else {
        c->send_total = 8 + (body_on_request ? opt.size : 0);
        tcp_pump_send(w, c);
    }
}

static void finished(worker_t *w, conn_t *c)
{
    int64_t now = now_ns();
    w->outstanding--;
    w->total_completed++;
    if (now >= t_measure && now < t_end) {
        w->measured++;
        uint64_t latency = (uint64_t)(now > c->intended_ns ? now - c->intended_ns : 0);
        w->hist[hist_index(latency)]++;
        int64_t window = (now - t_measure) / WINDOW_NS;
        if (window < MAX_WINDOWS && latency > w->window_max[window]) {
            w->window_max[window] = latency;
        }
    }
    c->state = FREE_C;
    if (opt.rate == 0 && now < t_end) {
        issue(w, c, now_ns());
    } else {
        c->next_free = w->free_list;
        w->free_list = c;
    }
}

static void tcp_read(worker_t *w, conn_t *c)
{
    static __thread unsigned char buffer[CHUNK];
    for (;;) {
        ssize_t r = recv(c->fd, buffer, sizeof(buffer), 0);
        if (r == 0) {
            conn_fail(w, c);
            return;
        }
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            conn_fail(w, c);
            return;
        }
        if (c->state == FREE_C || c->received + (uint64_t)r > c->response_total) {
            conn_fail(w, c); /* Bytes nobody asked for. */
            return;
        }
        int bad = resp_is_ack ? buffer[0] != 'K'
                              : memcmp(buffer, pattern + c->received % PATTERN_PERIOD, (size_t)r) != 0;
        if (bad) {
            conn_fail(w, c);
            return;
        }
        c->received += (uint64_t)r;
        if (c->received == c->response_total) {
            finished(w, c);
            return;
        }
    }
}

static void udp_read(worker_t *w, conn_t *c)
{
    static __thread unsigned char buffer[MAX_DGRAM];
    for (;;) {
        ssize_t r = recv(c->fd, buffer, sizeof(buffer), 0);
        if (r < 0) {
            return;
        }
        if (c->state != RECEIVING || (size_t)r < 16 || get_u64(buffer) != c->seq) {
            continue; /* A reply that arrived after its request timed out. */
        }
        if ((uint64_t)r != expected_response() ||
            (r > 16 && memcmp(buffer + 16, pattern, (size_t)r - 16))) {
            w->errors++;
        }
        finished(w, c);
    }
}

static void *worker_main(void *argument)
{
    worker_t *w = argument;
    if (opt.cpu_count) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(opt.cpus[w->index % opt.cpu_count], &set);
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }
    pthread_barrier_wait(&barrier);
    struct timespec start = {.tv_sec = t_start / 1000000000LL, .tv_nsec = t_start % 1000000000LL};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, NULL);

    double interval_ns = opt.rate > 0 ? 1e9 * opt.threads / opt.rate : 0;
    /* Stagger the threads' schedules. Without this every thread sent at the same
     * instant, so paced traffic arrived in bursts of one request per thread. */
    int64_t phase_ns = (int64_t)(interval_ns * w->index / opt.threads);
    if (opt.rate == 0) {
        for (int i = 0; i < w->active_count; i++) {
            issue(w, &w->conns[i], now_ns());
        }
    } else {
        for (int i = 0; i < w->active_count; i++) {
            w->conns[i].next_free = w->free_list;
            w->free_list = &w->conns[i];
        }
    }
    int64_t drain_deadline = t_end + (opt.udp ? UDP_TIMEOUT_NS : 5000000000LL);
    int64_t next_scan = t_start;
    struct epoll_event events[512];
    for (;;) {
        int64_t now = now_ns();
        if (now >= t_end && (w->outstanding == 0 || now >= drain_deadline)) {
            break;
        }
        if (opt.rate > 0 && now < t_end) {
            for (;;) {
                int64_t due = t_start + phase_ns + (int64_t)((double)w->scheduled * interval_ns);
                if (due > now || !w->free_list) {
                    if (due <= now) {
                        w->late_schedule++;
                    }
                    break;
                }
                conn_t *c = w->free_list;
                w->free_list = c->next_free;
                w->scheduled++;
                issue(w, c, due);
            }
        }
        if (opt.udp && now >= next_scan) {
            next_scan = now + 20000000LL;
            for (int i = 0; i < w->active_count; i++) {
                conn_t *c = &w->conns[i];
                if (c->state == RECEIVING && now >= c->deadline_ns) {
                    w->timeouts++;
                    w->outstanding--;
                    c->state = FREE_C;
                    if (opt.rate == 0 && now < t_end) {
                        issue(w, c, now_ns());
                    } else {
                        c->next_free = w->free_list;
                        w->free_list = c;
                    }
                }
            }
        }
        int n;
        if (opt.rate > 0 && opt.pace != PACE_SPIN) {
            /* Sleep until the next request is due (at most 1 ms), so the client
             * does not keep its cores busy and push other work onto the server's. */
            int64_t due = t_start + phase_ns + (int64_t)((double)w->scheduled * interval_ns);
            int64_t wait = w->free_list && now < t_end ? due - now_ns() : 1000000;
            if (opt.pace == PACE_HYBRID && w->free_list && now < t_end) {
                wait -= HYBRID_SPIN_NS;
            }
            if (wait < 0) {
                wait = 0;
            } else if (wait > 1000000) {
                wait = 1000000;
            }
            struct timespec timeout = {.tv_sec = 0, .tv_nsec = (long)wait};
            n = epoll_pwait2(w->epfd, events, 512, &timeout, NULL);
        } else {
            n = epoll_wait(w->epfd, events, 512, opt.rate > 0 ? 0 : 1);
        }
        for (int i = 0; i < n; i++) {
            conn_t *c = events[i].data.ptr;
            if (opt.udp) {
                udp_read(w, c);
                continue;
            }
            if ((events[i].events & EPOLLOUT) && c->state == SENDING) {
                tcp_pump_send(w, c);
            }
            if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                tcp_read(w, c);
            }
        }
    }
    if (w->outstanding && !opt.udp) {
        w->failed = 1;
    }
    return NULL;
}

static int open_conn(const struct sockaddr_in *addr)
{
    int fd = socket(AF_INET, (opt.udp ? SOCK_DGRAM : SOCK_STREAM) | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (const struct sockaddr *)addr, sizeof(*addr))) {
        return -1;
    }
    if (!opt.udp) {
        int enabled = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
    } else {
        int size = 1024 * 1024; /* Room for a full reply even at 64 KiB datagrams. */
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

static void parse_cpus(char *text)
{
    for (char *p = strtok(text, ","); p && opt.cpu_count < MAX_THREADS; p = strtok(NULL, ",")) {
        opt.cpus[opt.cpu_count++] = atoi(p);
    }
}

static void sleep_until(int64_t t)
{
    struct timespec s = {.tv_sec = t / 1000000000LL, .tv_nsec = t % 1000000000LL};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &s, NULL);
}

int main(int argc, char **argv)
{
    opt = (options_t){.host = "127.0.0.1", .app = "echo", .port = 9090, .threads = 1, .conns = 10,
                      .active = -1, .size = 64, .duration = 10, .warmup = 2};
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *k = argv[i];
        char *v = argv[i + 1];
        if (!strcmp(k, "--host")) opt.host = v;
        else if (!strcmp(k, "--port")) opt.port = atoi(v);
        else if (!strcmp(k, "--app")) opt.app = v;
        else if (!strcmp(k, "--transport")) opt.udp = !strcmp(v, "udp");
        else if (!strcmp(k, "--conns")) opt.conns = atoi(v);
        else if (!strcmp(k, "--active")) opt.active = atoi(v);
        else if (!strcmp(k, "--size")) opt.size = strtoull(v, NULL, 10);
        else if (!strcmp(k, "--duration")) opt.duration = atof(v);
        else if (!strcmp(k, "--warmup")) opt.warmup = atof(v);
        else if (!strcmp(k, "--rate")) opt.rate = atof(v);
        else if (!strcmp(k, "--pace")) opt.pace = !strcmp(v, "sleep") ? PACE_SLEEP
                                                : !strcmp(v, "hybrid") ? PACE_HYBRID : PACE_SPIN;
        else if (!strcmp(k, "--threads")) opt.threads = atoi(v);
        else if (!strcmp(k, "--cpus")) parse_cpus(v);
        else { fprintf(stderr, "Unknown option %s\n", k); return 2; }
    }
    if (opt.active < 0 || opt.active > opt.conns) {
        opt.active = opt.conns;
    }
    if (opt.threads < 1 || opt.threads > MAX_THREADS || opt.threads > opt.conns) {
        fprintf(stderr, "Invalid thread count\n");
        return 2;
    }
    /* Wake from timed sleeps as precisely as the kernel allows. */
    prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0);
    struct rlimit limit = {65536, 65536};
    setrlimit(RLIMIT_NOFILE, &limit);
    for (size_t i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (unsigned char)(i % PATTERN_PERIOD);
    }
    body_on_request = !strcmp(opt.app, "echo") || !strcmp(opt.app, "upload") || !strcmp(opt.app, "memory");
    resp_is_ack = !strcmp(opt.app, "upload") || !strcmp(opt.app, "memory") || !strcmp(opt.app, "noop");

    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)opt.port)};
    if (inet_pton(AF_INET, opt.host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Bad host\n");
        return 2;
    }
    worker_t *workers = calloc((size_t)opt.threads, sizeof(*workers));
    pthread_barrier_init(&barrier, NULL, (unsigned)opt.threads + 1);
    int *idle = calloc((size_t)(opt.conns - opt.active + 1), sizeof(int));
    int idle_count = 0;
    for (int t = 0; t < opt.threads; t++) {
        worker_t *w = &workers[t];
        w->index = t;
        w->epfd = epoll_create1(EPOLL_CLOEXEC);
        w->active_count = opt.active / opt.threads + (t < opt.active % opt.threads);
        w->conns = calloc((size_t)w->active_count + 1, sizeof(conn_t));
        for (int i = 0; i < w->active_count; i++) {
            conn_t *c = &w->conns[i];
            c->fd = open_conn(&addr);
            if (c->fd < 0) {
                fprintf(stderr, "connect: %s\n", strerror(errno));
                return 1;
            }
            c->response_total = opt.udp ? 0 : expected_response();
            struct epoll_event e = {.events = EPOLLIN, .data.ptr = c};
            epoll_ctl(w->epfd, EPOLL_CTL_ADD, c->fd, &e);
        }
    }
    for (int i = opt.active; i < opt.conns; i++) {
        idle[idle_count] = open_conn(&addr);
        if (idle[idle_count] < 0) {
            fprintf(stderr, "connect idle: %s\n", strerror(errno));
            return 1;
        }
        idle_count++;
    }
    usleep(200000); /* Let every connection be accepted and registered first. */
    for (int t = 0; t < opt.threads; t++) {
        pthread_create(&workers[t].thread, NULL, worker_main, &workers[t]);
    }
    t_start = now_ns() + 50000000LL;
    t_measure = t_start + (int64_t)(opt.warmup * 1e9);
    t_end = t_measure + (int64_t)(opt.duration * 1e9);
    pthread_barrier_wait(&barrier);
    sleep_until(t_measure);
    printf("MEASURE_START\n");
    fflush(stdout);
    sleep_until(t_end);
    printf("MEASURE_END\n");
    fflush(stdout);
    for (int t = 0; t < opt.threads; t++) {
        pthread_join(workers[t].thread, NULL);
    }

    uint64_t hist[HIST_SIZE] = {0};
    uint64_t measured = 0, total = 0, errors = 0, timeouts = 0, late = 0;
    int failed = 0;
    for (int t = 0; t < opt.threads; t++) {
        worker_t *w = &workers[t];
        for (int i = 0; i < HIST_SIZE; i++) {
            hist[i] += w->hist[i];
        }
        measured += w->measured;
        total += w->total_completed;
        errors += w->errors;
        timeouts += w->timeouts;
        late += w->late_schedule;
        failed |= w->failed;
        for (int i = 0; i < w->active_count; i++) {
            close(w->conns[i].fd);
        }
    }
    for (int i = 0; i < idle_count; i++) {
        close(idle[i]);
    }

    double percentile[5] = {0}, wanted[5] = {.50, .90, .95, .99, .999}, sum = 0;
    uint64_t seen = 0;
    int next = 0;
    for (int i = 0; i < HIST_SIZE; i++) {
        if (!hist[i]) {
            continue;
        }
        double value = hist_value(i);
        seen += hist[i];
        sum += value * (double)hist[i];
        while (next < 5 && (double)seen >= wanted[next] * (double)measured) {
            percentile[next++] = value / 1000.0;
        }
    }
    double throughput = (double)measured / opt.duration;
    printf("{\"valid\":%s,\"measured\":%" PRIu64 ",\"total_completed\":%" PRIu64
           ",\"errors\":%" PRIu64 ",\"udp_timeouts\":%" PRIu64 ",\"late_schedule_events\":%" PRIu64
           ",\"throughput_rps\":%.3f,\"payload_mib_s\":%.4f,\"mean_us\":%.3f,"
           "\"p50_us\":%.3f,\"p90_us\":%.3f,\"p95_us\":%.3f,\"p99_us\":%.3f,\"p999_us\":%.3f,\"histogram\":[",
           failed || errors || measured == 0 ? "false" : "true", measured, total, errors, timeouts, late,
           throughput, throughput * (double)opt.size / 1048576.0,
           measured ? sum / (double)measured / 1000.0 : 0, percentile[0], percentile[1],
           percentile[2], percentile[3], percentile[4]);
    int first = 1;
    for (int i = 0; i < HIST_SIZE; i++) {
        if (hist[i]) {
            printf("%s[%.3f,%" PRIu64 "]", first ? "" : ",", hist_value(i) / 1000.0, hist[i]);
            first = 0;
        }
    }
    /* Worst latency per 100 ms window: shows whether a high p99 comes from
     * one stall or is spread across the whole run. */
    printf("],\"window_max_us\":[");
    int windows = (int)(opt.duration * 1e9 / WINDOW_NS);
    for (int i = 0; i < windows && i < MAX_WINDOWS; i++) {
        uint64_t worst = 0;
        for (int t = 0; t < opt.threads; t++) {
            if (workers[t].window_max[i] > worst) {
                worst = workers[t].window_max[i];
            }
        }
        printf("%s%.1f", i ? "," : "", worst / 1000.0);
    }
    printf("]}\n");
    for (int t = 0; t < opt.threads; t++) {
        close(workers[t].epfd);
        free(workers[t].conns);
    }
    free(workers);
    free(idle);
    return failed || errors ? 1 : 0;
}
