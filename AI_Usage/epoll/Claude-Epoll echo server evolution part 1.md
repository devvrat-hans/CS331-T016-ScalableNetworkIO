# Epoll echo server evolution

## User:

> File: epoll_echo_server_v1.c

> File: epoll_server.c

I’ve attached my epoll echo server (v1). I want to evolve it into a standalone single-threaded workload server that will sit beside three other engines with identical application behavior. It must compile as one C file with no extra headers. First, don’t change anything: summarize what v1 does and list what you’d need to change to support echo, upload and download workloads.

## Assistant:

> Listing protocol and structural changes for the new workloads.
>
> - Listing protocol and structural changes for the new workloads.
> - **Done**

## What v1 does

v1 is a single-threaded, level-triggered epoll TCP echo server.

- **Setup:** It takes an optional positional port (default 9090), binds to `INADDR_ANY`, and registers the listener in epoll with a stack-allocated `conn_t` as `data.ptr`.
- **Accepting:** On a listener event it drains up to 64 connections with `accept4(SOCK_NONBLOCK)`. It keeps a spare `/dev/null` fd so that on `EMFILE`/`ENFILE` it can accept and drop instead of spinning.
- **Per-connection state:** Each client is a heap-allocated `conn_t` with a 4 KB buffer and `bytes_to_write`/`write_pos`. Clients sit in a doubly-linked list for O(1) removal and shutdown cleanup.
- **Echo logic:** On `EPOLLIN`, it does one `read()` if nothing is pending, then writes that exact chunk back. If the write is partial, it switches the interest set to `EPOLLOUT` with `EPOLL_CTL_MOD` and back to `EPOLLIN` once drained. `peer_closed` is set only when `read()` returns 0.
- **Shutdown:** It stops on SIGINT/SIGTERM, then prints totals and a `CSV,epoll,...` line with syscall counters.

## Things in v1 worth knowing

- **No framing:** A "message" is whatever one `read()` returned, which isn't a stable unit across engines.
- **`EPOLLRDHUP` busy-loop:** It's in the mask but never used. If a peer half-closes while a write is blocked, it fires on every wait and spins.
- **Blocking `epoll_wait`:** The `-1` timeout means a signal arriving just before the call can delay shutdown.
- **Write loop:** It writes until `EAGAIN`, which would let one large download monopolize the loop.

## What to change for echo, upload and download

1. **Wire protocol:** Add an 8-byte big-endian length header, then the payload. Echo caps at 4096 bytes and must read the whole payload before echoing it back. Upload receives N bytes and replies with a 1-byte ack. Download sends N bytes in response to the header.
2. **Verifiable data:** Uploads and downloads use a deterministic pattern (`(offset % 251)`). The server checks uploads before acking and generates downloads on the fly. Cap N (e.g. 64 MiB), and reject 0 and oversize values.
3. **State machine:** Replace `bytes_to_write`/`write_pos` with explicit stages (HEADER, RECEIVE, SEND, ACK) plus `done`/`want` for the current chunk and `total`/`left` for the request. This lets multi-GB-scale transfers stream through the fixed 4 KB buffer.
4. **Direction-driven interest:** Arm `EPOLLIN` when the stage reads and `EPOLLOUT` when it sends, calling `EPOLL_CTL_MOD` only on a direction change. Drop `EPOLLRDHUP`.
5. **One I/O step per event:** Do a single `recv`/`send` per wakeup for fairness, using `MSG_NOSIGNAL` and `TCP_NODELAY`.
6. **Close semantics:** A clean EOF is valid only at a header boundary. Otherwise it counts as an incomplete or error close.
7. **Limits and counters:** Add `--max-clients` with a rejected count, and track accepted/closed/active/peak, errors, incomplete, messages, and bytes read/written.
8. **CLI and output:** Replace the positional port with `--port`, `--bind`, `--app echo|upload|download` and `--max-clients`. Print a `READY <port>` line (using `getsockname`), and emit a JSON summary instead of the CSV row, including elapsed time, CPU time and peak RSS.
9. **Loop hygiene:** Use a finite `epoll_wait` timeout (about 100 ms) so shutdown is prompt, and handle SIGINT/SIGTERM without `SA_RESTART`.
10. **Single file:** Everything is already in one file, so the only extra step is adding `<netinet/tcp.h>`, `<arpa/inet.h>`, `<sys/resource.h>` and `<time.h>`.

## One thing to confirm

I inferred the protocol details (header format, `% 251` pattern, `'K'` ack, 64 MiB cap) from your second upload, `epoll_server.c`. That file already looks like the evolved version of v1, and its extra fields (`ready_next`, `pending`) suggest a shared struct with the io_uring engine. Is that your target spec, or a draft you want me to review? I can diff it against v1 and audit it for bugs next.

## User:

```text
actual transcript:

System overhead is there, waiting on the system call after system call, so we should have implemented the IO routing part more correctly. So we can improve on the IO routing part.

So you have three approaches that you have compared...
Four.
Four approaches. Select, poll, epoll, and io_uring.

And of course your test setup... I said your architecture itself is wrong. That is a primary problem that I have. I think your entire setup of trying to do the testing is not having anything about architecture. I asked, explain to me the architecture. None of you answered. None of you asked me that what is it that you are expecting as an architecture.

I don't remember who asked that what is the relation of that to TCP... that I asked the question why TCP. What is the relation of that to this particular... that was not answered. So all of these were pointing towards lack of understanding of what you are trying to compare, how you should be comparing, what you should look at.

And anything on the literature that you read in this space? You are not the first one to be doing this. So the literatures that you have read... what should I be ready with? Nothing was wrong. That is exactly what I call as the gap in understanding the problem itself.

Even given the problem, that's why I asked in the first slide, put problem statement and what you derived as the objective. That itself shows whether you have understood the problem currently. So that's where I made these things. And I feel there is a lot to be done with respect to the understanding, with respect to evaluation.

First, what is it that people have done in this space? That is something you have to do. Maybe they have not compared all four. They have compared only io_uring with select or something. Okay, how this is evaluating? And what is it that you will have to do? This is a network inference. What are the aspects of networking that you will have to consider?

It's not one application that you can run, let's say that I'm done with the application. This is... I may have network applications like when you do operating system project this time. Is it optional? Optional. But if you had, what are the things that you will look for as a part of operating systems? Memory management, scheduling, concurrency, IO management... all of things. And didn't all of these actually affect the way the whole testing is happening? The way the OS component is only... right? Under the hood there is a networking application that is running on top and transferring. But what is it that is happening? This is all the OS construct.

So what should be your way of building? I build network applications that are traffic intensive. I build network applications that are memory intensive. I build network like... I have WhatsApp. Nothing to do with memory, it's all small messages that keep going, but they are latency sensitive. File transfer. I do file transfer, these are hugely IO sensitive, a lot of IO that has to happen and then memory. Or I can have data that is continuously streaming, continuous memory.

So these are the applications under which the operation of each of these will differ. The number of times that interrupts are going to be raised will change, the wakeups that happen will change. So these are the characteristics that you have to first find out in networking applications, then analyze. Just making this one small relay and saying okay this is all that is not wrong. You have to understand what kind of networking applications you are trying to cater to. One will work good for some networking application, another will work good for another. Not that everything will work good for everything.

And that is what is the point of the element. So you have to analyze more from systems point of view than your EC specific. That would do justice to the project, otherwise you will learn more or anything will happen from the point of view of... the inference that you are drawing is also misguided.

So that is what I expect that you review. Understand different network applications. For that you have to do first literature on what people have mapped and how networking applications are actually compared. Then apply the same principle and build in this.

The baseline code is there, that's all. You don't have proper applications, you don't have proper instrumentation, and the tools with which you will write this. All the three are important. So that is what you should be focusing on and then...

Sir and I want to clear up things. Since we have implemented for a thread, means everything will work on thread. So do we have to implement for multiple thread or a single thread?

One thread uses a select call. The blocker is just one thread. If there are multiple threads you are making a select call, then which is returning on the same call... must be different. Then if you scale the thread, there will be an impact. But at least for one thread first you have to understand okay what is happening. That depends on the kind of activity that thread is doing. And that is what I mentioned. It will change.

How many times okay the baseline first is I do nothing in the thread. Thread is just going up, takes, comes back, takes, comes back. And that is what will tell you what is the top operating frequency at which we will be able to read and write. Go in database, we do this analysis. That is the same here, but everything that you are doing. And now I scale the thread. File descriptors are another dimension. How many you are going to do the read, how many you are going to do the write. Each API has a different way of saying okay select FD is what are the FD using standard source.

So if I am read intensive, I will have more reads but write less. Which one is better? That will change. And that is exactly the kind of framework that you have to build. That becomes your architecture. Say okay how am I building this framework, what am I trying to assess, and you change applications whichever we want is a map over your baseline framework. So the framework has to be defined first. And that is what I was asking from how have you architected this string. Once you architect this framework, network applications are just the plugins.

You have to think through this right and then...

One question you asked, why did you all use TCP? So like why TCP, is it if any of this do anything with just TCP? That was exactly the question I brought. TCP queue is used... and you have TCP relay. Echo... that is exactly the relay that happens... Does this mean that it will not work for UDP? No right, it should also work for UDP. So what is the particular building of the application?

I think I thought it was in the project idea statement. In the project, you need to build some network application. Project is not saying do only TCP, project is not saying do relay. Compare all select, poll, epoll, io_uring.

But for testing we have to like have a system on which we test, so we implemented on TCP, so is there any problem with that? Will your solution not work for UDP? Answer this. Networking applications are just the plugins. Networking application has nothing to do with how your testing is essentially trying to build. That's why I said your framework is not there, you have not been able to figure out, you have hooked your networking application with the framework. It should not be the case. First build a framework. And then networking application is just a plugin that you say okay I keep running networking applications but still the same thing works. Right now you have married both your TCP echo with distinguishing from one and the other.

At least do all of you get the understanding now of okay what is the difference between a select, poll, epoll... not with respect to the API. With respect to how the API is executed on the server. I asked I think I have a final pointer, signal. Nobody was able to answer, have you used a signal or not. Signal case is there...

Lord GPT or whatever tools that you use, Copilot, they will generate you the code, but if you don't understand the code what is the point? At least you have to understand how the things work, what are the ways that you are comparing, why is there a difference. What difference is it bringing, for what purpose, underline what is changing.
```

Replace the positional [port] argument with options: `--port`, `--bind` (default 127.0.0.1, numeric IPv4 only, via inet_pton), `--app echo|upload|download`, `--max-clients` (default 10000, cap 100000), and `--help`. Use a small `number()` helper that rejects empty strings, negatives and overflow. Exit code 2 for bad usage.

Attached the expectation and conversation with sir

## Assistant:

> Editing the includes, helper functions, and argument parsing.
>
> - Editing the includes, helper functions, and argument parsing.
> - **Done**

I'll apply this to v1 as a new file so your original stays intact, then compile and test it.

> Checking where the system message came from.
>
> - Copy v1 to a writable v2 file
> - Add arpa/inet.h for inet_pton
> - Add limits defines
> - Add number() and print_usage helpers, new option variables at top of main
> - Replace positional port parsing with option parsing
> - Wire bind_ip and banner, compile with warnings, test CLI edge cases
> - Checking where the system message came from.
> - **Done**

Quick status: the CLI change builds cleanly and all the edge cases behave, so I'm handing you the file now.

> Presented file
>
> - Presenting file(s)...
> - **Done**

I applied the option parsing to v1 and saved it as `epoll_echo_server_v2.c`; your original is untouched. It compiles with no warnings under `-Wall -Wextra`.

**What changed**
- **Options:** `--port`, `--bind`, `--app echo|upload|download`, `--max-clients` and `--help` replace the positional port. `--help` prints usage to stdout and exits 0.
- **`number()`:** The first character must be a digit, so empty strings, `-1`, `+80` and leading whitespace are rejected. It also rejects trailing junk, overflow (`ERANGE`) and values above the limit. I made it stricter than a plain `strtoul` check because `strtoul` silently accepts `+` and whitespace and wraps negatives.
- **`--bind`:** It defaults to `127.0.0.1` and is validated with `inet_pton`, so there is no DNS lookup. It now feeds `bind()` instead of `INADDR_ANY`.
- **Limits:** `--port` takes 1–65535, and `--max-clients` takes 1–100000 with a default of 10000.
- **Exit codes:** Every bad-usage case exits 2 with a message on stderr. I tested 15 bad inputs, including a missing value, an unknown flag, the old `9090` positional form and a 23-digit overflow, and all returned 2. Valid runs started and shut down cleanly.

**Behaviour you should know about**
- **Default bind:** The server now listens on loopback only, where v1 listened on all interfaces. Anything off-box needs `--bind 0.0.0.0`.
- **`--app`:** `upload` and `download` are parsed and validated, but the server still echoes. It prints a warning to stderr so a benchmark run can't silently measure the wrong workload.
- **`--max-clients`:** It is parsed and shown in the startup banner, but not enforced yet. Enforcing it needs the `rejected` counter and a check in the accept loop.
- **Port 0:** It is rejected, as in v1. Allowing it would need `getsockname` so the banner and a future `READY` line report the real port.

On the transcript: your professor's main point is that the framework should come first and the applications should be plugins. `--app` is a good seam for that, but right now the echo logic is still hard-wired into the event loop. The next step that would address it is the protocol state machine, with the workloads behind it. I can do that next if you want.

> File: [/mnt/user-data/outputs/epoll_echo_server_v2.c](https://claude.ai/api/organizations/c9319657-36fb-4b99-aa1c-19680c09f81e/conversations/9511bcca-0f67-4fe4-b0c4-7a0ac658bbd3/wiggle/download-file?path=%2Fmnt%2Fuser-data%2Foutputs%2Fepoll_echo_server_v2.c)

## User:

Wait

Give all set of steps we need to follow the final sir's expectation
List out
Then we will deep down in each part and write code

## Assistant:

> Checking the transcript for the steps the professor expects.
>
> - Checking the transcript for the steps the professor expects.
> - **Done**

Here's the full set of steps, grouped by what your professor asked for. Nothing here is code yet.

## Phase 1: Show you understand the problem
1. **Problem statement and objective.** Write the first slide properly. The question is which I/O multiplexing mechanism (select, poll, epoll, io_uring) suits which kind of network workload, and why. It is not "which is fastest".
2. **Literature review.** Find what has been done: which mechanisms were compared, with what workloads, with what metrics, and where the gaps are. Prepare to say what prior work did and how yours differs, since he expects you to be ready for this.
3. **Why TCP?** Prepare a clear answer on whether the framework would also work for UDP. TCP should be one transport plugin, not baked into the framework.

## Phase 2: Understand the workloads
4. **Classify network applications** by what they stress:
   - latency-sensitive small messages (chat-style)
   - bulk file transfer (I/O heavy)
   - continuous streaming (memory heavy)
   - read-heavy versus write-heavy mixes
5. **Map each class to OS effects.** For each class, predict how syscalls, wakeups, interrupts, context switches and memory behaviour differ. These become your hypotheses.
6. **Choose representative workloads** from that classification. Echo, upload and download are one candidate set, but you must justify why they cover the classes.

## Phase 3: Understand the mechanisms (kernel side, not API)
7. **How each mechanism executes in the kernel.** Cover select/poll scanning the fd set on every call versus epoll's ready list versus io_uring's shared submission/completion rings.
8. **OS lens for each.** Cover scheduling, concurrency, memory management and I/O management.
9. **Signals.** Be able to say whether you use signals, how SIGINT/SIGTERM are handled (including `SA_RESTART` and `EINTR`), and why SIGPIPE is ignored or suppressed.

## Phase 4: Architecture (framework first, apps as plugins)
10. **Draw the architecture.** Show these layers and their interfaces:
    - **workload generator:** clients, load shapes
    - **application plugin:** echo, upload, download, and later anything else
    - **transport layer:** TCP now, UDP possible
    - **I/O engine:** select, poll, epoll, io_uring
    - **instrumentation:** counters, `perf`, `/proc`
11. **Define the plugin interface.** The app must not know which engine drives it, and the engine must not know what the app does.
12. **Decouple what is currently married.** Today `epoll_server.c` keeps the app logic (`client_complete`) separate from the loop, but it still calls `recv`/`send` and sets `TCP_NODELAY` directly. That is the coupling to remove or isolate.

## Phase 5: Experiment design
13. **Baseline: do nothing.** Measure the empty loop (wait, return, wait, return) to get the maximum wake-up and read/write rate per mechanism.
14. **Single thread first.** Understand one thread fully before scaling.
15. **Dimensions to vary, one at a time:**
    - number of fds (idle versus active)
    - read/write ratio
    - message size
    - then thread count
16. **Control variables.** Fix the application behaviour, buffer sizes, CPU pinning, machine state and loopback versus real NIC, and repeat runs for variance.

## Phase 6: Instrumentation
17. **Metrics to collect.** Throughput, latency percentiles, syscalls per message, wakeups, context switches (voluntary and involuntary), CPU user/system split, and peak memory.
18. **External tools.** Use `perf stat`, `strace -c`, `/proc/self/status` and `/proc/interrupts`. Decide what the server counts internally and what outside tools measure, so the two can cross-check each other.
19. **Same output schema for all four engines** so results compare directly.

## Phase 7: Implementation
20. **Finish the shared application layer** (protocol, verification, counters) with the CLI contract identical across engines.
21. **Implement each engine** against the same interface.
22. **Correctness checks.** Upload verification, download verification, and clean EOF versus incomplete-close accounting.
23. **Client and load generator** that drives all workloads and is independent of the server engine.

## Phase 8: Analysis and defence
24. **Run the matrix** (mechanism × workload × fd count × threads) and tabulate.
25. **Explain every difference** with the kernel mechanism, not just the number.
26. **Draw conclusions per workload.** No single winner is claimed. State which mechanism suits which workload, and where the results contradict the hypotheses from step 5.
27. **State limitations.** Cover loopback, one machine, one kernel version and single-thread scope.
28. **Viva prep.** You must be able to explain every line of code, since he specifically warned against unexplained generated code.

## Suggested order for the deep dives
Phases 1–2 shape everything, so they should come first. Phase 4 (architecture) is what he said was most wrong, so I would do it next. Phases 5–6 follow, and Phase 7 is where the code gets written.

Which part do you want to go deep on first? My suggestion is the architecture in step 10, because the code structure depends on it.

## User:

Let's start with part 1

## Assistant:

> Searching for real literature on kernel scalability.
>
> - Searching for real literature on kernel scalability.
> - **Done**

Part 1 has three pieces: the problem statement and objective, the literature review, and the "why TCP" answer. The literature review needs real citations, so I'll verify those first rather than rely on memory.

> Searched the web
>
> - Comparing and evaluating epoll, select, and poll event mechanisms Gammo Ottawa Linux Symposium 2004
> - **Done**

That confirms the select/poll/epoll side. Next I'll check the io_uring side.

> Comparing I/O mechanisms in published studies and their workloads.
>
> - io_uring vs epoll network server performance evaluation paper
> - Comparing I/O mechanisms in published studies and their workloads.
> - **Done**

Here is Part 1 in three pieces. The literature below is what I could verify just now. I mark anything I'm citing from memory so you can check it before it goes on a slide.

## 1. Problem statement and objective

Your professor's complaint was that the slide didn't show you understood the problem. A comparison of four mechanisms stated as "which is fastest?" is the wrong framing. Here is a draft:

**Problem.** Linux offers four ways for a single thread to wait on many file descriptors: select, poll, epoll and io_uring. They differ in how the kernel finds ready descriptors, how often the application crosses into the kernel, and who performs the actual read or write. Which one is best depends on the workload. Small latency-sensitive messages, bulk file transfer and streaming stress different parts of the OS, and a benchmark built around one application can't show that.

**Objective.** Build a workload-independent framework and use it to measure how each mechanism behaves, at the OS level, under different application classes, descriptor counts and read/write mixes. The goal is to explain why they differ, not to name one winner.

**Research questions you can defend:**
- How does cost per event scale with the number of descriptors, and does it matter whether they are active or idle?
- Where does the crossover between mechanisms sit as per-message work grows from tiny echo to large transfers?
- How much of the difference is syscall count and batching, and how much is kernel work that no mechanism removes?

## 2. Literature review

**What I verified:**

| Work | What it did | Why it matters to you |
|---|---|---|
| Gammo, Brecht et al., OLS 2004 | Compared select, poll and epoll in one web server. It found select and poll comparable to epoll when there are no idle connections. | It is the closest ancestor of your project, and its warning is that results depend on the workload. |
| Brecht's preliminary epoll notes | An early epoll implementation made too many `epoll_ctl` calls, and that syscall is expensive. | This applies directly to v1, which calls `EPOLL_CTL_MOD` on every direction change. |
| IPSJ journal paper, 2008 | Implemented multi-process, multi-thread, select, poll and epoll as modules for a fair comparison. It found select and poll had no advantage, and that multi-process and multi-thread servers came close to epoll. | Its modular design is the same idea as your professor's "framework with plugins". |
| Jasny et al., "High-Performance DBMSs with io_uring" (arXiv 2512.04859) | For network shuffling with large 1 MiB chunks, epoll is only marginally slower than io_uring, because the chunk size amortizes syscall costs. With zero-copy send io_uring is much better, and zero-copy receive, which only io_uring supports, gives up to 2.5×. | Message size changes the answer, which supports your workload-classes argument. |
| OpenAnolis / Alibaba analysis | It models epoll's per-operation cost as syscall transition plus kernel work, and io_uring's as ring overhead plus transition, divided by batch size. At 1000 connections they measured io_uring about 10% above epoll in throughput. | It gives you an analytical model to test your measurements against. |
| Afsharian, UringCL thesis, Waterloo 2026 | Reports up to 40% higher throughput than epoll in bulk-transfer workloads, attributed to batching. | It is recent and agrees with the model above. |

**From memory, so verify these before citing:**
- Banga, Mogul and Druschel, "A Scalable and Explicit Event Delivery Mechanism for UNIX" (USENIX 1999). A search snippet describes it as showing a mechanism whose cost doesn't depend on the number of descriptors.
- Provos and Lever, "Scalable Network I/O in Linux" (2000). A snippet says it compares `/dev/poll` against RT signals.
- Kegel's C10K page, which is the usual motivation for epoll.
- Axboe's "Efficient IO with io_uring" paper, which is the primary source for the ring design.

**The gap, as far as I found.** The papers above compare two or three mechanisms, mostly inside a web server or an echo loop, and mostly report throughput. I found no study that puts all four behind one framework and varies workload class, descriptor count and read/write mix while collecting OS-level counters. I can't prove none exists, so on the slide say "in the literature I surveyed". The io_uring-versus-epoll debate also appears to be unsettled. The OpenAnolis authors say the argument has been running in the io_uring community for years. That is a useful opening: the answer depends on conditions, and your framework measures the conditions.

## 3. Why TCP?

This was the question nobody answered. The answer has two parts.

**The mechanisms are transport-agnostic.** select, poll, epoll and io_uring work on file descriptors, and a UDP socket, a pipe or an eventfd is just another one. So the comparison does not depend on TCP.

**TCP is not neutral, though, and you should say so.**
- TCP is a byte stream. That is why your protocol needs the 8-byte length header and why partial reads and writes exist. UDP keeps message boundaries, so those problems disappear.
- A TCP server has one fd per connection. UDP can serve many peers from one socket. So "number of descriptors", which is a key dimension for select and poll, is easy to scale with TCP and awkward with UDP.
- TCP gives backpressure through the send buffer. This drives the `EPOLLOUT` and `EAGAIN` behaviour you've already built.

**Suggested answer for the viva:** "The mechanisms work on any fd, so UDP would work. We used TCP as one transport plugin because per-connection fds make descriptor count easy to vary and because stream semantics exercise partial I/O and backpressure. The framework should treat the transport as replaceable, and our current code doesn't yet."

That last sentence is true. In `epoll_server.c` the `recv`, `send` and `TCP_NODELAY` calls are still baked into the shared helpers. That is the coupling he spotted, and it's where Part 4 starts.

## What Part 1 should produce

1. One slide with the problem statement, objective and three research questions.
2. A literature table like the one above, with the unverified papers checked.
3. A one-paragraph "why TCP" answer plus the UDP caveats.

I can draft the slide text next, or move on to Part 2 and classify the workloads. Which do you prefer?

---

**Sources:**

- [Note - we've now conducted a more complete study. See :](https://cs.uwaterloo.ca/~brecht/servers/epoll)
- [ols2004v1 pages 215 226](https://www.kernel.org/doc/ols/2004/ols2004v1-pages-215-226.pdf)
- [Comparing and Evaluating epoll, select, and poll Event Mechanisms](https://kharkovconcurrencygroup.blogspot.com/2010/04/some-linux-io-papers.html)
- [zhwiki.oracleblog.org](https://zhwiki.oracleblog.org/zh-hant/Lighttpd)
- [zhwiki.oracleblog.org](https://zhwiki.oracleblog.org/zh-tw/Lighttpd)
- [Information and Media Technologies](https://www.jstage.jst.go.jp/article/imt/3/2/3_2_272/_article/-char/en)
- [scitepress.org](https://scitepress.org/publishedPapers/2022/119497/pdf/index.html)
- [zhwiki.oracleblog.org](https://zhwiki.oracleblog.org/wiki/Lighttpd)
- [select, poll, and epoll: What’s the real difference? A deep dive into I/O multiplexing](https://www.besthub.dev/articles/select-poll-and-epoll-what-s-the-real-difference-a-deep-dive-into-i-o-multiplexing-28735b19be75)
- [zh.wikipedia.com](https://zh.wikipedia.com/wiki/Lighttpd)
- [High-Performance DBMSs with io_uring: When and How to use it](https://arxiv.org/pdf/2512.04859)
- [io_uring vs epoll ,谁在网络编程领域更胜一筹?](https://developer.aliyun.com/article/834974)
- [512b320f bcf4 469a 9f30 f6bd2999bd23](https://uwspace.uwaterloo.ca/signposting/describedby/512b320f-bcf4-469a-9f30-f6bd2999bd23)
- [masters thesis presentation systems and networking uringcl](https://cs.uwaterloo.ca/events/masters-thesis-presentation-systems-and-networking-uringcl)
- [masters thesis presentation systems and networking uringcl](https://uwaterloo.ca/computer-science/events/masters-thesis-presentation-systems-and-networking-uringcl)
- [UringCL: A Lightweight io_uring Convergence Layer for Adoption in Legacy Event Loops](https://uwspace.uwaterloo.ca/items/512b320f-bcf4-469a-9f30-f6bd2999bd23)
- [UringCL: A Lightweight io_uring Convergence Layer for Adoption in Legacy Event Loops](https://uwspace.uwaterloo.ca/items/512b320f-bcf4-469a-9f30-f6bd2999bd23/full)
- [alibabacloud.com](https://alibabacloud.com/blog/599544)
- [io uring vs  epoll which is better in network programming 599544](https://www.alibabacloud.com/blog/io-uring-vs--epoll-which-is-better-in-network-programming_599544)
- [Does Netty’s io_uring Make the 2× CPU Thread Rule Obsolete?](https://www.besthub.dev/articles/does-netty-s-io-uring-make-the-2-cpu-thread-rule-obsolete-3358ba3ca620)

## User:

thanks, part 1 is nice. the why TCP part i understood half only. u said our code is not transport replaceable because recv/send and TCP_NODELAY is in the shared helpers. what i have to do to fix this, simple way? i will ask in part 4 maybe. for now please start part 2

## Assistant:

> Mapping the workload classes onto the three apps.
>
> - Mapping the workload classes onto the three apps.
> - **Done**

## Quick answer: making the transport replaceable

Right now the code mixes two jobs: the **application** (read a header, check the data, decide what to send) and the **transport** (how bytes move: `accept4`, `TCP_NODELAY`, `recv`, `send`). The fix is to move the transport calls behind three small functions:

- `transport_accept()` creates a client connection, including the `TCP_NODELAY` setting
- `transport_read(client, buf, len)` wraps `recv`
- `transport_write(client, buf, len)` wraps `send`

The app logic would call only these. To change transport, you change these three functions and nothing else. One honest limit: this makes TCP *replaceable in code shape*. UDP would still need more work, because it has no per-client connection and no partial reads. We'll sort that out in Part 4.

## Part 2: Understanding the workloads

Your professor said the mechanisms behave differently for different kinds of applications, so this part answers three questions: what kinds exist, what each one stresses in the OS, and which ones you test.

### Step 4: Classes of network applications

These come straight from his examples:

| Class | Example | What it looks like |
|---|---|---|
| Small, latency-sensitive messages | chat (his WhatsApp example) | many tiny messages, each must be answered quickly |
| Bulk transfer, read-heavy server | file upload | server mostly receives |
| Bulk transfer, write-heavy server | file download | server mostly sends |
| Continuous streaming | video or sensor feed | long-lived connections, steady data over time |

### Which class each of your three apps covers

- **echo** covers small messages, and it is symmetric (one read, one write).
- **upload** covers read-heavy bulk. The server receives N bytes and sends back one byte.
- **download** covers write-heavy bulk.
- **Streaming is not covered.** A 64 MiB download is a big burst, not a steady paced stream. Either add a paced mode later or tell the professor this is a known limit. I'd choose to state the limit.

### Step 5: What each class does to the OS

These are **hypotheses to test**, not results. I'm predicting from how the mechanisms work.

**Small messages (echo).** The fixed cost per message dominates. At low load that is roughly one `epoll_wait`, one `recv` and one `send` per message. When many connections are ready at once, `epoll_wait` cost is shared across them. I expect the mechanism's *waiting* cost to matter most here.

**Bulk transfer (upload/download).** Your buffer is 4096 bytes and each event does one `recv` or `send`, so a 64 MiB transfer takes at least 16,384 chunks and the syscall count is large. Per-byte cost is mostly data copying plus these repeated syscalls. io_uring's batching could help here, because the literature suggests batching is where it gains, but that is only a prediction until you measure it.

**Read-heavy versus write-heavy.** Download needs `EPOLLOUT`. With level-triggered epoll, a socket with free send-buffer space is reported writable again and again, so the server wakes up very often. Upload mostly waits for data to arrive. I expect different wakeup counts for the two, even with the same bytes moved.

**Many connections.** select and poll scan the whole descriptor set on every call, so I expect their cost to grow with connection count even when most connections are idle. epoll and io_uring should not. One practical problem: **select on Linux is limited to 1024 descriptors by default.** Your 10,000-client test cannot run on select, so the framework should report "unsupported at this size" instead of crashing or giving a wrong number.

### What to measure for each class

| Class | Main thing to watch |
|---|---|
| Small messages | latency percentiles, syscalls per message, wakeups per second |
| Upload | syscalls per MiB, bytes/s, CPU system time |
| Download | wakeups per MiB, `EAGAIN` count, bytes/s |
| Many connections | cost per event as connection count grows |

### Step 6: A proposed set of experiments

This is my suggestion. Please confirm or change it before we build anything.

- **echo:** message sizes 64 B, 1 KB and 4 KB
- **upload and download:** transfer sizes 64 KB, 1 MB and 16 MB
- **connections:** 1, 10, 100, 1,000 and 10,000, with select skipped above its limit
- **active versus idle:** for each connection count, run once with all connections active and once with about 10% active
- **threads:** one thread only, as your professor asked

One thing to settle before this goes on a slide: the **client** matters as much as the server. The load generator has to be fast enough that it isn't the bottleneck, and it has to use the same behaviour against all four engines.

**Does this set of experiments look right to you, or should we cut or change anything before I go on to Part 3 (how each mechanism works inside the kernel)?**

## User:

Okk,
Good now we will follow to the next Phase, If I get doubt I will reach you

## Assistant:

> Tracing how io_uring hands off completions to the kernel.
>
> - Tracing how io_uring hands off completions to the kernel.
> - **Done**

## Part 3: How each mechanism works inside the kernel

This covers steps 7 to 9: how the kernel executes each mechanism, what that means for the OS, and how signals fit in. It's the part your professor stressed with "not the API, how it is executed on the server".

### Step 7: What the kernel does on each call

**select and poll.** Every call hands the kernel your whole interest set.
1. The kernel copies the set in from user space.
2. For each descriptor it asks the underlying socket "are you ready?", and if none are ready it hooks the thread onto that socket's wait queue.
3. The thread sleeps. When any socket gets data, it wakes the thread.
4. The kernel walks the set again, builds the result and copies it back out. You then scan the result to find the ready ones.

So the cost per call grows with the **size of the set**, not the number of ready descriptors, and the set is rebuilt every time. The two differ mainly in representation. select uses a bitmap capped at 1024 descriptors (`FD_SETSIZE`). poll uses an array with no such cap, but it has the same linear walk.

**epoll.** The interest set lives **inside the kernel**, and you register descriptors once with `epoll_ctl`.
1. At registration the kernel attaches a callback to the socket's wait queue.
2. When data arrives, the callback runs at that moment and puts the descriptor on a **ready list**.
3. `epoll_wait` just returns what is on that list, so its cost follows the number of ready descriptors.
4. In level-triggered mode (what you use), a descriptor that is still ready gets put back on the list after you collect it.

The trade-off is that interest changes cost a syscall each. That is the `epoll_ctl` cost the 2004 paper pointed at, and it is why v1 switching between `EPOLLIN` and `EPOLLOUT` with `EPOLL_CTL_MOD` is worth counting.

**io_uring.** This is a different model. epoll tells you a socket is **ready**, and then you still make the `recv` or `send` syscall yourself. With io_uring you submit the **operation itself** and are told when it **completed**.
1. You write requests (submission entries) into a ring buffer that is shared memory between you and the kernel.
2. One `io_uring_enter` call can submit many requests and optionally wait for completions.
3. The kernel tries each operation immediately. If a socket isn't ready, the kernel waits on it internally and finishes the operation later.
4. Results appear in a second shared ring, which you read without a syscall.

The saving comes from **batching**: many operations per kernel crossing. This matches the cost model in Part 1, where the per-op cost is divided by the batch size. At a batch of one, io_uring has no advantage.

| | Set handed over | Cost per wait | Who does the read/write | Syscalls per message (typical) |
|---|---|---|---|---|
| select / poll | every call | grows with set size | you, after being told ready | wait + read + write |
| epoll | once | grows with ready count | you, after being told ready | wait + read + write (+ ctl on changes) |
| io_uring | per operation, batched | amortized by batch | the kernel | fewer when batched |

Details like internal polling, multishot and submission-polling threads vary by kernel version, so before the slides I'd confirm what your test machine's kernel supports.

### Step 8: The OS lens

- **Scheduling:** all four put the thread to sleep when nothing is ready. The cost you measure is the sleep and wakeup cycle plus a context switch. At low load the loop sleeps often; at high load it rarely does.
- **Concurrency:** you are single-threaded, so there are no locks in your code. Inside the kernel there is still the wait-queue and ready-list machinery. Scaling threads later changes this, for example several threads waiting on one epoll descriptor.
- **Memory:** select and poll copy the set on every call. epoll keeps a kernel record for each registered descriptor. io_uring uses shared ring memory that is set up once. Your own cost is the 4 KB buffer per client.
- **I/O management:** data arrives through interrupt handling, then the network stack, then the socket buffer, then a wakeup, then a copy to user space. Mechanisms differ in what happens after the wakeup.

One trap for the interrupts point: **on loopback there is no real network card interrupt.** The sender's own kernel path delivers the data, so interrupt counts from your tests will not look like a real network. Say this in the limitations, or run one test over a real link.

### Step 9: Signals

Your professor asked "have you used a signal or not?" and nobody could answer. Here is the answer for your code.

**What the code does:**
- SIGINT and SIGTERM set a flag so the loop exits and prints results. That is a graceful-shutdown use only.
- SIGPIPE is ignored in v1 with `SIG_IGN`. The newer file instead passes `MSG_NOSIGNAL` to `send`. Both stop a write to a closed peer from killing the process.
- **Signal-driven I/O (SIGIO or real-time signals) is not used.** That is a separate, older mechanism the Provos paper studied. You can say the project covers four mechanisms and why that one is excluded.

**Two details to be ready for:**
1. `epoll_wait` is **never restarted** after a signal handler runs, whatever the `SA_RESTART` flag says. It returns `EINTR`, and the code handles that by checking the flag again.
2. There is a **race**: if the signal lands just after the `while (running)` check but before `epoll_wait` starts, a timeout of `-1` would block until some event arrives. v1 has that problem. The newer file avoids it with a 100 ms timeout, which limits the delay. The fully correct fixes are `epoll_pwait` with a signal mask or a `signalfd`. The 100 ms timeout is the pragmatic choice, but you should be able to explain why it is only a bound.

### What to be able to explain in the viva

1. Why select's cost grows with the set size and epoll's with the ready count.
2. Why epoll still needs a `recv` syscall after being told "ready", and what io_uring does differently.
3. Why io_uring only wins when it can batch.
4. Why the interrupt counts on loopback can mislead.
5. What your code does with signals, and the shutdown race.

Phase 4 is the architecture, which is the part he said was most wrong, so it's the one to take slowly. Come back with any doubts from this part first, and say when you're ready for Phase 4.

## User:

```text
## Part 3: How each mechanism works inside the kernel

This covers steps 7 to 9: how the kernel executes each mechanism, what that means for the OS, and how signals fit in. It's the part your professor stressed with "not the API, how it is executed on the server".

### Step 7: What the kernel does on each call

**select and poll.** Every call hands the kernel your whole interest set.
1. The kernel copies the set in from user space.
2. For each descriptor it asks the underlying socket "are you ready?", and if none are ready it hooks the thread onto that socket's wait queue.
3. The thread sleeps. When any socket gets data, it wakes the thread.
4. The kernel walks the set again, builds the result and copies it back out. You then scan the result to find the ready ones.

So the cost per call grows with the **size of the set**, not the number of ready descriptors, and the set is rebuilt every time. The two differ mainly in representation. select uses a bitmap capped at 1024 descriptors (`FD_SETSIZE`). poll uses an array with no such cap, but it has the same linear walk.

**epoll.** The interest set lives **inside the kernel**, and you register descriptors once with `epoll_ctl`.
1. At registration the kernel attaches a callback to the socket's wait queue.
2. When data arrives, the callback runs at that moment and puts the descriptor on a **ready list**.
3. `epoll_wait` just returns what is on that list, so its cost follows the number of ready descriptors.
4. In level-triggered mode (what you use), a descriptor that is still ready gets put back on the list after you collect it.

The trade-off is that interest changes cost a syscall each. That is the `epoll_ctl` cost the 2004 paper pointed at, and it is why v1 switching between `EPOLLIN` and `EPOLLOUT` with `EPOLL_CTL_MOD` is worth counting.

**io_uring.** This is a different model. epoll tells you a socket is **ready**, and then you still make the `recv` or `send` syscall yourself. With io_uring you submit the **operation itself** and are told when it **completed**.
1. You write requests (submission entries) into a ring buffer that is shared memory between you and the kernel.
2. One `io_uring_enter` call can submit many requests and optionally wait for completions.
3. The kernel tries each operation immediately. If a socket isn't ready, the kernel waits on it internally and finishes the operation later.
4. Results appear in a second shared ring, which you read without a syscall.

The saving comes from **batching**: many operations per kernel crossing. This matches the cost model in Part 1, where the per-op cost is divided by the batch size. At a batch of one, io_uring has no advantage.

| | Set handed over | Cost per wait | Who does the read/write | Syscalls per message (typical) |
|---|---|---|---|---|
| select / poll | every call | grows with set size | you, after being told ready | wait + read + write |
| epoll | once | grows with ready count | you, after being told ready | wait + read + write (+ ctl on changes) |
| io_uring | per operation, batched | amortized by batch | the kernel | fewer when batched |

Details like internal polling, multishot and submission-polling threads vary by kernel version, so before the slides I'd confirm what your test machine's kernel supports.

### Step 8: The OS lens

- **Scheduling:** all four put the thread to sleep when nothing is ready. The cost you measure is the sleep and wakeup cycle plus a context switch. At low load the loop sleeps often; at high load it rarely does.
- **Concurrency:** you are single-threaded, so there are no locks in your code. Inside the kernel there is still the wait-queue and ready-list machinery. Scaling threads later changes this, for example several threads waiting on one epoll descriptor.
- **Memory:** select and poll copy the set on every call. epoll keeps a kernel record for each registered descriptor. io_uring uses shared ring memory that is set up once. Your own cost is the 4 KB buffer per client.
- **I/O management:** data arrives through interrupt handling, then the network stack, then the socket buffer, then a wakeup, then a copy to user space. Mechanisms differ in what happens after the wakeup.

One trap for the interrupts point: **on loopback there is no real network card interrupt.** The sender's own kernel path delivers the data, so interrupt counts from your tests will not look like a real network. Say this in the limitations, or run one test over a real link.

### Step 9: Signals

Your professor asked "have you used a signal or not?" and nobody could answer. Here is the answer for your code.

**What the code does:**
- SIGINT and SIGTERM set a flag so the loop exits and prints results. That is a graceful-shutdown use only.
- SIGPIPE is ignored in v1 with `SIG_IGN`. The newer file instead passes `MSG_NOSIGNAL` to `send`. Both stop a write to a closed peer from killing the process.
- **Signal-driven I/O (SIGIO or real-time signals) is not used.** That is a separate, older mechanism the Provos paper studied. You can say the project covers four mechanisms and why that one is excluded.

**Two details to be ready for:**
1. `epoll_wait` is **never restarted** after a signal handler runs, whatever the `SA_RESTART` flag says. It returns `EINTR`, and the code handles that by checking the flag again.
2. There is a **race**: if the signal lands just after the `while (running)` check but before `epoll_wait` starts, a timeout of `-1` would block until some event arrives. v1 has that problem. The newer file avoids it with a 100 ms timeout, which limits the delay. The fully correct fixes are `epoll_pwait` with a signal mask or a `signalfd`. The 100 ms timeout is the pragmatic choice, but you should be able to explain why it is only a bound.

### What to be able to explain in the viva

1. Why select's cost grows with the set size and epoll's with the ready count.
2. Why epoll still needs a `recv` syscall after being told "ready", and what io_uring does differently.
3. Why io_uring only wins when it can batch.
4. Why the interrupt counts on loopback can mislead.
5. What your code does with signals, and the shutdown race.

Phase 4 is the architecture, which is the part he said was most wrong, so it's the one to take slowly. Come back with any doubts from this part first, and say when you're ready for Phase 4.
```

Part 3 is done, so on to Phase 4, the architecture professor said was missing.

## Assistant:

> Checking where the current code couples transport and engine.
>
> - Checking where the current code couples transport and engine.
> - **Done**

Phase 4 is where the structure gets decided, so a diagram will help. I'll draw the layers first, then walk through the interfaces and what has to change in your current code.

Powered by Claude Exporter (https://www.ai-chat-exporter.net)
