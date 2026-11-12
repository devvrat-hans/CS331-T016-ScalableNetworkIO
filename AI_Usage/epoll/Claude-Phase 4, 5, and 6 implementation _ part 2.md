# Phase 4, 5, and 6 implementation

## User:

> File: Claude-Epoll echo server evolution part 1.md

> File: epoll_echo_server_v1.c

> File: epoll_server.c

Added chat, then also added the files required
Move to phase 4 and 5 and 6 together

## Assistant:

I'll read the chat export first to see what phases 4, 5 and 6 are.

> Planning the measurement matrix and metrics for the experiments.
>
> - Locate phase references in the chat export
> - Read start of chat export
> - Read rest of chat export
> - Planning the measurement matrix and metrics for the experiments.
> - **Done**

Phase 4 starts with the diagram, so I'm loading the diagram tooling.

> Placing boxes and labels in the layered diagram.
>
> - Placing boxes and labels in the layered diagram.
> - **Done**

> [Interactive Widget: benchmark_framework_layers]

## Phase 4: Architecture

### Step 10: The layers
Each layer has one job and only talks to its neighbour.

- **Load generator:** a separate client program. It knows the wire protocol but nothing about which engine is on the other end.
- **App plugin:** defines what a request means (echo, upload, download).
- **Transport:** how bytes are accepted, read and written (TCP now).
- **I/O engine:** how the thread waits and gets told something can proceed.
- **Instrumentation:** observes all of this without changing behaviour.

Your professor's complaint was that the app and the testing were married. The server binary is the thing under test, and the load generator and the harness scripts sit outside it.

### Step 11: The plugin interface
Your `epoll_server.c` is already close to this. The app is a state machine that answers two questions:

1. **What do you want next?** `client_is_reading`, `client_buffer` and `client_length` together mean "read up to N bytes into this pointer" or "write N bytes from this pointer".
2. **Here is what happened.** `client_complete(c, result)` takes the byte count (or an error), advances the stage, and returns 0, 1 or an error.

That pair is the whole contract: `next_op(c) → {direction, ptr, len}` and `complete(c, result)`.

- **epoll, poll, select** (readiness engines) wait until the fd is ready in that direction. They run `recv` or `send` themselves, then call `complete`.
- **io_uring** (completion engine) submits the same `{direction, ptr, len}` as an SQE. When the CQE arrives, it calls `complete` with `cqe->res`.

The app never learns which model is driving it, and the engine never learns what the bytes mean. `ready_next` and `pending` in your struct are engine-owned fields. They belong in an engine wrapper, not in the app's `client_t`.

### Step 12: What to decouple in the current code

| Today | Problem | Fix |
|---|---|---|
| `accept_client()` sets `TCP_NODELAY` | transport detail inside shared helper | move into `transport_accept()` |
| `client_step()` calls `recv`/`send` directly | transport call inside the shared app section | move into `transport_read()` and `transport_write()` |
| `client_t` has `ready_next`, `pending` | engine fields in the app's struct | put them in an engine-side wrapper that contains the app client |
| `--app` branches inside `client_complete` | fine for now, but an `app_ops` table is cleaner | optional, only if you want a fourth workload |

Because the task requires one C file per engine, "shared layer" means a block copied verbatim into each file. Put markers like `/* BEGIN SHARED */ ... /* END SHARED */` around it and have your harness check that the block is byte-identical across the four files (hash it). That is how you can say "identical application behaviour" and prove it.

An answer for the earlier doubt about UDP: with this split, UDP means a different `transport_*` set plus an app that doesn't need the stream-reassembly stages. The engines don't change. The app does, because UDP has no byte stream.

## Phase 5: Experiment design

### Step 13: The baseline
Your professor's baseline is "I do nothing in the thread". It measures the ceiling of each mechanism before any application work exists.

- **E0a, empty loop:** one always-ready fd (an `eventfd` or a socket with data kept available). The loop does wait, return, wait, return, and does not read. Measure iterations per second for each engine. The nuance is that for level-triggered epoll this ready fd keeps firing. Define the same behaviour for each engine and write it down.
- **E0b, minimal ping-pong:** one connection, 1-byte echo, closed loop. This gives the round-trip floor, including the syscall cost of recv and send.
- **E0c, N idle fds:** N registered fds with one active. This isolates the cost of scanning the set (select and poll) versus the ready list (epoll).

### Steps 14–15: One thread, then one dimension at a time

| Exp | Varies | Fixed | Tests hypothesis |
|---|---|---|---|
| E1 | connection count: 1, 10, 100, 1k, 10k | echo 64 B, all active | select/poll cost grows with set size |
| E2 | active fraction: 100%, 10%, 1% | 1k conns, echo 64 B | idle fds hurt select/poll, not epoll |
| E3 | message size: 64 B, 1 KB, 4 KB | 100 conns | per-message vs per-byte cost |
| E4 | transfer size: 64 KB, 1 MB, 16 MB | upload and download, 10 conns | syscalls per MiB, batching in io_uring |
| E5 | read/write mix: upload vs download vs echo | same bytes moved | `EPOLLOUT` wakeups and `EAGAIN` behaviour |
| E6 | thread count (later) | best config from above | scaling, only after single thread is understood |

Select is capped by `FD_SETSIZE` (1024). The harness should record "unsupported" for select above about 1000 connections, not skip silently or crash.

### Step 16: Control variables

- **CPU pinning:** server on one core, client on others (`taskset`), and the same cores every run.
- **Machine state:** performance governor, no other heavy processes, `ulimit -n` raised, same kernel and the same build flags (`-O2`) for all engines.
- **Client must not be the bottleneck.** Record client CPU. If the client's core is near 100%, that run measures the client, not the server.
- **Warm-up and duration:** discard the first few seconds and run a fixed duration, not a fixed message count.
- **Repeats:** at least 10 runs per cell. Report the median and spread (IQR), not just the mean.
- **Loopback caveat:** state it in limitations. Loopback skips the NIC and its interrupts, so interrupt-related counts do not reflect a real network.

## Phase 6: Instrumentation

### Step 17: What to measure

| Metric | Where it comes from |
|---|---|
| Throughput (msgs/s, MiB/s) | client, over the measured window |
| Latency p50, p99, p99.9 | client, per-message timestamps (`CLOCK_MONOTONIC`) |
| Syscalls per message | server counters, cross-checked with `strace -c` |
| Wakeups and events per wait | server counters |
| Context switches (voluntary, involuntary) | `getrusage` (`ru_nvcsw`, `ru_nivcsw`), cross-checked with `perf stat` |
| CPU user vs system | `getrusage`, also `perf stat` |
| Peak memory | already collected from `VmHWM` |

Two measurement traps. First, latency must be taken at the client, since the server can't see queueing before it reads. Second, a closed-loop client (send, wait, send) hides overload. Use closed-loop for latency and a fixed-rate or saturating mode for throughput, and say which one each result used.

### Step 18: Internal counters versus external tools

Your current JSON output has `loop_calls` but not a per-syscall breakdown. Add these fields to the shared output schema:

- `wait_calls` (the existing `loop_calls`), `empty_wakeups` (wait returned 0 events), `events_total`
- `ctl_calls`, `recv_calls`, `send_calls`, `eagain_recv`, `eagain_send`
- `partial_recv`, `partial_send` (result shorter than requested)
- `vol_ctx_switches`, `invol_ctx_switches`
- `syscalls_total`, defined identically for all engines

**Define `syscalls_total` carefully.** For readiness engines it is wait + ctl + recv + send. For io_uring it is `io_uring_enter` calls (and `io_uring_register` if used). The ring handles the data operations inside the kernel, so counting each SQE as a syscall would be wrong. Report SQEs per enter as a separate batch-size metric.

**Cross-checks** make the numbers defensible:

- `strace -c -f` call counts should match the internal counters within a small margin. Run it separately, because strace slows the server and distorts timing.
- `perf stat -e context-switches,cpu-migrations,page-faults,cycles,instructions` against the server PID should agree with `getrusage`.
- `/proc/softirqs` (NET_RX, NET_TX) before and after a run, with the loopback caveat.
- If internal and external counts disagree, a counter has a bug or a call is hidden. That is worth finding before anyone else does.

### Step 19: One schema

Keep the JSON keys and units identical in all four engines, including `engine`, `app`, `server_ok`, and a `config` echo (port, max-clients, app). Fields that don't apply to an engine are emitted as `0`, not omitted. The harness then writes one CSV row per run without special cases.

## What this means for the code

Phases 4 to 6 give Phase 7 a concrete to-do list on your `epoll_server.c`:

1. Add `transport_accept/read/write` and call them from `client_step` and the accept path.
2. Move engine-only fields out of `client_t`.
3. Add the counters above and the context-switch fields to the JSON.
4. Add a `--app null` (or equivalent) mode for the E0 baselines.
5. Wrap the shared block in markers so you can hash it across engines.

I'd do these in that order, starting with the transport split. Want me to apply them to `epoll_server.c` as a new file, leaving your original untouched?

## User:

Lets dive into the remaining parts now

## Assistant:

> Splitting engine-specific code into a marked block.
>
> - Splitting engine-specific code into a marked block.
> - **Done**

Phases 7 and 8 are what's left. Phase 7 is code, so I'll build and test it first. Phase 8 can only be a plan until there are results to analyse.

I'll apply the Phase 4–6 to-do list to `epoll_server.c` as a new file and leave your original alone.

> Ran command
>
> - Create v3 server with transport split, engine block, counters, sink/spin apps
> - **Done**

It compiles with no warnings. Next is a correctness client that doesn't depend on the server's own accounting.

> Tallying how many reads each message costs.
>
> - Run correctness tests against v3
> - Tallying how many reads each message costs.
> - **Done**

All the checks pass. I'll add the script that proves the shared layer is identical across engines, then copy everything to outputs.

> Ran command, presented file
>
> - Add shared-layer hash check, verify it detects a change, copy outputs
> - Presenting file(s)...
> - **Done**

## Phase 7: Implementation (epoll side done, three engines to go)

I built `epoll_server_v3.c`, `test_server.py` and `check_shared.sh`. Your original is untouched. v3 compiles with no warnings under `-Wall -Wextra`, and all 27 correctness checks pass.

**What changed in the server**
- **Transport split:** `transport_accept/read/write` now hold every `accept4`, `TCP_NODELAY`, `recv` and `send` call. `client_step` only asks the app what it wants and passes the result back.
- **Engine block:** everything between `ENGINE BEGIN` and `ENGINE END` belongs to epoll. It holds `run_event_loop`, the epoll includes and `ENGINE_NAME`. The `ready_next` and `pending` fields are gone from `client_t`, because the io_uring engine should wrap the client instead.
- **Counters:** the new JSON fields from Phase 6 are all there. They include `syscalls_total`, `empty_wakeups`, `events_total`, `ctl_calls`, `eagain_*`, `partial_*` and context switches.
- **Two baseline apps:**
  - `--app spin` registers an always-readable `eventfd` that is never read, so each `epoll_wait` returns at once. This is the E0a empty loop.
  - `--app sink` reads and discards bytes with no parsing, which is the "loop plus `recv` only" baseline.
- **Shared-layer proof:** `check_shared.sh` strips the ENGINE block and hashes the rest of each file. I confirmed it detects a one-word change. Run it on all four engine files once they exist.

**What the tests cover**
- Echo at 1, 64 and 4096 bytes, including a second message on the same connection, and a byte-by-byte writer that splits the header.
- Rejection of oversized and zero-length headers, and 300 concurrent clients.
- Upload accepted when the data is correct, and closed with no ack when I flip one byte.
- Download pattern verified for 2 MiB+77 bytes, and separate error and incomplete accounting.

**Two things the counters already show.** These come from a 50-message loopback echo run, so treat them as hints, not results. Each message cost 3 `recv` calls (header, body and the EOF at close) and 4 `epoll_ctl` calls (ADD, two MODs, DEL). The two MODs per message come from switching to `EPOLLOUT` for the echo and back. That is the `epoll_ctl` overhead the 2004 paper warned about, and it is a real design choice to discuss: you could try the write first and only switch to `EPOLLOUT` on `EAGAIN`. If you change that, it has to change in the shared logic for all engines, or the comparison is unfair.

**Not done yet, and I won't claim otherwise**
1. **The other three engines.** Each needs only its own ENGINE block, with the shared layer copied verbatim. The io_uring one is the hard part, because it uses completions rather than readiness and has to count `io_uring_enter` calls as `loop_calls`.
2. **A performance-grade load generator.** `test_server.py` checks correctness but is far too slow for measurements. The real client should be C or Rust, use epoll itself, record per-message `CLOCK_MONOTONIC` latency, and support closed-loop and saturating modes.
3. **The harness** that runs the matrix, pins CPUs, repeats runs and writes CSV.
4. `spin` printed about 3.8M wakeups/s here, but that figure includes startup time and this sandbox isn't your machine. Don't quote it.

## Phase 8: Analysis and defence

This phase is mostly discipline, because the numbers don't exist yet. Here is how to prepare.

### Steps 24–25: Run the matrix, then explain every gap
For each cell of the matrix, write the explanation before looking at why the number came out that way. Use this template:

- **Hypothesis (from Phase 2):** for example, "select cost grows with the fd set, epoll's doesn't."
- **Result:** the median and IQR.
- **Mechanism:** which counter explains it (e.g. `syscalls_total` per message, `empty_wakeups`, `system_cpu_s`).
- **Verdict:** confirmed, refuted or partly confirmed.

A difference you can't tie to a counter is not yet explained. Run `perf` to find where the time went before writing it up.

### Step 26: Conclusions per workload
Write one short conclusion per workload class, as your professor wanted, and never "X is best". For example: "for small echo messages with few connections the four are within noise, and the gap opens with idle fds." Also record the cases where your results contradicted your predictions. Those are the most credible part of the report.

### Step 27: Limitations to state up front
- Loopback only: no NIC interrupts and no real network delay.
- One machine, one kernel version, one core for the server.
- Streaming is not covered, since the workloads are bursts.
- The load generator's own cost, and whether it was ever the bottleneck.
- Level-triggered epoll only. Edge-triggered would change the `epoll_ctl` and wakeup picture.

### Step 28: Viva preparation
Your professor said generated code is not an excuse, so be ready to explain the following without notes:

1. Why `epoll_wait` cost follows ready fds and `select` follows set size, and what the kernel does in each.
2. Why `recv` is still needed after epoll reports readiness, and what io_uring does instead.
3. What `partial_recv` and `eagain_*` tell you, and why echo makes 3 `recv` calls per message in your own data.
4. Why `EPOLLOUT` is armed only while sending, and why level-triggered epoll would spin if you left it on.
5. What the transport/app/engine split means and how `check_shared.sh` proves identical behaviour.
6. How signals are used (`SIGINT`/`SIGTERM` set a flag, `MSG_NOSIGNAL` replaces `SIGPIPE`) and why the 100 ms wait bounds the shutdown race.
7. Why you chose TCP and what changes for UDP.
8. What your literature review found, what is unverified, and where the gap is.

Which should I do next: the poll and select engines (simplest, and they test the shared layer), the C load generator, or the io_uring engine?

> File: [/mnt/user-data/outputs/epoll_server_v3.c](https://claude.ai/api/organizations/0485c86a-fcef-4bb1-9a06-f435440161db/conversations/7ba5a87c-b973-4fa0-bac0-795e646dda85/wiggle/download-file?path=%2Fmnt%2Fuser-data%2Foutputs%2Fepoll_server_v3.c)

> File: [/mnt/user-data/outputs/test_server.py](https://claude.ai/api/organizations/0485c86a-fcef-4bb1-9a06-f435440161db/conversations/7ba5a87c-b973-4fa0-bac0-795e646dda85/wiggle/download-file?path=%2Fmnt%2Fuser-data%2Foutputs%2Ftest_server.py)

> File: [/mnt/user-data/outputs/check_shared.sh](https://claude.ai/api/organizations/0485c86a-fcef-4bb1-9a06-f435440161db/conversations/7ba5a87c-b973-4fa0-bac0-795e646dda85/wiggle/download-file?path=%2Fmnt%2Fuser-data%2Foutputs%2Fcheck_shared.sh)

## User:

Could you explain the whole code once again according to sir's expectation

## Assistant:

> Outlining the walkthrough section by section.
>
> - Outlining the walkthrough section by section.
> - **Done**

Here is the whole of `epoll_server_v3.c`, in the order the file runs. For each part I say what it does, why it is there, and which OS idea it connects to, since that is the angle your professor asked for.

## 0. The one-paragraph picture

The server is a single thread in a loop. It waits until the kernel says some socket can make progress, does one small `recv` or `send` on it, advances a per-client state machine, and goes back to waiting. Three things are kept apart, as the diagram showed:

| Layer | Question it answers | Where in the file |
|---|---|---|
| App | what does this request mean, and what bytes are needed next? | `client_complete`, `prepare_chunk`, `next_header` |
| Transport | how do bytes physically move? | `transport_accept/read/write` |
| Engine | how does the thread wait and get told? | the `ENGINE BEGIN … ENGINE END` block |

The shared layer is everything outside the ENGINE block. The other three engines must copy it exactly.

## 1. Data structures

**`app_t`** picks the workload: `ECHO`, `UPLOAD`, `DOWNLOAD`, plus the two baselines `SINK` and `SPIN`. These are the plugins.

**`stage_t`** is the state of one client: `HEADER`, `RECEIVE`, `SEND` or `ACK`. TCP is a byte stream, so one `recv` can return half a header. The server therefore has to remember where it is between wakeups, which is why each client carries a state.

**`client_t`** is one connection:
- `fd` and `stage`.
- `buffer[4096]`, a fixed buffer. A 64 MiB transfer streams through it in chunks, so memory per client stays constant.
- `done` and `want` track progress inside the current chunk, and `total` and `left` track progress through the whole request.
- `prev` and `next` form a list used only at shutdown. Nothing scans it for I/O.
- There are no engine fields. An engine that needs its own state wraps this struct.

**`server_t`** holds the listener, the limits and every counter. The counters are grouped by who increments them: app counters (`messages`, `bytes_*`), engine counters (`loop_calls`, `ctl_calls`, `events_total`, `empty_wakeups`) and transport counters (`recv_calls`, `eagain_*`, `partial_*`).

## 2. Signals

`handle_signal` only sets `stop_requested = 1`, which is the only safe thing to do in a handler. In `main`, `sigaction` is installed with flags 0, so there is no `SA_RESTART`. A signal that arrives while the thread sleeps in `epoll_wait` makes it return `-1` with `EINTR`, the loop checks the flag, and it exits. Even with `SA_RESTART`, `epoll_wait` is never restarted, so the point is the same.

There is still a race. If the signal lands after the `while (!stop_requested)` check but before the thread sleeps, it would sleep through it. That is why the timeout is `WAIT_MS` (100 ms) and not infinite: shutdown is delayed by at most 100 ms. The race-free fixes are `epoll_pwait` with a signal mask or a `signalfd`, and you should know that.

`SIGPIPE`: a write to a peer that has closed would normally kill the process. `send(..., MSG_NOSIGNAL)` turns that into an `EPIPE` error instead. v1 used `signal(SIGPIPE, SIG_IGN)` for the same purpose. Signal-driven I/O (`SIGIO`) is not used.

## 3. The app layer: protocol and state machine

**Wire protocol.** Every request starts with an 8-byte big-endian length `N`.
- Echo: `N` bytes follow (at most 4096), and the server sends the same `N` bytes back.
- Upload: `N` bytes follow, and the server replies with one byte, `'K'`, only if all bytes were correct.
- Download: no body. The server sends `N` bytes.
- Data follows the pattern `byte[i] = i % 251`, so correctness can be checked cheaply. 251 is prime, so the pattern doesn't line up with the 4096-byte buffer, and a mis-ordered chunk would be noticed.

**`next_header`** resets a client to "waiting for 8 header bytes". For `SINK` it sets `want = BUFFER_SIZE` instead, so that mode just drains.

**`client_add` / `client_close`** manage the lifecycle. `client_add` enforces `--max-clients` by closing the new fd at once and counting `rejected`. `client_close` removes the client from the list, closes the fd and updates counters. If the client was in the middle of a request (`stage != HEADER` or `done != 0`) it counts as `incomplete`, and a negative error code counts in `errors`.

**`prepare_chunk`** sets `want` to `min(left, 4096)` for the next chunk. For downloads it also fills the buffer with the pattern at the correct offset.

**`client_complete(c, result)`** is the heart of the app. It receives "what the last I/O call returned":
1. **Negative result.** `EAGAIN`/`EINTR` means "nothing happened, try again" and returns 0. Any other error is returned as failure.
2. **Zero result (peer closed).** If we're at a clean boundary (`HEADER`, `done == 0`) it returns 1, a normal close that is neither an error nor incomplete. At any other point it returns `-ECONNRESET`, a truncated request. This is the "clean EOF versus incomplete" rule.
3. **Sink shortcut:** count bytes and return.
4. **Verification (upload only).** Each received byte is compared to the pattern before `done` is advanced. A mismatch returns `-EBADMSG`, so no ack is sent and the connection closes.
5. **Partial progress.** If `done != want`, return 0 and wait for more.
6. **Chunk finished.** What happens depends on the stage:
   - `HEADER`: decode `N`, reject `0`, anything over 64 MiB, or echo over 4096 (`-EMSGSIZE`), then go to `RECEIVE`, or `SEND` for download.
   - `RECEIVE`: echo flips to `SEND` using the same buffer. Upload either asks for the next chunk or becomes `ACK` with the byte `'K'`.
   - `SEND` and `ACK`: when `left` hits 0, `messages++` and the client returns to `HEADER`.

The app never calls a socket function and never mentions epoll. It only says what it needs next.

**`client_step`** connects the app to the transport. If `client_is_reading(c)`, it calls `transport_read` with `client_buffer(c)` and `client_length(c)`. Otherwise it calls `transport_write`. Then it passes the result to `client_complete`. This is one I/O call per event, which keeps one busy client from starving the others.

## 4. The transport layer

All TCP-specific code lives here:
- **`transport_accept`** calls `accept4` with `SOCK_NONBLOCK | SOCK_CLOEXEC`, so the new socket is non-blocking in one syscall. Then it sets `TCP_NODELAY` so small replies aren't delayed by Nagle's algorithm, which matters for latency tests.
- **`transport_read`** wraps `recv`.
- **`transport_write`** wraps `send` with `MSG_NOSIGNAL`.

Each returns a byte count or `-errno`, and increments the counters (`recv_calls`, `eagain_recv`, `partial_recv`, and the send equivalents). Counting here means the numbers mean the same thing in every readiness engine. Swapping to UDP would mean replacing these three functions, plus changing the app, since UDP has no byte stream.

## 5. The ENGINE block (epoll)

**Setup.** `epoll_create1` makes the epoll instance. The listener is registered with `EPOLLIN` and `data.ptr = NULL`, which marks "this is the listener". In `spin` mode a second always-readable `eventfd` is registered with a sentinel pointer.

**What epoll does in the kernel.** Registration (`epoll_ctl ADD`) happens once and puts a callback on the socket's wait queue. When data arrives, the callback puts that fd on a ready list. `epoll_wait` just returns the list, so its cost follows the number of ready fds, not the number registered. Contrast select and poll, which re-hand the whole set to the kernel every call.

**The loop** runs `epoll_wait` with a 100 ms timeout, then counts `loop_calls`, `empty_wakeups` (zero events) and `events_total`. For each event:
- **Listener event:** accept up to `ACCEPT_BATCH` (64) connections, so a flood of new connections can't hog one wakeup. Each gets a `client_t` and an `EPOLLIN` registration.
- **Client event:** call `client_step`. If it returns non-zero, `EPOLL_CTL_DEL` and close. Otherwise, if the direction changed (reading to sending or back), do `EPOLL_CTL_MOD` to switch between `EPOLLIN` and `EPOLLOUT`.

**Why switch directions.** This is level-triggered. If you left `EPOLLOUT` armed while waiting for a request, the socket would nearly always be writable and `epoll_wait` would return constantly with nothing to do. Interest is armed only for the direction the app is waiting on. Errors and hangups (`EPOLLERR`, `EPOLLHUP`) are always reported, and they are handled naturally because the next `recv`/`send` then fails or returns 0.

**`spin`:** the always-ready fd is never read, so every `epoll_wait` returns at once and the loop does nothing. The loop rate is the baseline for how fast a thread can wait and return.

## 6. `main`

- **Arguments:** `--port`, `--bind`, `--app`, `--max-clients`, `--help`, parsed with the strict `number()` helper. Bad input gives exit code 2.
- **Listener:** `socket` (non-blocking), `SO_REUSEADDR` so a quick restart doesn't hit `TIME_WAIT`, `bind` to a numeric IPv4 address, `listen` with backlog 128. `getsockname` then reads the real port, which makes `--port 0` work. The engine prints `READY <port>` only after its own setup succeeds, so the harness knows when to start the client.
- **After the loop:** close any remaining clients, close the listener, then read the clock, `getrusage` (CPU time, context switches) and `/proc/self/status` (`VmHWM`, peak memory), and print one JSON line. It reads `VmHWM` rather than `ru_maxrss` because `ru_maxrss` can include the launcher's memory from before `exec`.

## 7. One 64-byte echo, traced through the kernel

| Step | Call | Counter |
|---|---|---|
| connect arrives | `epoll_wait` returns listener | `loop_calls` |
| accept | `accept4`, `setsockopt` | `accept_syscalls` ×2 |
| register | `epoll_ctl ADD` | `ctl_calls` |
| request arrives | `epoll_wait`, then `recv` 8 B | `recv_calls` |
| body | `recv` 64 B | `recv_calls` |
| switch to send | `epoll_ctl MOD` (EPOLLOUT) | `ctl_calls` |
| writable | `epoll_wait`, then `send` 64 B | `send_calls` |
| back to reading | `epoll_ctl MOD` (EPOLLIN) | `ctl_calls` |
| client closes | `epoll_wait`, then `recv` returns 0 | `recv_calls` |
| cleanup | `epoll_ctl DEL`, `close` | `ctl_calls` |

That matches the measured 3 `recv` and 4 `epoll_ctl` calls per connection from the test.

## 8. Weaknesses to know before the viva

- **Header and body use separate `recv` calls.** Echo costs at least 2 `recv` + 1 `send` per message. Reading both at once would cut syscalls but change shared behaviour for all engines.
- **Two `MOD` calls per echo message.** Trying the `send` first and switching only on `EAGAIN` would avoid them.
- **Any non-`EAGAIN` accept error ends the server.** Your v1 handled `EMFILE` with a spare fd. v3 does not, so with a low `ulimit -n` the 10k-connection test could kill the server instead of dropping a client. Raise `ulimit -n`, or I can port the spare-fd trick into the shared transport code.
- **One `calloc` of about 4 KB per client** adds memory and allocator cost. That is visible in `max_rss_kib` at 10k clients.
- **The 100 ms timeout** adds up to 100 ms of shutdown delay and a few empty wakeups per second when idle.
- **`select` can't run above about 1000 connections**, so the harness has to report "unsupported" there.

## 9. How this answers your professor

| What he asked | Where it is in the code |
|---|---|
| Architecture first, apps as plugins | the layer split, ENGINE block, `--app` |
| Not tied to TCP | all socket calls inside `transport_*` |
| How the mechanism executes, not the API | epoll ready list versus per-call set copy, `EPOLLOUT` handling |
| Signals | `sigaction` without `SA_RESTART`, `EINTR`, `MSG_NOSIGNAL`, the 100 ms bound |
| Baseline "do nothing" loop | `--app spin` and `--app sink` |
| Instrumentation | the counters and the JSON schema |
| Understand the code, don't just generate it | this walkthrough, and the viva list in Phase 8 |

If you want, I can fix the `EMFILE` weakness next, since it's the only one that could break a benchmark run rather than just cost performance.

Powered by Claude Exporter (https://www.ai-chat-exporter.net)
