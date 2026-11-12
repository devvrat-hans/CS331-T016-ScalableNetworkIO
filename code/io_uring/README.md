# io_uring Network I/O Engine

## 1. Overview

This component implements the `io_uring`-based network I/O engine for the CS331 project, **Scalable Network I/O: From select to io_uring**.

The implementation uses Linux's `io_uring` interface through **liburing** to perform asynchronous network operations. It supports both TCP and UDP transports and integrates with the project's shared networking framework.

The engine supports three operating modes:

- **Plain io_uring:** Standard submission and completion processing.
- **SQPOLL:** Uses a kernel thread to poll the Submission Queue.
- **SQPOLL with busy polling:** Combines SQPOLL with userspace polling of the Completion Queue.

## 2. Implementation Structure

The implementation is contained in:

```text
code/io_uring/io_uring_server.c
```

The engine follows the shared project architecture:

- **Engine:** Handles asynchronous socket operations and completion processing.
- **Framework:** Manages connection state, request processing, application callbacks, statistics and shutdown.
- **Application plugins:** Define the request and response behavior independently of the I/O engine.

The engine implements the framework interface:

```c
int engine_run(server_t *s);
```

Each server worker initializes and manages its own io_uring instance.

## 3. Ring Initialization and Configuration

The engine uses the following configuration constants:

| Parameter | Value | Purpose |
|---|---:|---|
| `RING_DEPTH` | 256 | Submission Queue depth |
| `SQPOLL_IDLE_MS` | 100 ms | SQPOLL thread idle timeout |
| `WAIT_MS` | 100 ms | Completion wait timeout |

Ring initialization is performed by `ring_setup()` using `io_uring_queue_init_params()`.

The implementation configures:

- `IORING_SETUP_CQSIZE` to explicitly configure the Completion Queue size.
- `IORING_SETUP_SQPOLL` when SQPOLL is enabled.
- `IORING_SETUP_SQ_AFF` when CPU affinity for the SQPOLL thread is specified.

The Completion Queue capacity is calculated dynamically by `completion_entries()`, based on the configured maximum number of clients, UDP receive depth and additional capacity for control operations.

The size is selected as a power of two, starting at twice the Submission Queue depth, with a maximum of 65,536 entries.

## 4. Submission and Completion Management

The implementation uses the liburing interface to prepare, submit and process asynchronous operations.

### Submission

`counted_submit()` handles submission of prepared SQEs using `io_uring_submit()`.

It also tracks kernel-entry counts and handles the SQPOLL wake-up condition using `IORING_SQ_NEED_WAKEUP`.

### Completion

`counted_wait()` checks for available completions using `io_uring_peek_cqe()` before waiting.

It supports:

- Immediate completion retrieval without an additional wait syscall.
- Timed completion waiting using `io_uring_wait_cqe_timeout()`.
- Busy polling when SQPOLL busy mode is enabled.

The engine uses `io_uring_cqe_get_data()` to retrieve the operation's associated `user_data` and `io_uring_cqe_seen()` to mark processed completions as consumed.

The `outstanding` counter tracks operations awaiting completion processing.

## 5. TCP Implementation

TCP handling is implemented in `run_tcp()`.

### Connection acceptance

The engine maintains at most one outstanding asynchronous accept operation at a time.

- Accept requests are prepared using `io_uring_prep_accept()`.
- The listening socket is used to accept incoming connections.
- Accepted sockets are configured with `SOCK_NONBLOCK` and `SOCK_CLOEXEC`.
- `TCP_NODELAY` is enabled on successfully accepted connections.
- New connections are initialized through the framework's `conn_add()` function.

### Ready queue

The implementation maintains an application-side FIFO ready queue using `head`, `tail` and the `ready_next` field of `conn_t`.

The queue stores connections that need their next I/O operation submitted.

This queue is separate from the kernel's Submission Queue.

### Receive and send operations

For each queued connection, the engine prepares one operation according to its current framework state:

- `io_uring_prep_recv()` for connections in the HEADER or RECEIVE stage.
- `io_uring_prep_send()` for connections in the SEND stage.

The engine uses the framework's `conn_buffer()`, `conn_length()` and `conn_is_reading()` functions to obtain the appropriate buffer, remaining length and operation type.

`MSG_NOSIGNAL` is used for send operations.

Each SQE stores the corresponding `conn_t` pointer in `user_data`. The connection's `pending` flag is set while its operation is outstanding.

### Completion processing

The engine processes completed operations from the Completion Queue.

- Accept completions create and enqueue new connections.
- Receive and send completions are passed to the framework's `conn_complete()` function.
- Connections that remain valid are re-enqueued for their next operation.
- Connections that complete with a closure or error are handled through `conn_close()`.

The framework maintains the request state machine, including partial reads, partial writes, request headers and response progress.

### Batching and queue-stall handling

The engine prepares operations for multiple ready connections before submitting them.

If the Submission Queue fills while connections remain in the application ready queue, the engine uses `io_uring_peek_cqe()` instead of blocking for a completion.

This allows it to process available completions and return to submitting the remaining queued operations, avoiding a stall caused by waiting while unscheduled work remains.

Completion processing is also bounded to `RING_DEPTH` entries per iteration.

## 6. UDP Implementation

UDP handling is implemented through `run_udp()` and two helper functions:

- `post_udp_recv()`
- `post_udp_send()`

### UDP slots

The engine allocates a configurable number of `udp_slot_t` structures based on `udp_depth`.

Each slot contains:

- Sender address information.
- An `iovec` and `msghdr` for message operations.
- An input buffer.
- An output buffer.
- A state flag indicating whether the slot is sending.

### Datagram processing

At startup, the engine posts `udp_depth` asynchronous receive operations on the UDP socket.

Each slot follows this lifecycle:

```text
RECVMSG
   |
   v
Process datagram using udp_handle()
   |
   v
SENDMSG
   |
   v
Post another RECVMSG
```

If no response is generated, the slot is directly reused for another receive.

The sender's address is stored during reception and used as the destination for the response.

The configurable UDP depth controls the number of receive operations maintained concurrently.

## 7. Shutdown and Resource Management

The engine uses `drain()` to handle outstanding operations during shutdown.

The shutdown procedure:

1. Prepares an asynchronous cancellation request using `io_uring_prep_cancel()`.
2. Submits cancellation requests.
3. Collects outstanding completion events.
4. Handles accepted sockets that complete during shutdown by closing them.
5. Updates TCP connection state for successful operations whose completions had not yet been processed.
6. Repeats cancellation after a timeout when necessary.

The `pending` state of TCP connections is cleared as their operations are accounted for.

The UDP slot array is freed only after draining outstanding operations.

Finally, `engine_run()` releases the ring using `io_uring_queue_exit()`.

## 8. Performance-Related Features

The implementation includes the following features relevant to performance:

- **Submission batching:** Multiple operations can be prepared and submitted together.
- **Completion batching:** Multiple CQEs are processed in one event-loop iteration.
- **Configurable Completion Queue size:** Reduces completion queue overflow under high connection counts.
- **SQPOLL:** Allows a kernel thread to consume submissions without requiring a normal submission syscall while it is active.
- **Busy polling:** Allows userspace to poll the Completion Queue instead of sleeping for completions.
- **UDP receive depth:** Allows the number of outstanding UDP receives to be configured.
- **CPU affinity:** Supports assigning the SQPOLL thread to a specified CPU.

## 9. Statistics and Instrumentation

The engine integrates with the framework's statistics and syscall counters.

It tracks or contributes to measurements including:

- Number of event-loop iterations.
- Bytes read and written.
- Accepted, closed and rejected connections.
- Dropped UDP datagrams.
- io_uring kernel-entry and completion-wait counts.

The `counted_submit()` and `counted_wait()` helpers account for relevant kernel entries in the io_uring execution path.

These counters are used by the shared framework for reporting and performance evaluation.

## 10. Dependencies and Integration

The implementation requires:

- Linux with io_uring support (introduced in Linux 5.1).
- GCC or a compatible C compiler.
- liburing development headers and library.
- The project's shared `framework.h` and `framework.c` components.

The io_uring engine is compiled and linked as part of the project's shared build system.

From the integrated repository, the project build command is:

```bash
cd code/benchmark
make
```

The shared framework provides the runtime configuration, including transport selection, SQPOLL options, UDP receive depth, thread configuration and other server settings.

Relevant io_uring options include:

| Option | Description |
|---|---|
| `--sqpoll 1` | Enable SQPOLL mode |
| `--sqpoll-busy 1` | Enable busy polling with SQPOLL |
| `--udp-depth N` | Configure the number of outstanding UDP receive slots |

The engine is designed to operate within the common framework so that the same application plugins and protocol handling can be used across the different network I/O implementations.

---