# io_uring Echo Server

**Author:** Niraj Kumar (24110222)

**Course:** CS331 - Scalable Network I/O (Team T016)

Single-threaded TCP echo server implemented using Linux `io_uring` for asynchronous network I/O.

The server uses the `io_uring` submission/completion model and maintains independent state and buffers for each connected client. It uses a **multishot accept operation**, batched completion processing, and handles partial sends.

## Build

```bash
make
```

or compile directly:

```bash
gcc io_uring_echo_server.c -o io_uring_echo_server -luring
```

## Run

```bash
./io_uring_echo_server
```

The server listens on `0.0.0.0:9090`.

## Testing

Use `nc` (netcat) as a client:

```bash
nc 127.0.0.1 9090
```

Type a message and press Enter. The server receives the message and sends the same data back to the client.

Example:

```text
Hello io_uring
Hello io_uring
```

Multiple clients can connect to the server simultaneously.

## How It Works

The server uses `io_uring` to submit asynchronous I/O operations to the Linux kernel and process their results through Completion Queue Entries (CQEs).

The main flow for each connected client is:

```text
        ACCEPT
          │
          ▼
        RECV
          │
          ▼
        SEND
          │
          ▼
        RECV
          │
          ▼
        SEND
          │
          ▼
         ...
```

### ACCEPT

The listening socket uses `IORING_OP_ACCEPT` with a **multishot accept** operation.

A single multishot accept request can generate multiple CQEs as new clients connect, so the server does not need to submit a new accept request after every successful connection.

The server checks the `IORING_CQE_F_MORE` flag to determine whether the multishot accept operation is still active. A new accept operation is submitted only when the existing multishot operation terminates.

### RECEIVE

When a client connects, the server creates a per-client structure containing:

* Client socket file descriptor
* Receive/send buffer
* Send position
* Number of bytes pending transmission
* Current operation state

The server submits an asynchronous `IORING_OP_RECV` operation for the client.

When the receive completes, the CQE result indicates the number of bytes received.

### SEND

After receiving data, the server submits an `IORING_OP_SEND` operation using the client's buffer.

The received data is sent back to the same client, implementing the echo functionality.

The implementation handles **partial sends**. If only part of the buffer is transmitted, the server keeps track of the number of bytes already sent and submits another send operation for the remaining data.

After the complete response has been transmitted, another receive operation is submitted for that client.

### Completion Processing

The server waits for completions using:

```c
io_uring_submit_and_wait()
```

After completions become available, the server processes all pending CQEs in a batch rather than handling only one completion at a time.

The `user_data` field of each SQE stores a pointer to the relevant client structure (or the accept marker). This allows the server to determine which client and operation a CQE belongs to.

## io_uring Model

The implementation follows the submission/completion model:

```text
             Server
                │
                │ Prepare SQE
                ▼
       ┌──────────────────┐
       │  Submission Queue │
       └────────┬─────────┘
                │
                │ submit
                ▼
             Kernel
                │
                │ performs I/O
                ▼
       ┌──────────────────┐
       │  Completion Queue │
       └────────┬─────────┘
                │
                │ CQE
                ▼
             Server
                │
                │ process result
                ▼
          Next operation
```

The server does not repeatedly scan all connected sockets to determine whether they are ready.

Instead, it submits the required I/O operation and waits for the kernel to report completion through the completion queue.

For example:

```text
Server
  │
  │ Submit RECV SQE
  ▼
Kernel
  │
  │ waits for client data
  │
  │ data arrives
  ▼
Kernel completes RECV
  │
  │ CQE
  ▼
Server
  │
  │ Submit SEND SQE
  ▼
Kernel
```

## Client Operation State

Each connected client maintains its own state.

At most one client I/O operation is outstanding at a time:

```text
RECV
  │
  │ completion
  ▼
SEND
  │
  │ completion
  ▼
RECV
  │
  ▼
...
```

This simplifies buffer and client lifetime management because the client's buffer remains associated with the operation until its CQE is processed.

## Multishot Accept

The server uses:

```c
io_uring_prep_multishot_accept()
```

instead of repeatedly submitting a normal accept operation.

Conceptually:

```text
          One ACCEPT SQE
                │
                ▼
             Kernel
          ┌─────┼─────┐
          │     │     │
          ▼     ▼     ▼
       Client  Client  Client
          1      2      3
          │      │      │
          ▼      ▼      ▼
        CQE    CQE    CQE
```

As long as `IORING_CQE_F_MORE` is present, the multishot accept operation remains active and can produce additional completions.

This reduces the need to repeatedly prepare and submit accept requests when many clients connect.

## Batching

The event loop uses:

```c
io_uring_submit_and_wait(&ring, 1);
```

to submit pending SQEs and wait for at least one completion.

Once completions are available, the server processes all pending CQEs in the completion queue before advancing the queue.

Conceptually:

```text
        Submit / Wait
             │
             ▼
       ┌─────────────┐
       │ CQE 1       │
       │ CQE 2       │
       │ CQE 3       │
       │ CQE 4       │
       └─────────────┘
             │
             ▼
       Process all
       completions
```

This allows multiple completed operations to be handled together instead of waiting separately for every CQE.

## Supported Operations

The server uses the following `io_uring` operations:

* `IORING_OP_ACCEPT` - accepts new TCP connections using a multishot accept operation.
* `IORING_OP_RECV` - receives data from connected clients.
* `IORING_OP_SEND` - sends data back to clients.

## Error and Disconnect Handling

The server checks the result of every CQE.

For receive operations:

* A positive result indicates the number of bytes received.
* `0` indicates that the client has closed the connection.
* A negative result indicates an I/O error.

For send operations:

* A positive result indicates the number of bytes successfully transmitted.
* Negative results indicate an I/O error.

When a client disconnects or an unrecoverable I/O error occurs, its socket is closed and its associated client structure is released.

The server also ignores `SIGPIPE` so that attempting to communicate with a disconnected client does not unexpectedly terminate the server.

## Graceful Shutdown

The server handles `SIGINT` and `SIGTERM`.

When the server receives a termination signal, it stops accepting new work and exits the event loop cleanly.

During shutdown, it:

1. Closes connected client sockets.
2. Frees client structures.
3. Destroys the `io_uring` instance.
4. Closes the listening socket.
5. Prints server statistics.

This allows the server to be terminated using:

```text
Ctrl+C
```

without abruptly terminating the process.

## Statistics

The server maintains runtime statistics including:

* Number of client connections
* Number of receive operations
* Number of send operations
* Total bytes received
* Total bytes sent
* Number of event-loop submit/wait calls
* Total CQEs processed

These statistics can be used to understand the server's workload and support performance analysis against other I/O mechanisms.

## Design Considerations

* Uses a **single server thread**.
* Supports multiple connected clients concurrently.
* Uses the `io_uring` submission queue and completion queue for asynchronous I/O.
* Uses **multishot accept** to handle multiple incoming connections from a single accept request.
* Processes multiple CQEs in a batch.
* Maintains a separate buffer and state for each client.
* Allows only one outstanding client I/O operation at a time, simplifying lifetime management.
* Handles partial `send()` completions instead of assuming the entire buffer is transmitted in one operation.
* Ignores `SIGPIPE`.
* Handles graceful shutdown through `SIGINT` and `SIGTERM`.
* Uses a queue depth of `1024` to provide sufficient capacity for multiple outstanding I/O requests.
* The implementation does not explicitly configure sockets with `O_NONBLOCK`; asynchronous I/O is submitted through `io_uring`.

## Limitations

* This implementation is intended as a baseline for comparing `io_uring` with `select`, `poll`, and `epoll`.
* It is a single-threaded implementation and does not use multiple worker threads.
* The current server uses a fixed listening port of `9090`.
* Performance characteristics depend on the Linux kernel, `liburing` version, workload, and system configuration.
* The measured number of submit/wait calls is an application-level statistic and should not be interpreted as an exact count of every underlying `io_uring_enter` system call.
