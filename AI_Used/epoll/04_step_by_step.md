# Step-by-Step: Where and How AI Contributed

1. **Understanding the project**  
   AI was first used to explain the overall project, its objectives, the four network I/O mechanisms, and the specific work required for the `epoll` component.

2. **Understanding `epoll`**  
   AI helped explain what an `epoll` server is, how the interest list and ready list work, the difference between level-triggered and edge-triggered operation, and how `epoll` can be used to handle multiple TCP clients in an echo server.

3. **Planning the team work**  
   AI was used to help divide the six-person project into independent tasks and identify the shared conventions required for the different implementations to work consistently.

4. **Initial implementation planning**  
   AI was used to break the `epoll` server into smaller subtasks, covering socket setup, non-blocking mode, `epoll` creation, connection state, the event loop, accepting clients, reading and writing data, cleanup, metrics, and shutdown handling.

5. **Implementation**  
   AI was used to develop the `epoll`-based TCP echo server incrementally from scratch. A teammate's `poll` implementation was used as a structural reference so that the `epoll` server followed the same project conventions while using the appropriate I/O mechanism.

6. **Testing the implementation**  
   AI was used to create and run tests for basic echo behavior, concurrent connections, large payloads, clean and abrupt disconnects, SIGTERM shutdown, and concurrency bursts.

7. **Debugging and handling edge cases**  
   AI was used to investigate issues involving partial writes, connection cleanup, data loss under load, syscall overhead, metrics, and port handling. The possible causes and fixes were analyzed before modifying the implementation.

8. **Code verification and peer review**  
   The implementation was compiled and tested rather than only reviewed by reading the source. A teammate's measured code review was also used to identify real problems, after which the fixes were applied and the same failure scenarios were re-tested.

9. **Cross-checking with other AI tools**  
   Gemini and ChatGPT were used as secondary tools to help fix and verify issues that had been identified in the `epoll` implementation.

10. **Build and documentation support**  
    AI was used to understand and prepare the Makefile and README for the `epoll` server, following the conventions of the team's existing files.

11. **Final validation**  
    The updated implementation was compiled, stress-tested, and reviewed again. Additional regressions found during this process were corrected before the code was considered complete.

12. **AI usage documentation**  
    AI was used to prepare the Markdown files documenting the tools, prompts, thought process, and step-by-step contributions, following the structure used for the project's other components.