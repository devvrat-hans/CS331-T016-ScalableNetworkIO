# Poll Echo Server

**Author:** Devvrat Hans (Roll No: 23110094)  
**Course:** CS331 - Scalable Network I/O (Team T016)

Single-threaded TCP echo server using `poll()` for I/O multiplexing.

## Build
```bash
make
```

## Run
```bash
./poll_echo_server
```
The server listens on `0.0.0.0:9090` by default.

## Testing
Connect using `nc` (netcat):
```bash
nc 127.0.0.1 9090
```
Type any message and press Enter; the server will echo it back.

## How It Works
- Uses `poll()` to monitor multiple sockets concurrently in a single thread.
- Listening and connected client sockets are configured in non-blocking mode (`O_NONBLOCK`).
- Separates readiness: only registers `POLLOUT` when there is pending buffer data to send back.
- When new connections arrive, an `accept()` loop drains all pending incoming handshakes.
- When a client disconnects, its slot in the `fds` array is compacted by swapping in the last active descriptor in $O(1)$ time.

## Limitations
- Linear scanning overhead: $O(N)$ system call cost per event check where $N$ is total monitored descriptors.
- Level-triggered mechanism only (re-notifies on every poll cycle if data is unread).
- Bounded to `MAX_FDS` (1024 descriptors).

