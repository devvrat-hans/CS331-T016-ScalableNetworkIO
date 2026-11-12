/*
 * apps.c - application plugins. Each one is a table of functions; adding a new
 * network application means adding one table here. No engine changes.
 *
 * Traffic each plugin represents:
 *   noop      Sir's baseline: the thread does nothing but go down and come back.
 *   echo      small request/reply, latency sensitive (chat-like).
 *   upload    read-intensive bulk transfer; every received byte is verified.
 *   download  write-intensive bulk transfer.
 *   stream    fixed-size frames; the load generator paces them (video-like).
 *   memory    keeps a 256 KiB buffer per client; each request is written at the
 *             next position, so successive requests walk the whole buffer and,
 *             with many clients, the working set exceeds the CPU caches.
 */
#define _GNU_SOURCE
#include "framework.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define PATTERN_PERIOD 251
#define MEMORY_STATE_SIZE (256 * 1024)

/* Pattern byte i is i modulo 251. Precomputing it means a single memcmp or
 * memcpy does the work, instead of a per-byte modulo whose cost depends on
 * what the compiler happens to generate for each server. */
#define PATTERN_CHUNK 65536
static unsigned char pattern[PATTERN_PERIOD + PATTERN_CHUNK];

void pattern_init(void)
{
    for (size_t i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (unsigned char)(i % PATTERN_PERIOD);
    }
}

const unsigned char *pattern_at(uint64_t offset)
{
    return pattern + offset % PATTERN_PERIOD;
}

static uint64_t zero(uint64_t length)
{
    (void)length;
    return 0;
}
static uint64_t same(uint64_t length)
{
    return length;
}
static uint64_t one(uint64_t length)
{
    (void)length;
    return 1;
}

static void fill_ack(conn_t *c, uint64_t offset, unsigned char *out, size_t n)
{
    (void)c;
    (void)offset;
    (void)n;
    out[0] = 'K';
}

/* Copy or compare the pattern in pieces, so a 1 MiB buffer needs no 1 MiB table. */
static void copy_pattern(unsigned char *out, uint64_t offset, size_t n)
{
    while (n) {
        size_t k = n < PATTERN_CHUNK ? n : PATTERN_CHUNK;
        memcpy(out, pattern_at(offset), k);
        out += k;
        offset += k;
        n -= k;
    }
}

static int matches_pattern(const unsigned char *data, uint64_t offset, size_t n)
{
    while (n) {
        size_t k = n < PATTERN_CHUNK ? n : PATTERN_CHUNK;
        if (memcmp(data, pattern_at(offset), k)) {
            return 0;
        }
        data += k;
        offset += k;
        n -= k;
    }
    return 1;
}

static void fill_pattern(conn_t *c, uint64_t offset, unsigned char *out, size_t n)
{
    (void)c;
    copy_pattern(out, offset, n);
}

/* noop */
static int noop_validate(uint64_t length)
{
    return length ? 0 : -EMSGSIZE;
}
static long noop_datagram(uint64_t response, const unsigned char *body, size_t n,
                          unsigned char *out, size_t capacity)
{
    (void)response;
    (void)body;
    (void)n;
    (void)out;
    (void)capacity;
    return 0;
}

/* echo: the request stays in the connection buffer and is sent straight back. */
static int echo_validate(uint64_t length)
{
    return length && length <= ECHO_MAX ? 0 : -EMSGSIZE;
}
static long echo_datagram(uint64_t response, const unsigned char *body, size_t n,
                          unsigned char *out, size_t capacity)
{
    (void)response;
    if (n > capacity) {
        return -EMSGSIZE;
    }
    memcpy(out, body, n);
    return (long)n;
}

/* upload */
static int bulk_validate(uint64_t length)
{
    return length && length <= MAX_TRANSFER ? 0 : -EMSGSIZE;
}
static int upload_body(conn_t *c, uint64_t offset, const unsigned char *data, size_t n)
{
    (void)c;
    return matches_pattern(data, offset, n) ? 0 : -EBADMSG;
}
static long upload_datagram(uint64_t response, const unsigned char *body, size_t n,
                            unsigned char *out, size_t capacity)
{
    (void)response;
    (void)out;
    (void)capacity;
    return matches_pattern(body, 0, n) ? 0 : -EBADMSG;
}

/* download and stream */
static long pattern_datagram(uint64_t response, const unsigned char *body, size_t n,
                             unsigned char *out, size_t capacity)
{
    (void)body;
    (void)n;
    if (response > capacity) {
        return -EMSGSIZE;
    }
    copy_pattern(out, 0, (size_t)response);
    return (long)response;
}

/* memory: a resident per-client buffer that requests are written into.
 * Earlier every request overwrote the first bytes of the buffer, so only 4 KiB
 * per client was ever touched and everything stayed in cache. Now each request
 * starts where the previous one ended (wrapping around), like a log or a cache
 * filling its slab: 500 clients cycle through 125 MiB, far more than the L3. */
typedef struct memory_state_t {
    size_t next;   /* Where the next request starts. */
    size_t base;   /* Where the current request started. */
    unsigned char data[MEMORY_STATE_SIZE];
} memory_state_t;

static int memory_validate(uint64_t length)
{
    return length && length <= MEMORY_STATE_SIZE ? 0 : -EMSGSIZE;
}
static int memory_open(conn_t *c)
{
    memory_state_t *state = calloc(1, sizeof(*state));
    if (!state) {
        return -ENOMEM;
    }
    /* calloc can return untouched pages; make the memory actually resident. */
    memset(state->data, 1, MEMORY_STATE_SIZE);
    c->app_state = state;
    return 0;
}
static void memory_close(conn_t *c)
{
    free(c->app_state);
    c->app_state = NULL;
}
static int memory_body(conn_t *c, uint64_t offset, const unsigned char *data, size_t n)
{
    memory_state_t *state = c->app_state;
    if (offset == 0) {
        state->base = state->next;
        state->next = (size_t)((state->next + c->length) % MEMORY_STATE_SIZE);
    }
    size_t at = (size_t)((state->base + offset) % MEMORY_STATE_SIZE);
    size_t first = n < MEMORY_STATE_SIZE - at ? n : MEMORY_STATE_SIZE - at;
    memcpy(state->data + at, data, first);
    memcpy(state->data, data + first, n - first); /* Wrap around the end. */
    return 0;
}

static const app_ops_t apps[] = {
    {.name = "noop", .supports_udp = 1, .validate = noop_validate, .body_size = zero,
     .response_size = one, .fill = fill_ack, .on_datagram = noop_datagram},
    {.name = "echo", .supports_udp = 1, .validate = echo_validate, .body_size = same,
     .response_size = same, .on_datagram = echo_datagram},
    {.name = "upload", .supports_udp = 1, .validate = bulk_validate, .body_size = same,
     .response_size = one, .on_body = upload_body, .fill = fill_ack,
     .on_datagram = upload_datagram},
    {.name = "download", .supports_udp = 1, .validate = bulk_validate, .body_size = zero,
     .response_size = same, .fill = fill_pattern, .on_datagram = pattern_datagram},
    {.name = "stream", .supports_udp = 1, .validate = bulk_validate, .body_size = zero,
     .response_size = same, .fill = fill_pattern, .on_datagram = pattern_datagram},
    {.name = "memory", .supports_udp = 0, .validate = memory_validate, .body_size = same,
     .response_size = one, .on_body = memory_body, .fill = fill_ack, .on_open = memory_open,
     .on_close = memory_close},
};

const app_ops_t *app_find(const char *name)
{
    for (size_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        if (!strcmp(apps[i].name, name)) {
            return &apps[i];
        }
    }
    return NULL;
}
