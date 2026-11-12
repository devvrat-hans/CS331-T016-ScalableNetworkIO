/*
 * framework.h - the contract between the three layers of the project.
 *
 *   Engine (select/poll/epoll/io_uring)  decides WHEN a socket can make progress.
 *   Framework (framework.c)              moves bytes through the request stages.
 *   Application plugin (apps.c)          decides WHAT a request means.
 *
 * An engine never looks at an application, and an application never makes a
 * socket call, so any application runs unchanged on any engine and transport.
 * framework.c and apps.c are compiled once and linked into every server, so all
 * engines execute the same application machine code.
 */
#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#include <netinet/in.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <signal.h>

#define DEFAULT_BUFFER 4096        /* Per-client buffer; --buffer changes it. */
#define MIN_BUFFER 4096            /* Echo requests are at most 4 KiB. */
#define MAX_BUFFER (1024 * 1024)
#define ECHO_MAX 4096
#define MAX_TRANSFER (64ULL * 1024 * 1024)
#define MAX_DATAGRAM 65536
#define DGRAM_HEADER 16
#define WAIT_MS 100
#define ACCEPT_BATCH 64
#define UDP_BATCH 32
#define MAX_THREADS 64

typedef struct server_t server_t;
typedef struct conn_t conn_t;

typedef enum { TRANSPORT_TCP, TRANSPORT_UDP } transport_t;
typedef enum { HEADER, RECEIVE, SEND } stage_t;

/* The plugin contract. Optional hooks may be NULL. */
typedef struct app_ops_t {
    const char *name;
    int supports_udp;
    /* Is this request length acceptable? 0 means yes, or a negative errno. */
    int (*validate)(uint64_t length);
    /* Body bytes the client will send, and response bytes the server returns. */
    uint64_t (*body_size)(uint64_t length);
    uint64_t (*response_size)(uint64_t length);
    /* Consume n received body bytes that start at payload offset. */
    int (*on_body)(conn_t *c, uint64_t offset, const unsigned char *data, size_t n);
    /* Produce n response bytes that start at response offset. */
    void (*fill)(conn_t *c, uint64_t offset, unsigned char *out, size_t n);
    /* Per-connection state, for applications that keep memory per client. */
    int (*on_open)(conn_t *c);
    void (*on_close)(conn_t *c);
    /* UDP: build a reply body in out and return its length, or a negative errno. */
    long (*on_datagram)(uint64_t response_length, const unsigned char *body, size_t n,
                        unsigned char *out, size_t capacity);
} app_ops_t;

struct conn_t {
    int fd;
    stage_t stage;
    size_t done, want;        /* Progress through the current buffer. */
    uint64_t length;          /* Declared request length. */
    uint64_t body_left;       /* Request bytes not yet received. */
    uint64_t response_total;  /* Response size for this request. */
    uint64_t response_left;   /* Response bytes not yet sent. */
    server_t *server;
    conn_t *prev, *next;      /* Shutdown list, not scanned for I/O. */
    conn_t *ready_next;       /* Used only by io_uring's submission queue. */
    int pending;              /* io_uring owns the buffer while this is true. */
    void *app_state;          /* Owned by the application plugin. */
    int interest_out;         /* epoll lt-direct: registered for EPOLLOUT now. */
    unsigned char buffer[];   /* config->buffer_size bytes, allocated with the client. */
};

/* Options shared by all engine threads. */
typedef struct config_t {
    const app_ops_t *app;
    transport_t transport;
    int threads;
    int edge_triggered;       /* epoll only. */
    int epoll_direct;         /* epoll only: lt-direct, reply at once without epoll_ctl. */
    int send_full;            /* select/poll/epoll LT: keep sending until the socket is full. */
    int sqpoll;               /* io_uring only. */
    int sqpoll_busy;          /* io_uring only: poll the completion queue, never sleep. */
    size_t buffer_size;       /* Per-client buffer; data moves through it in chunks this size. */
    int sqpoll_cpu;           /* CPU for the SQPOLL kernel thread, or -1. */
    int udp_depth;            /* io_uring only: receives waiting on the UDP socket. */
    int cpus[MAX_THREADS];    /* CPU per server thread. */
    int cpu_count;
    size_t max_clients;
} config_t;

/* One server_t per engine thread; nothing below is shared between threads. */
struct server_t {
    int listener;             /* TCP listening socket, or the UDP socket. */
    int port;
    int index;
    const config_t *config;
    conn_t *clients;
    uint64_t accepted, closed, active, peak, rejected, errors, incomplete;
    uint64_t messages, bytes_read, bytes_written, loop_calls;
    uint64_t datagrams_dropped;
    /* Exact system-call counters: no tracer, so timing is not disturbed. */
    uint64_t sc_recv, sc_send, sc_wait, sc_ctl, sc_accept, sc_enter;
    unsigned char *dgram_in, *dgram_out;
};

extern volatile sig_atomic_t stop_requested;

/* Implemented by exactly one engine file per server binary. */
extern const char *const ENGINE_NAME;
int engine_run(server_t *s);

/* Application registry (apps.c). */
const app_ops_t *app_find(const char *name);
void pattern_init(void);
const unsigned char *pattern_at(uint64_t offset);

/* TCP connection services used by the engines. */
int accept_client(server_t *s);
conn_t *conn_add(server_t *s, int fd);
void conn_close(conn_t *c, int error);
int conn_is_reading(const conn_t *c);
unsigned char *conn_buffer(conn_t *c);
size_t conn_length(const conn_t *c);
/* Feed a recv/send result (bytes or -errno) into the request state machine. */
int conn_complete(conn_t *c, ssize_t result);
/* One non-blocking recv or send. Sets *would_block on EAGAIN when not NULL. */
int conn_step(conn_t *c, int *would_block);
/* What a readiness engine does for one event: one conn_step, or with
 * --send-mode full, sends until the response is done or the socket is full. */
int conn_progress(conn_t *c);
/* Run the shared accept loop; add_cb registers each new connection. */
int accept_ready(server_t *s, int (*add_cb)(server_t *, conn_t *, void *), void *context);

/* UDP services. udp_drain handles up to UDP_BATCH datagrams and returns >0 if
 * it stopped because the socket is empty, 0 if the batch limit was reached. */
int udp_drain(server_t *s);
/* Build the reply for a received datagram; returns its size, or -1 to discard. */
ssize_t udp_handle(server_t *s, unsigned char *in, size_t n, unsigned char *out);

/* Announce readiness; READY is printed once every engine thread has arrived. */
void server_ready(server_t *s);

#endif
