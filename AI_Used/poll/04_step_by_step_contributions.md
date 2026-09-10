# 04. Step-by-Step AI Contributions

## 2. Poll Implementation
- **Member:** Devvrat Hans (Roll No: 23110094)
- **Component:** `code/poll/`

> Each row: one AI contribution + how I verified it.

| Step | File | What AI contributed | How I verified it |
|------|------|---------------------|-------------------|
| 1 | `poll_echo_server.c` | Initial `poll()` echo server to my spec: non-blocking sockets, accept-drain loop, `POLLOUT` only when a reply is pending, O(1) swap-remove. | Read the code line by line and traced the paths by hand; built clean; echoed with `nc` from several terminals. |
| 2 | `poll_echo_server.c` | Edge-case fixes: unified close path, `EINTR` retries, index stepped back after swap-remove. | Traced by hand; churn test showed no fd leaks in `/proc/<pid>/fd`. |
| 3 | `poll_echo_server.c` | Reviewed my own code (event-loop rewrite, shutdown/statistics block); suggested the `STATS_HEADER` line. | Confirmed the logic myself against the man pages before accepting. |
| 4 | `poll_echo_server.c` | Timeout `1000` → `-1`; removed `ret == 0` branch. | Idle server 4 s → `poll_calls=1`. |
| 5 | `poll_echo_server.c` | `install_handler()` via `sigaction()` (`sa_flags = 0`) for `SIGINT`/`SIGTERM`/`SIGPIPE`; explained `SA_RESTART`. | Checked `signal(7)` myself; signals exit cleanly with the stats line, exit 0. |
| 6 | `poll_echo_server.c` | Six counters + `STATS_HEADER`/`STATS` shutdown output. | Matched the client at 10/20/200 connections; cross-checked with `strace -f -c`. |
| 7 | `README.md` | Drafted blocking behaviour, field meanings, limitations. | Re-checked every field against the code; corrected the `messages` claim. |
| 8 | — | Shell/Python load-test commands for the counters. | Scripts kept outside the repo; results reported here. |

### What I wrote myself (reviewed by AI)
- Event-loop skeleton + accept-drain loop (rewrote AI's first draft to my design).
- Shutdown/statistics CSV block and counter wiring.
- `Makefile`.

### Evidence recorded during testing

```
# 4 seconds idle, no clients (the 1000 ms version would show ~4 wakeups)
STATS,poll,1,0,0,0,0,0

# 200 connections x 20 messages x 512 bytes, batched client
STATS,poll,484,200,4000,4000,4000,2048000
  -> accepts, messages and bytes match the client exactly
  -> (484 + 4000 + 4000) / 4000 = 2.121 syscalls per message
```

### Decisions I made that the AI did not
- Event-loop design; interface constants (port 9090, 4 KiB buffer, `MAX_FDS` 1024); infinite timeout.
- Rejected the self-pipe trick (documented the shutdown race as a tradeoff instead).
- Kept the delayed reply (extra poll cycle per message) as a documented fairness caveat.
- `STATS,engine,...` format so all four servers' logs can be concatenated.

### Human contribution
Design, specs, tradeoff calls, the code I wrote myself above, builds, tests, and final acceptance were mine. I verified every AI change (and AI's reviews of my code) before keeping it — full context on the component, nothing accepted unverified. Codebuff (DeepSeek V4 Flash 07/31) was the only AI tool used.