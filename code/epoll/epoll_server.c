/*
 * epoll_server.c - the epoll engine.
 * Only the event loop lives here; see Chirag/code/common/framework.h for the contract.
 *
 * --epoll-mode lt (default): level-triggered. One I/O step per reported event;
 * the interest set is changed with EPOLL_CTL_MOD whenever a client switches
 * between reading and writing.
 *
 * --epoll-mode et: edge-triggered. Each socket is registered once for both
 * directions. An edge is reported only when readiness changes, so the handler
 * must keep going until the kernel says EAGAIN, or data would be left stranded.
 */
#define _GNU_SOURCE
#include "framework.h" /* Chirag/code/common/framework.h; the Makefile adds -I */

#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

const char *const ENGINE_NAME = "epoll";

#define EVENT_BATCH 256

static int remember(server_t *s, conn_t *c, void *context)
{
    int epfd = *(int *)context;
    struct epoll_event interest = {.data.ptr = c};
    interest.events = s->config->edge_triggered ? EPOLLIN | EPOLLOUT | EPOLLET : EPOLLIN;
    s->sc_ctl++;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &interest)) {
        conn_close(c, -errno);
    }
    return 0;
}

static void drop(server_t *s, int epfd, conn_t *c, int rc)
{
    s->sc_ctl++;
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    conn_close(c, rc);
}

static void handle_level(server_t *s, int epfd, conn_t *c)
{
    int was_reading = conn_is_reading(c);
    int rc = conn_progress(c);
    if (rc) {
        drop(s, epfd, c, rc);
    } else if (was_reading != conn_is_reading(c)) {
        struct epoll_event interest = {.data.ptr = c};
        interest.events = conn_is_reading(c) ? EPOLLIN : EPOLLOUT;
        s->sc_ctl++;
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &interest)) {
            int error = errno;
            drop(s, epfd, c, -error);
        }
    }
}

/* lt-direct: level-triggered, but once a request is complete the reply is sent
 * at once instead of waiting for an EPOLLOUT event. A small reply normally fits
 * in the socket buffer, so the interest set only changes (epoll_ctl) when a send
 * would block. This is how event-driven servers usually avoid epoll_ctl. */
static void handle_direct(server_t *s, int epfd, conn_t *c)
{
    int rc = conn_progress(c);
    if (!rc && !conn_is_reading(c) && !c->interest_out) {
        rc = conn_progress(c);
    }
    if (rc) {
        drop(s, epfd, c, rc);
        return;
    }
    int want_out = !conn_is_reading(c);
    if (want_out != c->interest_out) {
        struct epoll_event interest = {.events = want_out ? EPOLLOUT : EPOLLIN, .data.ptr = c};
        s->sc_ctl++;
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &interest)) {
            int error = errno;
            drop(s, epfd, c, -error);
            return;
        }
        c->interest_out = want_out;
    }
}

static void handle_edge(server_t *s, int epfd, conn_t *c)
{
    for (;;) {
        int would_block = 0;
        int rc = conn_step(c, &would_block);
        if (rc) {
            drop(s, epfd, c, rc);
            return;
        }
        if (would_block) {
            return; /* The next edge will bring us back. */
        }
    }
}

int engine_run(server_t *s)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        return -errno;
    }
    int is_udp = s->config->transport == TRANSPORT_UDP;
    struct epoll_event interest = {.events = EPOLLIN, .data.ptr = NULL};
    if (is_udp && s->config->edge_triggered) {
        interest.events |= EPOLLET;
    }
    s->sc_ctl++;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, s->listener, &interest)) {
        int error = errno;
        close(epfd);
        return -error;
    }
    int result = 0;
    server_ready(s);
    while (!stop_requested) {
        struct epoll_event events[EVENT_BATCH];
        s->loop_calls++;
        s->sc_wait++;
        int n = epoll_wait(epfd, events, EVENT_BATCH, WAIT_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            result = -errno;
            break;
        }
        for (int i = 0; i < n; i++) {
            conn_t *c = events[i].data.ptr;
            if (is_udp) {
                int rc;
                do {
                    rc = udp_drain(s);
                } while (rc == 0 && s->config->edge_triggered && !stop_requested);
                if (rc < 0) {
                    result = rc;
                    goto finished;
                }
            } else if (!c) { /* NULL identifies the listening socket. */
                result = accept_ready(s, remember, &epfd);
                if (result < 0) {
                    goto finished;
                }
            } else if (s->config->edge_triggered) {
                handle_edge(s, epfd, c);
            } else if (s->config->epoll_direct) {
                handle_direct(s, epfd, c);
            } else {
                handle_level(s, epfd, c);
            }
        }
    }
finished:
    close(epfd);
    return result;
}
