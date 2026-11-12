# Phase 4, 5, and 6 implementation

## User:

> File: epoll_server_1.c

Now carry out the detailed analysis of the code, into 3 to 4 phases, we will cover phase wise, 1st pahse now

## Assistant:

> Checking the return-value contract and event flags.
>
> - Planning the phased analysis, starting with phase one.
> - Check whether framework.h is available
> - Checking the return-value contract and event flags.
> - **Done**

`framework.h` and the other files it pulls in weren't uploaded, so I can only see this engine file. Wherever I describe a function from the framework, I mark it as inferred from how it's used. If you send `framework.h` and `framework.c` (or whichever files hold `conn_progress`, `conn_step`, `accept_ready` and `udp_drain`), I can confirm the inferences in the later phases.

## The four phases

| Phase | Focus |
|---|---|
| 1 (now) | What this file is, the engine contract, setup and the main loop |
| 2 | The level-triggered paths: `handle_level` and `handle_direct`, and the `epoll_ctl` cost |
| 3 | The edge-triggered path and UDP, including the "drain until EAGAIN" rule |
| 4 | Bugs and risks, what the counters can and can't show, and viva questions |

## Phase 1: Role, contract and main loop

### 1.1 What this file is
This is the engine layer from the architecture we designed, now in its own file. The header comment says it directly: only the event loop lives here, and the contract is in `framework.h`. Compared with `epoll_server_v3.c`:

| Concern | v3 (one file) | This file |
|---|---|---|
| App state machine | `client_complete` | `conn_progress` / `conn_step` (framework) |
| Transport | `transport_*` | framework, and `udp_drain` shows UDP exists |
| Accept loop | inside `run_event_loop` | `accept_ready` (framework) with an engine callback |
| Shared layer enforced by | the hash script | the compiler and the linker (separate files) |
| Modes | LT only | LT, LT-direct and ET, chosen at run time |

Separating the shared layer into its own file is stronger than the hash trick, because the other engines then link the same object, not a copy.

### 1.2 The contract, inferred from usage
The engine calls these framework pieces:

| Name | Meaning (inferred) |
|---|---|
| `server_t`, `conn_t` | server state and one connection |
| `s->config->{transport, edge_triggered, epoll_direct}` | run-time options |
| `s->listener` | the listening TCP socket, or the UDP socket |
| `accept_ready(s, callback, ctx)` | accept in a bounded batch, create a `conn_t` for each, then call `callback(s, c, ctx)` so the engine can register it |
| `conn_progress(c)` | do one I/O step, return 0 to continue, non-zero to close |
| `conn_step(c, &would_block)` | like `conn_progress` but also reports "the kernel said EAGAIN" |
| `conn_is_reading(c)` | which direction the app wants next |
| `conn_close(c, rc)` | free the connection and update counters |
| `udp_drain(s)` | receive and answer UDP datagrams |
| `server_ready(s)` | print `READY <port>` |
| `stop_requested`, `WAIT_MS` | signal flag and wait timeout |
| `loop_calls`, `sc_wait`, `sc_ctl` | counters; `sc_` looks like "syscall count" |
| `c->interest_out` | engine-visible flag: which direction is currently registered |

The engine only needs to implement `engine_run` and export `ENGINE_NAME`. That is the plugin interface the professor wanted. `const char *const ENGINE_NAME` is a file-scope object with external linkage in C, so the framework can read it as `extern`.

One mismatch to know about: the header comment documents two modes (`--epoll-mode lt` and `et`), but the code also has a third, `lt-direct`, selected by `config->epoll_direct`. It is only explained in the comment above `handle_direct`. A viva question like "how many modes are there?" would catch this, so update the header.

### 1.3 `remember`: the registration hook
`accept_ready` calls this for each new connection. The framework owns accepting, and the engine owns how the fd enters the mechanism.

- **LT:** register `EPOLLIN`, since a new connection first waits for a request.
- **ET:** register once with `EPOLLIN | EPOLLOUT | EPOLLET`. The interest set never changes after this, which is how ET avoids `EPOLL_CTL_MOD` entirely.
- If `EPOLL_CTL_ADD` fails, it calls `conn_close(c, -errno)` without a `DEL`, which is right because the fd was never added.
- It always returns 0 and uses `conn_close` to report failure, so a failed registration kills one client and not the server.

Two kernel facts matter here:
1. `EPOLL_CTL_ADD` checks the socket's current readiness. If data had already arrived before the `ADD`, an initial event is queued, so ET cannot miss the first edge.
2. In ET mode, registering `EPOLLOUT` on a fresh socket reports an immediate writable edge, because the send buffer is empty. Each new connection therefore causes one wakeup that finds nothing to do (`recv` returns `EAGAIN` and `handle_edge` returns on `would_block`). This is a small per-connection cost that shows up as extra wakeups and `EAGAIN`s in the ET counters.

### 1.4 `engine_run` setup

1. `epoll_create1(EPOLL_CLOEXEC)` creates the instance. Failure returns `-errno`.
2. The listener is added with `data.ptr = NULL`. NULL is the marker that distinguishes the listener from clients, so no per-listener object is needed.
3. For UDP with ET, `EPOLLET` is added to the listener, because one UDP socket serves every client. For a TCP listener it stays level-triggered.
4. `sc_ctl++` is incremented before each `epoll_ctl`, so failed calls are counted as syscalls too.
5. `server_ready(s)` prints `READY` only after setup succeeded. This is the same rule as v3.

### 1.5 The main loop

```
while (!stop_requested):
    loop_calls++, sc_wait++
    n = epoll_wait(epfd, events, 256, WAIT_MS)
    if n < 0: EINTR → retry, else error → exit
    for each event:
        if UDP:           drain the socket
        elif c == NULL:   accept_ready(...)
        elif ET:          handle_edge
        elif direct:      handle_direct
        else:             handle_level
```

- **Dispatch order.** UDP is checked first, because for UDP the only registered fd is the socket, so `c` is always NULL and means something different. For TCP, NULL means "the listener".
- **Mode is checked per event.** `config` doesn't change at run time, so this is a few predictable branches. It is cheap, but it is work an optimised engine could hoist out of the loop. It is the same for every event, so it doesn't bias comparisons between modes much.
- **`loop_calls` and `sc_wait` are always equal here.** They differ in other engines (e.g. io_uring, where one `io_uring_enter` handles many operations), so keeping both is right for the shared schema.
- **The event flags are never read.** `events[i].events` is ignored. The engine just calls `conn_progress` or `conn_step` and lets `recv`/`send` discover errors, hangups and EOF. This is valid here because the interest set matches what the app wants, and a hangup makes the next call fail or return 0. Using the flags instead would save a syscall on some error paths but spread socket knowledge into the engine.
- **No wakeup statistics.** v3 counted `empty_wakeups` and `events_total`, and this file doesn't. If the framework doesn't count them elsewhere, "events per `epoll_wait`" can't be computed, and that metric is useful for explaining batching.
- **Stack array.** `events[256]` is declared inside the loop, which is free (just stack reserved once per call). The array is not zeroed.
- **Exit paths.** `goto finished` closes only `epfd`. The listener and connections are closed by the framework's `main`, which is consistent with v3.

### 1.6 Two things to check before relying on this loop
1. **`result` after `accept_ready`.** The code sets `result = accept_ready(...)` and exits only if it is negative. If `accept_ready` returns a positive value (for example, the number accepted), `result` would stay positive and `engine_run` would eventually return it as if it were an error code, and `main` would print `strerror(-rc)` for a positive number. This depends on the contract in `framework.h`, so it needs checking. If it returns 0 on success, there is no problem. A safer version is to use a separate variable and only copy it into `result` on failure.
2. **Header comment drift**, described in 1.2.

### Phase 1 summary
This file is a clean engine plugin. It owns the epoll instance, the interest sets and the wait loop, and delegates everything else to the framework. Three modes share one loop, the listener is marked with NULL, and the event flags are ignored on purpose. Its weak spots are the possible `result` leak and the missing wakeup counters.

Say "go" for Phase 2, where I'll trace `handle_level` and `handle_direct` step by step with the exact `epoll_ctl` counts for a 64-byte echo, and compare them with v3's 4 `epoll_ctl` calls per connection.

## User:

next, we will cover phase 2 now

## Assistant:

> Patching the test variant to measure direct handling.
>
> - Patching the test variant to measure direct handling.
> - **Done**

Phase 2 depends on exact counts, so I'm testing the logic before quoting numbers. I can't compile your file without `framework.h`, so I'll port the `handle_direct` logic into my v3 server as a variant and measure LT against direct on the same workload.

> Recounting the test pass results after the patch.
>
> - Build direct-send variant and compare counters with plain LT
> - Recounting the test pass results after the patch.
> - **Done**

The port reproduces the predicted counts, so the numbers below are measured. There is one correction first: I said earlier that 27 checks pass in `test_server.py`, and the real total is 26. All 26 still pass, and the direct variant passes them too.

## Phase 2: The level-triggered paths

### 2.1 What level-triggered means here
With LT, `epoll_wait` keeps reporting a socket for as long as the condition is true: data is waiting to be read, or there is room to write. Two consequences drive both handlers:
1. **One step per event is safe.** If you read only part of the data, the next `epoll_wait` reports the socket again. Nothing is stranded.
2. **Interest must match what the app wants.** A socket with an empty send buffer is almost always writable, so leaving `EPOLLOUT` on while waiting for a request would make `epoll_wait` return constantly with nothing to do.

### 2.2 `handle_level`: the plain version
```
was_reading = conn_is_reading(c)
rc = conn_progress(c)              // exactly one recv or send
if rc != 0:       DEL + close
elif direction changed:  MOD to EPOLLIN or EPOLLOUT
```
The rule is "the interest set follows the app's direction". Trace of one 64-byte echo on a fresh connection, assuming `conn_progress` does one I/O call per invocation as in v3:

| Event | What happens | `epoll_ctl` |
|---|---|---|
| listener readable | `accept_ready` accepts, `remember` registers `EPOLLIN` | ADD |
| readable | recv 8-byte header, still reading | none |
| readable again (LT) | recv 64-byte body, app now wants to send | MOD → `EPOLLOUT` |
| writable | send 64 bytes, app wants a header again | MOD → `EPOLLIN` |
| readable | recv returns 0, clean close, `rc = 1` | DEL |

So a connection carrying one message costs 4 `epoll_ctl` calls and each extra message on it costs 2 (the two MODs), and the message needs three wakeups.

### 2.3 `handle_direct`: the "try the write first" version
This is the idea I suggested when the counters first showed two MODs per message. The team already built it, with one extra field, `c->interest_out`, recording whether `EPOLLOUT` is currently registered.

```
rc = conn_progress(c)
if rc == 0 and app now wants to write and EPOLLOUT not registered:
        rc = conn_progress(c)      // send the reply right away
if rc != 0: DEL + close; return
want_out = !conn_is_reading(c)
if want_out != interest_out:  MOD, interest_out = want_out
```

- The second `conn_progress` runs only when the reply is ready and the engine isn't already waiting on `EPOLLOUT`. A small reply fits in the kernel's send buffer, so the send completes, the app flips back to reading, `want_out` is 0, and no MOD is needed.
- If the send can't finish (`EAGAIN` or a partial send), the app stays in the write state, `want_out` becomes 1, and the code does MOD to `EPOLLOUT`. That is the only case where it pays `epoll_ctl`.
- Once `interest_out` is 1, the second step is skipped, so it never sends twice in one event, and the next `EPOLLOUT` event finishes the job.

### 2.4 Measured comparison
I ported the same logic into my v3 server and ran two workloads over loopback with a closed-loop client. Every number is a real counter from the run:

| Workload | Mode | `epoll_wait` returns | `epoll_ctl` | `recv` | `send` |
|---|---|---|---|---|---|
| 1 connection, 100 messages | LT | 302 events | 203 | 201 | 100 |
| | direct | 202 events | **3** | 201 | 100 |
| 50 connections, 1 message each | LT | 250 events | 201 | 150 | 50 |
| | direct | 200 events | **101** | 150 | 50 |

Reading the table:
- Per message on a long-lived connection, `epoll_ctl` drops from 2 to 0. The remaining 3 are the listener ADD plus ADD and DEL for the one connection.
- Events per message drop from about 3 to 2, because the "socket is writable" wakeup disappears. Per connection with one message it is 5 down to 4.
- `recv` and `send` counts don't change. Direct mode saves wakeups and `epoll_ctl` calls, not data-moving calls.
- Syscalls per message for a single ping-pong connection go from about 8 (3 waits, 2 recv, 1 send, 2 ctl) to about 5 (2 waits, 2 recv, 1 send). With many connections the `epoll_wait` cost is shared, so the gap narrows to the saved `epoll_ctl` calls.

### 2.5 Where direct mode doesn't help
These follow from the code and the 4096-byte buffer, and I haven't measured them:
- **Large downloads.** After the header, the first chunk is sent immediately, but the transfer is still in the SEND stage, so `want_out` is 1 and a MOD to `EPOLLOUT` follows. Each later `EPOLLOUT` event sends one 4 KB chunk. A 16 MiB download is about 4096 events and 4096 `send` calls whichever LT mode you use, with 2 MODs per request at most.
- **Uploads.** The final 1-byte `'K'` ack is sent immediately in direct mode, so no MOD, while plain LT pays 2 MODs per upload. The upload data itself still moves one 4 KB chunk per event.
- **Slow readers.** When the client doesn't read, the send buffer fills, `send` hits `EAGAIN`, and direct mode degrades to plain LT behaviour for that connection.

This is the right place to discuss one-step-per-event. It is fair to other connections, and it costs an extra wakeup per chunk. A "drain until EAGAIN" loop would save wakeups but costs one extra failing `recv` per drain and lets one busy client dominate a pass.

### 2.6 Details worth noticing in the code

1. **`errno` is saved before `drop`.** Both handlers do `int error = errno;` before calling `drop`, because `drop` runs `epoll_ctl`, which can overwrite `errno`. That is a careful detail and a good viva point.
2. **`drop` issues a `DEL` before `close`.** Closing the last descriptor for a socket removes it from the epoll set automatically, as long as the fd wasn't duplicated (no `dup` or `fork`), so the DEL is usually redundant. In direct mode it is half of the per-connection `epoll_ctl` cost (ADD plus DEL). Either keep it, since it is explicit and safe, and note it, or drop it and justify why. Either way, record the decision, because it changes the `epoll_ctl` count you report.
3. **`interest_out` belongs to the direct mode only.** `handle_level` uses `was_reading` and never touches it, and a process runs in one mode for its whole life, so the two can't conflict. It relies on the framework zero-initialising `conn_t` (v3 uses `calloc`). That is inferred.
4. **The optimisation isn't specific to epoll.** Any readiness engine can send the reply immediately instead of waiting for a writable event, and poll and select would save wakeups the same way. If only epoll gets it, a comparison against poll or select mixes an engine difference with an app-level tweak. Either apply the same try-write-first rule in every engine, or present `epoll lt-direct` as a labelled variant beside plain `epoll lt`. Pick one before running Phase 5 experiments.
5. **MOD failures close the connection.** In both handlers a failed MOD goes through `drop`, so one broken connection never stops the server.
6. **Event flags are still ignored.** As in Phase 1, `EPOLLERR` and `EPOLLHUP` are discovered through the next `recv` or `send`. An error on a socket with write interest is found by the send, and one with read interest by the recv.

### Phase 2 summary
Plain LT is simple and pays two `epoll_ctl` calls and one extra wakeup per message to follow the app's direction. Direct mode removes both costs for any reply that fits in the socket buffer and falls back to plain LT when the kernel pushes back. In my measurements it cut `epoll_ctl` from 2 to 0 per message and events from 3 to 2, with the same number of `recv` and `send` calls. Because the optimisation is engine-neutral, applying it to all engines (or labelling it) matters for the fairness of the final comparison.

Say "go" for Phase 3, where I'll cover the edge-triggered path (`handle_edge`, the `would_block` rule, why ET registers both directions once) and the UDP branch, with the pitfalls specific to each.

## User:

noew we will dive into the 3rd pahse

## Assistant:

> Patching the edge-triggered drain loop into the v3 build.
>
> - Patching the edge-triggered drain loop into the v3 build.
> - **Done**

Phase 3 has two claims I want to check before explaining them: that edge-triggered mode needs a drain loop, and what the drain costs. I'll port the ET logic into my v3 server as a variant and also build a deliberately broken one-step ET to see the failure.

> Comparing the three event-loop modes on the ping-pong run.
>
> - Build ET variants, compare counters, demonstrate stalling without drain loop
> - Comparing the three event-loop modes on the ping-pong run.
> - **Done**

I ported the ET logic into my v3 server as a variant and measured it, as I did for Phase 2. The counts below are real, except where I say a point is derived from the code. The ET variant passes all 26 correctness checks, including the 3 MiB upload, the 2 MiB download and 300 concurrent clients.

## Phase 3: The edge-triggered path and UDP

### 3.1 What edge-triggered changes
Level-triggered asks "is the socket ready right now?" Edge-triggered asks "did the socket just become ready?" That moves responsibility to your code:

1. **You must drain.** If you read only part of the data, the kernel doesn't report the socket again, so the rest is stranded until something new arrives.
2. **You must know when to stop.** The only reliable signal that a socket is drained is that a `recv` or `send` returns `EAGAIN`.
3. **Interest can be set once.** Because readiness is reported as changes, you can register both directions at `ADD` and never call `epoll_ctl MOD`.

### 3.2 Registration in ET mode
`remember` registers `EPOLLIN | EPOLLOUT | EPOLLET` once. `ADD` checks current readiness and queues an initial event, so data that arrived before registration is not lost. The cost is that a fresh socket is always writable, so there is always an initial `EPOLLOUT` edge. If it arrives before the client's data, the handler runs, `recv` returns `EAGAIN`, and the wakeup is wasted. I measured this with 50 connections of one message each: `recv_calls` was 230, against 200 if every connection needed exactly 4, and the extra 30 are consistent with 30 connections whose initial edge arrived before their data. The exact number depends on timing.

### 3.3 `handle_edge` and `conn_step`
```
loop:
    would_block = 0
    rc = conn_step(c, &would_block)     // one I/O call
    if rc != 0:  DEL + close; return
    if would_block: return               // kernel said EAGAIN → drained
    // else made progress: go around again
```
`conn_progress` can't be used here. It returns 0 both for "made progress" and for "got `EAGAIN`", so the caller couldn't tell whether to stop. `conn_step` adds the `would_block` out-parameter so the loop knows when the socket is drained. This is why the framework offers two entry points.

The loop also crosses stage boundaries by itself. After the request is received, the next iteration sends the reply, then tries to read the next header, all without an `epoll_ctl` and without returning to `epoll_wait`. The loop ends only when some call hits `EAGAIN`, in a real run usually the final read of the next header.

Trace of one 64-byte echo:

| Iteration | Call | Result |
|---|---|---|
| 1 | recv header (8 B) | progress |
| 2 | recv body (64 B) | progress, app now wants to send |
| 3 | send reply (64 B) | progress, app wants a header |
| 4 | recv | `EAGAIN` → return to `epoll_wait` |

### 3.4 Measured comparison of the three TCP modes
Single connection, 100 messages, closed-loop:

| Mode | `epoll_wait` events | `epoll_ctl` | `recv` | `send` |
|---|---|---|---|---|
| LT | 302 | 203 | 201 | 100 |
| LT direct | 202 | 3 | 201 | 100 |
| ET | **100** | 3 | **299** | 100 |

Reading it:
- **ET gets to one wakeup per message** and, like direct mode, needs no MOD.
- **ET pays about one extra failing `recv` per message** (about 3 per message against 2). That is the price of "drain until `EAGAIN`". The count was 299 and not exactly 300 because on a few messages the next request had already arrived, so the recv returned data and not `EAGAIN`. Counts like this are timing-dependent, so use medians across runs.
- **Syscalls per message in this ping-pong case:** LT is about 8, direct is about 5 (2 waits, 2 recv, 1 send) and ET is about 5 (1 wait, 3 recv, 1 send). They tie on total; they differ in what the calls are. `epoll_wait` is shared across ready connections under load, while the failing `recv` is per connection. So ET may lose to direct at high concurrency and win when wakeups are expensive. That is a hypothesis for the Phase 5 experiments, not a result.

### 3.5 What happens without the drain loop
I built an ET server that does one step per event, the way the LT handler does, and sent it one echo request:

| Server | Result | `recv` | `send` |
|---|---|---|---|
| ET with drain loop | echoed correctly | 4 | 1 |
| ET, one step per event | **stalled, no reply after 2 s** | 2 | 0 |

The broken server received the whole request (2 `recv` calls) and then sat there. After the last `recv`, the app wanted to send, but ET reports changes, and no change was coming: the socket was already writable, so there was no new edge. This is the standard ET bug: no crash and no error, just a silent hang under specific timing. It is also a good viva answer to "why can't you just reuse the LT handler?".

### 3.6 ET hazards that the code doesn't protect against
These follow from the design; I didn't measure them.

1. **Starvation.** `handle_edge` runs until `EAGAIN`. For a fast uploader, `recv` may never block, and for a fast download reader, `send` may not block until the socket buffer fills, so one connection can keep the thread for a long stretch while others wait. LT's one-step-per-event gives natural round-robin. The usual fix is a per-event step budget, but then you need your own ready queue to resume, because no new edge will arrive. This matters most for mixed workloads (experiment E5) and tail latency (p99).
2. **A short read doesn't mean drained**, and the code correctly doesn't assume it. Some servers treat "read fewer bytes than requested" as "socket drained" and skip the failing `recv`. That is an optimisation with edge cases, so the one-extra-recv cost is the safe price.
3. **Spurious `EPOLLOUT` edges.** I only measured the initial one. After a `send` has hit a full buffer, Linux generates a writable edge when space frees up, and if the app is reading at that moment the handler does a wasted `recv`. This matters for slow-reader tests.
4. **The TCP listener stays level-triggered, on purpose.** `accept_ready` accepts in a bounded batch (`ACCEPT_BATCH`, I believe 64, as in v3). With ET on the listener, connections left over after the batch would produce no new edge and would hang. LT re-reports the listener until the queue is empty. The code applies ET to a listener only in the UDP case (3.7), where draining is a different matter.
5. **Per-connection cost versus direct mode:** both have ADD and DEL only, so `epoll_ctl` is the same.

### 3.7 The UDP branch
I can't measure this, since my test server has no UDP path and `udp_drain` isn't visible. This is analysis of the engine code, with the framework behaviour inferred.

```
if is_udp:
    do { rc = udp_drain(s) } while (rc == 0 && edge_triggered && !stop_requested)
    if rc < 0: fatal
```
- **One socket serves everyone.** The only fd in epoll is the UDP socket, so `data.ptr` is NULL and there are no `accept`, `conn_t` or MOD calls. Counters like `accepted` and `peak` don't mean the same thing.
- **LT UDP:** one `udp_drain` per event. If it handles one datagram per call, each datagram costs one wakeup, plus the receive and send calls.
- **ET UDP:** the loop repeats `udp_drain` until it returns non-zero. So `udp_drain` must return non-zero (probably a positive value) when it sees `EAGAIN`. If it returned 0 for "nothing left", the loop would spin forever. Check this in `framework.h`.
- **LT versus ET trade-off for UDP.** Under load, the ET drain loop handles a burst of datagrams per wakeup and pays one failing receive per burst. At low rates it pays one failing receive per datagram. So ET should win under a flood and lose slightly under light traffic. That is a testable prediction.
- **The loop checks `stop_requested`**, so a sustained flood can't prevent shutdown. Good.
- **Loss is invisible to the server.** If the kernel receive buffer fills, datagrams are dropped silently, and a slower drain loop drops more. Loss must be counted at the client, and `SO_RCVBUF` and the kernel's UDP receive-error counters (`/proc/net/snmp`) belong in the instrumentation list for UDP runs.
- **Any negative return ends the server**, via `goto finished`. If `udp_drain` reports transient errors such as `ENOBUFS` as negative, one bad moment kills the whole run. Check how `udp_drain` maps errors.

### 3.8 Phase 3 summary
ET registers each socket once and moves the work into a drain loop. In my port that gave one wakeup per message and no `epoll_ctl` calls, at the price of about one extra failing `recv` per message and a risk of starving other connections. The same per-message syscall total as direct mode means ET's advantage has to come from the wakeup cost, which the real experiments must separate. Drain-until-`EAGAIN` is mandatory, and I showed the silent hang without it. The UDP branch is simpler structurally but needs the loss accounting that TCP doesn't.

Say "go" for Phase 4, where I'll cover the bug and risk list across the whole file (including the `result` question from Phase 1), what the counters can and can't prove, and the viva questions for this engine.


## User:

Continue for phase 4

## Assistant:

> Judging whether the counters measure the right things.
>
> - Judging whether the counters measure the right things.
> - **Done**

Phase 4 is code reading only. I didn't run anything, because every item below follows from the code, or from the framework behaviour I've inferred. I've marked which is which, and I've ranked the findings by how likely they are to affect your results.

## Phase 4: Bugs, risks, counters and viva

### 4.1 Findings, ranked

| # | Finding | Type | Severity |
|---|---|---|---|
| 1 | `result` can leak a positive value from `accept_ready` | Code bug (conditional) | Medium |
| 2 | Mode flags have silent precedence and silent no-ops | Design gap | Medium |
| 3 | ET UDP loop spins forever if `udp_drain` returns 0 at `EAGAIN` | Contract risk | High if true |
| 4 | `accept_ready` failing with `EMFILE` | Contract and environment risk | Medium |
| 5 | `SIGPIPE` on `send` to a reset peer | Framework risk | High if unhandled |
| 6 | Shutdown race on `stop_requested` | Minor | Low |
| 7 | Stale `conn_t` pointers within one event batch | Latent hazard | Low today |
| 8 | ET starvation, with no fairness budget | Design limit | Matters for E5 and p99 |
| 9 | Header comment drift | Documentation | Low |

### 4.2 The findings in detail

**1. The `result` leak (from 1.6).**
```c
result = accept_ready(s, remember, &epfd);
if (result < 0) goto finished;
```
If `accept_ready` returns a count, `result` stays positive, and the loop continues. When `stop_requested` ends it, `engine_run` returns that positive number as if it were an error code. The fix is trivial and safe whichever way the contract reads:
```c
int rc = accept_ready(s, remember, &epfd);
if (rc < 0) { result = rc; goto finished; }
```

**2. Mode flags.**
- The dispatch order is ET, then direct, then plain LT. If a run sets both `edge_triggered` and `epoll_direct`, ET wins silently, so a result labelled `lt-direct` could really be ET.
- For UDP, `epoll_direct` is never read. A UDP run with `lt-direct` behaves exactly like `lt`, so a results table could show two "different" rows that are identical.
- The fix is to reject the combinations in argument parsing, or at least print the effective mode in the `READY` line or the summary.

**3. The ET UDP loop.**
```c
do { rc = udp_drain(s); } while (rc == 0 && edge_triggered && !stop_requested);
```
The loop exits only when `udp_drain` returns non-zero. So the contract must be "0 means I handled something, positive means `EAGAIN`, negative means fatal". If `udp_drain` returns 0 for both, the engine busy-loops at 100% CPU and the counters would show a flood of failed `recvfrom` calls. This is the first thing to check in `framework.h`.

**4. `EMFILE` on accept.**
- A negative return from `accept_ready` ends the whole server (`goto finished`).
- In LT, if the framework instead swallowed `EMFILE` and returned 0, the listener would stay readable, and the loop would spin without making progress.
- For the 300-client and larger runs, raise `ulimit -n` and print it in the run metadata.
- Either way, the framework needs a defined policy for `EMFILE`, and the viva may ask for it.

**5. `SIGPIPE`.**
- The engine never sees it, but a `send` to a peer that has already reset kills the process unless the framework uses `MSG_NOSIGNAL` or ignores the signal.
- Stress and slow-reader runs hit this when clients are killed mid-transfer.
- Check how the framework sends data.

**6. Shutdown race.**
- A signal that arrives between the `while (!stop_requested)` check and `epoll_wait` isn't seen until the wait times out, so shutdown can lag by up to `WAIT_MS`.
- It's harmless as long as `WAIT_MS` is small (such as 100 ms) and your script allows for it.
- The strict fix is `epoll_pwait` with a signal mask, or a self-pipe, but it isn't worth the extra code here.

**7. Stale pointers in one batch.** I traced this carefully, because it's the classic epoll bug.
- `data.ptr` points at a `conn_t`. If an earlier event in the same batch freed a connection that appears again later in the batch, that would be a use-after-free.
- That can't happen today. `epoll_wait` returns each fd at most once per call, and only a connection's own handler closes it.
- If someone later adds an idle-timeout sweep, or closes one connection from another's handler, this becomes a real bug. A note in the code would protect the next person.

**8. ET starvation.** Covered in 3.6. It is a design limit and not a bug, but it explains why ET's tail latency can look worse than LT's under mixed load.

**9. Header comment.** It documents two modes but the code has three, and it hard-codes a path (`Chirag/code/common/framework.h`) that's wrong for anyone else's checkout.

### 4.3 Things that are correct

- **`errno` is saved before `drop`.** Every path takes a copy first.
- **Failed `ADD` doesn't `DEL`.** The fd was never in the set.
- **`DEL` with a NULL event** is valid on modern kernels (2.6.9 and later).
- **The TCP listener stays LT** because `accept_ready` is bounded. ET there would hang leftover connections.
- **The `stop_requested` check inside the UDP drain loop** keeps a flood from blocking shutdown.
- **`EINTR` goes back to the top of the loop**, so signals are handled promptly.
- **One bad client can't kill the server.** Registration, MOD and I/O failures all go through `drop` or `conn_close`.

### 4.4 What the counters can and can't prove

| Question | Can the counters answer it? |
|---|---|
| Does mode X make fewer syscalls per message than mode Y? | **Yes**, `sc_ctl`, `sc_wait`, and the framework's recv/send counts show this exactly |
| Why is a mode cheaper or dearer in syscalls? | **Yes**, by the per-message arithmetic in Phases 2 and 3 |
| Which mode is faster? | **No.** Syscalls differ in cost: `epoll_wait` is amortised across ready connections, a failing `recv` is cheap, and `epoll_ctl` takes a lock |
| How much batching is happening? | **No**, because this file doesn't count events returned per wait (v3 did) |
| Idle versus busy wakeups | **Partly.** `sc_wait` includes timeouts, so on a quiet server it inflates |
| Tail latency and fairness | **No.** Measure them at the client (p50, p95, p99) |
| CPU use and context switches | **No.** Use `getrusage`, `/proc/<pid>/status` or `perf stat` |
| UDP loss | **No.** The server can't see drops; count at the client and read `/proc/net/snmp` |
| ET `EAGAIN` waste | **Only if** the framework counts failing `recv`/`send` separately. If it doesn't, derive it as `recv_calls − (useful recvs)` |

Two cautions:
- `loop_calls` equals `sc_wait` in this engine, so don't present both as independent evidence.
- ET's drain steps happen inside one loop call, so `loop_calls` understates the work per pass and can't be compared with an engine that does one step per pass.

For the final comparison, I'd add three things: events returned per `epoll_wait` (a histogram or the mean), failing-`recv`/`send` counts, and CPU time per request.

### 4.5 Viva questions for this engine

1. **Why can't the ET handler reuse the LT handler?** Because one step per event strands data, since no new edge arrives. I showed the silent hang in Phase 3.
2. **Why is the TCP listener level-triggered?** `accept_ready` accepts in a bounded batch. With ET, connections left in the queue would never be reported again.
3. **Why does `conn_step` exist alongside `conn_progress`?** ET needs to know when the kernel said `EAGAIN`, and `conn_progress` returns 0 for both progress and `EAGAIN`.
4. **What does `lt-direct` save, and where doesn't it help?** It saves two `epoll_ctl` calls and one wakeup per small message (measured 2 to 0, and 3 events to 2). It doesn't help large transfers or slow readers.
5. **Is `lt-direct` fair to compare against poll and select?** Only if the same try-write-first rule is applied to all engines, or it is labelled as a variant.
6. **Why are the event flags ignored?** The interest set matches what the app wants, so errors and hangups surface in the next `recv` or `send`. This keeps socket knowledge out of the engine.
7. **Why save `errno` before calling `drop`?** `drop` calls `epoll_ctl`, which may overwrite it.
8. **Is the `DEL` before `close` needed?** Usually not, because closing the last descriptor removes it from the set. It is explicit and safe, but it adds one `epoll_ctl` per connection to your count.
9. **Why does ET sometimes cost an extra `recv` per message?** The drain loop ends on a failing `recv`. I measured 299 against 200 in the single-connection run, and the count is timing-dependent.
10. **What is the risk of `data.ptr` holding a `conn_t*`?** Use-after-free if a connection is closed while a stale event for it is still in the batch. It is safe today because only a connection's own handler closes it.
11. **Why can UDP ET win under a flood and lose at low rates?** One failing receive is paid per burst under load, but per datagram when traffic is light.
12. **What can't your counters tell you?** Latency, CPU cost, fairness, and UDP loss (table in 4.4).

### 4.6 Fix list, in order

1. Replace the `result` assignment (finding 1). It takes one minute and removes a conditional bug.
2. Check `udp_drain`'s return contract (finding 3) and `accept_ready`'s (findings 1 and 4) in `framework.h`.
3. Reject conflicting mode flags, and print the effective mode (finding 2).
4. Confirm `SIGPIPE` handling and `ulimit -n` (findings 4 and 5).
5. Update the header comment to list all three modes, and decide the `DEL` and try-write-first policy before Phase 5.
6. Add the missing counters from 4.4.

### Phase 4 summary
The file is small and mostly right. Its real risks are all at the framework boundary: return-value contracts, signals and file-descriptor limits. The one confirmed code defect is the `result` leak, which is conditional on `accept_ready`'s contract. The counters are good evidence for syscall counts but not for speed, fairness, or loss, so the Phase 5 plan needs client-side timing and CPU measurement alongside them.

If you send `framework.h`, I can settle findings 1, 3, 4 and 5 definitively, and I can also write the four-phase analysis up as a single Word document if you want one.

Powered by Claude Exporter (https://www.ai-chat-exporter.net)
