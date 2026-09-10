# Step-by-Step AI Contributions

This document records the main points during development where AI was useful. The project was developed incrementally, so AI involvement was not limited to one stage.

## 1. Understanding the existing TCP server

**Tool: ChatGPT**

I started by understanding the normal TCP echo server before moving to `io_uring`.

I used ChatGPT to clear up questions about `accept()`, `recv()`, and `send()`, especially the difference between the listening socket and the socket returned by `accept()`.

This gave me the basic flow:

```text
Client connects
      ↓
accept()
      ↓
recv()
      ↓
send()
      ↓
recv()
      ↓
...
```

This became the basis for understanding the equivalent `io_uring` flow.

## 2. Learning the io_uring submission/completion model

**Tool: ChatGPT**

I then used ChatGPT to understand SQEs and CQEs and how the kernel interacts with them.

A key question was:

I understand accept/recv/send in a normal TCP server, but I'm confused about how the same flow works in io_uring. Can you explain it step by step?

The important concept I took from this was that the application prepares and submits an operation, while the kernel performs it and reports the result through a CQE.

This helped me understand the event loop before adding more functionality.

## 3. Getting the first io_uring operation working

**Tool: ChatGPT**

I set up `liburing` and worked on the basic server socket first.

I then added an asynchronous accept operation and learned how the result in the CQE represents the newly accepted client socket.

I compiled and tested the program locally after this stage.

## 4. Adding receive and send

**Tool: ChatGPT**

Once accept was working, I added receive and send operations.

I used ChatGPT to understand functions such as:

```c
io_uring_prep_recv()
io_uring_prep_send()
```

and how to interpret their completion results.

After both operations were working, I tested the echo functionality using:

```bash
nc 127.0.0.1 9090
```

At this point, the server had the basic:

```text
ACCEPT → RECV → SEND → RECV → SEND
```

flow.

## 5. Understanding user_data and client state

**Tool: ChatGPT**

After getting receive and send working, I had to understand how a completion could be connected to the correct client.

I specifically asked:

I have the recv and send working now. Can you explain what user_data is doing here and why we need it?

I then used `user_data` to associate operations with the relevant client structure.

The client structure keeps the socket, buffer, operation state, and send progress.

This allowed different clients to maintain independent state while still being handled by one server thread.

## 6. Handling partial sends

**Tool: ChatGPT**

I asked what happens if a send operation does not transmit the complete buffer.

The implementation was changed to keep track of the send position and remaining length.

Conceptually:

```text
Received 100 bytes
       ↓
Send 60 bytes
       ↓
40 bytes remain
       ↓
Send remaining 40 bytes
```

This avoided assuming that a single send completion always means the entire response has been transmitted.

## 7. Simplifying operation management

**Tool: ChatGPT**

I discussed whether a separate dynamically allocated operation structure was necessary for every I/O request.

Since the implementation maintains at most one outstanding client I/O operation at a time, I kept the operation state directly in the client structure.

This simplified the lifetime relationship between:

```text
client
buffer
operation
CQE
```

and avoided unnecessary allocation for every operation.

## 8. Claude code review

**Tool: Claude**

After the main server functionality was working, I used Claude to review the implementation.

The review pointed out several areas worth considering, including:

* Per-message output.
* CQE batching.
* Queue depth.
* Multishot accept.
* Partial sends.
* Client lifetime.
* Error handling.
* Possible `O_NONBLOCK` usage.

This review was useful mainly because it gave me another perspective on the implementation.

## 9. Evaluating the review instead of applying everything

**Tool: ChatGPT + Claude**

I then discussed the suggestions and separated them into things that were important for the project and things that were optional optimizations.

For example, I kept:

* CQE batching.
* A larger queue depth.
* Multishot accept.
* Proper partial-send handling.
* Client lifetime handling.
* Removal of per-message logging.

I did not add everything suggested in the review.

For example, command-line port support was not necessary for the project, and I did not add `O_NONBLOCK` simply to make the implementation look like the `poll` implementation.

This was one of the main points where AI was used for **decision support rather than direct code generation**.

## 10. Adding multishot accept

**Tool: ChatGPT**

I asked about multishot accept after noticing that a normal accept operation would have to be submitted again after a successful connection.

I learned about:

```c
io_uring_prep_multishot_accept()
```

and:

```c
IORING_CQE_F_MORE
```

I then incorporated multishot accept into the server and tested it with multiple client connections.

The resulting idea is:

```text
             One ACCEPT SQE
                    ↓
                  Kernel
              ↙     ↓     ↘
           Client  Client  Client
              ↓     ↓     ↓
             CQE   CQE   CQE
```

## 11. Batching completions

**Tool: ChatGPT**

I also discussed why processing only one CQE at a time could add unnecessary overhead.

The event loop was changed to use:

```c
io_uring_submit_and_wait(&ring, 1);
```

and then process all currently available CQEs.

The idea was:

```text
Submit / Wait
     ↓
CQE 1
CQE 2
CQE 3
CQE 4
     ↓
Process available completions
```

This was particularly relevant because the project is intended to compare the behaviour of different network I/O mechanisms under load.

## 12. Removing per-message logging

**Tool: ChatGPT + Claude**

During the performance discussion, I realized that printing every message received by the server could become a bottleneck during benchmarking.

I removed the per-message logging from the main I/O path.

The goal was to make the benchmark measure the I/O implementation rather than terminal output.

## 13. Graceful shutdown

**Tool: ChatGPT**

I added handling for `SIGINT` and `SIGTERM`.

The server can therefore be stopped with `Ctrl+C` while still performing its normal cleanup.

The cleanup includes closing client sockets, freeing client structures, destroying the io_uring instance, and closing the listening socket.

## 14. Runtime statistics

**Tool: ChatGPT**

I added statistics so that the server could provide useful information after a benchmark run.

The server records:

* Connections.
* Receive operations.
* Send operations.
* Zero-byte receive completions.
* Errors.
* Bytes received.
* Bytes sent.
* Total CQEs.
* Event-loop submit/wait calls.

The final output has the form:

```text
STAT connections=... recv_ops=... send_ops=... zero_byte_recv=... total_cqes=... errors=... bytes_received=... bytes_sent=... enter_calls=...
```

This makes the output easier to process in scripts or compare between runs.

## 15. Makefile and build debugging

**Tool: ChatGPT**

I added a Makefile using `pkg-config` for the `liburing` compiler and linker flags.

During this process I encountered a build problem where `pkg-config` was not being used correctly, which resulted in the `-luring` flag being missing and linker errors such as:

```text
undefined reference to `io_uring_submit'
```

I used ChatGPT to understand what the linker error meant and then fixed the build setup.

The final compilation command correctly includes:

```text
-luring
```

## 16. Verifying the single-threaded design

**Tool: ChatGPT**

Because the project compares I/O mechanisms rather than thread-per-client designs, I also verified that the server remains single-threaded.

While the server was running, I used:

```bash
ps -o pid,nlwp,cmd -C io_uring_echo_server
```

and checked that:

```text
NLWP = 1
```

This confirms that multiple clients are being handled by the same server thread rather than by creating additional worker threads.

## 17. README and final documentation

**Tool: ChatGPT**

Finally, I used ChatGPT to organize the README around the actual implementation.

The README was updated to describe the features that were actually implemented, including:

* `io_uring` submission/completion model.
* Multishot accept.
* CQE batching.
* Per-client state.
* Partial sends.
* Error handling.
* Graceful shutdown.
* Runtime statistics.
* Single-threaded design.
* Build instructions and limitations.

The documentation was checked against the final code rather than being created separately from it.

## Summary

The main contribution of AI throughout the project was helping me get through concepts and implementation issues that I was unfamiliar with.

The development process was generally:

```text
I encounter a concept/problem
          ↓
Ask ChatGPT / Claude
          ↓
Understand the explanation
          ↓
Implement or modify the code
          ↓
Compile and test
          ↓
Keep, modify, or reject the suggestion
```

AI therefore acted mainly as a **learning assistant, debugging aid, and code reviewer**, while the implementation decisions were made based on the project requirements and testing of the actual program.
