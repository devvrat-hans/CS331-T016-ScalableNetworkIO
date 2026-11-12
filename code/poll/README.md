# poll Network I/O Engine

**Author:** Devvrat Hans (Roll No: 23110094)
**Course:** CS331 - Scalable Network I/O (Team T016)

## 1. Overview

This component implements the `poll()`-based network I/O engine for the CS331 project, **Scalable Network I/O: From select to io_uring**.

`poll()` is a synchronous readiness-notification mechanism. The engine runs a single-threaded event loop that asks the kernel which sockets can make progress, then performs one `recv()` or `send()` on each ready socket. It supports both TCP and UDP transports and integrates with the project's shared networking framework.

## 2. Implementation Structure

The implementation is contained in:

```text
code/poll/poll_server.c
```

The engine follows the shared project architecture:

- **Engine:** Decides when each socket can make progress, using `poll()`.
- **Framework:** Manages sockets, connection state, request processing, statistics and shutdown.
- **Application plugins:** Define the request and response behavior independently of the I/O engine.

The engine implements the framework interface:

```c
int engine_run(server_t *s);
```

## 3. Descriptor Table

The engine keeps two parallel arrays in a `table_t`:

| Field | Type | Purpose |
|---|---|---|
| `fds` | `struct pollfd *` | Array passed directly to `poll()` |
| `clients` | `conn_t **` | Connection that owns the matching `fds` entry |
| `count` | `size_t` | Number of entries in use |
| `capacity` | `size_t` | `max_clients + 1` |

- Entry `i` of `fds` and entry `i` of `clients` describe the same connection, so finding the connection for a ready entry is O(1).
- Slot 0 is always the listening socket.
- Both arrays are allocated once with `calloc()` when the engine starts and freed when it exits.

## 4. TCP Implementation

TCP handling is implemented in `engine_run()`.

### Interest selection

Before every `poll()` call, each client's `events` field is set from its framework state using `conn_is_reading()`:

- `POLLIN` while the connection is in the HEADER or RECEIVE stage.
- `POLLOUT` while it is in the SEND stage.

The engine never asks for both. A socket with free buffer space is always writable, so asking for `POLLOUT` with nothing to send would wake the loop constantly.

### Event processing

- `poll()` is called with the whole table and a `WAIT_MS` (100 ms) timeout.
- `EINTR` restarts the loop, so the stop flag set by `SIGINT`/`SIGTERM` is checked promptly.
- For every client with non-zero `revents`, `conn_progress()` performs one `recv()` or `send()`.
- `POLLHUP`/`POLLERR` are not decoded directly. A hang-up can arrive together with unread data, so the result of `recv`/`send` decides whether the connection has closed or failed.

### Swap removal

When a connection closes, the last entry of the table is moved into its slot and `count` is decremented. Removal is O(1) and the array stays compact. The loop index is not advanced afterwards, because the moved entry must still be checked in the same pass.

### Connection acceptance

- When the listener has `POLLIN`, the framework's `accept_ready()` accepts the pending connections.
- New connections are accepted after the existing clients have been served.
- `remember()` appends each new connection with `POLLIN`, since every client starts by reading a header.
- If the table is full, the connection is closed and counted as `rejected`.

## 5. UDP Implementation

UDP handling is implemented in `run_udp()`.

UDP has no per-client sockets, so only the single listening socket is polled. When it is readable, the framework's `udp_drain()` processes the waiting datagrams.

## 6. poll vs select

| | select | poll |
|---|---|---|
| Interface | 3 bitmaps + `nfds` | Array of `struct pollfd` |
| Descriptor limit | 1024 (`FD_SETSIZE`) | None, only `ulimit -n` |
| Cost depends on | Highest fd number | Number of entries |
| Input overwritten? | Yes, sets rebuilt every call | No, `events` and `revents` are separate |
| Bytes copied per fd | 1 bit per set | 8 bytes |
| Scaling with idle fds | O(N) | O(N) |

## 7. Statistics and Instrumentation

The engine integrates with the framework's statistics and syscall counters. Every `poll()` call increments `loop_calls` and `sc_wait`, which are reported with the other counters (connections, bytes, CPU time and peak memory) in the framework's JSON summary.

## 8. Limitations

- **O(N) per call:** the whole `pollfd` array is copied into the kernel and every entry is checked on every call, however few sockets are ready. User space also walks the whole array afterwards. This is why poll degrades under many mostly idle connections, like select.
- **More bytes copied than select:** each `pollfd` is 8 bytes, against one bit per set in select.
- **Level-triggered only:** a socket is reported again on every call for as long as it stays readable or writable.
- **Interest rebuilt every iteration:** the `events` loop runs over all clients before each `poll()` call.
- The connection ceiling is `--max-clients` and the open-file limit (`ulimit -n`).

## 9. Dependencies and Integration

The implementation requires:

- Linux (or another POSIX system providing `poll()`).
- GCC or a compatible C compiler.
- The project's shared `framework.h` and `framework.c` components.

The poll engine is compiled and linked as part of the project's shared build system.

From the integrated repository, the project build command is:

```bash
cd code/benchmark
make
```

The shared framework provides the runtime configuration, including transport selection (`--transport tcp|udp`), `--max-clients` and the other server settings.
