# Epoll Echo Server

**Author:** Patil Nachiket Kiran (Roll No: 24110250)  
**Course:** CS331 - Scalable Network I/O (Team T016)

Single-threaded TCP echo server using `epoll()` for I/O multiplexing.

## Build
```bash
make
```

## Run
```bash
./epoll_echo_server
```
The server listens on `0.0.0.0:9090` by default.

## Testing
Connect using `nc` (netcat):
```bash
nc 127.0.0.1 9090
```
Type any message and press Enter; the server will echo it back.

## How It Works
- Uses `epoll` (level-triggered) to monitor multiple sockets concurrently in a single thread.
- Listening and connected client sockets are configured in non-blocking mode (`O_NONBLOCK`).
- Registers interest once per connection via `epoll_ctl`, instead of rebuilding a watch set every loop; only registers `EPOLLOUT` when there is pending buffer data to send back.
- When new connections arrive, an `accept()` loop drains all pending incoming handshakes.
- Per-connection state (`conn_t`) is heap-allocated; each `epoll_event.data.ptr` points directly at its owning connection, so there is no separate fd-to-state lookup table to keep in sync.
- A connection is removed via `EPOLL_CTL_DEL` before its fd is closed and its `conn_t` freed — no array compaction is needed, since epoll maintains the watch set internally rather than in a flat array the program has to manage.
- A small intrusive linked list of live connections is kept solely so shutdown can walk and close every still-open connection cleanly; epoll itself does not expose "everything currently registered."

## Limitations
- `epoll_wait` returns only the ready descriptors, so per-call cost is proportional to the number of *ready* fds, not the number watched; registering/deregistering a fd (`epoll_ctl`) is still a per-fd operation, just paid once at connect/disconnect time rather than every loop.
- Level-triggered mechanism only (re-notifies on every `epoll_wait` cycle if data is unread); an edge-triggered (`EPOLLET`) variant is a possible extension but requires draining each fd to `EAGAIN` on every notification.
- Linux-specific (`epoll` is not portable to other POSIX systems, unlike `select`/`poll`).
- No fixed connection cap analogous to `select`'s `FD_SETSIZE`; the practical ceiling is the system-wide open file descriptor limit (`ulimit -n`).