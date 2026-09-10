# 01. Tools Used

## 2. Poll Implementation
- **Member:** Devvrat Hans (Roll No: 23110094)
- **Component:** `code/poll/`

### Tools & Models Used

**Codebuff (AI coding agent, accessed via Freebuff) — model: DeepSeek V4 Flash (build 07/31)**
- The **only** AI tool used for this component.
- *Usage scope:* writing `poll_echo_server.c` to my designs, reviewing the event loop, changing the `poll()` timeout to `-1`, adding the shutdown counters, drafting `README.md`, and the build/test commands used to validate the counters.

### Workflow
For every change: **I specified the behaviour → Codebuff implemented/reviewed it → I built, tested, and inspected the result → I accepted or rejected it.** The AI never made an unsupervised decision.

I also wrote code myself — the event-loop skeleton, the accept-drain loop, and the shutdown/statistics block — and had Codebuff review those parts too. I had full context on the component: I traced every path, knew what each counter meant, and can explain the server end to end.

### Not done by AI
- Event-loop design (non-blocking sockets, `POLLOUT` only when a reply is pending, O(1) compaction, infinite timeout); port/buffer/`MAX_FDS` choices.
- Writing the event-loop skeleton, accept-drain loop, and shutdown/statistics block (my own code, reviewed by AI).
- Running tests and accepting/rejecting each change; cross-checking counters against the client and `strace`.