# Backend Testing Results

## Prompt:
here is the backend files by my tesmmatees along with my testing bench

[Attachment: code.zip]

## Response:
I’ve inspected the ZIP structure. It contains the four backend implementations from your teammates and your benchmarking framework/testing suite.

The project is organized as follows\:

| Contributor | Implementation |
|--------|--------|
| Parth | `select_server.c` |
| Devvrat | `poll_server.c` |
| Nachiket | `epoll_server.c` |
| Niraj | `io_uring_server.c` |
| Chirag | Common framework, load generator, experiments, correctness tests and execution scripts |

**My initial observation\:** Your `Makefile` successfully compiles the common framework, load generator, and the `select`, `poll`, and `epoll` backends. The full build currently stops at `io_uring_server.c` because `liburing.h` is unavailable in the environment. This is an environment/dependency issue so far, not evidence of a defect in the implementation.

## 1\. Initial correctness-test results

I ran your correctness suite against the three backends that successfully compiled.

**select** (11/11 passed)

**poll** (11/11 passed)

**epoll** (47/47 passed)

---

**Overall**

# 135/199

64 tests could not execute because the io\_uring binary was not built. These are not 64 demonstrated correctness failures.

The important positive result is that the three readiness-based backends passed their available correctness tests, including\:

- TCP echo, upload and download.
- Connection limits and many simultaneous connections.
- Invalid requests and client resets.
- Signal handling and shutdown.
- UDP functionality.
- Epoll edge-triggered and level-triggered modes.
- Two-thread configurations and additional socket configurations.

This is a good starting point. Your framework and the three corresponding backend integrations appear compatible at the functional-test level.

## 2\. Integration assessment

There are a few things worth addressing before treating this as a final benchmarking framework.

| Area | Assessment |
|--------|--------|
| Common framework | Compiles cleanly with strict compiler warnings |
| Backend interface | Compatible with select, poll and epoll |
| TCP/UDP functionality | Passed available correctness tests |
| Load generator | Compiles successfully |
| Python experiment framework | Both Python files pass syntax compilation |
| io\_uring integration | Not yet verified |
| Full experimental pipeline | Not yet verified |

I also checked the test breakdown more precisely\:

- `select`\: 41 passed.
- `poll`\: 31 passed.
- `epoll`\: 63 passed.
- `io_uring`\: 64 tests could not run.

The missing io\_uring dependency also prevents a complete evaluation of the team's most important backend comparison.

## 3\. Issues I would prioritize

**P0 - Get io\_uring compiling and tested**

This is the biggest outstanding item. Until then, we cannot establish whether the four backends implement the same protocol and correctness guarantees.

Install the dependencies on your Linux machine\:

```bash
sudo apt update
sudo apt install build-essential pkg-config liburing-dev strace \
    python3 python3-matplotlib iproute2
```

Then\:

```bash
cd Chirag/code
make clean
make -j
python3 correctness.py results/correctness
```

**P1 - Validate syscall accounting**

Your framework maintains its own syscall counters, and `experiments.py` has a `check-counters` experiment to compare them against `strace`\.

This is important because syscall counts are part of the performance comparison. I couldn't run that validation here because `strace` isn't installed.

**P2 - Validate benchmark fairness**

I would pay particular attention to\:

- CPU affinity and server/client core separation.
- Identical workloads across backends.
- Warm-up and measurement durations.
- Throughput and latency measurement consistency.
- UDP packet loss.
- Whether io\_uring SQPOLL overhead is accounted for fairly.
- Whether failed or incomplete benchmark runs are excluded from analysis.

**P3 - Run the complete experimental pipeline**

Your `run_all.sh` estimates approximately 4.5 hours for the 1,200 benchmark runs. I haven't run this expensive experiment suite. Syntax compilation and correctness testing are not substitutes for performance validation.

## 4\. My overall assessment

(Promising foundation)

I like the architecture. In particular, separating the common protocol/application framework from the four event-loop implementations is the right approach for a comparative systems project.

I would preserve your existing architecture rather than redesign it.

One important distinction: **passing functional tests does not establish that the performance comparison is scientifically valid.** That requires validating the workload generator, measurement methodology, CPU placement and syscall accounting as well.

## 5\. What I suggest we do next

I'd approach this as an integration and review exercise, in this order\:

1. Review the four backend implementations against the common framework contract.
2. Resolve any integration and correctness issues.
3. Validate io\_uring, including SQPOLL and UDP.
4. Audit the load generator and experimental methodology.
5. Run the full benchmark suite and evaluate the resulting data.

**My recommendation\:** Let's not rewrite anything yet. First, let's establish which parts of the combined codebase are already correct and which need changes. That way we preserve everyone's contributions and avoid introducing regressions.

I can work through the existing code and produce a prioritized list of concrete fixes, with file names and function-level references.
