## Step-by-Step: Where and How AI Contributed

| Stage | What was needed | AI contribution |
|---|---|---|
| **1. Understanding the assignment** | Make sense of a dense project brief | Claude explained the project's core question (readiness vs completion notification), what deliverables were expected, and how the four mechanisms conceptually differ |
| **2. Team task division** | Split the project across 6 people without blocking dependencies | Claude proposed a 6-way split (4 mechanism implementations + benchmarking harness + profiling/report), and defined the shared contract (port convention, echo protocol, metrics format) needed for the split to actually be non-blocking |
| **3. Concept building (my part: epoll)** | Understand epoll specifically before coding it | Claude explained epoll vocabulary (interest list, ready list, level- vs edge-triggered), walked through why/how/what/when/where/who, and ran a step-by-step dry-run trace over simulated connections |
| **4. External learning resources** | Video resources to reinforce understanding | Claude searched for and curated a sequence of YouTube videos, ordered from foundational (the C10K problem) to concept (epoll vs poll) to applied (an epoll echo server walkthrough) |
| **5. Implementation planning** | Break the actual coding task into manageable pieces | Claude defined 10 subtasks in build order, each explained conceptually before being coded |
| **6. Code implementation** | Write `epoll_echo_server.c` from scratch (raw syscalls only) | Claude wrote each subtask's code incrementally, explaining every design decision (e.g., `.data.ptr` vs `.data.fd`, partial-write handling, cleanup ordering), then assembled the full file matching a teammate's poll-server conventions |
| **7. Verification** | Confirm the server actually works | Claude compiled the code and ran functional tests (echo correctness, graceful shutdown) directly, rather than only presenting untested code |
| **8. Test automation** | A reusable test suite | Claude wrote `test_epoll_server.sh` (8 test cases: basic echo, concurrency, large payloads, clean/abrupt disconnects, SIGTERM shutdown, concurrency burst), and iterated on it after a stray tool issue and a weak assertion were found |
| **9. Build tooling** | A Makefile matching the team's conventions | Claude explained what a teammate's Makefile did line by line, confirmed the epoll server compiled cleanly under the same strict flags (`-Wpedantic`), and produced a matching Makefile |
| **10. Documentation** | A README matching the team's format | Claude produced a README mirroring a teammate's structure, adapted to epoll-specific behavior and limitations, and revised it after a submission-scope change (test script excluded) |
| **11. Bug triage from peer review** | Assess a teammate's measured code review (data loss, metric errors, syscall overhead, port mismatch) | Claude applied every fix, then rebuilt and re-ran the reviewer's exact failure scenarios (2 MiB flood, RST during final message, port override) to confirm each fix actually held |
| **12. Re-review of an updated file** | Validate a further-rewritten version of the code | Claude compiled and stress-tested the rewrite directly (not just read it), confirmed all original issues were fixed, and identified two new regressions (dropped error-checking on two syscalls, an undocumented change to a team-agreed constant) for correction |
| **13. Gemini / ChatGPT fixes** | The broken code is fixed | The broken code is fixed |

---