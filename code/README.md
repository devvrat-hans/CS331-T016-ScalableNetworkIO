# io_uring Echo Server #

**Author:** Niraj Kumar (24110222) 

**Course:** CS331 - Scalable Network I/O (Team T016)

Single-threaded TCP echo server implemented using Linux `io_uring` for asynchronous network I/O.

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

The server listens on 0.0.0.0:9090 by default.

## Testing

Use nc (netcat) as a client:

```bash
nc 127.0.0.1 9090
```

Type a message and press Enter. The server receives the message and sends the same data back to the client.
Example:
```
Hello io_uring
Hello io_uring
```

Multiple clients can connect to the server simultaneously.

## How It Works
The server uses `io_uring` to submit network I/O operations to the Linux kernel and process their results through completion queue entries (CQEs).

The main flow for each client is:
```
ACCEPT
   ↓
RECEIVE
   ↓
SEND
   ↓
RECEIVE
   ↓
SEND
   ↓
   ...
```

* **ACCEPT**: Waits for a new TCP client connection and obtains a separate socket file descriptor for that client.
* **RECEIVE**: Submits an asynchronous receive operation to obtain data sent by a connected client.
* **SEND**: Submits an asynchronous send operation to echo the received data back to the client.

* After sending the complete message, the server submits another receive operation for that client.

* The listening socket continuously has another ACCEPT operation submitted so that new clients can connect while existing clients are being served.

* Completion information is retrieved from CQEs, and user_data is used to identify which operation and client a completion belongs to.

* Partial sends are handled by keeping track of the number of bytes already transmitted and submitting another send operation when necessary.

* A client is closed when it disconnects or when an I/O operation reports an error.

## io_uring Model ##
The implementation follows the submission/completion model:
```
          Server
             │
             │ Submit SQE
             ▼
     ┌─────────────────┐
     │ Submission Queue│
     └────────┬────────┘
              │
              ▼
           Kernel
              │
              │ performs I/O
              ▼
     ┌─────────────────┐
     │ Completion Queue│
     └────────┬────────┘
              │
              │ CQE
              ▼
          Server
```
Instead of repeatedly checking every socket for readiness, the server submits the desired I/O operation and processes the completion when the kernel finishes it.

## Supported Operations ##
The server uses the following io_uring operations:

* `IORING_OP_ACCEPT` - accepts new TCP connections.
* `IORING_OP_RECV` - receives data from connected clients.
* `IORING_OP_SEND` - sends data back to clients.

## Design Considerations ##

* Uses a single server thread.

* Supports multiple connected clients concurrently.

* Uses a submission queue and completion queue to communicate I/O requests and results with the kernel.

* Maintains per-client buffers so that data belonging to different connections is kept separate.

* Handles partial `send()` completions rather than assuming the entire buffer is transmitted in one operation.

* Ignores `SIGPIPE` so that a disconnected client does not unexpectedly terminate the server.

## Limitations ##

* This implementation is intended as a baseline for comparing `io_uring` with `select`, `poll`, and `epoll`.

* The current implementation submits and processes operations individually; batching multiple SQEs can be explored as an optimization.

* It is a single-threaded implementation and does not use multiple worker threads.

* Performance characteristics depend on the Linux kernel, `liburing` version, workload, and system configuration.
