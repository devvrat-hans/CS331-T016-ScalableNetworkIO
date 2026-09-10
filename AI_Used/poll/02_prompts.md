# 02. Prompts

## 2. Poll Implementation
- **Member:** Devvrat Hans (Roll No: 23110094)
- **Component:** `code/poll/`

> Prompts were written by me with the design and verification plan attached — never a blank "build this" task. I also wrote parts of the code myself and had the AI review them.

### Prompts

1. **Implementation spec** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "can u write a single threaded tcp echo server in c using poll() for io multiplexing. my design - port 9090, all sockets non blocking, drain the accept queue till EAGAIN, only arm POLLOUT when there is a pending reply to send, compact the fds array in O(1) by swapping the last entry when a client disconnects, handle EINTR and EAGAIN everywhere. should build clean with -Wall -Wextra -Wpedantic. also explain the non obvious parts so i can check them myself"
   - *Outcome:* Server per spec; I read it line-by-line, traced the paths, then rewrote the event-loop skeleton to my design before keeping it.

2. **Edge-case review** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "ok now review the event loop against the man pages - what happens on POLLHUP, zero byte read, EAGAIN on read/write, EINTR inside the accept loop. walk me through the disconnect path and the swap remove step by step, i want to trace it by hand. point out any path which could leak a fd or corrupt the fds array"
   - *Outcome:* Unified close path, `EINTR` retries, index stepped back; churn test showed no fd leaks.

3. **Review my own code** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "here's my rewrite of the event loop and the shutdown/stats block that i wrote myself. check the accept drain loop, the swap remove and the counter logic against the man pages, flag anything wrong"
   - *Outcome:* Review confirmed the logic; suggested printing a `STATS_HEADER` line alongside the values so logs are self-describing — I kept it.

4. **SO_REUSEADDR (my change)** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "btw i added SO_REUSEADDR on the listening socket myself, because when i restart the server quickly after a run i was getting bind: address already in use. i think that fixes it but confirm the option is being set right and that it won't hide anything"
   - *Outcome:* Confirmed the option is set before `bind()`; no downside for the benchmark — the server restarts cleanly between runs.

5. **Client array on the heap (my change)** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "also i moved the clients array to the heap with malloc instead of keeping it on the stack - 1024 clients with 4kb buffers each was too much stack space and i was worried about it. check the malloc path and that it gets freed properly on exit"
   - *Outcome:* Malloc path checked; `free(clients)` on every exit path confirmed; no leaks (matches the churn test).

6. **SIGPIPE (my change)** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "one more thing - during the churn test the server kept dying with a SIGPIPE when clients closed while data was still pending. so i ignored SIGPIPE with SIG_IGN and i handle write errors in the loop instead. confirm that's the right way and that i'm not missing an error path"
   - *Outcome:* SIGPIPE now ignored; write errors handled per-fd (retry on `EINTR`/`EAGAIN`, close on other errors); no more unexplained deaths in the churn test.

7. **Idle wakeups + counters** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "change the poll timeout from 1000 to -1 so it never wakes up when idle, remove the ret==0 branch now. then add counters for poll calls accepts reads writes messages bytes and print them as a csv line at shutdown"
   - *Follow-up:* "also don't add the self pipe trick for the shutdown race, that complicates the loop too much. just document it as a known tradeoff"
   - *Outcome:* Idle server → `poll_calls=1`; 200-connection counters matched the client.

8. **Shutdown path** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "why does signal() restart poll() on glibc but sigaction() with sa_flags=0 not? quote signal(7)/sigaction(2) so i can verify myself, and trace the EINTR path when SIGTERM comes"
   - *Outcome:* Confirmed `SA_RESTART` claim; signals exit cleanly, exit 0.

9. **Cross-check README claims** — *Codebuff (DeepSeek V4 Flash 07/31)*
   - *Prompt:* "i've finished reviewing my part against context.md. now cross check these specific readme claims against the code - the stats field meanings, the (poll_calls+reads+writes)/messages formula and the limitations list. flag anything that doesn't match the implementation"
   - *Outcome:* Claims matched the code; I tightened the `messages` wording (only exact when no short read splits a request) and added the shutdown race to the limitations after my own second pass.