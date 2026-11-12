# **Linux I/O Multiplexing Mechanisms Compared**

*User prompt: Explain me the complete networking systems of select, poll, epoll, io\_uring, how they are implemented, their theory, comaprisos, expected results, everything*

Response: Modern high-performance network servers solve a fundamental bottleneck: managing tens of thousands of concurrent sockets without dedicating an OS thread to each connection. While blocking I/O works for low concurrency, spawning 100,000 threads exhausts RAM via thread stacks and thrashes the CPU scheduler through constant context switching.

Linux solves this through two distinct asynchronous I/O paradigms:

> 1. **Readiness-Based I/O (select, poll, epoll):** The Reactor pattern. The kernel notifies your application when a file descriptor (FD) is *ready* to perform I/O without blocking. Your application must still issue subsequent read() or write() syscalls to copy data between kernel socket buffers and user space.  
> 2. **Completion-Based I/O (io\_uring):** The Proactor pattern. Your application submits full I/O instructions (recv, send, accept) into shared memory; the kernel executes the data movement asynchronously and posts completion receipts when finished.

\<svg xmlns="http\://www\.w3.org/2000/svg" viewBox="0 0 820 350" width="100%" role="img" aria-label="Architectural comparison of select/poll, epoll, and io\_uring across user space and kernel space"\>  
  \<defs\>  
    \<marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"\>  
      \<path d="M 0 1 L 10 5 L 0 9 z" fill="currentColor" /\>  
    \</marker\>  
  \</defs\>

  \<\!-- Space Divider \--\>  
  \<line x1="15" y1="168" x2="805" y2="168" stroke="currentColor" stroke-dasharray="6,4" stroke-opacity="0.5" /\>  
  \<text x="20" y="22" font-size="13" font-weight="bold" fill="currentColor" opacity="0.7"\>USER SPACE\</text\>  
  \<text x="20" y="188" font-size="13" font-weight="bold" fill="currentColor" opacity="0.7"\>KERNEL SPACE\</text\>

  \<\!-- Column 1: select / poll \--\>  
  \<rect x="20" y="34" width="245" height="118" rx="8" fill="currentColor" fill-opacity="0.04" stroke="currentColor" stroke-opacity="0.3" /\>  
  \<text x="142" y="56" font-size="14" font-weight="bold" text-anchor="middle" fill="currentColor"\>1. select / poll\</text\>  
  \<rect x="35" y="68" width="215" height="36" rx="6" fill="var(--danger, \#e11d48)" fill-opacity="0.12" stroke="var(--danger, \#e11d48)" /\>  
  \<text x="142" y="90" font-size="12" text-anchor="middle" fill="currentColor"\>Full FD Array / Bitmask\</text\>  
  \<text x="142" y="128" font-size="12" text-anchor="middle" fill="currentColor"\>Re-scan all N FDs on return\</text\>

  \<rect x="20" y="200" width="245" height="132" rx="8" fill="currentColor" fill-opacity="0.04" stroke="currentColor" stroke-opacity="0.3" /\>  
  \<rect x="35" y="216" width="215" height="44" rx="6" fill="var(--danger, \#e11d48)" fill-opacity="0.12" stroke="var(--danger, \#e11d48)" /\>  
  \<text x="142" y="235" font-size="12" text-anchor="middle" fill="currentColor"\>Copy N FDs from User\</text\>  
  \<text x="142" y="251" font-size="12" text-anchor="middle" fill="currentColor"\>O(N) Linear Socket Poll\</text\>  
  \<text x="142" y="286" font-size="12" text-anchor="middle" fill="currentColor"\>Copy modified N FDs back\</text\>  
  \<text x="142" y="312" font-size="12" font-weight="bold" text-anchor="middle" fill="currentColor"\>Stateless • O(N) per call\</text\>

  \<path d="M 105 104 L 105 214" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(\#arrow)" /\>  
  \<path d="M 180 214 L 180 104" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(\#arrow)" /\>

  \<\!-- Column 2: epoll \--\>  
  \<rect x="287" y="34" width="245" height="118" rx="8" fill="currentColor" fill-opacity="0.04" stroke="currentColor" stroke-opacity="0.3" /\>  
  \<text x="410" y="56" font-size="14" font-weight="bold" text-anchor="middle" fill="currentColor"\>2. epoll\</text\>  
  \<rect x="302" y="68" width="215" height="36" rx="6" fill="var(--accent, \#2563eb)" fill-opacity="0.12" stroke="var(--accent, \#2563eb)" /\>  
  \<text x="410" y="90" font-size="12" text-anchor="middle" fill="currentColor"\>events\[\] Buffer (Size K)\</text\>  
  \<text x="410" y="128" font-size="12" text-anchor="middle" fill="currentColor"\>Issue read()/write() per FD\</text\>

  \<rect x="287" y="200" width="245" height="132" rx="8" fill="currentColor" fill-opacity="0.04" stroke="currentColor" stroke-opacity="0.3" /\>  
  \<rect x="302" y="216" width="98" height="44" rx="6" fill="var(--accent, \#2563eb)" fill-opacity="0.12" stroke="var(--accent, \#2563eb)" /\>  
  \<text x="351" y="235" font-size="12" text-anchor="middle" fill="currentColor"\>RB-Tree\</text\>  
  \<text x="351" y="251" font-size="11" text-anchor="middle" fill="currentColor"\>O(log N) FDs\</text\>

  \<rect x="419" y="216" width="98" height="44" rx="6" fill="var(--success, \#16a34a)" fill-opacity="0.14" stroke="var(--success, \#16a34a)" /\>  
  \<text x="468" y="235" font-size="12" text-anchor="middle" fill="currentColor"\>Ready List\</text\>  
  \<text x="468" y="251" font-size="11" text-anchor="middle" fill="currentColor"\>O(1) Callback\</text\>

  \<path d="M 400 238 L 417 238" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(\#arrow)" /\>  
  \<text x="410" y="286" font-size="12" text-anchor="middle" fill="currentColor"\>NIC IRQ triggers callback\</text\>  
  \<text x="410" y="312" font-size="12" font-weight="bold" text-anchor="middle" fill="currentColor"\>Stateful • O(K) ready events\</text\>

  \<path d="M 351 104 L 351 214" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(\#arrow)" /\>  
  \<path d="M 468 214 L 468 104" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(\#arrow)" /\>

  \<\!-- Column 3: io\_uring \--\>  
  \<rect x="554" y="34" width="245" height="118" rx="8" fill="currentColor" fill-opacity="0.04" stroke="currentColor" stroke-opacity="0.3" /\>  
  \<text x="676" y="56" font-size="14" font-weight="bold" text-anchor="middle" fill="currentColor"\>3. io\_uring\</text\>  
  \<text x="676" y="84" font-size="12" text-anchor="middle" fill="currentColor"\>Write SQEs (recv/send)\</text\>  
  \<text x="676" y="106" font-size="12" text-anchor="middle" fill="currentColor"\>Read CQEs (done bytes)\</text\>  
  \<text x="676" y="132" font-size="12" font-weight="bold" text-anchor="middle" fill="currentColor"\>Zero-copy shared memory\</text\>

  \<\!-- Shared mmap Ring Buffers spanning boundary \--\>  
  \<rect x="569" y="146" width="100" height="44" rx="6" fill="var(--warning, \#d97706)" fill-opacity="0.16" stroke="var(--warning, \#d97706)" /\>  
  \<text x="619" y="165" font-size="12" font-weight="bold" text-anchor="middle" fill="currentColor"\>SQ Ring\</text\>  
  \<text x="619" y="181" font-size="11" text-anchor="middle" fill="currentColor"\>mmap SPSC\</text\>

  \<rect x="684" y="146" width="100" height="44" rx="6" fill="var(--success, \#16a34a)" fill-opacity="0.16" stroke="var(--success, \#16a34a)" /\>  
  \<text x="734" y="165" font-size="12" font-weight="bold" text-anchor="middle" fill="currentColor"\>CQ Ring\</text\>  
  \<text x="734" y="181" font-size="11" text-anchor="middle" fill="currentColor"\>mmap SPSC\</text\>

  \<rect x="554" y="200" width="245" height="132" rx="8" fill="currentColor" fill-opacity="0.04" stroke="currentColor" stroke-opacity="0.3" /\>  
  \<rect x="569" y="216" width="215" height="44" rx="6" fill="var(--success, \#16a34a)" fill-opacity="0.12" stroke="var(--success, \#16a34a)" /\>  
  \<text x="676" y="235" font-size="12" text-anchor="middle" fill="currentColor"\>Async I/O Execution Engine\</text\>  
  \<text x="676" y="251" font-size="11" text-anchor="middle" fill="currentColor"\>Inline / io-wq / SQPOLL Thread\</text\>  
  \<text x="676" y="286" font-size="12" text-anchor="middle" fill="currentColor"\>Executes full network & disk ops\</text\>  
  \<text x="676" y="312" font-size="12" font-weight="bold" text-anchor="middle" fill="currentColor"\>Batched or Zero Syscalls\</text\>

  \<path d="M 619 190 L 619 214" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(\#arrow)" /\>  
  \<path d="M 734 214 L 734 190" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(\#arrow)" /\>  
\</svg\>

## **1\. select: Fixed Bitmasks and Stateless Scanning**

Introduced in 4.2BSD (1983), select monitors three separate bit arrays of file descriptors: readfds, writefds, and exceptfds.

### **Kernel Implementation**

> 1. **User-to-Kernel Copy:** When you call select(nfds, \&readfds, \&writefds, \&exceptfds, \&timeout), the kernel allocates memory and copies all three bitmasks from user space up to nfds bits.  
> 2. **Wait-Queue Registration & Polling:** The kernel loops from 0 to nfds \- 1\. For every set bit, it looks up the struct file in the process's file descriptor table and invokes its f\_op-\>poll() method. This registers the current thread onto each socket's internal wait queue (wait\_queue\_head\_t) and checks immediate readiness.  
> 3. **Sleep & Wakeup:** If zero descriptors are ready, the thread transitions to TASK\_INTERRUPTIBLE and sleeps. When a Network Interface Card (NIC) interrupt delivers a packet to any monitored socket, the socket's sk\_data\_ready callback wakes the sleeping thread.  
> 4. **Full Re-Scan & Copy Back:** Upon waking, the kernel **does not know which socket woke it up**. It loops from 0 to nfds \- 1 a second time, querying every socket again, clearing bits for unready FDs, and copying the modified bitmasks back to user space.

### **Architectural Limitations**

> * **Hard 1024-FD Ceiling:** fd\_set is a static bit array governed by FD\_SETSIZE (1024 on glibc). Monitoring FD 1024 or higher causes out-of-bounds memory corruption.  
> * **Destructive Bitmasks:** Because the kernel overwrites your input bitmasks with output readiness states, you must re-copy and re-initialize all three fd\_set structures on every single event loop iteration.  
> * ***O*****(*N*) Triple Overhead:** Every call incurs *O*(*N*) user-to-kernel copy, *O*(*N*) kernel polling, and *O*(*N*) user-space bit-scanning—even when only 1 socket out of 1,000 has traffic.

## **2\. poll: Dynamic Arrays Without the 1024 Cap**

Introduced in UNIX System V (1986), poll fixes the ergonomic and capacity limits of select without changing the underlying stateless execution model.

### **Kernel Implementation**

Instead of three bitmasks, poll accepts a contiguous array of struct pollfd:

struct pollfd {  
    int   fd;      // File descriptor to monitor  
    short events;  // Requested events bitmask (POLLIN, POLLOUT)  
    short revents; // Returned events bitmask populated by kernel  
};

> 1. **Page-Chained Copy:** The kernel copies the entire struct pollfd array from user space into a linked list of kernel memory pages (struct poll\_list).  
> 2. **Linear Poll Loop:** Just like select, the kernel walks every entry in poll\_list, attaches wait-queue hooks to each socket, sleeps if none are ready, and re-walks the entire array on wakeup to populate revents.  
> 3. **Copy Back:** The kernel copies the revents fields back to user space.

### **What Improved vs. What Remained Broken**

> * **Fixed:** No 1024 FD cap (scales to RLIMIT\_NOFILE), and separating events from revents eliminates rebuilding the array every loop.  
> * **Unchanged:** Still stateless. Passing 50,000 descriptors copies 400 KB (50,000 \* 8 bytes) into the kernel and executes 50,000 socket poll() calls on *every* iteration.

## **3\. epoll: Stateful Kernel Event Tracking**

Added in Linux 2.5.44 (2002) specifically to solve the **C10K problem** (handling 10,000+ concurrent connections), epoll splits registration from waiting by persisting interest state inside the kernel.

### **Kernel Implementation**

Calling epoll\_create1() allocates an struct eventpoll inside the kernel containing two critical data structures:

> 1. **Red-Black Tree (rbr):** Stores every monitored file descriptor wrapped in an struct epitem. Adding, modifying, or removing an FD via epoll\_ctl() takes *O*(log*N*) time and happens **only once** per connection lifecycle.  
> 2. **Doubly-Linked Ready List (rdllist):** Holds pointers exclusively to epitem nodes whose sockets currently have pending I/O events.

**The Event-Driven Fast Path:** When you register a socket with epoll\_ctl(epfd, EPOLL\_CTL\_ADD, sockfd, \&event), the kernel attaches a custom callback—ep\_poll\_callback—directly to the socket's wait queue.

> * When packets arrive at the NIC and hit the socket's receive buffer (sk\_receive\_queue), the kernel network stack invokes ep\_poll\_callback.  
> * ep\_poll\_callback acquires the eventpoll lock, inserts that specific epitem into rdllist in ***O*****(1) time**, and wakes any thread blocked in epoll\_wait().  
> * epoll\_wait() inspects rdllist. Instead of scanning *N* registered sockets, it copies only the *K* ready events into your user-space struct epoll\_event array in ***O*****(*K*) time**.

### **Triggering Modes: Level-Triggered vs. Edge-Triggered**

> * **Level-Triggered (LT — Default):** epoll\_wait() continues returning an FD as long as unread bytes remain in the kernel receive buffer (or space remains in the send buffer). Safe and forgiving, but incurs extra epoll\_wait() wakeups if you read data in small chunks.  
> * **Edge-Triggered (ET — EPOLLET):** epoll\_wait() notifies you **only once** when new data transitions the socket from empty to non-empty (or when new bytes arrive).  
  * *Mandatory Rule:* You must set sockets to O\_NONBLOCK and loop read()/write() until they return \-1 with errno \== EAGAIN / EWOULDBLOCK. If you stop reading early, the remaining bytes stay in the buffer and epoll\_wait() will never wake up for them again unless a new packet arrives.

## **4\. io\_uring: Shared-Memory Asynchronous Completion**

Introduced in Linux 5.1 (2019) by Jens Axboe, io\_uring eliminates both the readiness-syscall bottleneck and the architectural split between network I/O and disk I/O.

### **Why epoll Eventually Hits a Wall**

Even with epoll, processing a single HTTP request requires at least **three syscalls**: epoll\_wait() (discover readiness) → recv() (copy packet payload) → send() (copy response payload). Following CPU hardware mitigations such as KPTI, syscall transitions became an expensive bottleneck at millions of packets per second.

### **Kernel Implementation**

io\_uring\_setup() allocates two lock-free, Single-Producer Single-Consumer (SPSC) circular ring buffers in kernel memory and maps them directly into user space via mmap():

> 1. **Submission Queue (SQ):** An array of 64-byte **Submission Queue Entries (SQE)**. User space writes full I/O commands (IORING\_OP\_ACCEPT, IORING\_OP\_RECV, IORING\_OP\_SEND, IORING\_OP\_READ) along with a 64-bit user\_data pointer (typically pointing to your connection state struct), then atomically increments the SQ tail index.  
> 2. **Completion Queue (CQ):** An array of 16-byte **Completion Queue Entries (CQE)**. When the kernel finishes executing an SQE, it writes the syscall return value (res, e.g., bytes transferred or \-errno) and echoes back your user\_data pointer, then atomically advances the CQ tail index.

### **Advanced Kernel Optimizations**

> * **Batched Execution:** A single io\_uring\_enter() syscall can submit 128 queued network sends/receives and reap 128 completions simultaneously.  
> * **Kernel-Side Polling (IORING\_SETUP\_SQPOLL):** Spawns a dedicated kernel thread that continuously polls the SQ ring's tail pointer in shared memory. As long as traffic keeps the thread awake, your application submits and completes network I/O with **zero syscalls**.  
> * **Registered Buffers (io\_uring\_register\_buffers):** Pre-pins user-space memory pages into the kernel so every recv/send skips per-operation page-table lookups (get\_user\_pages()).  
> * **Multishot Operations (IORING\_RECV\_MULTISHOT / IORING\_ACCEPT\_MULTISHOT):** You submit *one* SQE for accept() or recv(); every time a client connects or a packet arrives, the kernel emits a new CQE continuously without requiring you to re-submit SQEs.  
> * **Provided Buffer Rings:** User space hands a pool of buffers to the kernel. When packets arrive on any of 50,000 idle sockets, the kernel picks a free buffer on the fly, copies the payload, and attaches the buffer\_id to the CQE—eliminating pre-allocated per-socket receive buffers.

## **5\. Complete Technical Comparison**

| Dimension | select | poll | epoll | io\_uring |
| :---- | :---- | :---- | :---- | :---- |
| **I/O Model** | Readiness (Reactor) | Readiness (Reactor) | Readiness (Reactor) | Completion (Proactor) |
| **Max Monitored FDs** | 1,024 (FD\_SETSIZE) | Unlimited (RLIMIT\_NOFILE) | Unlimited (RLIMIT\_NOFILE) | Unlimited (Ring sizes up to 32k+ entries, unlimited FDs) |
| **Kernel State** | Stateless | Stateless | Stateful (RB-Tree \+ Ready List) | Stateful (Shared mmap SQ/CQ Rings) |
| **Registration Cost** | *O*(*N*) every call | *O*(*N*) every call | *O*(log*N*) once via epoll\_ctl | *O*(1) ring push (optional IORING\_OP\_FILES\_UPDATE) |
| **Wait / Poll Complexity** | *O*(*N*) kernel \+ *O*(*N*) user | *O*(*N*) kernel \+ *O*(*N*) user | *O*(*K*) ready events | *O*(1) atomic pointer read (K completions) |
| **Memory Copy Overhead** | Copies 3 bitmasks both ways every call | Copies full pollfd\[\] both ways every call | Copies only *K* ready epoll\_event structs | Zero copy for SQE/CQE control; zero-copy payload capable |
| **Syscalls per I/O Cycle** | 1+2*K* (select \+ read/write) | 1+2*K* (poll \+ read/write) | 1+2*K* (epoll\_wait \+ read/write) | **0 to 1 batched** (io\_uring\_enter or SQPOLL) |
| **Regular Disk File I/O** | Always reports "ready" (blocks thread on read) | Always reports "ready" (blocks thread on read) | Fails with EPERM on regular files | **True async** NVMe/page-cache I/O via same ring |
| **Portability** | Universal POSIX (Linux, BSD, macOS, Windows) | Universal POSIX (Linux, BSD, macOS, Windows WSAPoll) | Linux-specific (kqueue on BSD/macOS, IOCP on Windows) | Linux 5.1+ (5.19+/6.1+ recommended for networking) |

## **6\. Expected Results & Real-World Performance Behavior**

Performance across these four mechanisms diverges sharply based on two variables: **total concurrent connections (*N*)** and **active connections per cycle (*K*)**.

### **Scenario A: Low Concurrency (*N*≤100, High Activity Ratio)**

> * **Expected Result:** select, poll, and epoll deliver nearly identical throughput and latency.  
> * **Why:** Scanning 64 bits or 100 contiguous pollfd structs fits inside two L1 CPU cache lines (\~128 bytes). epoll incurs slightly higher overhead here because epoll\_ctl requires a separate syscall per socket and Red-Black tree nodes scatter across heap memory.

### **Scenario B: High Concurrency, Sparse Activity (*N*\=100,000, *K*\=500 — Classic Chat/WebSocket/Proxy Load)**

> * **select:** Fails completely once FD numbers exceed 1023\.  
> * **poll:** CPU usage pins at **100% on a single core** just copying 800 KB of pollfd structs and executing 100,000 kernel socket checks per loop. Throughput collapses below \~5,000 requests/sec, and p99 tail latency spikes into hundreds of milliseconds.  
> * **epoll:** CPU drops below **10%**, easily sustaining 100,000+ idle connections with sub-millisecond p99 latency because the kernel only touches the 500 active sockets in rdllist.

### **Scenario C: Extreme Throughput & Microsecond Latency (\>1M PPS, High-Frequency Proxies, Databases, Storage+Net Engines)**

> * **epoll:** Reaches a syscall ceiling around 1M–1.5M small packet round-trips per core due to context-switch overhead (epoll\_wait \+ recv \+ send).  
> * **io\_uring:**  
  * **Throughput:** Delivers **1.5x to 3x higher requests per second** than epoll on Linux 6.1+ when combining multishot recv, buffer rings, and batched submissions.  
  * **Tail Latency (p99/p99.9):** Drops by **30% to 50%** because user space reaps completions directly from memory without context switches.  
  * **Mixed Workloads (e.g., Kafka/database engines serving disk files over TCP):** Eliminates thread-pool handoffs completely—an NVMe disk read CQE can immediately chain (IOSQE\_IO\_LINK) into a network send SQE inside the kernel.

## **7\. Production Pitfalls & Engineering Guidelines**

> * **Multi-Threaded epoll Thundering Herd:** If 8 worker threads call epoll\_wait() on the same epfd listening socket, a single incoming connection can wake all 8 threads simultaneously. Always pass EPOLLEXCLUSIVE when registering shared listening sockets, or use SO\_REUSEPORT with one independent epoll loop per worker thread.  
> * **Multi-Threaded epoll Race Conditions:** If Thread A wakes up on EPOLLIN and starts reading a socket, a second packet arriving mid-read can wake Thread B on the same socket. Use EPOLLONESHOT so the kernel automatically disables the FD after one event until Thread A finishes processing and re-arms it with EPOLL\_CTL\_MOD.  
> * **io\_uring Container & Security Policies:** Because io\_uring bypasses traditional syscall-entry hooks used by older security monitors, default Docker/Kubernetes seccomp profiles prior to recent runtimes blocked io\_uring\_setup. Verify kernel version (≥6.1 LTS recommended for stable network multishot/buffer-ring features) and container seccomp permissions before deploying in production.

---

*User prompt: i have to build the benchmarking suite, how should i go about it*

Response:  
---

*User prompt: i have to build the benchmarking suite, how should i go about it*

Response: Building an accurate benchmarking suite for select, poll, epoll, and io\_uring requires isolating the I/O subsystem from artificial bottlenecks like TCP loopback limits, coordinated omission, and unfair tuning.

### **1\. Define the Benchmark Architecture**

Keep the application logic identical across all four backends so you measure kernel I/O mechanics rather than parsing or memory-allocator differences.

> * **Pluggable Event-Loop Interface:** Build a single C or Rust server binary with an abstract reactor\_ops interface (init, add\_fd, mod\_fd, del\_fd, step) implemented independently for select, poll, epoll (both Level-Triggered and Edge-Triggered), and io\_uring.  
> * **Canonical Workloads:** Test at least four distinct traffic patterns, as epoll and io\_uring trade places depending on workload shape:  
  1. **Ping-Pong (Echo):** Small payloads (64 B – 512 B) testing raw syscall and wakeup latency.  
  2. **High-Concurrency Idle (C10K / C100K):** 10,000 to 100,000 open connections where only 1% are active per tick, exposing *O*(*N*) vs. *O*(ready) scaling.  
  3. **Streaming Throughput:** Large payloads (16 KB – 64 KB) saturating socket buffers to measure data-copy and zero-copy (IORING\_OP\_SEND\_ZC) efficiency.  
  4. **Mixed Network \+ Disk I/O:** An HTTP static-file server or log-appending proxy, demonstrating io\_uring's unified async file and socket advantage over epoll's blocking disk reads.

### **2\. Avoid Benchmark Traps & Ensure Fairness**

Most public epoll vs. io\_uring benchmarks produce misleading numbers due to flawed setup:

> * **Eliminate Coordinated Omission:** Do not use closed-loop clients that wait for a response before sending the next request—when the server stalls, closed-loop clients pause and hide tail latency. Use an open-loop load generator (such as wrk2, vegeta, or a custom io\_uring client) that sends requests at a fixed arrival rate and records queuing delays.  
> * **Never Benchmark Over localhost (lo):** Loopback traffic bypasses NIC hardware queues, interrupt coalescing, and realistic MTU framing, while sharing CPU cache between client and server. Run the load generator and server on **two separate physical machines** (or isolated VMs with SR-IOV) connected via a dedicated 10GbE/25GbE+ link.  
> * **Include a Baseline and Tuned io\_uring:** Test two io\_uring configurations:  
  * *Standard io\_uring:* Default submission/completion without SQPOLL or registered buffers.  
  * *Optimized io\_uring:* Multishot accept/recv, provided buffer rings (IORING\_OP\_PROVIDE\_BUFFERS), fixed files, and optionally IORING\_SETUP\_SQPOLL.  
> * **Match Threading Models:** Run single-threaded tests first to measure pure per-core efficiency, then scale to multi-threaded shared-nothing architectures (SO\_REUSEPORT with one ring/epoll instance per core).

### **3\. Tune the Linux Environment**

Before running any tests, lock down system variables so kernel limits or CPU frequency scaling do not skew results:

> 1. **Raise File Descriptor Limits:**  
>    ulimit \-n 1048576  
>    sudo sysctl \-w fs.nr\_open=1048576

> 2. **Expand Ephemeral Ports & Socket Queues (Client & Server):**  
>    sudo sysctl \-w net.ipv4.ip\_local\_port\_range="1024 65535"  
>    sudo sysctl \-w net.core.somaxconn=65535  
>    sudo sysctl \-w net.ipv4.tcp\_max\_syn\_backlog=65535

> 3. **Pin CPU Cores & Lock Governor:**  
   * Set CPU frequency scaling to performance (cpupower frequency-set \-g performance).  
   * Pin server threads with taskset \-c 0-3 and IRQ affinity (/proc/irq/\*/smp\_affinity) so NIC interrupts do not collide with your single-threaded server core.  
   * Note Spectre/Meltdown mitigations (mitigations=off vs default): KPTI drastically penalizes epoll's frequent syscalls while leaving io\_uring batching largely unaffected—document which setting your kernel uses.

### **4\. Metrics & Instrumentation**

Throughput (RPS) alone hides CPU cost and tail stalls. Capture four dimensions on every run:

| Metric Category | What to Measure | Recommended Tools |
| :---- | :---- | :---- |
| **Latency Distribution** | p50, p95, p99, and **p99.9** (max) latency | HdrHistogram (hdrhistogram\_c or Rust hdrhistogram) |
| **Throughput** | Requests/sec (RPS) and Goodput (Gbps) | Load generator counters |
| **CPU Efficiency** | User-space vs. Kernel-space CPU %, IPC, context switches, LLC cache misses | perf stat \-e cycles,instructions,context-switches,syscalls:sys\_enter\_\* |
| **Syscall & Kernel Cost** | Syscalls per request, time spent in do\_epoll\_wait vs io\_uring\_enter | bpftrace / strace \-c (dry runs only) |

### **5\. Recommended Implementation Roadmap**

> 1. **Phase 1 — Harness & Load Generator:** Build an automated runner script (Python or Bash) that configures sysctl, starts the target server pinned to fixed cores, warms up the TCP connection pool for 10 seconds, runs a 60-second measurement window, and exports JSON results.  
> 2. **Phase 2 — Implement poll and epoll (LT & ET):** Validate correctness and establish your baseline latency/throughput curves across 100, 1,000, 10,000, and 50,000 connections.  
> 3. **Phase 3 — Implement select:** Run up to 1,000 connections (and demonstrate failure/degradation past FD\_SETSIZE \= 1024).  
> 4. **Phase 4 — Implement io\_uring (via liburing or Rust io-uring):** Start with basic async recv/send, verify parity with epoll, and then layer on multishot operations and buffer rings to measure the incremental gains of each feature.  
> 5. **Phase 5 — Visualization:** Plot **Latency (p99) vs. Throughput** curves and **CPU Cycles per Request vs. Connection Count** charts to clearly show the crossover points where io\_uring overtakes epoll.

---

*User prompt: after taling with chatgpt for a while, i feel like this kind of framework would suit me bettet code/ ├── common/ │ ├── framework.h \# Common interface │ ├── framework.c \# Networking, sockets, threads, UDP, │ └── apps.c \# Application workloads ├── loadgen.c \# C-based load generator ├── experiments.py \# Experiment execution and analysis ├── correctness.py \# Automated testing ├── Makefile ├── run\_all.sh ├── check\_env.sh └── Dockerfile.test*

Response: That is a clean, practical layout. Separating the hot-path C code (loadgen.c, server backends) from Python orchestration (experiments.py, correctness.py) is the standard pattern for systems benchmarking because you avoid Python overhead during measurement while keeping data analysis and test automation painless.

Before you start writing code, refine four architectural details in this structure so you do not hit a wall when adding io\_uring:

### **1\. Where the Backends Live & The Readiness vs. Completion Trap**

Your tree currently omits the actual server backend files. Add a backends/ directory, and be careful about the abstraction boundary in common/framework.h:

> * **The Trap:** select, poll, and epoll are **readiness-based** (the loop tells you "socket X is readable," then you call recv()). io\_uring is **completion-based** (you give the kernel a buffer *first*, and the loop tells you "4 KB was already read into buffer Y"). If framework.h defines a readiness interface like on\_socket\_ready(int fd), your io\_uring backend will be forced to do extra buffer copies or poll-emulation, artificially crippling its performance.  
> * **The Fix:** Design framework.h around **buffer lifecycle callbacks** rather than readiness events:  
  * Backends own the I/O submission and buffer acquisition.  
  * Once bytes arrive in a buffer (via recv() in epoll or CQE completion in io\_uring), the backend passes (conn\_t \*conn, uint8\_t \*buf, size\_t len) up to apps.c.

### **2\. Keep common/apps.c Purely Stateless / Syscall-Free**

For your workload comparisons to be 100% fair, apps.c must never call read(), write(), or pread() directly. Instead, have apps.c return an **intent struct** to the backend:

// In common/framework.h  
typedef enum {  
    APP\_ACTION\_RECV,       // Need more data from client  
    APP\_ACTION\_SEND,       // Send out\_buf\[0..out\_len\] to client  
    APP\_ACTION\_FILE\_READ,  // Async read file\_fd at offset into out\_buf (tests disk+net\!)  
    APP\_ACTION\_CLOSE       // Close connection  
} app\_action\_t;

typedef struct {  
    app\_action\_t action;  
    uint8\_t     \*buf;  
    size\_t       len;  
    int          file\_fd;  
    off\_t        file\_offset;  
} app\_result\_t;

With this boundary:

> * In epoll.c, APP\_ACTION\_SEND arms EPOLLOUT and calls send(), while APP\_ACTION\_FILE\_READ falls back to blocking pread() (or a thread pool).  
> * In io\_uring.c, APP\_ACTION\_SEND queues IORING\_OP\_SEND (or SEND\_ZC), and APP\_ACTION\_FILE\_READ queues native async IORING\_OP\_READ on the exact same ring.

### **3\. Docker & Environment Caveats (Dockerfile.test & check\_env.sh)**

> * **Docker Blocks io\_uring by Default:** Standard Docker and containerd seccomp profiles block io\_uring\_setup and io\_uring\_enter with EPERM due to past kernel CVE mitigations. In your test runner, execute Dockerfile.test with \--security-opt seccomp=unconfined, and **use Docker strictly for correctness.py**, never for experiments.py (container veth bridging distorts network latency).  
> * **What check\_env.sh Should Verify:** Have it fail fast if:  
  * ulimit \-n is below 1048576  
  * kernel.io\_uring\_disabled (sysctl) is non-zero (1 or 2 disables io\_uring system-wide)  
  * CPU scaling governor is not performance  
  * Target server and loadgen are accidentally sharing the same CPU cores or running over loopback (127.0.0.1) during performance runs.

### **4\. Updated Project Structure**

code/  
├── common/  
│   ├── framework.h        \# Connection state, buffer ring interface, app\_result\_t  
│   ├── framework.c        \# Socket creation (SO\_REUSEPORT, TCP\_NODELAY), thread pinning, UDP  
│   ├── apps.c             \# Echo, HTTP static file, and UDP packet workloads (no syscalls)  
│   └── hdr\_hist.h         \# Lightweight latency histogram (or vendored hdrhistogram\_c)  
├── backends/  
│   ├── srv\_select.c       \# Bitmask loop (caps at FD\_SETSIZE)  
│   ├── srv\_poll.c         \# Dynamic pollfd array  
│   ├── srv\_epoll.c        \# Supports \--mode=lt and \--mode=et  
│   └── srv\_uring.c        \# Supports \--mode=basic and \--mode=opt (multishot \+ buf\_ring)  
├── loadgen.c              \# Multi-threaded open-loop client (built on epoll or io\_uring)  
├── correctness.py         \# Edge cases: partial writes, 10k concurrent connects, abrupt RSTs  
├── experiments.py         \# Runs matrix, parses JSON metrics, generates latency/RPS plots  
├── Makefile               \# Builds 4 server binaries \+ loadgen with \-O3 \-march=native  
├── check\_env.sh           \# Validates kernel version, sysctls, ulimit, CPU governor  
├── run\_all.sh             \# Runs check\_env.sh \-\> correctness.py \-\> experiments.py  
└── Dockerfile.test        \# Reproducible CI environment for correctness.py

### **Recommended Build Order**

> 1. **framework.h \+ framework.c \+ apps.c (Echo only):** Lock down the buffer-passing interface first.  
> 2. **backends/srv\_epoll.c \+ loadgen.c \+ correctness.py:** Build a working baseline and write correctness.py (testing fragmented packets, simultaneous disconnects, and payload integrity) before touching io\_uring.  
> 3. **backends/srv\_select.c & srv\_poll.c:** Drop these into the exact same framework.h harness—they should each take under 150 lines of C.  
> 4. **backends/srv\_uring.c:** Implement basic SQE/CQE echo first, pass correctness.py, and then add IORING\_RECV\_MULTISHOT and provided buffer rings behind a CLI flag so you can benchmark both modes.

---

*User prompt: yeah dont worry about the 4 implementations, they will be made by my other team members*

Response: That clarifies your role completely: you own the **interface contract, shared utilities, load generator, correctness gate, and experiment pipeline**.

When four people write backends in parallel, integration usually breaks on mismatched CLI flags, inconsistent shutdown behavior, or an interface that accidentally forces io\_uring to behave like epoll. Locking down two contracts upfront will let your teammates work independently and plug straight into your scripts.

### **1\. The Contract to Hand Your Teammates**

Give your team a frozen common/framework.h and a mandatory CLI specification so correctness.py and experiments.py can launch any binary identically:

> * **Standardized CLI Interface:** Every server binary (srv\_select, srv\_poll, srv\_epoll, srv\_uring) must accept:  
>   ./srv\_\<backend\> \--port 8080 \--threads 4 \--workload \<echo\|http\|udp\> \--mode \<default\|et\|sqpoll\|opt\>

  * **Readiness Signal:** Print READY\\n to stdout (and fflush(stdout)) once the listening socket is bound. Your Python harness waits for that line before starting loadgen, eliminating flaky time.sleep() race conditions.  
  * **Clean Exit:** Handle SIGINT/SIGTERM by breaking the event loop and returning 0 so perf stat and sanitizers (ASan/UBSan) flush clean reports.  
> * **Completion-Friendly framework.h Boundary:**  
  * Have framework.c handle socket creation (SO\_REUSEPORT, TCP\_NODELAY, O\_NONBLOCK), CPU core pinning (sched\_setaffinity), and UDP socket setup.  
  * Have apps.c expose a pure state-machine function (app\_on\_data(conn\_t \*c, const uint8\_t \*in, size\_t len)) that returns an action (RECV, SEND, READ\_FILE, CLOSE) and a pointer/length to the response payload. The epoll teammate translates that into send()/pread(), while the io\_uring teammate translates it into SQE submissions.

### **2\. Your Four Core Deliverables**

> * **common/framework.c & common/apps.c:**  
  * Implement SO\_REUSEPORT listener creation so multi-threaded benchmarks scale linearly across cores without a shared accept lock.  
  * Implement the three workloads in apps.c: **TCP Echo** (raw syscall overhead), **HTTP/1.1 Static File** (tests disk \+ network concurrency where io\_uring shines), and **UDP Echo** (tests datagram packet-per-second rates).  
> * **loadgen.c:**  
  * Build it on epoll (so it runs on any Linux machine regardless of io\_uring permissions) with multiple worker threads pinned to separate cores.  
  * Use an **open-loop timer** (timerfd or CLOCK\_MONOTONIC busy-spin pacing) to send requests at target RPS rates without coordinated omission, and output a single JSON summary line at exit (rps, goodput\_mbps, p50\_us, p99\_us, p999\_us, errors).  
> * **correctness.py:**  
  * Gate every teammate's PR against edge cases before benchmarking:  
    1. **Trickle/Fragmented Writes:** Send a request 1 byte at a time with 5 ms delays to catch broken buffer/edge-triggered loops.  
    2. **Pipeline Burst:** Send 50 pipelined requests in a single write() call.  
    3. **Half-Close & Abrupt RST:** Drop sockets mid-response (SO\_LINGER \= 0\) to ensure no backend crashes on SIGPIPE or stale FD reuse.  
    4. **FD Scale Check:** Open 2,000 connections (verifies select fails gracefully at 1024 while poll/epoll/io\_uring pass).  
> * **check\_env.sh & experiments.py:**  
  * check\_env.sh verifies ulimit \-n, kernel.io\_uring\_disabled \== 0, CPU governor (performance), and warns if running over 127.0.0.1.  
  * experiments.py wraps each server invocation in perf stat \-x, \-e cycles,instructions,context-switches,syscalls:sys\_enter\_\* so your final CSV/plots correlate latency directly with syscall counts and CPU cycles per request.

---

