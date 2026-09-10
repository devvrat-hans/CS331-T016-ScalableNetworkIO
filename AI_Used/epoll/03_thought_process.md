# Thought Process: How AI Was Integrated Into the Workflow

AI was used as a supporting tool throughout the development process, with the main approach being **understand first, implement gradually, and verify before accepting the result**.

1. **Understand the project before implementation.**  
   AI was first used to understand the overall project requirements, the purpose of scalable network I/O, and the differences between `select`, `poll`, `epoll`, and `io_uring`. This helped build an understanding of readiness-based I/O and the specific role of `epoll`.

2. **Understand `epoll` before writing code.**  
   AI was used to explain the `epoll` interest list, ready list, level-triggered and edge-triggered behavior, non-blocking sockets, and how an `epoll`-based TCP echo server handles multiple clients.

3. **Plan the work before implementation.**  
   AI was used to divide the implementation into smaller subtasks so that the server could be developed incrementally. Each subtask was understood before moving to the next one instead of generating the complete implementation immediately.

4. **Follow the project's existing implementation style.**  
   A teammate's `poll_echo_server.c` was used as a reference for the overall server structure, signal handling, metrics, cleanup, and coding conventions. AI helped adapt those conventions to `epoll` without losing the behavior expected from the project.

5. **Use AI for implementation and debugging.**  
   AI was used to write and refine the `epoll` server, test script, Makefile, and README. Potential problems such as partial writes, cleanup ordering, data loss, syscall overhead, and connection handling were discussed and addressed during development.

6. **Cross-check the generated work.**  
   The implementation was not accepted simply because AI produced it. The code was compiled and tested, and different AI tools were used to cross-check specific issues. Teammate feedback and measured code-review results were also considered.

7. **Verify fixes against real failure scenarios.**  
   When peer review identified problems such as data loss under load, metric errors, syscall overhead, and port handling, the fixes were tested against the same or equivalent failure scenarios. A later rewrite was also compiled and stress-tested to identify regressions.

8. **Use AI for supporting documentation.**  
   AI was used to prepare the Makefile, README, testing script, and Markdown files documenting AI usage, while keeping the `epoll` implementation consistent with the rest of the project.

9. **Perform final verification.**  
   The final implementation was compiled, tested, reviewed, and corrected where necessary before being considered ready for integration. AI was treated as an assistance tool, while the final decisions and integration were performed by the team.