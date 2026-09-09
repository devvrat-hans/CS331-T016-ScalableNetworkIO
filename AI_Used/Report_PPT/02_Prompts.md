# Representative AI Prompts

## Introduction

The following are representative prompts used during the project. The prompts cover the major stages of AI-assisted work: understanding the project, learning the technical concepts, understanding the team implementations, preparing the report, preparing the presentation, analyzing benchmark results, generating plots, and reviewing the final material.

Prompts were often followed by additional iterative instructions to revise, simplify, expand, restructure, or correct the generated response.

---

## 1. Understanding the Project

### Prompt 1 — Project understanding and report planning

> I have a project to do called "T016-ScalableNetworkIO" where the objective are as follows:
>
> "Project Description: Build and compare single-threaded TCP echo servers using four distinct Linux socket-handling mechanisms: select, poll, epoll, and io_uring. Implement each server paradigm from scratch and explore the shift from synchronous notification to truly asynchronous completion queue processing pattern. By using appropriate load generators (tcpkali, wrk etc.), students should benchmark and profile the behavior of these mechanisms by varying the connection loads (10, 100…) to observe system call overhead, memory scalability, and CPU efficiency.
>
> Tools/Technologies: C/C++/Rust, Linux network stack, io_uring
>
> Expected Outcomes: Upon completion, students will deliver fully functional C/C++ implementations for all four engines alongside a benchmarking report analyzing throughput, latency distributions, and system call frequencies..."
>
> According to these requirements and the files provided, explain what needs to be included in the report and how to make the report technically complete.

---

## 2. Understanding the Implementations

### Prompt 2 — Understanding the code and project

> Take a look into my project. Everything about what I have to do is described in the project description, and these are the files made by my teammates. Explain what each implementation is doing and identify the important implementation details that should be reflected in the report and PPT.

Follow-up prompts were used to ask for clarification of specific implementation details and to compare the four mechanisms.

---

## 3. Understanding `select`, `poll`, `epoll`, and `io_uring`

### Prompt 3 — Technical explanation

> Explain select, poll, epoll, and io_uring from the basic level. I need to understand how each one really works internally, not just a textbook definition. Explain what happens when a connection becomes ready, what the application gives to the kernel, what the kernel returns, and where the scaling cost comes from.

Additional prompts were used to ask about:

- O(N) scanning
- `O(max_fd)` behavior of `select`
- `O(ready)` behavior of `epoll`
- io_uring submission and completion queues
- system-call overhead
- user/kernel transitions
- why select and poll degrade under high concurrency
- why io_uring can reduce syscall frequency

---

## 4. Report Structure

### Prompt 4 — Initial report planning

> I have to make a technical report for this project. Tell me from the very base up what things I need to include so that it satisfies the project description and covers the implementation, benchmarking, results, discussion, limitations, and anything else important.

Further prompts were used to add sections, improve organization, and make the report more complete.

---

## 5. Benchmark Analysis

### Prompt 5 — Analyze the benchmark data

> I have been told that I have to look into the benchmarking.zip for throughput, latency distributions, and system call frequencies for all of the mechanisms. Analyze the benchmark files and tell me what measurements, tables, charts, trends, and observations are important to include in the report.

Additional prompts were used to compare results across 10, 100, and 1,000 connections and to identify meaningful interpretations rather than simply listing values.

---

## 6. Report Generation

### Prompt 6 — Generate a first report draft

> Make a technical report based on the project description, the uploaded source files, and the benchmark results. Include throughput, latency, syscall frequency, CPU/memory results, system design, methodology, discussion, limitations, and conclusion. Use the actual measured data rather than generic examples.

The generated report was subsequently reviewed, edited, expanded, and reformatted.

---

## 7. LaTeX

### Prompt 7 — Convert and improve the report in LaTeX

> Give me the LaTeX code for the report, using the required formatting and including the tables, charts, methodology, results, discussion, limitations, and conclusion.

Follow-up prompts were used to:

- Increase the report length.
- Add missing technical details.
- Improve table and chart placement.
- Fix formatting.
- Simplify the LaTeX preamble.
- Make the final document fit the required page length.

---

## 8. PPT Planning

### Prompt 8 — Adapt the PPT to the required structure

> According to all of the information I have sent you, make a new PPT taking the content from the old PPT and implement it in the required format:
>
> 1. Problem Statement & Objectives
> 2. Architecture / Mechanism — your design and how it works
> 3. Extension / Issues Fixed / Evaluation — what you built on top, bugs resolved, and results
> 4. Non-Functional Testing Parameters — performance, scalability, reliability, etc.
> 5. Challenges Faced
>
> The PPT should explain what we did, how it works, why the design behaves differently, and what the benchmark results show.

---

## 9. PPT Content and Technical Depth

### Prompt 9 — Technical presentation flow

> I am thinking of structuring the presentation as:
>
> Intro → project requirements and aim → single-threaded TCP echo servers → select, poll, epoll and io_uring → how they work conceptually → benchmarking and profiling → throughput, latency, syscall overhead, CPU and memory → O(N) vs O(ready) vs O(1) behavior → why select and poll degrade → io_uring trade-offs.
>
> Review this structure and tell me what should be added, removed, or changed.

---

## 10. Plot Generation

### Prompt 10 — Generate plots

> Based on the benchmark data, tell me which graphs would best communicate throughput, latency, syscall frequency, CPU utilization, and memory behavior. Provide plotting code that I can run on the benchmark data.

The plotting code generated by AI was then used with the actual benchmark data to create the charts included in the report and presentation.

---

## 11. Review and Iteration

### Prompt 11 — Final report review

> Review this report against the project description and tell me what is missing, technically incorrect, weakly supported, redundant, or unnecessary. Check whether the numerical claims agree with the benchmark results and whether the discussion is actually supported by the measurements.

---

## 12. PPT Review

### Prompt 12 — Final PPT review

> Review this PPT against the required five-slide structure and the project description. Check whether the slides are technically correct, whether the important results are included, and whether the presentation explains not just what we implemented but why the four mechanisms behave differently.

---

## Nature of Prompting

The prompts were not always used once. A common pattern was:

1. Give the project context and files.
2. Ask AI for an initial explanation or draft.
3. Inspect the output.
4. Ask follow-up questions about missing or unclear points.
5. Compare the response with the actual code or benchmark data.
6. Ask AI to revise the material.
7. Manually edit the final version.

Therefore, the prompts above represent the major classes of prompts used rather than every short follow-up message in the project conversation.