# Review epoll Echo Server

**User:** Anonymous  
**Created:** 9/5/2026 15:57:16  
**Updated:** 9/5/2026 16:05:27  
**Exported:** 9/10/2026 1:20:22  
**Link:** [https://chatgpt.com/c/6a9bee7b-e760-83ee-a0b5-05cdc82c4e54](https://chatgpt.com/c/6a9bee7b-e760-83ee-a0b5-05cdc82c4e54)  

## Prompt:
05/09/2026, 15:57:14

/*
 * epoll_echo_server.c - Single-threaded TCP echo server using epoll()
 *
 * Course: CS331 - Scalable Network I/O (Project ID: 13, Team ID: T016)
 * Author: Patil Nachiket Kiran (Roll No: 24110250)
 *
 * Implementation details:
 * - Uses epoll (level-triggered) for I/O multiplexing in a single thread
 * - Sets listening and client sockets to non-blocking mode (O_NONBLOCK)
 * - Registers EPOLLOUT only when pending echo data exists (EPOLL_CTL_MOD)
 * - Drains new incoming connections using a non-blocking accept() loop
 * - Per-connection state is heap-allocated; epoll_event.data.ptr points
 *   directly at it, avoiding a separate fd-to-state lookup table
 * - A singly-linked list of live connections exists only to allow a clean
 *   walk-and-close on shutdown (epoll itself does not expose this)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
 
#define DEFAULT_PORT 9090
#define BACKLOG 128
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096
 
static volatile sig_atomic_t running = 1;
 
typedef struct conn {
    int fd;
    int is_listener;
    char buffer[BUFFER_SIZE];
    size_t bytes_to_write;
    size_t write_pos;
    uint32_t registered_events;   /* cached epoll interest mask */
    struct conn *next;
} conn_t;
 
static conn_t *conn_list_head = NULL;
 
static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}
 
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
 
    if (flags == -1)
        return -1;
 
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
 
int main(int argc, char *argv[])
{
    int server_fd, epfd;
    int port = DEFAULT_PORT;
    struct sockaddr_in server_addr;
    struct epoll_event ev, events[MAX_EVENTS];
    conn_t listener_conn;
 
    long total_bytes = 0;
    long total_reads = 0;
    int active_connections = 0;
 
    if (argc > 1) {
        char *endptr;
        long p = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || p <= 0 || p > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 1;
        }
        port = (int)p;
    }
 
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
 
    /* create listening socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }
 
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }
 
    if (set_nonblocking(server_fd) == -1) {
        perror("fcntl");
        close(server_fd);
        return 1;
    }
 
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port);
 
    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }
 
    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }
 
    /* create epoll instance and register the listener */
    epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }
 
    listener_conn.fd = server_fd;
    listener_conn.is_listener = 1;
    listener_conn.bytes_to_write = 0;
    listener_conn.write_pos = 0;
    listener_conn.registered_events = EPOLLIN;
    listener_conn.next = NULL;
 
    ev.events = EPOLLIN;
    ev.data.ptr = &listener_conn;
 
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        close(server_fd);
        close(epfd);
        return 1;
    }
 
    printf("Epoll server listening on port %d\n", port);
 
    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 1000);
 
        if (n == -1) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }
 
        if (n == 0)
            continue;
 
        for (int i = 0; i < n; i++) {
            conn_t *c = events[i].data.ptr;
 
            /* new connection(s) on listener */
            if (c->is_listener) {
                while (1) {
                    int client_fd = accept(server_fd, NULL, NULL);
 
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        if (errno == EINTR)
                            continue;
                        perror("accept");
                        break;
                    }
 
                    if (set_nonblocking(client_fd) == -1) {
                        perror("fcntl");
                        close(client_fd);
                        continue;
                    }
 
                    conn_t *nc = malloc(sizeof(conn_t));
                    if (!nc) {
                        perror("malloc");
                        close(client_fd);
                        continue;
                    }
                    nc->fd = client_fd;
                    nc->is_listener = 0;
                    nc->bytes_to_write = 0;
                    nc->write_pos = 0;
                    nc->registered_events = EPOLLIN;
 
                    struct epoll_event cev;
                    cev.events = EPOLLIN;
                    cev.data.ptr = nc;
 
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev) == -1) {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                        free(nc);
                        continue;
                    }
 
                    nc->next = conn_list_head;
                    conn_list_head = nc;
 
                    active_connections++;
                }
 
                continue;
            }
 
            if ((events[i].events & EPOLLIN) && c->bytes_to_write == 0) {
                ssize_t r;
 
                do {
                    r = read(c->fd, c->buffer, BUFFER_SIZE);
                } while (r == -1 && errno == EINTR);
 
                if (r > 0) {
                    c->bytes_to_write = (size_t)r;
                    c->write_pos = 0;
                    total_reads++;
                    goto try_write;
                }
                else if (r == 0) {
                    goto cleanup_conn;
                }
                else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    goto cleanup_conn;
                }
            }
 
            if (events[i].events & EPOLLOUT) {
            try_write:
                while (c->write_pos < c->bytes_to_write) {
                    ssize_t w = write(c->fd,
                                      c->buffer + c->write_pos,
                                      c->bytes_to_write - c->write_pos);
 
                    if (w > 0) {
                        c->write_pos += (size_t)w;
                        total_bytes += w;
                    }
                    else if (w == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    else if (w == -1 && errno == EINTR) {
                        continue;
                    }
                    else {
                        goto cleanup_conn;
                    }
                }
 
                uint32_t desired;
                if (c->write_pos == c->bytes_to_write) {
                    c->bytes_to_write = 0;
                    c->write_pos = 0;
                    desired = EPOLLIN;     
                } else {
                    desired = EPOLLOUT;    
                }
 
                if (desired != c->registered_events) {
                    struct epoll_event mev;
                    mev.events = desired;
                    mev.data.ptr = c;
 
                    if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &mev) == -1) {
                        perror("epoll_ctl: MOD");
                        goto cleanup_conn;
                    }
                    c->registered_events = desired;
                }
            }
 
            if (events[i].events & (EPOLLERR | EPOLLHUP))
                goto cleanup_conn;
 
            continue;
 
        cleanup_conn: {
            conn_t **pp = &conn_list_head;
            while (*pp && *pp != c) pp = &(*pp)->next;
            if (*pp) *pp = c->next;
 
            epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
            close(c->fd);
            free(c);
            active_connections--;
        }
        }
    }
 
    printf("\nShutting down...\n");
    printf("Active connections: %d\n", active_connections);
    printf("Total read operations: %ld\n", total_reads);
    printf("Total bytes echoed: %ld\n", total_bytes);
 
    conn_t *cur = conn_list_head;
    while (cur) {
        conn_t *next = cur->next;
        epoll_ctl(epfd, EPOLL_CTL_DEL, cur->fd, NULL);
        close(cur->fd);
        free(cur);
        cur = next;
    }
 
    close(server_fd);
    close(epfd);
 
    return 0;
}

What issues this code has?

## Response:
05/09/2026, 15:57:20

I’ll review it for correctness, edge cases, and epoll-specific behavior. The biggest concerns are connection shutdown handling and what happens when `EPOLLHUP` arrives with unread data.

> **Reviewing socket behavior**
>
> I’m checking the event loop, read/write transitions, hangup handling, stale pointers, and signal behavior. The main concern is whether cleanup or repeated events could invalidate connection state; most paths appear reasonable so far.
>
> **Reviewed socket behavior**
>
> 
>
> Worked for 12s

The code is a reasonable basic level-triggered `epoll` echo server, but it has several correctness and robustness issues. The most important ones are around `EPOLLHUP`/connection shutdown.

### 1. `EPOLLHUP` can cause data loss

This is the biggest bug.

You do:

```c
if (events[i].events & (EPOLLERR | EPOLLHUP))
    goto cleanup_conn;
```

That happens **after only one `read()`** and after at most one pending-buffer write attempt.

Suppose the client sends 10 KB and then closes the connection. You may receive:

```text
EPOLLIN | EPOLLHUP
```

You read only 4096 bytes, echo those bytes, and then immediately execute:

```c
goto cleanup_conn;
```

The remaining data is never read or echoed.

Even worse, if the echo write is partial and `bytes_to_write` remains nonzero, you still close the connection because of `EPOLLHUP`, throwing away the unsent response.

For an echo server, that violates the expected behavior.

A better approach is:

```text
EPOLLIN
    read until EAGAIN
    queue all received bytes

EPOLLOUT
    write until EAGAIN
    keep EPOLLOUT enabled while data remains

peer closed:
    stop accepting new input
    flush pending output
    close only after output is drained
```

`EPOLLRDHUP` is also useful for detecting a TCP peer half-close explicitly.

---

### 2. Listener `EPOLLHUP` / `EPOLLERR` is effectively ignored

This section runs first:

```c
if (c->is_listener) {
    while (1) {
        ...
    }

    continue;
}
```

So even if the listening socket generates:

```c
EPOLLERR
EPOLLHUP
```

you never reach:

```c
if (events[i].events & (EPOLLERR | EPOLLHUP))
```

for the listener.

You should check listener error/hangup events before or inside the listener branch. Otherwise a permanently-ready error condition could repeatedly wake the event loop.

---

### 3. Only one `read()` is performed per `EPOLLIN` event

You intentionally have:

```c
do {
    r = read(c->fd, c->buffer, BUFFER_SIZE);
} while (...);
```

but this isn't a drain loop.

With level-triggered epoll, the usual pattern is:

```c
while (1) {
    r = read(...);

    if (r > 0) {
        ...
        continue;
    }

    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        break;

    ...
}
```

Your code reads at most 4096 bytes and then switches to writing.

That's not inherently incorrect, but it reduces throughput substantially under heavy traffic because unread data remains in the socket and you require another epoll wakeup.

For a course project about scalable I/O, an examiner may specifically question this.

---

### 4. The per-connection buffer cannot hold more than one read

You have:

```c
char buffer[BUFFER_SIZE];
size_t bytes_to_write;
size_t write_pos;
```

This means a connection can have only one outstanding echo chunk.

That is simple and valid, but it limits throughput.

A more scalable server generally has a proper output queue, because:

```text
read -> read -> read -> ...
```

can accumulate multiple chunks while the client is slow to receive.

Your design instead applies very aggressive backpressure:

```text
read one chunk
    ↓
fully write it
    ↓
read next chunk
```

So it behaves more like a stop-and-wait echo server than a high-throughput event-driven server.

---

### 5. `EPOLLIN` is disabled while output is pending

You register:

```c
desired = EPOLLOUT;
```

when a write cannot complete.

That's logically safe, but consider the case where a client has both:

```text
unread input
+
unsent output
```

The server doesn't read more input until the existing output is completely drained.

This avoids needing a larger receive queue, but again reduces concurrency/throughput.

For an echo server that's an acceptable design tradeoff, but it should be understood as intentional backpressure rather than "fully scalable" behavior.

---

### 6. `EPOLLERR` handling is too aggressive

You treat:

```c
EPOLLERR | EPOLLHUP
```

as an immediate connection destruction.

`EPOLLERR` is particularly tricky because there may be socket error state that should be examined with `getsockopt(fd, SOL_SOCKET, SO_ERROR, ...)`.

For example, you'd generally distinguish:

```c
EPOLLERR
EPOLLHUP
EPOLLRDHUP
```

rather than lumping all of them into "free immediately."

---

### 7. No `EPOLLRDHUP`

For TCP connections, Linux provides:

```c
EPOLLRDHUP
```

which is useful for detecting that the peer performed a shutdown of its sending side.

A typical registration might be:

```c
cev.events = EPOLLIN | EPOLLRDHUP;
```

Then you can implement:

```text
peer half-closed
        ↓
read remaining data
        ↓
flush echo
        ↓
close
```

instead of relying mainly on `EPOLLHUP`.

---

### 8. `signal()` is less robust than `sigaction()`

You use:

```c
signal(SIGINT, handle_signal);
signal(SIGTERM, handle_signal);
```

For production-quality POSIX code, `sigaction()` is preferable because its behavior is more explicit and controllable.

For example, whether `epoll_wait()` gets automatically restarted after a signal depends on signal semantics/platform behavior.

Your code still works reasonably because the loop periodically wakes every second:

```c
epoll_wait(..., 1000);
```

but a cleaner implementation would use `sigaction()`.

---

### 9. The 1-second epoll timeout is unnecessary overhead

You use:

```c
epoll_wait(epfd, events, MAX_EVENTS, 1000);
```

This means the process wakes once per second even when nothing is happening.

If you only need signal-driven shutdown, you can generally use:

```c
epoll_wait(epfd, events, MAX_EVENTS, -1);
```

and arrange signal handling appropriately.

The current 1-second timeout isn't a correctness bug, but it creates unnecessary wakeups.

---

### 10. `long` is not ideal for byte counters

You have:

```c
long total_bytes;
long total_reads;
```

`total_bytes` can become very large on a long-running server.

Better:

```c
uint64_t total_bytes;
uint64_t total_reads;
```

and print with the appropriate format macro from `<inttypes.h>`.

On a 32-bit system, `long` may be only 32 bits.

---

### 11. `total_reads` doesn't really mean "all reads"

You increment:

```c
total_reads++;
```

only when:

```c
r > 0
```

That's fine if "read operations" means successful reads.

But if you later change the server to a read-until-`EAGAIN` loop, the meaning becomes more important. You may want separate counters for:

```text
successful read syscalls
bytes received
successful write syscalls
bytes echoed
```

Currently you only count successful reads and bytes written.

---

### 12. `total_bytes` is counted on writes, not received bytes

You print:

```c
printf("Total bytes echoed: %ld\n", total_bytes);
```

and increment:

```c
total_bytes += w;
```

That's actually a reasonable definition of "bytes echoed."

But it is not equal to bytes received in failure cases. For example, a client may send data and disconnect while the server has only partially written the response.

So your statistics can be interpreted differently depending on what the assignment expects.

---

### 13. Listener state is manually initialized incompletely

You do:

```c
conn_t listener_conn;
listener_conn.fd = server_fd;
listener_conn.is_listener = 1;
listener_conn.bytes_to_write = 0;
listener_conn.write_pos = 0;
listener_conn.registered_events = EPOLLIN;
listener_conn.next = NULL;
```

The buffer remains uninitialized.

It doesn't matter currently because the listener never uses `buffer`, but something like:

```c
conn_t listener_conn = {0};
listener_conn.fd = server_fd;
listener_conn.is_listener = 1;
```

is cleaner and avoids future mistakes.

---

### 14. `accept()` could use `accept4()`

Instead of:

```c
int client_fd = accept(server_fd, NULL, NULL);

if (set_nonblocking(client_fd) == -1) ...
```

Linux provides:

```c
accept4(server_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
```

This creates the accepted socket with the desired flags immediately.

Your current version is still valid, especially for a course project, but `accept4()` is cleaner on Linux.

---

### 15. `FD_CLOEXEC` isn't set

Neither:

```c
server_fd
epfd
client_fd
```

is marked close-on-exec.

If the process later calls `exec()` somewhere, these descriptors can leak into the child process.

For a simple standalone server this isn't important, but for robust server code you would normally use `SOCK_CLOEXEC`, `EPOLL_CLOEXEC`, etc.

---

### 16. `conn_list_head` exists only for cleanup, but cleanup is O(n)

This comment is correct:

```c
/* A singly-linked list of live connections exists only to allow
 * a clean walk-and-close on shutdown
 */
```

However, each normal connection close does:

```c
conn_t **pp = &conn_list_head;
while (*pp && *pp != c)
    pp = &(*pp)->next;
```

So removing a connection costs **O(number of connections)**.

That means your normal connection cleanup is:

```text
O(n)
```

per close.

For a scalability-focused assignment, that's worth mentioning.

You could make it O(1) with a doubly-linked list:

```c
prev
next
```

or maintain another mechanism for removal.

The epoll pointer design itself is good:

```c
ev.data.ptr = nc;
```

and avoids an fd → state lookup table.

---

### 17. Error handling on `epoll_ctl(DEL)` is ignored

You do:

```c
epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
close(c->fd);
free(c);
```

without checking the return value.

Usually this is harmless during cleanup, but at least distinguishing `EBADF`, `ENOENT`, etc. can be useful when debugging.

---

### 18. Binding to `INADDR_ANY` exposes it on every IPv4 interface

You have:

```c
server_addr.sin_addr.s_addr = INADDR_ANY;
```

So the server accepts connections from any reachable interface.

For a lab machine this may be fine. If the intention is local testing only, you'd usually bind to:

```c
127.0.0.1
```

instead.

This is more of a deployment/security consideration than a bug.

---

## The biggest logical flaw

The central issue can be summarized as:

```text
EPOLLIN + EPOLLHUP
        ↓
read only once
        ↓
possibly write once / until EAGAIN
        ↓
immediately close
        ↓
unread / unsent data is lost
```

That should be fixed first.

A better state machine is roughly:

```text
             +----------------+
             |  waiting       |
             |  EPOLLIN       |
             +-------+--------+
                     |
                  read()
                     |
             +-------v--------+
             | output pending |
             +-------+--------+
                     |
                  write()
               +-----+-----+
               |           |
             EAGAIN     all sent
               |           |
               v           v
          EPOLLOUT     EPOLLIN
               
peer EOF/RDHUP
       |
       v
stop reading
       |
pending output?
   /         \
 yes          no
  |            |
flush        close
```

### What I would change first

For your CS331 project, I'd prioritize these:

1. **Fix `EPOLLHUP`/`EPOLLRDHUP` handling so received data is not discarded.**
2. **Read until `EAGAIN` rather than only one `read()`.**
3. **Use a proper pending-output state and flush it before closing after peer shutdown.**
4. **Handle listener `EPOLLERR/HUP`.**
5. **Replace the O(n) connection-list removal if scalability is being evaluated.**
6. **Use `sigaction()` and preferably `accept4()` for a more robust Linux implementation.**
7. **Use 64-bit counters.**

The `epoll_event.data.ptr` approach and the cached `registered_events` idea are actually good design choices. The main problem isn't your use of epoll itself; it's the connection state machine around EOF/HUP and partial I/O.

## Prompt:
05/09/2026, 15:58:20

create report on which is needed to fix

## Response:
05/09/2026, 15:58:22

I’ll turn the code review into a structured technical report focused on the fixes that are actually necessary, separating critical bugs from optional improvements.

> **Reading document creation guidelines**
>
>
> **Read document creation guidelines**
>
> 
>
> Worked for 6s

I created the technical fix report with prioritized issues, recommended code changes, connection-state logic, and validation tests.

[Download the Epoll Echo Server Fix Report](sandbox:/mnt/data/epoll_echo_server_fix_report.docx)

## Prompt:
05/09/2026, 16:00:14

````
### Code Audit & Remediation Report: `epoll_echo_server.c`

This report outlines the remaining architectural vulnerabilities, edge-case failures, and performance bottlenecks in the updated server implementation, ranked by severity.

---

### Issue Summary Matrix

| Issue | Severity | Component | Failure Mode |
| --- | --- | --- | --- |
| **1. Unbounded Accept Loop** | High | Listener Handling | Existing connections starve under connection bursts / SYN floods |
| **2. `EMFILE` / `ENFILE` CPU Busy-Spin** | High | Listener Error Handling | 100% CPU lockup when OS file descriptor limits are exhausted |
| **3. $O(N)$ Connection Teardown** | Medium | Memory & List Tracking | Latency spikes on disconnects at scale ($N > 1,000$) |
| **4. Redundant Syscalls on Handshake** | Medium | Socket Configuration | $3\times$ syscall overhead per inbound connection |
| **5. Abrupt Teardown on Client `FIN**` | Low | Read / State Transition | Truncates echo responses if client issues `shutdown(SHUT_WR)` |

---

### Detailed Findings & Code Fixes

#### 1. Unbounded Accept Loop Starvation

* **Location:** Lines 179–224 (`while (1) { int client_fd = accept(...); ... }`)
* **Problem:** The inner `accept()` loop drains incoming handshakes until `errno == EAGAIN`. Under a high connection arrival rate or SYN flood, the single-threaded event loop never exits this block. Active connections with pending `read` or `write` readiness are completely starved.
* **Remediation:** Enforce a batch cap per event cycle (e.g., 32–64 accepts) and return control to the main event loop.

```c
#define MAX_ACCEPTS_PER_CYCLE 64

if (c->is_listener) {
    int accept_count = 0;
    while (accept_count++ < MAX_ACCEPTS_PER_CYCLE) {
        int client_fd = accept4(server_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            perror("accept4");
            break;
        }
        /* Connection registration logic */
    }
    continue;
}

```

---

#### 2. `EMFILE` / `ENFILE` Level-Triggered Busy Loop

* **Location:** Lines 182–190
* **Problem:** When the process reaches `RLIMIT_NOFILE` (maximum open file descriptors), `accept()` fails with `EMFILE`. The code prints an error and breaks from the loop. However, in **level-triggered mode**, the unprocessed TCP connection remains in the kernel's listening backlog, leaving `server_fd` permanently readable. `epoll_wait()` will immediately wake up on the next cycle, spinning the CPU at 100% without making progress.
* **Remediation:** Allocate a dummy file descriptor (`/dev/null`) at startup. When `EMFILE` occurs, close the dummy descriptor to create a slot, `accept()` and immediately `close()` the excess connection, then reopen the dummy descriptor.

```c
/* Initialization before event loop */
int idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);

/* Inside accept loop */
if (client_fd == -1) {
    if (errno == EMFILE || errno == ENFILE) {
        close(idle_fd);
        int discard_fd = accept(server_fd, NULL, NULL);
        if (discard_fd != -1)
            close(discard_fd);
        idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        break;
    }
    /* handle EAGAIN/EINTR ... */
}

```

---

#### 3. $O(N)$ Connection Teardown Overhead

* **Location:** Lines 297–306 (`cleanup_conn`)
* **Problem:** `cleanup_conn` scans a singly-linked list sequentially from `conn_list_head` to locate the pointer to `c`. Under benchmark loads with thousands of concurrent connections, connection closes require $O(N)$ pointer traversals, introducing severe tail-latency spikes.
* **Remediation:** Convert `conn_t` to a doubly-linked list with `prev` and `next` pointers to achieve $O(1)$ removal.

```c
typedef struct conn {
    int fd;
    int is_listener;
    char buffer[BUFFER_SIZE];
    size_t bytes_to_write;
    size_t write_pos;
    uint32_t registered_events;
    struct conn *prev;
    struct conn *next;
} conn_t;

/* O(1) removal inside cleanup_conn */
cleanup_conn: {
    if (c->prev)
        c->prev->next = c->next;
    else
        conn_list_head = c->next;

    if (c->next)
        c->next->prev = c->prev;

    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    free(c);
    active_connections--;
}

```

---

#### 4. Multiple Syscalls per Handshake

* **Location:** Lines 182, 192–196 (`accept` + `fcntl(F_GETFL)` + `fcntl(F_SETFL)`)
* **Problem:** Each client setup requires three context switches (`accept`, read flags, set flags). Under high connection churning (e.g., short-lived HTTP-style traffic), syscall overhead dominates CPU time.
* **Remediation:** Replace `accept()` and `set_nonblocking()` with the Linux-specific `accept4()` system call to set `SOCK_NONBLOCK` and `SOCK_CLOEXEC` atomically.

```c
int client_fd = accept4(server_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
if (client_fd == -1) {
    /* error handling */
}
/* client_fd is already non-blocking; no fcntl() required */

```

---

#### 5. Premature Teardown on Client Half-Close (`FIN`)

* **Location:** Lines 246–248
* **Problem:** If a client finishes transmitting and shuts down its write half-channel (`shutdown(s, SHUT_WR)`), `read()` returns `0`. The current code jumps directly to `cleanup_conn`, unconditionally terminating the connection. If the client sent data that was read and partially written before the `FIN`, any unflushed bytes remaining in `c->buffer` are discarded.
* **Remediation:** Track a `closed_by_client` flag. When `r == 0`, mark the socket as read-closed, deregister `EPOLLIN`, and allow remaining writes to flush. Close the connection only after `write_pos == bytes_to_write`.

```c
else if (r == 0) {
    /* Client sent FIN. If there are pending writes, flush them before teardown */
    if (c->bytes_to_write > 0) {
        c->registered_events = EPOLLOUT;
        struct epoll_event mev = { .events = EPOLLOUT, .data.ptr = c };
        epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &mev);
        /* Mark read closed so no further read attempts occur */
    } else {
        goto cleanup_conn;
    }
}

```

---

### Implementation Priority Order

1. **Step 1 (Starvation & Stability):** Add the accept count throttle and the `idle_fd` pattern for `EMFILE`. This makes the server resilient against denial-of-service and file descriptor exhaustion.
2. **Step 2 (Data Integrity):** Guard half-closed sockets (`r == 0`) so pending write buffers flush completely prior to closing the descriptor.
3. **Step 3 (Throughput Optimization):** Switch to `accept4()` and replace the singly-linked connection list with a doubly-linked structure to eliminate $O(N)$ lookup costs.
````

This is report from my colleague

## Response:
05/09/2026, 16:00:16

Yes. Your colleague’s report is useful, but I would **not accept it unchanged**. A few findings are valid and important, while some severity claims and explanations need correction.

### My assessment of the colleague’s report

| Issue | Assessment | Priority |
|---|---|---|
| Unbounded accept loop | **Valid concern**, but somewhat workload-dependent | High |
| `EMFILE` / `ENFILE` handling | **Valid and important** | High |
| O(N) connection teardown | **Valid performance issue** | Medium |
| `accept4()` optimization | **Valid optimization**, but not really a correctness issue | Low/Medium |
| Client `FIN` truncating pending echo | **Definitely valid** | **Critical/High** |

The most important thing missing from the colleague's report is that the current server has a **broader shutdown-state problem**, not just the `read() == 0` case.

For example, this code:

```c
if (events[i].events & (EPOLLERR | EPOLLHUP))
    goto cleanup_conn;
```

can close a connection even when `bytes_to_write > 0`.

So this sequence is possible:

```text
client sends data
        ↓
server reads it
        ↓
server cannot write all of it
        ↓
client closes
        ↓
EPOLLIN / EPOLLHUP
        ↓
cleanup_conn
        ↓
pending echo is lost
```

That is a **data-integrity bug**, and I would rank it above the accept-loop optimization.

### One correction to the `EMFILE` section

The colleague is right that `EMFILE` needs special treatment. However, saying that the current code necessarily causes a **100% CPU lockup** is too strong.

The current code does:

```c
if (errno == EAGAIN || errno == EWOULDBLOCK)
    break;
...
perror("accept");
break;
```

Then it returns to `epoll_wait()`.

Because the listener remains readable while backlog entries exist, `epoll_wait()` may repeatedly return the listener and the server can indeed enter a busy loop while `EMFILE` persists. But the exact behavior depends on backlog state and incoming traffic. I would describe this as:

> **Potential persistent busy-loop / CPU exhaustion under file-descriptor exhaustion**

rather than guaranteeing 100% CPU in every circumstance.

### One correction to the accept starvation recommendation

The proposed:

```c
#define MAX_ACCEPTS_PER_CYCLE 64
```

is a reasonable mitigation, but it introduces a trade-off.

With level-triggered epoll, if the backlog remains readable after 64 accepts, the listener will simply be reported again. That's okay, and it gives other descriptors an opportunity to run.

For your project, I'd recommend a bounded batch such as 32–64, but I would describe this as **fairness protection**, not a mandatory correctness fix.

### Another important issue: `accept4()` does not solve `EMFILE`

The report presents these as separate improvements, which is good, but it's worth making explicit:

```c
accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)
```

reduces setup syscalls, but it **does not prevent `EMFILE`**.

You still need the idle-fd strategy independently.

---

# Recommended consolidated report

I would merge both reviews into this final priority order:

### P0 — Must fix before submission

**1. Incorrect connection shutdown / HUP handling**

Do not immediately `cleanup_conn` on `EPOLLHUP` when input/output still needs processing.

**2. Client FIN / `read() == 0` handling**

A peer half-close must transition the connection into a read-closed state while allowing pending echo data to drain.

**3. Preserve partial writes**

Keep `EPOLLOUT` active until:

```c
write_pos == bytes_to_write
```

even after peer EOF.

### P1 — Strongly recommended for a scalable server

**4. Handle `EMFILE` / `ENFILE`**

Use the spare/idle descriptor technique to prevent persistent listener-readiness spinning when file descriptors are exhausted.

**5. Bound the accept batch**

Use something like:

```c
#define MAX_ACCEPTS_PER_CYCLE 64
```

so a connection storm cannot monopolize the single-threaded event loop.

**6. Drain readable data**

Instead of one `read()`, read until `EAGAIN`, subject to the output-buffer design. If multiple chunks can be pending, introduce an output queue.

### P2 — Performance/engineering improvements

**7. O(1) connection removal**

Change the list from singly linked to doubly linked.

**8. Use `accept4()`**

Use:

```c
accept4(server_fd, NULL, NULL,
        SOCK_NONBLOCK | SOCK_CLOEXEC);
```

to avoid the separate `fcntl()` calls.

**9. Use `sigaction()`**

More robust signal setup.

**10. Use 64-bit statistics**

Prefer `uint64_t` for long-running byte/operation counters.

---

## One architectural change I strongly recommend

Instead of only adding:

```c
int closed_by_client;
```

I'd use explicit connection state:

```c
typedef struct conn {
    int fd;
    int is_listener;
    int peer_closed;

    char buffer[BUFFER_SIZE];
    size_t bytes_to_write;
    size_t write_pos;

    uint32_t registered_events;

    struct conn *prev;
    struct conn *next;
} conn_t;
```

Then the important rule becomes:

```text
peer_closed == 0
    → EPOLLIN may be enabled

peer_closed == 1
    → do not read new application data
    → EPOLLOUT remains enabled while output is pending

peer_closed == 1 && bytes_to_write == 0
    → close connection
```

That gives you a much cleaner state machine than treating `HUP`, EOF, and pending output as independent cleanup conditions.

### Bottom line

Your colleague's report identifies **three genuinely useful additional issues**—accept fairness, FD exhaustion, and O(N) teardown—but the **FIN/HUP problem should be elevated substantially**. The final report should make **correct shutdown and preservation of pending echo data the #1 fix**, followed by `EMFILE` handling and accept fairness.

I can also merge both reports into a single polished **submission-ready remediation report** with corrected severity levels and final recommended code patches.

## Prompt:
05/09/2026, 16:02:12

Scalable Network I/O: From select to io_uringProject Description: Build and compare single-threaded TCP echo servers using four distinct Linux socket-handling mechanisms: select, poll, epoll, and io_uring. Implement each server paradigm from scratch and explore the shift from synchronous notification to truly asynchronous completion queue processing pattern. By using appropriate load generators (tcpkali, wrk etc.), students should benchmark and profile the behavior of these mechanisms by varying the connection loads (10, 100…) to observe system call overhead, memory scalability, and CPU efficiency.

Tools/Technologies: C/C++/Rust, Linux network stack, io_uring

Expected Outcomes: Upon completion, students will deliver fully functional C/C++ implementations for all four engines alongside a benchmarking report analyzing throughput, latency distributions, and system call frequencies. Students will demonstrate a clear understanding of O(N) vs O(1) scaling bottlenecks, explain why select and poll degrade under high concurrency, and identify the trade-offs that make io_uring's ring-buffer design superior to epoll for minimizing user-to-kernel context switches.

This is the project statement and description

My part is related to epoll only...

Now create a list which I need to fix.

## Response:
05/09/2026, 16:02:14

Given the project statement, your **epoll implementation should prioritize correctness first, then scalability characteristics that you can meaningfully benchmark against `select`, `poll`, and `io_uring`**.

## Epoll server — fixes you should make

### Must fix before benchmarking

| # | Fix | Priority | Why it matters for your project |
|---|---|---|---|
| 1 | **Fix `EPOLLHUP` / `EPOLLERR` shutdown handling** | **Critical** | Current code can close a connection while unread data or unsent echo data still exists. This can produce incorrect benchmark results and functional failures. |
| 2 | **Handle `read() == 0` as peer half-close, not immediate destruction** | **Critical** | `shutdown(SHUT_WR)` / FIN can arrive while the server still has data to echo. Pending output must be flushed first. |
| 3 | **Preserve partial writes** | **Critical** | `write()` may return `EAGAIN` or a partial count. The connection must remain registered for `EPOLLOUT` until all pending bytes are sent. |
| 4 | **Add explicit connection shutdown state** | **Critical** | Add something like `peer_closed` so the server can distinguish "peer stopped sending" from "connection can now be freed." |
| 5 | **Handle listener `EPOLLERR` / `EPOLLHUP` separately** | **High** | Your listener currently `continue`s before the generic error handling, so listener failures are not handled correctly. |

### Needed for the "scalable" part of the assignment

| # | Fix | Priority | Why |
|---|---|---|---|
| 6 | **Bound the `accept()` loop** | **High** | Your `while (1)` can spend too long accepting connections during a connection burst, starving existing clients. A bounded batch such as 32–64 improves fairness. |
| 7 | **Handle `EMFILE` / `ENFILE`** | **High** | File-descriptor exhaustion can leave the listener continuously ready while `accept()` cannot succeed, causing repeated wakeups and CPU waste. Use an idle/spare FD. |
| 8 | **Read until `EAGAIN`** | **High** | You currently perform only one successful `read()` per event. Draining the socket better demonstrates the intended efficiency of level-triggered epoll. |
| 9 | **Use an output queue if you want high-throughput behavior** | **High** | Your single 4 KB buffer effectively creates a read → write → read pattern. A real output queue allows multiple received chunks to be pending. |
| 10 | **Avoid O(N) connection deletion** | **Medium** | Your singly linked list requires a scan when removing a connection. A doubly linked list gives O(1) removal. This is relevant to the project's O(N) vs O(1) discussion. |

### Good engineering improvements

| # | Fix | Priority | Why |
|---|---|---|---|
| 11 | **Use `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)`** | Medium | Removes the extra `fcntl()` setup calls and makes connection setup cheaper. |
| 12 | **Use `sigaction()` instead of `signal()`** | Medium | More robust signal handling. |
| 13 | **Use `epoll_wait(..., -1)` rather than a 1000 ms timeout** | Medium | Avoids unnecessary periodic wakeups when idle. |
| 14 | **Use `uint64_t` for counters** | Low | Better for long benchmark runs and large byte counts. |
| 15 | **Set close-on-exec on descriptors** | Low | Good resource hygiene; not central to the benchmark. |

---

# The 5 fixes I would consider essential

For your specific project, don't over-engineer the implementation. At minimum, fix these:

### 1. Correct EOF/HUP handling

Your current:

```c
if (events[i].events & (EPOLLERR | EPOLLHUP))
    goto cleanup_conn;
```

is unsafe.

A connection can have:

```text
EPOLLIN + EPOLLHUP
```

and still contain data that should be echoed.

---

### 2. Add a `peer_closed` flag

For example:

```c
int peer_closed;
```

Then:

```text
read() == 0
       ↓
peer_closed = 1
       ↓
stop reading
       ↓
flush pending writes
       ↓
close only when output == 0
```

Also consider registering:

```c
EPOLLIN | EPOLLRDHUP
```

for TCP clients.

---

### 3. Don't close until pending output is gone

The close condition should effectively be:

```c
if (c->peer_closed && c->bytes_to_write == 0) {
    /* cleanup */
}
```

not simply:

```c
if (EPOLLHUP)
    cleanup;
```

---

### 4. Bound the accept loop

Use something like:

```c
#define MAX_ACCEPTS_PER_CYCLE 64
```

This is especially relevant because your server is **single-threaded**. You don't want one event source monopolizing the event loop.

---

### 5. Handle `EMFILE` / `ENFILE`

This is a worthwhile scalability fix for an epoll server.

Use a spare FD such as `/dev/null`, so when descriptors are exhausted you can temporarily free one slot, accept and close one queued connection, and restore the spare FD.

---

# What you do **not** need to over-focus on

Since your project is specifically comparing **select vs poll vs epoll vs io_uring**, you should avoid making the epoll server unnecessarily complicated.

You do **not** need to turn it into a production-grade web server.

For example, these are lower priority:

- elaborate logging frameworks
- thread pools
- TLS
- sophisticated memory allocators
- complicated connection maps
- elaborate configuration systems

They don't help demonstrate the project's central comparison.

---

# Recommended final epoll architecture

Your implementation should have this basic model:

```text
                    epoll_wait()
                         |
              +----------+----------+
              |                     |
           listener              client
              |                     |
        accept batch          EPOLLIN / RDHUP
              |                     |
        nonblocking             read until
        registration             EAGAIN
                                      |
                                      v
                               pending output
                                      |
                                      v
                                  EPOLLOUT
                                      |
                              write until EAGAIN
                                      |
                         +------------+------------+
                         |                         |
                    output remains             output empty
                         |                         |
                      EPOLLOUT              EPOLLIN again
                         |
                  peer already closed?
                         |
                         v
                   close connection
```

## Suggested implementation priority

**Phase 1:** Fix HUP/EOF/partial-write correctness.

**Phase 2:** Add `EPOLLRDHUP`, bounded accept, and `EMFILE` handling.

**Phase 3:** Improve throughput with read-until-`EAGAIN` and, if needed, an output queue.

**Phase 4:** Optimize connection-list removal and `accept4()`.

**Phase 5:** Benchmark.

That ordering is important: **benchmark only after Phase 1 is complete**, otherwise your throughput/latency measurements may reflect dropped connections or truncated echoes rather than epoll performance.

### One project-specific point

Your report should **not claim that epoll itself is O(1) in every operation**. A better explanation for the project is:

- `select()`/`poll()` require user/kernel handling of an fd set/list that scales with the number of monitored descriptors.
- `epoll` maintains a kernel-side interest list and returns only ready events, avoiding a full scan of all monitored descriptors on each wait.
- Your **own singly-linked connection cleanup is still O(N)**, which is separate from epoll's event-notification mechanism.

That distinction will make your epoll section much stronger academically.

## Prompt:
05/09/2026, 16:04:00

Give me updated code, without new comments

## Response:
05/09/2026, 16:04:05

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#define DEFAULT_PORT 9090
#define BACKLOG 128
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096
#define MAX_ACCEPTS_PER_CYCLE 64

static volatile sig_atomic_t running = 1;

typedef struct conn {
    int fd;
    int is_listener;
    int peer_closed;
    char buffer[BUFFER_SIZE];
    size_t bytes_to_write;
    size_t write_pos;
    uint32_t registered_events;
    struct conn *prev;
    struct conn *next;
} conn_t;

static conn_t *conn_list_head = NULL;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1)
        return -1;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void add_connection_to_list(conn_t *c)
{
    c->prev = NULL;
    c->next = conn_list_head;

    if (conn_list_head)
        conn_list_head->prev = c;

    conn_list_head = c;
}

static void remove_connection_from_list(conn_t *c)
{
    if (c->prev)
        c->prev->next = c->next;
    else
        conn_list_head = c->next;

    if (c->next)
        c->next->prev = c->prev;
}

int main(int argc, char *argv[])
{
    int server_fd, epfd;
    int port = DEFAULT_PORT;
    int idle_fd;
    struct sockaddr_in server_addr;
    struct epoll_event ev, events[MAX_EVENTS];
    conn_t listener_conn;

    uint64_t total_bytes = 0;
    uint64_t total_reads = 0;
    int active_connections = 0;

    if (argc > 1) {
        char *endptr;
        long p = strtol(argv[1], &endptr, 10);

        if (*endptr != '\0' || p <= 0 || p > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 1;
        }

        port = (int)p;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    if (set_nonblocking(server_fd) == -1) {
        perror("fcntl");
        close(server_fd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }

    memset(&listener_conn, 0, sizeof(listener_conn));
    listener_conn.fd = server_fd;
    listener_conn.is_listener = 1;
    listener_conn.registered_events = EPOLLIN;

    ev.events = EPOLLIN;
    ev.data.ptr = &listener_conn;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        close(server_fd);
        close(epfd);
        return 1;
    }

    idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (idle_fd == -1) {
        perror("open");
        close(server_fd);
        close(epfd);
        return 1;
    }

    printf("Epoll server listening on port %d\n", port);

    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

        if (n == -1) {
            if (errno == EINTR)
                continue;

            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            conn_t *c = events[i].data.ptr;
            uint32_t revents = events[i].events;

            if (c->is_listener) {
                if (revents & (EPOLLERR | EPOLLHUP)) {
                    running = 0;
                    break;
                }

                int accept_count = 0;

                while (accept_count < MAX_ACCEPTS_PER_CYCLE) {
                    int client_fd = accept4(
                        server_fd,
                        NULL,
                        NULL,
                        SOCK_NONBLOCK | SOCK_CLOEXEC
                    );

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;

                        if (errno == EINTR)
                            continue;

                        if (errno == EMFILE || errno == ENFILE) {
                            close(idle_fd);

                            int discard_fd = accept4(
                                server_fd,
                                NULL,
                                NULL,
                                SOCK_NONBLOCK | SOCK_CLOEXEC
                            );

                            if (discard_fd != -1)
                                close(discard_fd);

                            idle_fd = open("/dev/null",
                                           O_RDONLY | O_CLOEXEC);

                            if (idle_fd == -1)
                                perror("open");

                            break;
                        }

                        perror("accept4");
                        break;
                    }

                    accept_count++;

                    conn_t *nc = calloc(1, sizeof(conn_t));
                    if (!nc) {
                        perror("calloc");
                        close(client_fd);
                        continue;
                    }

                    nc->fd = client_fd;
                    nc->is_listener = 0;
                    nc->peer_closed = 0;
                    nc->bytes_to_write = 0;
                    nc->write_pos = 0;
                    nc->registered_events = EPOLLIN | EPOLLRDHUP;

                    struct epoll_event cev;
                    memset(&cev, 0, sizeof(cev));
                    cev.events = nc->registered_events;
                    cev.data.ptr = nc;

                    if (epoll_ctl(epfd, EPOLL_CTL_ADD,
                                  client_fd, &cev) == -1) {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                        free(nc);
                        continue;
                    }

                    add_connection_to_list(nc);
                    active_connections++;
                }

                continue;
            }

            if (revents & EPOLLERR) {
                int socket_error = 0;
                socklen_t len = sizeof(socket_error);

                if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR,
                               &socket_error, &len) == -1) {
                    socket_error = errno;
                }

                (void)socket_error;

                remove_connection_from_list(c);
                epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                close(c->fd);
                free(c);
                active_connections--;
                continue;
            }

            if (revents & (EPOLLIN | EPOLLRDHUP)) {
                if (!c->peer_closed && c->bytes_to_write == 0) {
                    while (1) {
                        ssize_t r = read(c->fd,
                                         c->buffer,
                                         sizeof(c->buffer));

                        if (r > 0) {
                            c->bytes_to_write = (size_t)r;
                            c->write_pos = 0;
                            total_reads++;
                            break;
                        }

                        if (r == 0) {
                            c->peer_closed = 1;
                            break;
                        }

                        if (errno == EINTR)
                            continue;

                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;

                        remove_connection_from_list(c);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                        close(c->fd);
                        free(c);
                        active_connections--;
                        c = NULL;
                        break;
                    }

                    if (!c)
                        continue;
                }

                if (revents & EPOLLRDHUP)
                    c->peer_closed = 1;
            }

            if (c->bytes_to_write > 0 &&
                (revents & (EPOLLOUT | EPOLLIN | EPOLLRDHUP))) {
                while (c->write_pos < c->bytes_to_write) {
                    ssize_t w = write(
                        c->fd,
                        c->buffer + c->write_pos,
                        c->bytes_to_write - c->write_pos
                    );

                    if (w > 0) {
                        c->write_pos += (size_t)w;
                        total_bytes += (uint64_t)w;
                    } else if (w == -1 &&
                               (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else if (w == -1 && errno == EINTR) {
                        continue;
                    } else {
                        remove_connection_from_list(c);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                        close(c->fd);
                        free(c);
                        active_connections--;
                        c = NULL;
                        break;
                    }
                }

                if (!c)
                    continue;

                if (c->write_pos == c->bytes_to_write) {
                    c->bytes_to_write = 0;
                    c->write_pos = 0;
                }
            }

            if (c->peer_closed && c->bytes_to_write == 0) {
                remove_connection_from_list(c);
                epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                close(c->fd);
                free(c);
                active_connections--;
                continue;
            }

            uint32_t desired_events;

            if (c->bytes_to_write > 0) {
                desired_events = EPOLLOUT | EPOLLRDHUP;
            } else if (c->peer_closed) {
                desired_events = EPOLLRDHUP;
            } else {
                desired_events = EPOLLIN | EPOLLRDHUP;
            }

            if (desired_events != c->registered_events) {
                struct epoll_event mev;
                memset(&mev, 0, sizeof(mev));
                mev.events = desired_events;
                mev.data.ptr = c;

                if (epoll_ctl(epfd, EPOLL_CTL_MOD,
                              c->fd, &mev) == -1) {
                    perror("epoll_ctl: MOD");

                    remove_connection_from_list(c);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                    close(c->fd);
                    free(c);
                    active_connections--;
                    continue;
                }

                c->registered_events = desired_events;
            }
        }
    }

    printf("\nShutting down...\n");
    printf("Active connections: %d\n", active_connections);
    printf("Total read operations: %" PRIu64 "\n", total_reads);
    printf("Total bytes echoed: %" PRIu64 "\n", total_bytes);

    conn_t *cur = conn_list_head;

    while (cur) {
        conn_t *next = cur->next;

        epoll_ctl(epfd, EPOLL_CTL_DEL, cur->fd, NULL);
        close(cur->fd);
        free(cur);

        cur = next;
    }

    close(idle_fd);
    close(server_fd);
    close(epfd);

    return 0;
}
```

Compile with:

```bash
gcc -O2 -Wall -Wextra -std=c11 epoll_echo_server.c -o epoll_echo_server
```

One important limitation remains: this version still uses a **single 4 KB output buffer per connection**, so it does not implement a fully queued multi-read/multi-write pipeline. For your project, that is acceptable as a controlled epoll design, but it means the benchmark should document that architectural choice.



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)