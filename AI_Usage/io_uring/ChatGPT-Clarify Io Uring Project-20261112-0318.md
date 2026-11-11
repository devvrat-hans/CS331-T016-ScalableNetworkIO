# Clarify Io Uring Project

**User:** Anonymous  
**Created:** 9/4/2026 23:40:45  
**Updated:** 10/31/2026 12:11:47  
**Exported:** 11/12/2026 3:18:14  
**Link:** [https://chatgpt.com/c/6a9b0938-2c70-83e8-8edf-95feb9f357c0](https://chatgpt.com/c/6a9b0938-2c70-83e8-8edf-95feb9f357c0)  

## Prompt:
30/10/2026, 5:46:53 am

Hi! We are now moving on to Phase 2 of this project.

For context, our professor reviewed what we had built in Phase 1 and gave us feedback, suggesting that we continue working on the same project to better meet his expectations. We then had a meeting with him to discuss the next steps, where he clarified his expectations, the required deliverables, and his overall vision for Phase 2.

I have the transcript of that meeting, which I’ll share below.

First, please go through our previous chats to understand the complete project context, including what we built in Phase 1, the technical decisions we made, and my contribution (which is still **io_uring**).

Then, carefully analyze the meeting transcript and help me understand:

- What exactly the professor expects from Phase 2.
- What needs to be changed, improved, or added to our existing work.
- What my specific tasks and responsibilities are regarding io_uring.
- What should be my next steps, in a clear and practical order.

We have to build on our existing work rather than start from scratch, so please keep the previous project context in mind.

**Meeting transcript:**\
System overhead is there, waiting on the system call after system call, so we should have implemented the IO routing part more correctly. So we can improve on the IO routing part.\
\
So you have three approaches that you have compared...\
Four.\
Four approaches. Select, poll, epoll, and io_uring.\
\
And of course your test setup... I said your architecture itself is wrong. That is a primary problem that I have. I think your entire setup of trying to do the testing is not having anything about architecture. I asked, explain to me the architecture. None of you answered. None of you asked me that what is it that you are expecting as an architecture.\
\
I don't remember who asked that what is the relation of that to TCP... that I asked the question why TCP. What is the relation of that to this particular... that was not answered. So all of these were pointing towards lack of understanding of what you are trying to compare, how you should be comparing, what you should look at.\
\
And anything on the literature that you read in this space? You are not the first one to be doing this. So the literatures that you have read... what should I be ready with? Nothing was wrong. That is exactly what I call as the gap in understanding the problem itself.\
\
Even given the problem, that's why I asked in the first slide, put problem statement and what you derived as the objective. That itself shows whether you have understood the problem currently. So that's where I made these things. And I feel there is a lot to be done with respect to the understanding, with respect to evaluation.\
\
First, what is it that people have done in this space? That is something you have to do. Maybe they have not compared all four. They have compared only io_uring with select or something. Okay, how this is evaluating? And what is it that you will have to do? This is a network inference. What are the aspects of networking that you will have to consider?\
\
It's not one application that you can run, let's say that I'm done with the application. This is... I may have network applications like when you do operating system project this time. Is it optional? Optional. But if you had, what are the things that you will look for as a part of operating systems? Memory management, scheduling, concurrency, IO management... all of things. And didn't all of these actually affect the way the whole testing is happening? The way the OS component is only... right? Under the hood there is a networking application that is running on top and transferring. But what is it that is happening? This is all the OS construct.\
\
So what should be your way of building? I build network applications that are traffic intensive. I build network applications that are memory intensive. I build network like... I have WhatsApp. Nothing to do with memory, it's all small messages that keep going, but they are latency sensitive. File transfer. I do file transfer, these are hugely IO sensitive, a lot of IO that has to happen and then memory. Or I can have data that is continuously streaming, continuous memory.\
\
So these are the applications under which the operation of each of these will differ. The number of times that interrupts are going to be raised will change, the wakeups that happen will change. So these are the characteristics that you have to first find out in networking applications, then analyze. Just making this one small relay and saying okay this is all that is not wrong. You have to understand what kind of networking applications you are trying to cater to. One will work good for some networking application, another will work good for another. Not that everything will work good for everything.\
\
And that is what is the point of the element. So you have to analyze more from systems point of view than your EC specific. That would do justice to the project, otherwise you will learn more or anything will happen from the point of view of... the inference that you are drawing is also misguided.\
\
So that is what I expect that you review. Understand different network applications. For that you have to do first literature on what people have mapped and how networking applications are actually compared. Then apply the same principle and build in this.\
\
The baseline code is there, that's all. You don't have proper applications, you don't have proper instrumentation, and the tools with which you will write this. All the three are important. So that is what you should be focusing on and then...\
\
Sir and I want to clear up things. Since we have implemented for a thread, means everything will work on thread. So do we have to implement for multiple thread or a single thread?\
\
One thread uses a select call. The blocker is just one thread. If there are multiple threads you are making a select call, then which is returning on the same call... must be different. Then if you scale the thread, there will be an impact. But at least for one thread first you have to understand okay what is happening. That depends on the kind of activity that thread is doing. And that is what I mentioned. It will change.\
\
How many times okay the baseline first is I do nothing in the thread. Thread is just going up, takes, comes back, takes, comes back. And that is what will tell you what is the top operating frequency at which we will be able to read and write. Go in database, we do this analysis. That is the same here, but everything that you are doing. And now I scale the thread. File descriptors are another dimension. How many you are going to do the read, how many you are going to do the write. Each API has a different way of saying okay select FD is what are the FD using standard source.\
\
So if I am read intensive, I will have more reads but write less. Which one is better? That will change. And that is exactly the kind of framework that you have to build. That becomes your architecture. Say okay how am I building this framework, what am I trying to assess, and you change applications whichever we want is a map over your baseline framework. So the framework has to be defined first. And that is what I was asking from how have you architected this string. Once you architect this framework, network applications are just the plugins.\
\
You have to think through this right and then...\
\
One question you asked, why did you all use TCP? So like why TCP, is it if any of this do anything with just TCP? That was exactly the question I brought. TCP queue is used... and you have TCP relay. Echo... that is exactly the relay that happens... Does this mean that it will not work for UDP? No right, it should also work for UDP. So what is the particular building of the application?\
\
I think I thought it was in the project idea statement. In the project, you need to build some network application. Project is not saying do only TCP, project is not saying do relay. Compare all select, poll, epoll, io_uring.\
\
But for testing we have to like have a system on which we test, so we implemented on TCP, so is there any problem with that? Will your solution not work for UDP? Answer this. Networking applications are just the plugins. Networking application has nothing to do with how your testing is essentially trying to build. That's why I said your framework is not there, you have not been able to figure out, you have hooked your networking application with the framework. It should not be the case. First build a framework. And then networking application is just a plugin that you say okay I keep running networking applications but still the same thing works. Right now you have married both your TCP echo with distinguishing from one and the other.\
\
At least do all of you get the understanding now of okay what is the difference between a select, poll, epoll... not with respect to the API. With respect to how the API is executed on the server. I asked I think I have a final pointer, signal. Nobody was able to answer, have you used a signal or not. Signal case is there...\
\
Lord GPT or whatever tools that you use, Copilot, they will generate you the code, but if you don't understand the code what is the point? At least you have to understand how the things work, what are the ways that you are comparing, why is there a difference. What difference is it bringing, for what purpose, underline what is changing.

## Response:
30/10/2026, 5:46:59 am · gpt-6-t-mini

I’ll connect the professor’s feedback to the Phase 1 implementation you already built, then separate the required framework redesign from the work that can be reused. I’ll finish with a practical task order for your io\_uring contribution and the key questions your team should settle before coding.

> **Clarifying Phase 2**
>
> I’m interpreting Phase 2 as designing a framework-neutral benchmark for comparing event-driven, synchronous, and asynchronous I/O across applications. I’ll separate literature findings from transcript-based expectations and identify concrete next steps.
>
> **Defining the benchmark**
>
> I’m treating the professor’s goal as measuring operating limits under controlled workloads, then explaining when each I/O model performs best. I’ll prioritize a shared harness, backend adapters, workload plugins, and consistent metrics before adding many protocols.
>
> **Clarified Phase 2 and defined the benchmark**
>
> 
>
> **Interacted with Files**
>
> 
>
> **Refining the experiment**
>
> I’m separating API overhead from application work: first measure minimal read/write handling and maximum sustainable event frequency, then add realistic workloads, thread counts, file descriptors, message sizes, and workload mixes.
>
> **Expanding workload coverage**
>
> I’m organizing workloads by latency sensitivity, transfer size, streaming behavior, and read/write balance. TCP echo can remain the baseline, while the framework should permit UDP and other application plugins without redesign.
>
> **Instrumenting comparisons**
>
> I’m adding throughput, latency percentiles, CPU, memory, syscalls, context switches, wakeups, and signal interruptions. I’ll keep instrumentation separate from runs and qualify hardware-level results in virtualized environments.
>
> **Planning implementation**
>
> I’m turning the discussion into research questions, a literature matrix, an architecture diagram, a small fair test matrix, and staged instrumentation. I’ll also test pipeline depth, since one outstanding operation may underrepresent asynchronous behavior.
>
> **Mapping the design**
>
> I’m distinguishing explicit requirements from open choices: preserve backend-specific semantics, expose pipeline depth as a controlled variable, and avoid claiming universal superiority. The first deliverable should establish a fair, measurable baseline before optimization.
>
> **Separating workload types**
>
> I’m distinguishing network behavior from disk effects: memory-backed bulk transfer keeps the comparison focused, while disk-backed transfer should be labeled separately. I’ll also treat UDP, streaming, and request-response patterns as plugins rather than hard-coded assumptions.
>
> **Planned and refined experiment design across workloads and comparisons**
>
> 
>
> **Structuring the deliverable**
>
> I’m treating your main contribution as a reproducible, workload-driven framework—not another isolated echo server. I’ll preserve each backend’s native behavior while standardizing workload, configuration, measurements, and result reporting.
>
> **Connecting theory to code**
>
> I’m linking the literature review to your implementation: preserve the Phase 1 server as a baseline, then expose workload, thread, descriptor, queue-depth, pipeline, and signal behavior as measurable variables.
>
> **Structured the deliverable and connected theory to code**
>
> 
>
> **Searched 12 websites**
>
> 
>
> Worked for 3m 31s

# Phase 2: What your professor expects and what you should do

The most important takeaway from the meeting is this\:

**Your professor is not asking you to build a better TCP echo server. He is asking you to turn Phase 1 into a systematic framework for evaluating four I/O mechanisms: `select`, `poll`, `epoll`, and `io_uring`\.**

Your Phase 1 implementation is a starting point, not something you need to discard. The main change is how the project is designed, what workloads it tests, what it measures, and how you justify the results.

I'll connect his feedback to your existing code, separate what he explicitly said from what we can reasonably infer, and then give you a practical plan for your specific responsibility: **io\_uring**\.

## 1\. First, where Phase 1 stands

From our previous project discussions and the saved implementation, your io\_uring server already has several useful features.

**A working asynchronous echo server**

It uses liburing, submission and completion queues, and an event loop to manage client connections.

---

**Multishot accept and batched completion handling**

You arm multishot accept once and drain available CQEs in batches, rather than handling only one completion per loop iteration.

---

**Per-client state and correct send handling**

Each client has its own buffer, send position and operation state. The server handles partial sends and follows a RECV → SEND → RECV flow.

---

**Initial instrumentation and operational checks**

You track connection and I/O counts, bytes, errors, CQEs and event-loop calls. You also tested graceful shutdown and verified the single-threaded design.

These features are reusable. **You do not need to start over with another io\_uring implementation.**

However, the current server is still essentially one TCP echo workload. The professor's criticism is that running this workload against four different mechanisms does not, by itself, establish how those mechanisms behave across different network workloads.

## 2\. What exactly does the professor expect?

I read the transcript as containing six major requirements.

### A. A clear problem statement and objective

He explicitly mentioned that your first slide should define the problem and the objective derived from it.

At present, “compare four I/O mechanisms” is too broad by itself. You need to explain **what you are trying to learn by comparing them**\.

A stronger problem statement would be\:

> Different Linux I/O mechanisms have different costs in how they wait for, submit, and process network I/O. Their performance may change depending on the workload, number of connections, read/write pattern, and concurrency. We need a systematic framework to measure these differences and understand the underlying system-level reasons.

Your objective could be\:

> Develop a modular, reproducible benchmarking framework for comparing select, poll, epoll, and io\_uring across different networking workloads, measuring performance and operating-system overhead to understand which mechanisms suit which workloads.

That second sentence is critical: **you are trying to explain the results, not just produce a table of which mechanism is fastest.**

You can turn this into four research questions\:

1. How does each mechanism scale as the number of active file descriptors increases?
2. How do small, latency-sensitive messages compare with large, throughput-intensive transfers?
3. What explains the differences in performance: system calls, CPU usage, wakeups, interrupts, context switches, or memory overhead?
4. How does increasing the number of server threads change those results?

These questions give the literature review, architecture, benchmarks, and final report a common purpose.

### B. A reusable benchmarking framework, not one application tied to one API

This is the central architectural criticism in the meeting.

Your professor said that you had effectively married your TCP echo application to the mechanism being tested. He wants the **benchmarking framework defined first**, with network applications added as interchangeable plugins.

The architecture should conceptually look like this\:

### Experiment runner / configuration

Chooses workload, connections, thread count, payload size and test duration

### Common workload contract

Defines what traffic is generated and how it should be processed

**select**

Readiness-based

**poll**

Readiness-based

**epoll**

Readiness-based

**io\_uring**

Completion-based

### Common measurement and reporting layer

Throughput, latency, CPU, system calls, wakeups and other system metrics

The workload plugins might eventually include TCP echo, small request-response messages, sustained streaming, bulk transfers, or a UDP-based application.

A crucial distinction: the workload must remain comparable, but **the framework must not force every mechanism to use the same internal event loop**\. Otherwise, you could accidentally hide the very implementation differences you're supposed to measure.

For example, `select`, `poll`, and `epoll` report readiness so the application can perform I/O. io\_uring can instead queue operations and process their completion notifications. The research needs to compare these execution models, not merely compare four function names.

A closely related paper, *A Benchmark Tool for Network I/O Management Architectures*, used a modular benchmark so different mechanisms could be compared quantitatively and fairly. That is a useful starting point for understanding the professor's request. \[[A Benchmark Tool for Network I/O Management Architectures](https://www.jstage.jst.go.jp/article/ipsjdc/4/0/4_0_138/_article/-char/en?utm_source=chatgpt.com)\]

### C. Multiple workload types, each chosen for a reason

Your professor gave several examples to explain why a single echo application is insufficient.

| Workload profile | What it represents | What it helps reveal |
|--------|--------|--------|
| Tiny request-response | Short messages with frequent back-and-forth communication | Per-operation overhead and latency |
| Bulk transfer | Large amounts of data transferred continuously | Throughput, CPU cost and memory-copy overhead |
| Continuous streaming | Sustained data flow over time | Buffering, sustained throughput and resource use |
| Read-heavy / write-heavy | Different ratios of receiving and sending | Whether the operation pattern affects each mechanism differently |

You don't necessarily need four completely separate production-grade applications. You need **a small, justified set of workload profiles that exercise meaningfully different behaviours**\.

There is also an important nuance about file transfers. Your professor mentioned them as an example of a demanding application. You could begin with large in-memory network transfers to isolate networking costs, then add actual disk-backed transfers separately if required. Mixing disk access into the initial experiments would introduce another source of overhead.

**What about TCP versus UDP?**

The professor was clear that the project is not defined as “TCP echo server benchmarking.” The architecture should allow different applications and transport patterns to be plugged in. TCP echo can remain your initial baseline; a UDP workload is a logical extension, but the transcript does not specify an exact required number of protocols.

The important thing is to avoid assuming that a framework built around TCP `accept()` and connected-stream semantics is automatically a general solution for UDP. UDP has different connection and message-handling semantics, so its workload needs to be represented explicitly.

### D. A measurement system that can explain the results

The professor specifically criticised the lack of proper instrumentation and tools.

Your existing server statistics are useful, but application-level counts alone will not explain why one method performs differently from another.

Think in terms of two measurement layers\:

**1\. Application-level performance**

Throughput in requests or operations per second; bytes per second; latency distributions such as median and P99; active connections; successful operations; errors; and dropped or incomplete work where applicable.

---

**2\. Operating-system and implementation costs**

CPU usage, system-call counts, context switches, CPU cycles and instructions, wakeups, relevant interrupt/softirq activity, and memory usage. For io\_uring, also record submission/completion activity so that you can calculate how many operations are processed per kernel-entry call.

This is not an arbitrary list. A 2025 master's thesis, *Optimizing Web Servers with io\_uring*, describes controlled load testing and includes system metrics such as system calls, memory usage, and CPU utilization. The Linux kernel's workload-tracing documentation also describes using tools such as `strace` and `perf` to understand the system resources and code paths used by workloads. \[["Optimizing Web Servers with io\_uring" by Mihika Nigam](https://digitalcommons.calpoly.edu/theses/3037/?utm_source=chatgpt.com); [Discovering Linux kernel subsystems used by a workload](https://origin.kernel.org/doc/html/latest/admin-guide/workload-tracing.html?utm_source=chatgpt.com)\]

You should not collect everything with one tool or assume all measurements are equally reliable. For example, `strace` can substantially perturb a fast server, so syscall profiling may need separate runs from the clean performance measurements.

### E. A controlled scaling experiment: one thread first

The transcript directly answers the question about whether you should immediately implement multithreading.

Your professor said to start with **one thread**, understand what is happening, and then investigate scaling the thread count.

That means you should not immediately redesign everything into a multithreaded server. First establish a trustworthy single-thread baseline for each backend.

Then vary the relevant factors deliberately\:

- Number of active file descriptors or connections.
- Number and size of read/write operations.
- Read-heavy, write-heavy, and balanced traffic patterns.
- Offered load and message size.
- Eventually, the number of server threads.

The professor also mentioned a minimal baseline where the thread does very little beyond repeating the relevant operation. I would interpret this as a way to establish the cost of the framework and I/O mechanism before more application processing is added. Your team should document exactly what the minimal baseline does; otherwise, its results will be difficult to interpret.

### F. Understand what the code actually does, including signals

The professor explicitly said that the team could not explain how the APIs execute on the server, and he raised signals as an example.

For your implementation, this means you should be able to explain\:

- When an io\_uring SQE is prepared and submitted, and when the corresponding CQE is consumed.
- How the design's outstanding operations affect the amount of work that can be processed in a batch.
- How `io_uring_enter()` calls relate to submitted operations and completions.
- How blocking or waiting calls behave when interrupted by signals, and how shutdown is handled.

Your current io\_uring implementation already has SIGINT/SIGTERM handling, CQE batching, and a single outstanding I/O operation per client. Those are useful starting points, but the team should consider signal handling consistently across all four implementations. Treat this as part of the design and measurement plan, not just as a viva question.

---

## 3\. What should change in the existing Phase 1 work?

I would divide the changes into four categories.

| Component | Phase 1 | What Phase 2 needs |
|--------|--------|--------|
| I/O backends | Four mechanisms implemented separately | Preserve them, but make them participate in a common benchmarking framework |
| Application | Primarily a TCP echo server | Support multiple workload profiles independently of the backend |
| Testing | Basic client testing and performance comparison | Reproducible experiments with controlled variables |
| Instrumentation | Application counters and basic statistics | Application-level and system-level measurements that explain the results |

The biggest change is therefore **architectural rather than cosmetic**\. More optimizations in your current server won't fix a benchmarking framework that cannot distinguish the effects of the workload from those of the I/O mechanism.

### What can you reuse?

Keep the existing TCP echo server as the first workload. Retain its per-client state handling, multishot accept, CQE batching, partial-send support, error handling, and existing statistics.

What needs to change is the way you configure and evaluate it\:

- Make workload behaviour configurable or selectable instead of hard-coding echo behaviour into the whole experiment.
- Make io\_uring operate through an agreed interface with the common benchmarking harness.
- Add the measurements needed to compare it with the other three backends.
- Investigate its queue depth, batching, and outstanding-operation limits through controlled experiments.

You do **not** need to replace the implementation with a completely new server.

## 4\. Your responsibilities: the io\_uring part

Since you are responsible for io\_uring, I would divide your contribution into the following work packages.

**Task 1 - Integrate io\_uring into the new architecture**

Coordinate with the teammates responsible for select, poll and epoll. Agree on the configuration parameters, the workload interface, the required output metrics, and how a benchmark run is started and stopped.

Your current `io_uring_echo_server.c` should become the io\_uring backend of the framework, rather than remaining a standalone program whose behaviour is tied to TCP echo.

---

**Task 2 - Support different workload profiles**

Retain the working echo path, then adapt the I/O handling to the workload contract the team defines. For example, a request-response workload and a continuous stream should not have to use the exact same application behaviour.

Keep protocol handling separate. A future UDP workload must not have to pass through TCP-specific `accept()` and connected-stream logic.

---

**Task 3 - Improve io\_uring instrumentation**

Preserve the existing counters, but extend the measurements so they can explain the benefit, or cost, of asynchronous submission and batching.

Record operations submitted, completions processed, kernel-entry calls, throughput, latency, errors, and bytes transferred. Derive useful ratios such as completions per kernel-entry call.

---

**Task 4 - Investigate the limits of the current design**

Your current implementation allows one outstanding I/O operation per client. This simplifies state management, but caps the per-client pipeline depth.

Measure the current design first. Then, as a separate experimental configuration, consider allowing more work in flight, with suitable per-operation buffers and state tracking. Don't silently change the design before you have a baseline to compare against.

---

**Task 5 - Validate and explain your results**

Run the io\_uring tests under the same agreed conditions as the other backends. Explain when batching helps, when kernel-entry overhead remains significant, how connection counts affect the result, and what the measurements actually prove.

One important constraint: **do not assume that `io_uring` must win every benchmark**\. A short request-response workload with little opportunity for batching may behave differently from a highly concurrent stream or bulk transfer. Your task is to measure and explain that difference, not to engineer the test so that io\_uring always comes out on top.

## 5\. Your practical next steps, in order

Here is the order I would follow. It avoids spending time optimizing code before the team has settled the design.

1. **Write a one-page Phase 2 design proposal**
   
   Define the research questions, target backends, workload types, measurement goals, and experimental variables. State explicitly that TCP echo is the initial baseline workload, not the entire project.
2. **Build a small literature review**
   
   Read a few papers and record what they compare, which workloads they use, what they measure, and what limitations they acknowledge. Use this to justify your architecture and experiment design.
3. **Agree on the framework interface with your teammates**
   
   Decide how the benchmark selects a backend and workload, passes parameters, starts and stops a run, and emits results. Get this agreement before changing all four implementations.
4. **Establish the single-thread baseline**
   
   Run the same minimal workload and TCP echo workload through all four backends. Confirm that the application behaviour is equivalent and that the benchmark itself is not the bottleneck.
5. **Add systematic workload variations**
   
   Vary active connections/file descriptors, message sizes, read/write patterns and offered load. Add a UDP workload or further application plugins once the initial architecture supports them and the planned scope is agreed.
6. **Add system-level instrumentation**
   
   Combine the servers' internal counters with tools that capture system calls, CPU usage, context switches and relevant interrupt or wakeup activity. Keep profiling runs separate from clean performance runs where the tooling significantly changes execution.
7. **Evaluate scaling and io\_uring-specific optimizations**
   
   First establish the single-thread results. Then investigate additional threads and, separately, variations in queue depth and outstanding operations. Treat every optimization as a measurable experimental change.
8. **Analyse and report the results**
   
   Present throughput and latency against workload and concurrency. Explain the system-level reasons for differences, including cases where the methods perform similarly or where an optimization provides little benefit.

### A suggested initial experiment matrix

These values are **my proposed starting points**, not values specified by your professor.

| Variable | Suggested initial values |
|--------|--------|
| Backend | select, poll, epoll, io\_uring |
| Server threads | 1 initially |
| Active connections | 1, 64, 256; extend if supported |
| Payload size | 64 bytes, 1 KiB, 64 KiB |
| Workload | Minimal baseline, request-response, bulk transfer |
| Load | Several controlled offered-load levels |
| Repetitions | 5 runs per configuration, after warm-up |

Start small and increase the matrix only when each test produces valid, comparable results. Account for `select`'s descriptor-set limitations when choosing the maximum connection count. Once the single-thread results are reliable, introduce thread-count scaling as a separate experiment.

Use the same server workload, client behaviour, payloads, run duration, compiler settings and machine conditions across the backends. If the client and server share a machine, ensure the client does not become the bottleneck. Your previous WSL-based setup is sufficient for continuing implementation and functional testing, but verify which system-level counters and performance facilities are genuinely available before drawing conclusions about hardware interrupts or host-level behaviour.

For each configuration, it is useful to report median and P99 latency, along with throughput, rather than relying only on an average. Controlled-load experiments and system-resource measurements are also central elements of published io\_uring evaluation work. \[["Optimizing Web Servers with io\_uring" by Mihika Nigam](https://digitalcommons.calpoly.edu/theses/3037/?utm_source=chatgpt.com)\]

## 6\. What to instrument specifically in your io\_uring backend

Your current statistics already provide a foundation. I recommend extending them in two directions.

**Common metrics shared by all four mechanisms**

- Number of successful requests and I/O operations.
- Bytes received and sent.
- Throughput and latency distribution.
- Failed operations and disconnected clients.
- CPU consumption and relevant operating-system counters.

**Additional io\_uring metrics**

- Number of SQEs submitted.
- Number of CQEs processed.
- Number of kernel-entry/submit-wait calls.
- Average completions per kernel-entry call.
- Queue depth and outstanding-operation count.
- Batch-size distribution, if practical.

These counters help answer questions such as: Are we actually submitting multiple operations per kernel entry? Does batching improve as concurrency increases? Does the server become CPU-bound before the ring or connection limit is reached?

Be careful with the interpretation: **`io_uring_enter()` calls, SQEs and CQEs are different quantities**\. One kernel-entry call can submit multiple SQEs, and completions do not necessarily map one-to-one to client requests. The measurements need to preserve that distinction.

Also, don't rush into changing the ring to use advanced modes such as SQ polling. First identify where the baseline spends time, then introduce a specific optimization as a separate configuration and measure its effect.

## 7\. Literature you should start with

The professor directly criticised the lack of literature review, so this needs to be an actual deliverable rather than a few references in the report.

[![When Poll Is Better Than Interrupt: Jisoo Yang Dave B. Minturn Frank Hady | PDF | Thread (Computing) | Solid State Drive](https://images.openai.com/static-rsc-4/3ZHaZtCggV-Lrw_7iad7spnY3jC_nsp3eA_o9lY_mjCrsgDehO9iW2DNuc2OFXrPQtJtOXn6ypzen6nCYbz8akmJvtNrM-IQPesrsMvSi_FCcyF57l8QreptckqlYtaRcpg3sx7lMEeBtwgrvbXvxtjFomvXeirqApcC7Lrmf6T2haVEcae1Yy71PPLyzQkn?purpose=fullsize)](https://www.scribd.com/document/241357079/Yang)

**1\. Eiji Kawai and Suguru Yamaguchi - “A Benchmark Tool for Network I/O Management Architectures” (2008)**

Why read it: Directly relevant to your framework design.

It discusses modular comparisons of network I/O management approaches, including select, poll and epoll. Focus on the experiment architecture, fairness of comparison, and the difference between evaluating a server application and evaluating the I/O management mechanism. \[[A Benchmark Tool for Network I/O Management Architectures](https://www.jstage.jst.go.jp/article/ipsjdc/4/0/4_0_138/_article/-char/en?utm_source=chatgpt.com)\]

[Read the paper summary](https://www.jstage.jst.go.jp/article/ipsjdc/4/0/4_0_138/_article/-char/en?utm_source=chatgpt.com)

---

[![How to Build a High-Performance TCP Server Part 2 – Is io_uring Always Faster Than epoll? A Deep Dive into System Internals.](https://images.openai.com/static-rsc-4/BEmHFHS-_4qUrV436_kv_xOs9l0e4B3N387tcQz9eEx1iUNP-A3X2VQjfKNF9yyYH6E_mOGYkUjOymjLgTI-lCJFIiG-fSGFmWaRQXaGKM_xsIYyLmZdVv0NnvKUCq7vswMMEQZBTlLlkPho1_te_8BFwT6u45kB6wdvrfM7SaWJLK9rZWNwExXD71Rstntv?purpose=fullsize)](https://www.chillvic.dev/p/how-to-build-a-high-performance-tcp-0c1)

**2\. Mihika Nigam - “Optimizing Web Servers with io\_uring” (2025)**

Why read it: A more recent study of workload-driven evaluation.

Review the controlled-load methodology, how performance is measured, and how CPU utilization, system calls and memory usage are considered alongside application performance. \[["Optimizing Web Servers with io\_uring" by Mihika Nigam](https://digitalcommons.calpoly.edu/theses/3037/?utm_source=chatgpt.com)\]

[Read the thesis page](https://digitalcommons.calpoly.edu/theses/3037/?utm_source=chatgpt.com)

---

[![Linux系统调试篇——Perf性能分析指南-阿里云开发者社区](https://images.openai.com/static-rsc-4/cNlOZ8-mRDZcCxLwUK0dKgDB2BtXDRf5MNym8RV11bgPFkY0iGlH2miPSxSHULCWmmH33tO4DORmi2sZKUnztMUgdNvkVjvKpV4RA__7UNc_ubw7Zg_N7-BmMHb12LFFNJCqZw8oJsK9PgcqjJP4nOPjIYUtNaVnTJvMIdshgfwyDSyfL49RJA1CoDXf5F1I?purpose=fullsize)](https://developer.aliyun.com/article/1324375)

**3\. Linux kernel documentation - “Discovering Linux kernel subsystems used by a workload”**

Why read it: For your instrumentation and system-level analysis.

It explains the use of `strace` and `perf` to understand system calls and resources used by a workload. This provides a practical starting point for deciding how to measure the OS-level effects your professor expects you to investigate. \[[Discovering Linux kernel subsystems used by a workload](https://origin.kernel.org/doc/html/latest/admin-guide/workload-tracing.html?utm_source=chatgpt.com)\]

[Read the Linux kernel documentation](https://origin.kernel.org/doc/html/latest/admin-guide/workload-tracing.html?utm_source=chatgpt.com)

Create a small literature table for your report with the columns **paper, mechanisms compared, workloads, metrics, key findings, limitations, and what we adopt for Phase 2**\. That final column is particularly important: it demonstrates how the literature informed your own architecture.

## 8\. What is still unspecified in the professor's feedback?

The transcript gives a strong direction, but it does not give an exact rubric. In particular, it does not specify the exact number of workload plugins, the number of papers required, whether a working UDP implementation is compulsory, or the precise connection/thread counts.

Treat those as design decisions to settle with your team rather than silently inventing requirements.

My recommendation is to agree on the architecture, one-thread baseline and minimum workload/measurement set first. Once those are working, expand the workload coverage and thread scaling.

## My overall recommendation

**Your immediate next step should be the architecture and experiment design-not more io\_uring optimization.**

Your personal contribution in Phase 2 is to turn the working io\_uring server into a backend that fits the shared framework, extend its instrumentation, and investigate its performance under controlled workloads. You already have a substantial amount of working code to build on.

The key difference between the two phases is\:

- **Phase 1\:** Implement the four mechanisms and compare them using a basic network application.
- **Phase 2\:** Build a reusable, controlled evaluation framework and explain how workload characteristics and operating-system behaviour affect the four mechanisms.

That is the direction that most directly addresses the professor's criticism.



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)