# Prompts Given to AI Tools

Most of my interaction with ChatGPT was conversational. I usually asked a question when I reached a part of the implementation that I did not understand, rather than writing one large prompt asking for the complete project.

**Note:** I asked some of the basic questions in ChatGPT's incognito tab to avoid unnecessary conversation in the main chat. The purpose was to keep the main chat focused and preserve the context of the project, while using the incognito chat for quick questions.

The following are actual prompts from the development process.

1. Scalable Network I/O: From select to io_uring

    Project Description: Build and compare single-threaded TCP echo servers using four distinct Linux socket-handling mechanisms: select, poll, epoll, and io_uring. Implement each server paradigm from scratch and explore the shift from synchronous notification to truly asynchronous completion queue processing pattern. By using appropriate load generators (tcpkali, wrk etc.), students should benchmark and profile the behavior of these mechanisms by varying the connection loads (10, 100…) to observe system call overhead, memory scalability, and CPU efficiency.

    Tools/Technologies: C/C++/Rust, Linux network stack, io_uring

    Expected Outcomes: Upon completion, students will deliver fully functional C/C++ implementations for all four engines alongside a benchmarking report analyzing throughput, latency distributions, and system call frequencies. Students will demonstrate a clear understanding of O(N) vs O(1) scaling bottlenecks, explain why select and poll degrade under high concurrency, and identify the trade-offs that make io_uring's ring-buffer design superior to epoll for minimizing user-to-kernel context switches.

    This is the project of the course Computer Networks we have been assigned.

    First explain me the project in detail, then all the concepts, then my part is io_uring so explain that in detail, then how can I do it

    Ask any clarifying questions first before starting

2. Answer of the clarifying questions asked by CHatGPT
    1. C
    2. Ubuntu/WSL
    3. yes other members are implementing select, poll, epoll and I am doing io\_uring
    4. The project document contains only the content I pasted
    5. explain everything, I don't have much experience
    6. yes, benchmarking is going to be done by other member
    7. code + report

3. ok it seems a good idea but just one thing I would want to clear before starting building normal TCP server

    can you explain this flow a bit clearly like whose part is which (client/server/program):
    ```
    START
    │
    ▼
    create listening socket
    │
    ▼
    create io_uring
    │
    ▼
    submit ACCEPT
    │
    ▼
    wait for CQE
    │
    ▼
    client connected
    │
    ▼
    submit RECV
    │
    ▼
    wait for CQE
    │
    ▼
    data received
    │
    ▼
    submit SEND
    │
    ▼
    wait for CQE
    │
    ▼
    data echoed
    │
    ▼
    submit RECV again
    │
    └──────────────► repeat
    ```

4. Can you explain the complete flow of a simple TCP echo server from the server starting to a client connecting and sending data?

5. yes, explain all other parts in short and then move to io_uring which is my part

6. In io_uring, who actually performs the recv? Does my program do it or does the kernel do it?

7. You said that completion order may be different from submission order, why is it so? is it asynchronous?

8. can you just give me the whole workflow of io_uring from start to end, I am confusing in term Accept, send, receive


### Claude review given by one of my team member

For the code review, my friend gave Claude the implementation to have a sync with other parts and asked for possible issues and improvements.

Claude suggested these changes to my io_uring server. Which ones are actually necessary for our project and which are just optimizations?

9. I checked my final code with claude and it gave some feedback

    so before starting any changes , I first want to understand what claude has suggested and why and even it is worth updating

    this is the message by claude:

    Niraj — io_uring engine:

    Remove all printf from the message loop (3 per message, one prints the whole payload with %s). Each is a write() syscall — it will dominate every benchmark number.
    io_uring_submit() is called once per operation. That's ~4 io_uring_enter per message, worse than epoll (3) and level with select (4) — it erases io_uring's whole advantage. Fix: one io_uring_submit_and_wait(&ring, 1) at the top of the loop, then drain all completions with io_uring_for_each_cqe + io_uring_cq_advance, and never call io_uring_submit() inside the handlers. Syscalls/msg then drops below 1 under load.
    No SIGINT/SIGTERM handler and no statistics printed at all. while (1) means Ctrl-C kills it with zero output — the benchmarker gets nothing from this engine. Needs a running flag plus a stats block on shutdown (bytes, ops, syscalls) matching the other three.
    QUEUE_DEPTH 256 is too small for the 1000-connection sweep — CQ ring is 512 slots vs ~1000 in-flight ops, so it overflows and you measure an artifact. Raise to 1024+.
    Drop the per-operation malloc/free of operation_t (2 per message). Only one op is outstanding per client, so store the op type in client_t or tag it into the client pointer's low bits.
    Use-after-free if io_uring_submit() fails: he free()s the client while the prepared SQE is still in the ring, so the next submit hands the kernel a freed pointer. On submit failure, don't free — retry the submit.
    Only one accept in flight — serialises connection setup at 1000 clients. Use io_uring_prep_multishot_accept.
    Contract mismatches: sockets are never set O_NONBLOCK (others are), buffer is BUFFER_SIZE + 1 vs 4096 elsewhere, and main(void) takes no CLI port argument. 

    Worth telling him what's right so the message doesn't read as all-negative: port 9090, backlog 128, SO_REUSEADDR, reading cqe->res before io_uring_cqe_seen, and the one-op-per-client invariant that keeps his free(client) paths safe.

    Flag separately to the team: this engine needs liburing-dev and -luring on the benchmark machine, unlike the other three.

10. What is multishot accept in io_uring?

11. Explain this part of the code line by line like what is the overall flow then exact line by line meaning
    ```
    static int submit_accept(struct io_uring *ring, int server_fd) {
        struct io_uring_sqe *sqe;

        // Get an empty SQE
        sqe = io_uring_get_sqe(ring);

        if (!sqe) {
            fprintf(stderr, "Failed to get SQE for ACCEPT\n");
            return -1;
        }

        // Prepare MULTISHOT ACCEPT operation
        io_uring_prep_multishot_accept(sqe,server_fd,NULL,NULL,0);

        /*
        * ACCEPT does not have a client_t yet so we use a special marker to identify
        * ACCEPT completions later. The same marker is reused
        * for every completion from the multishot request.
        */
        io_uring_sqe_set_data(sqe, &accept_marker);

        return 0;
    }
    ```

12. has all the thing which was discussed in must chnage or in good optimization done?

13. Debugging: 
    ```ps -o pid,nlwp,cmd -C io_uring_echo_server
        PID NLWP CMD
    ```

14. Update README file with the current architecture of my io_uring server.

15. io_uring: add a Makefile using pkg-config --cflags --libs liburing. Print machine-readable final statistics, including connections, recv/send operations, zero-byte recv completions, total CQEs, and errors. Confirm it remains one thread under load

16. What is this error?
    ```
    make: pkg-config: Not a directory
    make: pkg-config: Not a directory
    gcc -Wall -Wextra -O2  io_uring_echo_server.c -o io_uring_echo_server 
    /usr/bin/ld: /tmp/cczq8yo8.o: in function `submit_send':
    io_uring_echo_server.c:(.text+0x16c): undefined reference to `io_uring_submit'
    /usr/bin/ld: /tmp/cczq8yo8.o: in function `submit_recv.constprop.0':
    io_uring_echo_server.c:(.text+0x2ac): undefined reference to `io_uring_submit'
    /usr/bin/ld: /tmp/cczq8yo8.o: in function `main':
    io_uring_echo_server.c:(.text.startup+0x19c): undefined reference to `io_uring_queue_init'
    /usr/bin/ld: io_uring_echo_server.c:(.text.startup+0x1d6): undefined reference to `io_uring_submit'
    /usr/bin/ld: io_uring_echo_server.c:(.text.startup+0x249): undefined reference to `io_uring_submit_and_wait'
    /usr/bin/ld: io_uring_echo_server.c:(.text.startup+0x7eb): undefined reference to `io_uring_queue_exit'
    /usr/bin/ld: io_uring_echo_server.c:(.text.startup+0x896): undefined reference to `io_uring_queue_exit'
    /usr/bin/ld: io_uring_echo_server.c:(.text.startup+0x8bd): undefined reference to `io_uring_queue_exit'
    collect2: error: ld returned 1 exit status
    make: *** [Makefile:12: io_uring_echo_server] Error 1
    ```

