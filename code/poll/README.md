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
- Blocks in `poll()` with a timeout of `-1`, so the process is only scheduled when a descriptor is actually ready. There are no periodic timeout wakeups, which keeps the `poll()` count proportional to offered load instead of to how long the server was left running.
- Listening and connected client sockets are configured in non-blocking mode (`O_NONBLOCK`).
- Separates readiness: only registers `POLLOUT` when there is pending buffer data to send back.
- When new connections arrive, an `accept()` loop drains all pending incoming handshakes.
- When a client disconnects, its slot in the `fds` array is compacted by swapping in the last active descriptor in $O(1)$ time.

## Shutdown Statistics
`SIGINT` or `SIGTERM` interrupts the blocking `poll()` call, the event loop exits, and the
server writes a machine-readable CSV pair to stdout:

```
STATS_HEADER,engine,poll_calls,accepts,reads,writes,messages,bytes
STATS,poll,484,200,4000,4000,4000,2048000
```

Extract just the values with `grep '^STATS,'`. Field meanings:

| Field | Meaning |
|-------|---------|
| `engine` | Always `poll`, so results from all four servers can be concatenated. |
| `poll_calls` | `poll()` syscalls issued over the lifetime of the process. |
| `accepts` | Connections accepted (successful `accept()` returns). |
| `reads` | `read()` syscalls issued, including the zero-byte read that detects a disconnect, `EAGAIN` returns, and `EINTR` retries. This is why `reads` typically equals `messages + accepts`. |
| `writes` | `write()` syscalls issued, including short and `EAGAIN` writes. |
| `messages` | Echo replies fully written back, i.e. completed request/response pairs. |
| `bytes` | Payload bytes echoed back to clients. |

`(poll_calls + reads + writes) / messages` gives the syscalls-per-message figure used in
the report's syscall profile, without needing `strace`. Because the benchmark client is
strict request/response, one request arrives as one buffer, so `messages` should equal the
`completed` count summed over the client workers. (A short read would split one request
across two buffers and count twice; at the default 64-byte payload this does not occur.)

The benchmark harness captures the line automatically: it redirects server stdout to
`server-run-<n>.log` and stops the server with `SIGTERM`.

These counters were cross-checked against `strace -f -c` on Linux 6.8 / glibc 2.36 and match
the kernel-level counts, with three explainable offsets: `strace` shows one extra `read`
(the dynamic loader), one extra `write` (stdio flushing this log at exit), and one extra
`accept` (the `EAGAIN` call that ends each accept loop). Note that glibc implements `poll()`
on top of the `ppoll` syscall, so the `strace -c` row to compare against `poll_calls` is
labelled **`ppoll`**, not `poll`.

## Limitations
- Linear scanning overhead: $O(N)$ system call cost per event check where $N$ is total monitored descriptors.
- Level-triggered mechanism only (re-notifies on every poll cycle if data is unread).
- Bounded to `MAX_FDS` (1024 descriptors).
- The echo reply is sent on the poll cycle *after* the one that reported `POLLIN`, because `POLLOUT` is only armed once buffered data exists. Compared with an engine that writes inside the same event cycle, this adds one extra poll cycle per message, amortized over however many descriptors are ready in that cycle. It is noted as a fairness caveat in the report.
- Each connection carries a single 4 KiB echo buffer, which assumes request/response clients. A client that pipelines a second request before the first reply has drained would have the buffered data replaced by the newer read.

