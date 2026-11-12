# Select Workload Server

**Author:** Parth Kale (Roll No: 24110242)
**Course:** CS331 - Scalable Network I/O (Team T016)

Standalone single-threaded TCP server using `select()` for I/O multiplexing. It serves three workloads (small echo, verified upload, verified download) over one length-prefixed protocol, and prints a JSON summary of its statistics on exit.

## Build

```bash
gcc -O2 -Wall -Wextra -o select_server select_server.c
```

No project header or other C file is needed.

## Run

```bash
./select_server [--port 9090] [--bind 127.0.0.1] [--app echo|upload|download] [--max-clients 1024]
```

| Option | Meaning |
|---|---|
| `--port` | TCP port to listen on (default `9090`). Use `0` to let the kernel choose a free port. |
| `--bind` | Numeric IPv4 address to bind to (default `127.0.0.1`). |
| `--app` | Workload served by this process: `echo`, `upload` or `download`. |
| `--max-clients` | Maximum number of simultaneous clients (default `1024`). |

The server prints `READY <port>` once it is accepting connections. Press `Ctrl+C` (or send `SIGTERM`) to stop it; it then closes all clients and flushes a single JSON line of statistics.

To serve more than about 1,000 clients, raise the open-file limit in the shell that starts the server first (for example `ulimit -n 65536`). Otherwise `accept()` fails with `EMFILE` before `select`'s own limit is reached.

## Testing

`nc` cannot send the binary length header, so use a short Python client. Every request starts with an 8-byte big-endian payload length.

```python
import socket, struct

def connect(port=9090):
    return socket.create_connection(("127.0.0.1", port))

def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError("closed early")
        buf += chunk
    return buf

pattern = lambda n, off=0: bytes((off + i) % 251 for i in range(n))

# echo     (start server with --app echo)
s = connect(); s.sendall(struct.pack(">Q", 5) + b"hello")
print(recv_exact(s, 5))

# upload   (start server with --app upload)
s = connect(); s.sendall(struct.pack(">Q", 10000) + pattern(10000))
print(recv_exact(s, 1))            # b'K' means every byte was verified

# download (start server with --app download)
s = connect(); s.sendall(struct.pack(">Q", 10000))
assert recv_exact(s, 10000) == pattern(10000)
```

A wrong byte in an upload, or an oversized header, makes the server close that connection and count it under `errors` in the JSON output.

## How It Works

- **Multiplexing:** uses `select()` to monitor multiple file descriptors concurrently in a single thread, across the read and write sets (`fd_set`).
- **Socket setup:** the listening socket and accepted client descriptors are set to non-blocking mode via `fcntl()`, and `TCP_NODELAY` is enabled on each active client connection.
- **Protocol:** each request begins with an 8-byte big-endian length (non-zero, at most 64 MiB, and at most 4096 bytes for echo).
  - **Echo** receives the payload and sends it back.
  - **Upload** receives the payload in 4096-byte chunks, checks that every byte equals `(offset + i) % 251`, and replies with a single `'K'` only if all bytes matched.
  - **Download** sends `length` bytes of the same pattern.
  - After each request, the connection resets to the header-parsing stage and remains reusable.
- **State machine:** each client is managed through a small state machine (`HEADER`, `RECEIVE`, `SEND`, `ACK`).
- **Bitmaps rebuilt every iteration:** the read and write sets (`rfds`, `wfds`) are rebuilt on every iteration of the event loop. A descriptor is registered in `rfds` while reading and in `wfds` while sending.
- **Direct lookup:** connections are indexed by their file descriptor number in a lookup array (`clients[fd]`), matching the bitmap indexing of `select()`.
- **Safe fd reuse:** existing clients are processed before new connections are accepted. This prevents a descriptor number closed during the iteration from being reused by a newly accepted connection while its readiness bit from the previous `select()` call is still set.
- **Dynamic maximum fd:** the highest descriptor in use (`highest` / `max_fd`) is tracked and scanned downwards when connections close, which reduces scanning cost when high-numbered descriptors disconnect.
- **Timeout and signals:** `select()` uses a `WAIT_MS` timeout, reinitialized as a `struct timeval` on every iteration, so `SIGINT` and `SIGTERM` stop the loop promptly. `SIGPIPE` is ignored globally.
- **Shutdown summary:** a single JSON line is printed on exit with the same metrics as the other engines: accepted, closed, peak connections, rejected, errors, completed messages, bytes read and written, `select()` call counts, user and system CPU time, elapsed time, and peak memory usage (`VmHWM` from `/proc/self/status`).

## Limitations

- **Hard ceiling (`FD_SETSIZE`):** `select()` uses fixed-size bitmasks (`fd_set`), which glibc fixes at 1024 descriptors. A connection whose descriptor is `>= FD_SETSIZE` is accepted and then immediately closed, and counted under `rejected`. In practice this allows about 1,020 simultaneous clients, because descriptors 0-2 are stdin/stdout/stderr and one is the listening socket.
- **`O(N)` overhead per call:** the kernel must inspect every file descriptor from `0` up to `highest` on every call, and user space must repopulate both `fd_set` bitmasks and iterate over them (`O(max_fd)`), regardless of how few sockets are actually ready.
- **Bitmap copying:** passing the bitmasks across the user/kernel boundary on every call costs a memory copy proportional to `highest`.
- **Level-triggered only:** `select()` reports readiness again on every call for as long as data remains in a socket's receive buffer or space is available in its send buffer.
- **Scope:** IPv4 only, one workload per server process (chosen with `--app`), and `--bind` requires a numeric IP address.
