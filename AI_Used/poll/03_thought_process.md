# 03. Thought Process — How AI Was Integrated Into the Workflow

## 2. Poll Implementation
- **Member:** Devvrat Hans (Roll No: 23110094)
- **Component:** `code/poll/`

### How AI was integrated
Codebuff (DeepSeek V4 Flash 07/31) was the only AI tool, used as a **reviewer/implementer, not the designer**. I decided behaviour first → Codebuff implemented/checked it → I built and tested before keeping anything.

I wrote parts myself — the event-loop skeleton, the accept-drain loop, and the shutdown/statistics block — and had Codebuff review them, just as I reviewed its drafts. I had full context on the component: every path traced, every counter understood, the whole server explainable end to end.

- **Me:** design, my own code above, decisions, verification.
- **AI:** code to my spec, reviews (including of my code), explanations.

### How I reviewed AI output
- Read every diff; traced paths by hand.
- Asked "explain it" prompts, then verified claims myself (e.g. `SA_RESTART` in `signal(7)`).
- Counters checked against the client, `strace`, and by reasoning. Nothing was accepted unverified.

### Engineering decisions
1. **Timeout `-1`** — the 1000 ms version woke every second when idle, inflating `poll()` counts with uptime; `-1` only wakes on real events.
2. **Shutdown counters** — CSV line (`poll_calls`, `accepts`, `reads`, `writes`, `messages`, `bytes`) gives exact numbers, cross-checkable with the client + `strace`.
3. **`sigaction()` with `sa_flags = 0`** — with an infinite timeout, exit relies on `EINTR`; `signal()` would request `SA_RESTART`.
4. **O(1) compaction** — swap last descriptor in on disconnect, no shifting.
5. **Delayed reply kept** — reply goes out one poll cycle after `POLLIN`; kept as a documented fairness caveat.

### Verification
- `signal(7)` confirmed the `EINTR` shutdown path.
- 10/20/200 connections: counters matched the client exactly.
- Idle 4 s: `poll_calls=1` — no timeout wakeups.
- Cross-checked with `strace -f -c` (Linux 6.8/glibc 2.36).
- Clean builds: `-Wall -Wextra -Wpedantic`, `-std=c17 -D_POSIX_C_SOURCE=200809L`, `clang --analyze`.

### AI errors caught
- `messages` counter claim narrowed (short reads can split requests).
- Shutdown race flagged only on 2nd review; self-pipe fix rejected as over-engineering, documented instead.
- "Stop watching `POLLIN` while replying" rejected — level-triggered `poll()` would spin.
- Swap-remove skipped a descriptor (index not stepped back); fixed before testing.