/*
 * tcp_echo_load.c - Blocking TCP echo workload generator.
 *
 * One process opens --connections sockets before starting the workload.  Run
 * several instances from run_benchmark.sh to avoid making one Python process
 * the bottleneck at high connection counts.
 */
#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    uint64_t attempted, completed, bytes;
    uint64_t latency_total_ns, latency_min_ns, latency_max_ns;
} result_t;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int parse_positive(const char *value, int *out) {
    char *end = NULL;
    long n = strtol(value, &end, 10);
    if (*value == '\0' || *end != '\0' || n < 1 || n > INT_MAX) return -1;
    *out = (int)n;
    return 0;
}

static int write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int read_all(int fd, char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, buf + off, len - off, 0);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int connect_one(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL, *it;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc)); return -1; }
    int fd = -1;
    for (it = res; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd >= 0 && connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1", *port = "9090";
    int connections = 1, messages = 100, bytes = 64;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = argv[++i];
        else if (strcmp(argv[i], "--connections") == 0 && i + 1 < argc && parse_positive(argv[++i], &connections) == 0) {}
        else if (strcmp(argv[i], "--messages") == 0 && i + 1 < argc && parse_positive(argv[++i], &messages) == 0) {}
        else if (strcmp(argv[i], "--bytes") == 0 && i + 1 < argc && parse_positive(argv[++i], &bytes) == 0) {}
        else { fprintf(stderr, "Usage: %s [--host HOST] [--port PORT] [--connections N] [--messages N] [--bytes N]\n", argv[0]); return 2; }
    }
    char *request = malloc((size_t)bytes), *reply = malloc((size_t)bytes);
    int *fds = calloc((size_t)connections, sizeof(*fds));
    if (!request || !reply || !fds) { perror("allocation"); return 1; }
    for (int i = 0; i < bytes; i++) request[i] = (char)('A' + (i % 26));
    int opened = 0;
    for (; opened < connections; opened++) {
        fds[opened] = connect_one(host, port);
        if (fds[opened] < 0) { fprintf(stderr, "connection %d failed: %s\n", opened + 1, strerror(errno)); break; }
    }
    result_t r = {.latency_min_ns = UINT64_MAX};
    uint64_t started = now_ns();
    for (int m = 0; m < messages; m++) for (int c = 0; c < opened; c++) {
        r.attempted++;
        uint64_t before = now_ns();
        if (write_all(fds[c], request, (size_t)bytes) || read_all(fds[c], reply, (size_t)bytes) || memcmp(request, reply, (size_t)bytes)) continue;
        uint64_t latency = now_ns() - before;
        r.completed++; r.bytes += (uint64_t)bytes;
        r.latency_total_ns += latency;
        if (latency < r.latency_min_ns) r.latency_min_ns = latency;
        if (latency > r.latency_max_ns) r.latency_max_ns = latency;
    }
    uint64_t elapsed = now_ns() - started;
    for (int i = 0; i < opened; i++) close(fds[i]);
    printf("attempted=%" PRIu64 " completed=%" PRIu64 " bytes=%" PRIu64 " elapsed_ns=%" PRIu64 " latency_total_ns=%" PRIu64 " latency_min_ns=%" PRIu64 " latency_max_ns=%" PRIu64 " opened=%d\n",
           r.attempted, r.completed, r.bytes, elapsed, r.latency_total_ns,
           r.completed ? r.latency_min_ns : 0, r.latency_max_ns, opened);
    free(fds); free(reply); free(request);
    return opened == connections && r.completed == r.attempted ? 0 : 1;
}
