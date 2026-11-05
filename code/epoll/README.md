# Epoll Workload Server

**Author:** Patil Nachiket Kiran (Roll No: 24110250)  
**Course:** CS331 - Scalable Network I/O (Team T016)

Standalone single-threaded TCP server using `epoll()` for I/O multiplexing. It serves three workloads (small echo, verified upload, verified download) over one length-prefixed protocol, and prints a JSON summary of its statistics on exit.

## Build
```bash
gcc -O2 -Wall -Wextra -o epoll_server epoll_server_v2.c
```
No project header or other C file is needed.

## Run
```bash
./epoll_server [--port 9090] [--bind 127.0.0.1] [--app echo|upload|download] [--max-clients 10000]
```
The server listens on `127.0.0.1:9090` by default and prints `READY <port>` once it is accepting connections. Use `--port 0` to let the kernel choose a free port. Press `Ctrl+C` (or send `SIGTERM`) to stop it; it then closes all clients and prints one JSON line of statistics.

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
A wrong byte in an upload, or an oversized header, makes the server close that connection and count it under `errors` in the JSON line.

## How It Works
- Uses `epoll` (level-triggered) to monitor multiple sockets concurrently in a single thread.
- The listening socket and accepted client sockets are non-blocking (`SOCK_NONBLOCK` via `accept4()`), and `TCP_NODELAY` is set on each client.
- Each request begins with an 8-byte big-endian length (non-zero, at most 64 MiB, and at most 4096 bytes for echo). **Echo** receives the payload and sends it back. **Upload** receives it in 4096-byte chunks, checks every byte equals `(offset + i) % 251`, and replies with a single `'K'` only if all bytes matched. **Download** sends `length` bytes of the same pattern. After each request the connection goes back to the header stage and can be reused.
- Each client is a small state machine (`HEADER`, `RECEIVE`, `SEND`, `ACK`). One `recv()` or `send()` is done per event, and partial reads or writes simply continue on the next event.
- Interest is `EPOLLIN` while a client is reading and `EPOLLOUT` while it is sending; `EPOLL_CTL_MOD` is called only when the direction changes, not on every event.
- When new connections arrive, an `accept4()` loop drains up to 64 pending handshakes per wakeup, so a burst of new connections can't starve already-connected clients within the same `epoll_wait` cycle.
- Per-client state (`client_t`) is heap-allocated; `epoll_event.data.ptr` points directly at it, and `NULL` identifies the listener, so there is no separate fd-to-state lookup table to keep in sync.
- A connection is removed via `EPOLL_CTL_DEL` before its fd is closed and its `client_t` freed.
- A small intrusive doubly linked list of live clients is kept solely so shutdown can walk and close every still-open connection cleanly; it is never scanned for I/O.
- `epoll_wait` uses a 100 ms timeout, so the stop flag set by `SIGINT`/`SIGTERM` is noticed promptly. `SIGPIPE` is avoided with `MSG_NOSIGNAL` on every `send()`.
- On shutdown one JSON line is printed with: accepted, closed, peak, rejected, errors and incomplete connections, completed messages, bytes read and written on the wire, `epoll_wait` call count, elapsed time, user and system CPU time, and peak memory (`VmHWM` from `/proc/self/status`). The same fields are reported by the other engines so results can be compared.

## Limitations
- `epoll_wait` returns only the ready descriptors, so per-call cost is proportional to the number of *ready* fds, not the number watched; registering/deregistering a fd (`epoll_ctl`) is still a per-fd operation, paid at connect/disconnect time and whenever a client switches between reading and sending.
- Level-triggered mechanism only (re-notifies on every `epoll_wait` cycle if data is unread); an edge-triggered (`EPOLLET`) variant is a possible extension but requires draining each fd to `EAGAIN` on every notification.
- Only one `recv()` or `send()` per event, so large transfers take more `epoll_wait` cycles; this keeps the behavior identical across engines.
- Any accept error other than `EAGAIN`/`EINTR` (including `EMFILE`) stops the event loop and the summary reports `server_ok:false`; there is no reserved-fd workaround.
- One workload per server process (chosen with `--app`), IPv4 only, and `--bind` must be a numeric address.
- Linux-specific (`epoll` is not portable to other POSIX systems, unlike `select`/`poll`).
- The connection ceiling is `--max-clients` (at most 100000; extra connections are closed and counted as `rejected`) and the system open file descriptor limit (`ulimit -n`).
