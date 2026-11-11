/*
 * select_server.c - the select engine.
 * Only the event loop lives here; see Chirag/code/common/framework.h for the contract.
 *
 * Cost visible in this file: the fd_sets are rebuilt every iteration and both
 * we and the kernel walk every descriptor up to the highest one, whether or not
 * that descriptor has anything to do. That is the O(N) part of select.
 */
#define _GNU_SOURCE
#include "framework.h" /* Chirag/code/common/framework.h; the Makefile adds -I */

#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

const char *const ENGINE_NAME = "select";

typedef struct table_t {
    conn_t *clients[FD_SETSIZE];
    int highest;
} table_t;

static int remember(server_t *s, conn_t *c, void *context)
{
    table_t *table = context;
    if (c->fd >= FD_SETSIZE) {
        /* select cannot represent this descriptor number. */
        s->rejected++;
        s->accepted--;
        conn_close(c, 0);
        s->closed--;
        return 0;
    }
    table->clients[c->fd] = c;
    if (c->fd > table->highest) {
        table->highest = c->fd;
    }
    return 0;
}

static int run_udp(server_t *s)
{
    server_ready(s);
    while (!stop_requested) {
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(s->listener, &reads);
        struct timeval timeout = {.tv_sec = 0, .tv_usec = WAIT_MS * 1000};
        s->loop_calls++;
        s->sc_wait++;
        int n = select(s->listener + 1, &reads, NULL, NULL, &timeout);
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
    if (s->listener >= FD_SETSIZE) {
        return -EMFILE;
    }
    if (s->config->transport == TRANSPORT_UDP) {
        return run_udp(s);
    }
    static __thread table_t table;
    memset(&table, 0, sizeof(table));
    table.highest = s->listener;
    server_ready(s);
    while (!stop_requested) {
        fd_set reads, writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        FD_SET(s->listener, &reads);
        for (int fd = 0; fd <= table.highest; fd++) {
            if (!table.clients[fd]) {
                continue;
            }
            if (conn_is_reading(table.clients[fd])) {
                FD_SET(fd, &reads);
            } else {
                FD_SET(fd, &writes);
            }
        }
        struct timeval timeout = {.tv_sec = 0, .tv_usec = WAIT_MS * 1000};
        s->loop_calls++;
        s->sc_wait++;
        int n = select(table.highest + 1, &reads, &writes, NULL, &timeout);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (!n) {
            continue;
        }
        /* Existing clients first, so a closed descriptor number cannot be
         * reused by a new connection while its readiness bit is still set. */
        for (int fd = 0; fd <= table.highest; fd++) {
            if (!table.clients[fd] || (!FD_ISSET(fd, &reads) && !FD_ISSET(fd, &writes))) {
                continue;
            }
            int rc = conn_progress(table.clients[fd]);
            if (rc) {
                conn_close(table.clients[fd], rc);
                table.clients[fd] = NULL;
            }
        }
        while (table.highest > s->listener && !table.clients[table.highest]) {
            table.highest--;
        }
        if (FD_ISSET(s->listener, &reads)) {
            int rc = accept_ready(s, remember, &table);
            if (rc < 0) {
                return rc;
            }
        }
    }
    return 0;
}
