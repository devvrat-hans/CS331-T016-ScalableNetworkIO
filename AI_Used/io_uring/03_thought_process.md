# How AI Was Integrated Into the Workflow

I used AI mainly because `io_uring` was a new programming model for me. The approach was not to ask AI for the complete server and submit the generated code. Instead, I used it when I needed to understand a concept, decide between implementation approaches, or debug something I had already been working on.

The overall workflow was roughly:

```text
Understand the requirement
        ↓
Learn the underlying concept
        ↓
Implement a small part
        ↓
Compile and test
        ↓
Find something I don't understand / something fails
        ↓
Ask AI
        ↓
Understand the explanation
        ↓
Modify the implementation
        ↓
Test again
```

## Starting with the networking model

I first made sure I understood the normal TCP echo server.

In particular, I needed to be clear about the roles of the listening socket and connected client sockets and the difference between `accept()`, `recv()`, and `send()`.

This was important because otherwise it would have been difficult to understand what the corresponding `io_uring` operations were actually doing.

## Understanding io_uring before extending the code

The main conceptual difficulty was that `io_uring` does not follow the same programming style as a simple blocking server.

I used ChatGPT to understand the relationship between:

* The application.
* SQEs.
* The kernel.
* CQEs.
* `user_data`.

The model I eventually used for thinking about the server was:

```text
Application prepares an SQE
          ↓
SQE is submitted
          ↓
Kernel performs the I/O
          ↓
Kernel produces a CQE
          ↓
Application processes the CQE
          ↓
Application submits the next operation
```

Once this was clear, implementing `accept`, `recv`, and `send` became much easier to reason about.

## Building incrementally

I did not start with multishot accept, batching, statistics, and all the error handling at once.

I first got a basic server running, then added the receive and send paths.

After each stage, I compiled the program and tested it using `nc`.

For example:

```bash
nc 127.0.0.1 9090
```

This gave me a simple way to verify that the server was actually receiving and echoing data.

## Handling multiple clients

As the implementation became more concurrent, I needed a way to keep state for individual clients.

I used a client structure containing the socket and its I/O state. `user_data` was then used to associate an operation's completion with the relevant client.

I intentionally kept the design simple by having at most one client I/O operation outstanding at a time:

```text
RECV
 ↓
SEND
 ↓
RECV
 ↓
SEND
 ↓
...
```

This made the buffer and client lifetime easier to manage.

## Dealing with partial sends

One implementation detail that needed more thought was sending the received data back to the client.

A send operation may complete after transmitting only part of the requested data. Therefore, I added send position/length tracking so that the remaining portion can be submitted again.

This was an example where AI was useful for explaining the reason behind the implementation rather than simply providing code.

## Using Claude as a second opinion

After the server was working, I used Claude to review the implementation.

The review produced several suggestions. I did not treat the review as a list of mandatory changes.

For each suggestion, I considered whether it:

1. Was required for correctness.
2. Was useful for the project's benchmark.
3. Was only an optional optimization.
4. Could make the implementation unnecessarily complicated.

For example, removing per-message printing was useful because terminal output could interfere with benchmarking. Increasing the queue depth and batching completions also made sense for the intended workload.

Multishot accept was another improvement that was relevant to the `io_uring` design.

On the other hand, I did not add command-line port support because the project did not require it. I also did not add `O_NONBLOCK` simply because another implementation used it; the `io_uring` implementation was kept based on its own I/O model.

## Testing decisions

AI explanations were not treated as proof that something worked.

After making changes, I compiled and ran the program locally.

I tested basic echo functionality with `nc`, checked shutdown behaviour, and later checked the number of threads using:

```bash
ps -o pid,nlwp,cmd -C io_uring_echo_server
```

The process showed `NLWP = 1`, confirming that the implementation remained single-threaded.

## Final use of AI

By the end of the implementation, AI had been useful in three main ways:

### Learning

Understanding concepts that were new to me, especially SQE/CQE, asynchronous completion, `user_data`, multishot accept, and partial sends.

### Implementation and debugging

Helping me work through individual parts of the server and understand compiler, linker, and runtime issues.

### Review and decision-making

Providing another opinion on possible performance and correctness improvements.

The important part of the workflow was that I did not treat the AI output as the final answer. I used the explanations to understand the problem, made the changes, and then tested the resulting implementation myself.
