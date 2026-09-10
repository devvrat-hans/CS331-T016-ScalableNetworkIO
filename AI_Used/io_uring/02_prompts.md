# Prompts Given to AI Tools

Most of my interaction with ChatGPT was conversational. I usually asked a question when I reached a part of the implementation that I did not understand, rather than writing one large prompt asking for the complete project.

The following are representative prompts from the development process.

## Understanding the TCP basics first

Before working with `io_uring`, I wanted to make sure I understood the normal TCP server.

Some questions I asked were:

1. I am confused about accept, send and receive. Can you explain what each one actually does in a TCP server?
2. Can you explain the complete flow of a simple TCP echo server from the server starting to a client connecting and sending data?
3. Why does accept create another socket? Isn't the listening socket itself used for communication with the client?

This helped me understand the difference between the listening socket and the socket returned by `accept()`.

## Moving to io_uring

Once the normal server flow was clear, I started asking about the equivalent `io_uring` operations.

1. I understand accept/recv/send in a normal TCP server, but I'm confused about how the same flow works in io_uring. Can you explain it step by step?
2. In io_uring, who actually performs the recv? Does my program do it or does the kernel do it?
3. Can you explain SQE and CQE using my echo server example?
4. What exactly happens after I submit an SQE?

The goal here was mainly to understand the programming model before adding more code.

## Understanding CQEs and user_data

After getting the basic receive and send operations working, I had questions about how the server knows which client an operation belongs to.

1. I have the recv and send working now. Can you explain what user_data is doing here and why we need it?

2. What is stored in the CQE after an operation completes?

3. What does cqe->res mean for accept, recv and send?

4. Can completion order be different from submission order?

These discussions helped me understand how the completion queue can be used to manage multiple clients.

## While implementing the server

As I added functionality, I asked smaller implementation questions rather than replacing the whole program.

Examples include:

1. Can you help me add recv to the io_uring echo server we already have?

2. Now how do I submit the send operation after recv completes?

3. Explain this part of the code line by line.

4. Why do we need to call io_uring_cqe_seen?

5. What happens if send doesn't send the complete buffer?

I then compiled and tested the changes myself.

## Per-client state and partial sends

When the server became more involved, I asked about how to maintain state for each client.

1. Why do we need a separate structure for each client?

2. Can I store the operation type directly in the client structure instead of allocating a separate operation structure?
3. If I have one recv or send outstanding per client, how does that help with buffer lifetime?

I also asked about partial sends because I did not want to assume that one send operation always transmits the entire buffer.

## Performance-related questions

After the basic server was working, I started looking at how the implementation could be made more suitable for benchmarking.

Examples:

1. Why are we processing only one CQE at a time? Can we process multiple completions together?

2. Explain io_uring_submit_and_wait and why it can be useful here.

3. What is the benefit of draining all CQEs before calling submit_and_wait again?

4. Does batching reduce the number of user-kernel interactions?

## Multishot accept

I also asked about reducing the repeated accept submissions:

1. Right now we submit one accept operation and then submit another after a client connects. Is there a better way with io_uring?
2. What is multishot accept in io_uring?

3. Can one accept SQE generate multiple CQEs?

4. What does IORING_CQE_F_MORE mean?

This led to the multishot accept implementation that is present in the final server.

## Claude review

For the code review, I gave Claude the implementation and asked for possible issues and improvements.

One of the follow-up questions I used was:

Claude suggested these changes to my io_uring server. Which ones are actually necessary for our project and which are just optimizations?

I then discussed the suggestions further instead of applying them all directly.

The suggestions considered included batching, multishot accept, queue depth, logging, client lifetime, partial sends, and `O_NONBLOCK`.

## Debugging and build

I also used ChatGPT when I encountered specific errors.

For example:

1. My program is giving this linker error. What does it mean?

2. Why is make saying "Nothing to be done for all"?

3. My Makefile is using pkg-config but it is not adding the liburing flags. What should I check?

These were used to understand the error and fix the build rather than changing unrelated parts of the implementation.

## Documentation

After the implementation was stable, I asked ChatGPT to help organize the README around the code that I actually had.

For example:

1. Update the README to reflect the current architecture of my io_uring server.

2. I added multishot accept, batching, partial sends and statistics. What parts of the README need to be updated?

The README was then checked against the actual implementation.
