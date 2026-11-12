# epoll Network I/O Engine

## 1. Overview

This component implements the `epoll`-based network I/O engine for the CS331 project, **Scalable Network I/O: From select to io_uring**.

The implementation uses the Linux `epoll` interface to perform readiness-based event-driven network I/O. It supports both TCP and UDP transports and integrates with the project's shared networking framework.

The engine supports three operating modes:

- **Level-triggered (`lt`, default):** One I/O step per reported event; the interest set is switched between read and write as the connection changes state.
- **Level-triggered with direct send (`lt-direct`):** Level-triggered, but replies are sent immediately after a request completes instead of waiting for an `EPOLLOUT` event.
- **Edge-triggered (`et`):** Each socket is registered once for both directions and drained until the kernel reports `EAGAIN`.

## 2. Implementation Structure

The implementation is contained in:

```text
code/epoll/epoll_server.c
```

The engine follows the shared project architecture:

- **Engine:** Owns the epoll instance and the event loop, and decides when to issue I/O steps.
- **Framework:** Manages connection state, request processing, application callbacks, statistics and shutdown.
- **Application plugins:** Define the request and response behavior independently of the I/O engine.

The engine implements the framework interface:

```c
int engine_run(server_t *s);
```

Only the event loop lives in this file. Reading, writing and request state-machine logic are provided by the framework (`conn_step()`, `conn_is_reading()`, `conn_close()`, `accept_ready()`, `udp_drain()`).

## 3. Epoll Setup and Configuration

| Parameter | Value | Purpose |
|---|---:|---|
| `EVENT_BATCH` | 256 | Maximum events returned by one `epoll_wait()` call |
| `WAIT_MS` | framework-defined | `epoll_wait()` timeout, so the loop can notice a stop request |

`engine_run()` creates the epoll instance with `epoll_create1(EPOLL_CLOEXEC)` and registers the listening socket (`s->listener`) with `EPOLL_CTL_ADD`.

- The listening socket is identified in event data by a `NULL` pointer (`data.ptr = NULL`); every client connection stores its `conn_t` pointer in `data.ptr`.
- For UDP in edge-triggered mode, the socket is registered with `EPOLLET`; otherwise it is registered with `EPOLLIN` only.

After setup, `server_ready()` is called to signal the framework that the engine is accepting traffic.

## 4. Event Loop

The main loop runs until `stop_requested` is set:

1. Increment the loop-iteration and wait counters.
2. Call `epoll_wait()` with up to `EVENT_BATCH` events and the `WAIT_MS` timeout.
3. Retry on `EINTR`; any other error ends the loop and is returned.
4. Dispatch each returned event according to transport and mode.

Dispatch order for each event:

| Condition | Handler |
|---|---|
| UDP transport | `udp_drain()` |
| `data.ptr == NULL` (listener) | `accept_ready()` with the `remember()` callback |
| Edge-triggered mode | `handle_edge()` |
| Level-triggered with direct send | `handle_direct()` |
| Level-triggered (default) | `handle_level()` |

## 5. TCP Implementation

### Connection acceptance

When the listener is ready, the engine calls the framework's `accept_ready()`, passing the `remember()` callback.

For each newly accepted connection, `remember()` registers the socket with epoll using `EPOLL_CTL_ADD`:

- Level-triggered modes register `EPOLLIN` (every connection starts by reading).
- Edge-triggered mode registers `EPOLLIN | EPOLLOUT | EPOLLET` once, for the lifetime of the connection.

If registration fails, the connection is closed through `conn_close()`.

### Removing connections

`drop()` removes a connection with `EPOLL_CTL_DEL` and then calls `conn_close()` with the error or closure code. It is used by every handler whenever `conn_step()` reports a closure or error, or when an `epoll_ctl()` call fails.

### Level-triggered mode (`handle_level()`)

Each reported event performs exactly one `conn_step()`.

- If the step fails or the peer closes, the connection is dropped.
- If the connection switched between reading and writing, the interest set is updated with `EPOLL_CTL_MOD` (`EPOLLIN` when reading, `EPOLLOUT` when sending).

This costs one `epoll_ctl()` call per direction change, i.e. roughly two per request/response exchange.

### Level-triggered mode with direct send (`handle_direct()`)

This mode removes most of the `epoll_ctl()` calls.

- After a step, if the request is complete and the connection is not already waiting for writability (`c->interest_out` is clear), the reply is sent immediately with a second `conn_step()`.
- A small reply normally fits in the socket send buffer, so the interest set stays at `EPOLLIN`.
- Only when a send would block does the engine switch to `EPOLLOUT` with `EPOLL_CTL_MOD`, recording this in `c->interest_out`. It switches back to `EPOLLIN` once the reply has been fully sent.

### Edge-triggered mode (`handle_edge()`)

An edge is reported only when readiness changes, so the handler must make progress until the kernel says it cannot.

- `conn_step()` is called in a loop with a `would_block` flag.
- The loop exits when `would_block` is set; the next edge brings the connection back.
- On error or closure, the connection is dropped.

Because the socket is registered once for both directions, there are no `EPOLL_CTL_MOD` calls after registration.

## 6. UDP Implementation

UDP uses a single socket (the listener), so every event is dispatched to the framework's `udp_drain()`.

- **Level-triggered:** `udp_drain()` is called once per reported event.
- **Edge-triggered:** `udp_drain()` is called repeatedly until it returns a non-zero value or a stop is requested, so no datagrams are left stranded after an edge.
- A negative return value is treated as a fatal error and ends the loop.

Datagram parsing, application handling and replies are handled by the framework.

## 7. Shutdown and Resource Management

- The loop exits when `stop_requested` is set. Because `epoll_wait()` uses a timeout, the flag is observed within one `WAIT_MS` period even when the server is idle.
- Fatal errors from `epoll_wait()`, `accept_ready()` or `udp_drain()` jump to a common exit path and are returned as the result.
- The epoll file descriptor is closed on exit. Unlike an asynchronous engine, epoll has no outstanding kernel-side operations, so no drain phase is required.

## 8. Performance-Related Features

- **Event batching:** Up to 256 ready events are handled per `epoll_wait()` call.
- **Scalable readiness tracking:** The kernel returns only ready descriptors, so cost does not grow with the number of idle connections (unlike `select`/`poll`).
- **Reduced `epoll_ctl()` traffic:**
  - `lt-direct` avoids interest-set changes unless a send would block.
  - `et` registers each socket once for both directions.
- **Edge-triggered draining:** Handles a connection until `EAGAIN`, avoiding repeated wake-ups for the same connection.

## 9. Statistics and Instrumentation

The engine contributes to the framework's statistics and syscall counters:

| Counter | Meaning |
|---|---|
| `loop_calls` | Event-loop iterations |
| `sc_wait` | `epoll_wait()` calls |
| `sc_ctl` | `epoll_ctl()` calls (ADD, MOD and DEL) |

The framework additionally tracks bytes read and written, and accepted, closed and rejected connections, and uses all of these for reporting and performance comparison across engines.

## 10. Dependencies and Integration

The implementation requires:

- Linux with `epoll` support (introduced in Linux 2.6).
- GCC or a compatible C compiler (`_GNU_SOURCE` is defined).
- The project's shared `framework.h` and `framework.c` components.

No additional libraries are required.

The epoll engine is compiled and linked as part of the project's shared build system. From the integrated repository:

```bash
cd code/benchmark
make
```

Relevant epoll options:

| Option | Description |
|---|---|
| `--epoll-mode lt` | Level-triggered (default) |
| `--epoll-mode et` | Edge-triggered |
| (direct-send option) | Enables `lt-direct`, i.e. `config->epoll_direct` |

The engine is designed to operate within the common framework so that the same application plugins and protocol handling can be used across the different network I/O implementations.

---
