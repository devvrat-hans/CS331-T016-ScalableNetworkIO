# AI Tools Used

## ChatGPT

**Tool:** ChatGPT

ChatGPT was the main AI tool I used during the project.

I mainly used it as a learning and debugging assistant while working on the `io_uring` implementation. At the beginning, I used it to understand the basic TCP server model before trying to implement the same flow with `io_uring`.

Some of the areas where I used ChatGPT were:

* Understanding `accept()`, `recv()`, and `send()` in a normal TCP server.
* Understanding how the normal TCP flow maps to `io_uring`.
* Learning the purpose of SQEs and CQEs.
* Understanding `cqe->res` and `user_data`.
* Understanding how multiple clients can be handled by a single-threaded server.
* Implementing and debugging individual parts of the server.
* Understanding partial sends and maintaining per-client state.
* Learning about multishot accept.
* Understanding CQE batching and `io_uring_submit_and_wait()`.
* Discussing memory/lifetime issues and error handling.
* Adding runtime statistics and graceful shutdown.
* Debugging the Makefile and `liburing` linking.
* Organizing and checking the README against the final implementation.

I generally did not use ChatGPT by asking it to generate the entire project at once. I asked questions about concepts or specific parts of the implementation, made changes, compiled the code, and tested it locally.

## Claude

**Tool:** Claude

Claude was mainly used later in the development as a second opinion on the implementation.

I used Claude to review the `io_uring` server and suggest possible improvements related to performance, correctness, and scalability.

Some of the suggestions included:

* Removing per-message printing.
* Processing multiple CQEs together.
* Increasing the queue depth.
* Using multishot accept.
* Avoiding unnecessary per-operation allocations.
* Checking client lifetime and possible use-after-free situations.
* Handling partial sends correctly.
* Considering the use of `O_NONBLOCK`.

I did not apply all of these suggestions automatically. I discussed and evaluated them based on the project requirements and the design of the `io_uring` implementation before deciding which changes were useful.

## How AI Was Used

The main use of AI in this project was:

**Learning → implementation help → debugging → review → decision-making.**

The code was compiled and tested locally during development. AI suggestions were treated as guidance, and I tried to understand the reason behind a change before incorporating it.
