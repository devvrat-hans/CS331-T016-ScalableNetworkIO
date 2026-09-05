# Select Echo Server

**Author:** Parth Kale (Roll No: 24110242)
**Course:** CS331 - Scalable Network I/O (Team T016)

Single-threaded TCP echo server using `select()` for I/O multiplexing.

## Build

```bash
make
```

## Run

```bash
./select_echo_server          # listens on 0.0.0.0:9090
./select_echo_server 9091     # optional port override
```

## Testing

Connect using `nc` (netcat):

```bash
nc 127.0.0.1 9090
```

Type any message and press Enter; the server will echo it back. Send `SIGINT`
(Ctrl-C) or `SIGTERM` to shut down and print statistics.

## How It Works

- Uses `select()` to monitor multiple sockets concurrently in a single thread.
- Listening and connected client sockets are configured non-blocking (`O_NONBLOCK`).
- Client state is indexed **directly by file descriptor**. `select()` reports
  readiness as a bitmap over fd values and never says *which* descriptors are
  ready, so there is no compact slot array to maintain and no compaction step.
- Each loop iteration makes **three linear passes**:
  1. rebuild both `fd_set`s from local state — userspace, `O(max_fd)`
  2. `select()` itself — kernel, `O(max_fd)`
  3. `FD_ISSET` scan to discover which descriptors are ready — userspace, `O(max_fd)`
- Read and write readiness are **mutually exclusive per connection**: a
  connection sits in the read set while its buffer is empty and in the write set
  while it has bytes pending. It is never in both. This yields backpressure for
  free and makes it structurally impossible to overwrite unsent data.
- New connections are drained with a non-blocking `accept()` loop.
- When the highest-numbered descriptor closes, `max_fd` is recomputed by
  scanning downward, so `select()` stops paying for a dead range.

## Limitations

- **Hard ceiling at `FD_SETSIZE` (1024 with glibc).** `FD_SET()` on a descriptor
  at or above that limit writes past the end of `fd_set`, so `accept()` rejects
  such descriptors explicitly. Because this tests the fd *value* rather than a
  connection count, and fd numbers fragment as connections churn, the wall can
  be hit with well under 1024 connections actually open. In practice the server
  tops out at 1020 concurrent clients: descriptors 0-2 are stdio and 3 is the
  listener.
- **Cost scales with the highest live fd number, not the connection count.** Ten
  connections, one of which happens to be descriptor 900, costs roughly 901
  slots of work per iteration. `poll()` would cost 11.
- Level-triggered only, and one `read()` per readiness event — anything left in
  the socket buffer re-notifies on the next iteration.
- The `timeval` passed to `select()` is modified by Linux and must be reset every
  iteration; `poll()` takes a plain `int` and has no equivalent hazard.

## Statistics Output

On shutdown the server prints a human-readable block followed by **two lines of
CSV** (header plus one row) for the benchmark harness:

```
engine,port,select_calls,read_calls,write_calls,accept_calls,messages,
accepts_total,rejected_fd_too_high,maxfd_recomputes,bytes_read,bytes_written,
peak_max_fd,fdset_bytes_per_call,syscalls_per_msg
```

`syscalls_per_msg` is the headline metric for the O(N) versus O(1) comparison.
`fdset_bytes_per_call` is the bitmap volume crossing the syscall boundary on
every `select()` — two sets copied in and back out — which grows with
`peak_max_fd`: 8 bytes at `max_fd` 9, 512 bytes at `max_fd` 1023.
