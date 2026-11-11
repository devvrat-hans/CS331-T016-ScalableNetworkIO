/*
 * io_uring_server.c - the io_uring engine.
 * Only the event loop lives here; see Chirag/code/common/framework.h for the contract.
 *
 * Readiness engines ask "can I read?" and then read. Here we describe the whole
 * operation in a submission queue entry and later collect its result from the
 * completion queue, so several operations can share one kernel entry.
 *
 * --sqpoll 1 adds a kernel thread that polls the submission queue, so submitting
 * usually needs no system call at all. The cost is that thread spinning on a CPU
 * while it is awake, whether or not there is traffic to process.
 */
#define _GNU_SOURCE
#include "framework.h" /* Chirag/code/common/framework.h; the Makefile adds -I */

#include <errno.h>
#include <liburing.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

const char *const ENGINE_NAME = "io_uring";

/* Cancel every pending request; older liburing headers lack these names. */
#ifndef IORING_ASYNC_CANCEL_ALL
#define IORING_ASYNC_CANCEL_ALL (1U << 0)
#endif
#ifndef IORING_ASYNC_CANCEL_ANY
#define IORING_ASYNC_CANCEL_ANY (1U << 2)
#endif
#define CANCEL_EVERYTHING (IORING_ASYNC_CANCEL_ANY | IORING_ASYNC_CANCEL_ALL)

#define RING_DEPTH 256
#define SQPOLL_IDLE_MS 100

/* Exact kernel-entry accounting: liburing only enters the kernel in these cases. */
static int counted_submit(server_t *s, struct io_uring *ring)
{
    unsigned ready = io_uring_sq_ready(ring);
    if (s->config->sqpoll) {
        if (IO_URING_READ_ONCE(*ring->sq.kflags) & IORING_SQ_NEED_WAKEUP) {
            s->sc_enter++;
        }
    } else if (ready) {
        s->sc_enter++;
    }
    return io_uring_submit(ring);
}

static int64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
}

static int counted_wait(server_t *s, struct io_uring *ring, struct io_uring_cqe **cqe)
{
    if (io_uring_peek_cqe(ring, cqe) == 0) {
        return 0; /* A completion is already in shared memory: no system call. */
    }
    if (s->config->sqpoll_busy) {
        /* SQPOLL as intended for low latency: the kernel thread polls the
         * submission queue and we poll the completion queue, so neither side
         * sleeps or needs waking. Costs two busy cores. */
        int64_t deadline = monotonic_ns() + WAIT_MS * 1000000LL;
        while (!stop_requested) {
            if (io_uring_peek_cqe(ring, cqe) == 0) {
                return 0;
            }
            if (monotonic_ns() >= deadline) {
                return -ETIME;
            }
        }
        return -EINTR;
    }
    struct __kernel_timespec timeout = {.tv_sec = 0, .tv_nsec = WAIT_MS * 1000000L};
    s->sc_enter++;
    s->sc_wait++;
    return io_uring_wait_cqe_timeout(ring, cqe, &timeout);
}

static void enqueue(conn_t **head, conn_t **tail, conn_t *c)
{
    c->ready_next = NULL;
    if (*tail) {
        (*tail)->ready_next = c;
    } else {
        *head = c;
    }
    *tail = c;
}

/* One completion slot for every operation that can be in flight at once: a
 * receive or send per client, the accept, the UDP receives and the cancels.
 * With the default (twice the submission depth, 512) a run with 1000 clients
 * overflowed the completion queue and the kernel fell back to a slow path. */
static unsigned completion_entries(const server_t *s)
{
    size_t needed = s->config->max_clients + (size_t)s->config->udp_depth + 64;
    unsigned entries = 2 * RING_DEPTH;
    while (entries < needed && entries < 65536) {
        entries *= 2;
    }
    return entries;
}

static int ring_setup(server_t *s, struct io_uring *ring)
{
    struct io_uring_params params = {0};
    params.flags = IORING_SETUP_CQSIZE;
    params.cq_entries = completion_entries(s);
    if (s->config->sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = SQPOLL_IDLE_MS;
        if (s->config->sqpoll_cpu >= 0) {
            params.flags |= IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu = (unsigned)s->config->sqpoll_cpu + (unsigned)s->index;
        }
    }
    return io_uring_queue_init_params(RING_DEPTH, ring, &params);
}

/* Cancel everything still owned by the kernel and wait for every completion,
 * so no buffer is freed while the kernel could still write into it.
 * For TCP, an operation that had already finished when we stopped is still fed
 * to the state machine: the client may have received a reply whose completion
 * we had not yet collected, and the message counts must agree with the client. */
static void drain(server_t *s, struct io_uring *ring, unsigned outstanding, const void *accept_tag)
{
    struct io_uring_sqe *sqe;
    static __thread int cancel_tag;
    while ((sqe = io_uring_get_sqe(ring)) == NULL) {
        if (counted_submit(s, ring) < 0) {
            goto broken;
        }
    }
    io_uring_prep_cancel(sqe, NULL, CANCEL_EVERYTHING);
    io_uring_sqe_set_data(sqe, &cancel_tag);
    outstanding++;
    while (outstanding) {
        int rc = counted_submit(s, ring);
        if (rc < 0 && rc != -EINTR) {
            goto broken;
        }
        struct io_uring_cqe *cqe;
        struct __kernel_timespec timeout = {.tv_sec = 1, .tv_nsec = 0};
        rc = io_uring_wait_cqe_timeout(ring, &cqe, &timeout);
        if (rc == -EINTR) {
            continue;
        }
        if (rc == -ETIME) {
            /* Operations that started after the first cancel: cancel them too. */
            while ((sqe = io_uring_get_sqe(ring)) == NULL) {
                if (counted_submit(s, ring) < 0) {
                    goto broken;
                }
            }
            io_uring_prep_cancel(sqe, NULL, CANCEL_EVERYTHING);
            io_uring_sqe_set_data(sqe, &cancel_tag);
            outstanding++;
            continue;
        }
        if (rc < 0) {
            goto broken;
        }
        void *data = io_uring_cqe_get_data(cqe);
        int result = cqe->res;
        io_uring_cqe_seen(ring, cqe);
        outstanding--;
        if (!accept_tag || data == &cancel_tag) {
            continue; /* UDP, or one of our cancels. */
        }
        if (data == accept_tag) {
            if (result >= 0) {
                close(result); /* Accepted while stopping: never served. */
            }
        } else if (result > 0) {
            conn_t *c = data;
            c->pending = 0;
            conn_complete(c, result); /* Accounting only; nothing new is submitted. */
        } else {
            ((conn_t *)data)->pending = 0;
        }
    }
    return;
broken:
    /* A broken ring cannot prove the buffers are safe to free. */
    _exit(1);
}

/* ---- UDP: a pool of receive slots, each cycling recvmsg -> sendmsg -> recvmsg. */
typedef struct udp_slot_t {
    struct sockaddr_in peer;
    struct iovec iov;
    struct msghdr msg;
    unsigned char in[MAX_DATAGRAM], out[MAX_DATAGRAM];
    int sending;
} udp_slot_t;

static int post_udp_recv(struct io_uring *ring, server_t *s, udp_slot_t *slot, unsigned *outstanding)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        counted_submit(s, ring);
        sqe = io_uring_get_sqe(ring);
        if (!sqe) {
            return -EBUSY;
        }
    }
    slot->sending = 0;
    slot->iov = (struct iovec){.iov_base = slot->in, .iov_len = MAX_DATAGRAM};
    slot->msg = (struct msghdr){.msg_name = &slot->peer,
                                .msg_namelen = sizeof(slot->peer),
                                .msg_iov = &slot->iov,
                                .msg_iovlen = 1};
    io_uring_prep_recvmsg(sqe, s->listener, &slot->msg, 0);
    io_uring_sqe_set_data(sqe, slot);
    (*outstanding)++;
    return 0;
}

static int post_udp_send(struct io_uring *ring, server_t *s, udp_slot_t *slot, size_t length,
                         unsigned *outstanding)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        counted_submit(s, ring);
        sqe = io_uring_get_sqe(ring);
        if (!sqe) {
            return -EBUSY;
        }
    }
    slot->sending = 1;
    slot->iov = (struct iovec){.iov_base = slot->out, .iov_len = length};
    slot->msg.msg_iov = &slot->iov;
    slot->msg.msg_iovlen = 1;
    slot->msg.msg_namelen = sizeof(slot->peer);
    io_uring_prep_sendmsg(sqe, s->listener, &slot->msg, MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe, slot);
    (*outstanding)++;
    return 0;
}

static int run_udp(server_t *s, struct io_uring *ring)
{
    /* --udp-depth receives wait on the one socket. Every waiting receive is
     * woken when a datagram arrives, so the depth trades pipelining against
     * wasted wakeups; the experiments measure it instead of fixing one value. */
    int depth = s->config->udp_depth;
    udp_slot_t *slots = calloc((size_t)depth, sizeof(*slots));
    if (!slots) {
        return -ENOMEM;
    }
    unsigned outstanding = 0;
    int result = 0;
    for (int i = 0; i < depth && !result; i++) {
        result = post_udp_recv(ring, s, &slots[i], &outstanding);
    }
    server_ready(s);
    while (!stop_requested && !result) {
        int rc = counted_submit(s, ring);
        if (rc < 0 && rc != -EINTR && rc != -EAGAIN && rc != -EBUSY) {
            result = rc;
            break;
        }
        struct io_uring_cqe *cqe;
        s->loop_calls++;
        rc = counted_wait(s, ring, &cqe);
        if (rc == -ETIME || rc == -EINTR) {
            continue;
        }
        if (rc < 0) {
            result = rc;
            break;
        }
        for (int i = 0; i < RING_DEPTH && io_uring_peek_cqe(ring, &cqe) == 0; i++) {
            udp_slot_t *slot = io_uring_cqe_get_data(cqe);
            int n = cqe->res;
            io_uring_cqe_seen(ring, cqe);
            outstanding--;
            if (slot->sending) {
                if (n < 0) {
                    s->datagrams_dropped++;
                } else {
                    s->bytes_written += (uint64_t)n;
                }
                result = post_udp_recv(ring, s, slot, &outstanding);
            } else if (n >= 0) {
                ssize_t reply = udp_handle(s, slot->in, (size_t)n, slot->out);
                result = reply < 0 ? post_udp_recv(ring, s, slot, &outstanding)
                                   : post_udp_send(ring, s, slot, (size_t)reply, &outstanding);
            } else if (n == -EINTR || n == -EAGAIN) {
                result = post_udp_recv(ring, s, slot, &outstanding);
            } else {
                result = n;
            }
            if (result) {
                break;
            }
        }
    }
    drain(s, ring, outstanding, NULL);
    free(slots);
    return result;
}

/* ---- TCP ---- */
static int run_tcp(server_t *s, struct io_uring *ring)
{
    conn_t *head = NULL, *tail = NULL;
    static __thread int accept_tag;
    int accepting = 0;
    unsigned outstanding = 0;
    int result = 0;
    server_ready(s);

    while (!stop_requested) {
        struct io_uring_sqe *sqe;
        if (!accepting && (sqe = io_uring_get_sqe(ring))) {
            io_uring_prep_accept(sqe, s->listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
            io_uring_sqe_set_data(sqe, &accept_tag);
            accepting = 1;
            outstanding++;
        }
        while (head && (sqe = io_uring_get_sqe(ring))) {
            conn_t *c = head;
            head = c->ready_next;
            if (!head) {
                tail = NULL;
            }
            if (conn_is_reading(c)) {
                io_uring_prep_recv(sqe, c->fd, conn_buffer(c), conn_length(c), 0);
            } else {
                io_uring_prep_send(sqe, c->fd, conn_buffer(c), conn_length(c), MSG_NOSIGNAL);
            }
            io_uring_sqe_set_data(sqe, c);
            c->pending = 1;
            outstanding++;
        }
        int rc = counted_submit(s, ring);
        if (rc < 0 && rc != -EINTR) {
            if (rc == -EAGAIN || rc == -EBUSY) {
                /* Completion queue is full: reap below, then submit again. */
            } else {
                result = rc;
                break;
            }
        }
        struct io_uring_cqe *cqe;
        s->loop_calls++;
        if (head) {
            /* The submission queue filled before every ready client got an
             * entry. Waiting here could stall: the receives already submitted
             * may belong to clients that are themselves waiting for replies
             * still in our queue. So only collect what has finished, then go
             * back and submit the rest. */
            rc = io_uring_peek_cqe(ring, &cqe) == 0 ? 0 : -ETIME;
        } else {
            rc = counted_wait(s, ring, &cqe);
        }
        if (rc == -ETIME || rc == -EINTR) {
            continue;
        }
        if (rc < 0) {
            result = rc;
            break;
        }
        /* A bounded batch lets the next cycle submit follow-up operations. */
        for (int i = 0; i < RING_DEPTH && io_uring_peek_cqe(ring, &cqe) == 0; i++) {
            void *data = io_uring_cqe_get_data(cqe);
            int n = cqe->res;
            io_uring_cqe_seen(ring, cqe);
            outstanding--;
            if (data == &accept_tag) {
                accepting = 0;
                if (n >= 0) {
                    int enabled = 1;
                    if (setsockopt(n, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled))) {
                        close(n);
                        s->rejected++;
                        continue;
                    }
                    conn_t *c = conn_add(s, n);
                    if (c) {
                        enqueue(&head, &tail, c);
                    }
                } else if (n != -EAGAIN && n != -EINTR && n != -ECONNABORTED) {
                    result = n;
                    goto shutdown;
                }
            } else {
                conn_t *c = data;
                c->pending = 0;
                int status = conn_complete(c, n);
                if (status) {
                    conn_close(c, status);
                } else {
                    enqueue(&head, &tail, c);
                }
            }
        }
    }

shutdown:
    drain(s, ring, outstanding, &accept_tag);
    return result;
}

int engine_run(server_t *s)
{
    struct io_uring ring;
    int rc = ring_setup(s, &ring);
    if (rc < 0) {
        return rc;
    }
    rc = s->config->transport == TRANSPORT_UDP ? run_udp(s, &ring) : run_tcp(s, &ring);
    io_uring_queue_exit(&ring);
    return rc;
}
