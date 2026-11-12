/*
 * poll_server.c - the poll engine.
 * Only the event loop lives here; see Chirag/code/common/framework.h for the contract.
 *
 * Unlike select, poll has no FD_SETSIZE ceiling. But the whole pollfd array is
 * still copied into the kernel and scanned on every call, so the cost is O(N)
 * in the number of watched descriptors, not the number that are ready.
 */
#define _GNU_SOURCE
#include "framework.h" /* Chirag/code/common/framework.h; the Makefile adds -I */

#include <errno.h>
#include <poll.h>
#include <stdlib.h>

const char *const ENGINE_NAME = "poll";

/* fds[i] and clients[i] describe the same connection; slot 0 is the listener. */
typedef struct table_t {
    struct pollfd *fds;
    conn_t **clients;
    size_t count;
    size_t capacity;
} table_t;

/* Called for each accepted connection: append it to the table, or reject it if full. */
static int remember(server_t *s, conn_t *c, void *context)
{
    table_t *table = context;
    if (table->count == table->capacity) {
        s->rejected++;
        s->accepted--;
        conn_close(c, 0);
        s->closed--;
        return 0;
    }
    table->clients[table->count] = c;
    table->fds[table->count++] = (struct pollfd){.fd = c->fd, .events = POLLIN};
    return 0;
}

/* UDP has no per-client sockets, so only the listener is watched. */
static int run_udp(server_t *s)
{
    server_ready(s);
    while (!stop_requested) {
        struct pollfd item = {.fd = s->listener, .events = POLLIN};
        s->loop_calls++;
        s->sc_wait++;
        int n = poll(&item, 1, WAIT_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (n > 0) {
            int rc = udp_drain(s);
            if (rc < 0) {
                return rc;
            }
        }
    }
    return 0;
}

int engine_run(server_t *s)
{
    if (s->config->transport == TRANSPORT_UDP) {
        return run_udp(s);
    }
    table_t table = {.capacity = s->config->max_clients + 1, .count = 1};
    table.fds = calloc(table.capacity, sizeof(*table.fds));
    table.clients = calloc(table.capacity, sizeof(*table.clients));
    if (!table.fds || !table.clients) {
        free(table.fds);
        free(table.clients);
        return -ENOMEM;
    }
    int result = 0;
    table.fds[0] = (struct pollfd){.fd = s->listener, .events = POLLIN};
    server_ready(s);
    while (!stop_requested) {
        /* Interest is rebuilt every iteration: POLLIN while reading, POLLOUT while sending. */
        for (size_t i = 1; i < table.count; i++) {
            table.fds[i].events = conn_is_reading(table.clients[i]) ? POLLIN : POLLOUT;
        }
        s->loop_calls++;
        s->sc_wait++;
        int n = poll(table.fds, table.count, WAIT_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            result = -errno;
            break;
        }
        if (!n) {
            continue;
        }
        for (size_t i = 1; i < table.count;) {
            if (!table.fds[i].revents) {
                i++;
                continue;
            }
            /* recv/send decides EOF and errors: POLLHUP may coexist with unread data. */
            int rc = conn_progress(table.clients[i]);
            if (rc) {
                /* Swap-remove: move the last entry into this slot and re-check slot i. */
                conn_close(table.clients[i], rc);
                table.count--;
                table.fds[i] = table.fds[table.count];
                table.clients[i] = table.clients[table.count];
            } else {
                i++;
            }
        }
        /* Slot 0 is the listener: accept new connections once existing clients are served. */
        if (table.fds[0].revents & POLLIN) {
            result = accept_ready(s, remember, &table);
            if (result < 0) {
                break;
            }
        }
    }
    free(table.fds);
    free(table.clients);
    return result;
}
