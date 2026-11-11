# Explain Sockets And I O

**User:** Anonymous  
**Created:** 11/08/2026 1:07:19  
**Updated:** 11/08/2026 2:58:12  
**Exported:** 11/08/2026 3:14:31  
**Link:** [https://chatgpt.com/c/6af4c3ec-a354-83e8-bffa-c1f5282fbbf3](https://chatgpt.com/c/6af4c3ec-a354-83e8-bffa-c1f5282fbbf3)  

## Prompt:
08/11/2026, 1:07:57 am

Hi! I am working on a Computer Networks project Scalable Network I/O: From select to io_uring.

My assigned part was implementing the io_uring engine in C. Before getting into my implementation, I want to properly understand the basics because I am not very clear about how all these networking and I/O mechanisms work internally.

Can we start from the very basics? First explain what sockets and file descriptors are, how a TCP server communicates with multiple clients, and what blocking and non-blocking I/O actually mean.

use simple examples and build things step by step. Don't directly jump into io_uring yet. I want to understand the problem that these different I/O mechanisms are trying to solve.

## Response:
08/11/2026, 1:08:07 am · gpt-6-t-mini-mini

We’ll build the mental model in layers: what a socket represents, why the OS gives it a file descriptor, how a TCP server handles several connections, and what changes between blocking and non-blocking calls. Then we’ll connect those ideas to why scalable I/O mechanisms are needed—without jumping into `io_uring`\.

> **Explaining networking basics**
>
> I’m preparing a step-by-step explanation distinguishing sockets from file descriptors, including how operating systems represent endpoints. I’ll keep it practical and offer a small follow-up exercise rather than assume recurring reminders.
>
> **Explaining socket behavior**
>
> I’m outlining TCP socket creation, binding, listening, accepting, and data transfer. I’ll distinguish listening and connected sockets, explain blocking versus nonblocking calls, and show why readiness-based concurrency helps servers scale.
>
> **Bridging toward scalable I/O**
>
> I’m clarifying that a descriptor is a handle to a kernel socket, while TCP provides a byte stream without message boundaries. Next, I’ll connect blocking behavior to readiness-based designs before discussing completion-based approaches.
>
> **Explained networking basics, socket behavior, and scalable I/O**
>
> 
>
> Worked for 42s

# 1\. What are sockets and file descriptors?

Let's start with a simple situation.

Suppose you open a website in your browser. Your browser needs to communicate with a server somewhere on the internet.

How does your program communicate with another program running on a different machine?

The answer is **sockets**\.

## 1.1 What is a socket?

A socket is a communication endpoint that allows two programs to exchange data.

Think of it like a telephone\:

- Your phone is one endpoint.
- The other person's phone is another endpoint.
- You establish a connection.
- Both sides can send and receive information.

Similarly, a client and server use sockets to communicate.

[![PPT - Introduction to Socket Programming PowerPoint Presentation, free download - ID:9220511](https://images.openai.com/static-rsc-4/jRYwxYEnLWSPkl2tQD-GcmQMu5Ud8mcIWPw01rJW4hfmDGB5vAwLekgcPHcrDrAYwOAjXfyoWcneeFreF-4R8hUD_wEepuGC4RIH7CMObdWFzLpdUb5Pv0FP4OjiDsCjhwxNffrAjqExuK200L9pcVkMBOz5pVo7w5ljelsNadwsW2D2eBDileYFmJK2uVvh?purpose=fullsize)](https://www.slideserve.com/osborne/introduction-to-socket-programming-powerpoint-ppt-presentation)

For example\:

- Your browser is the client.
- Google's web server is the server.
- Both use sockets to communicate.
- TCP can provide reliable communication between those sockets.

A socket is not itself a physical connection or a network cable. It is an abstraction provided by the operating system.

## 1.2 What is a file descriptor (FD)?

Now comes an important operating-system concept.

When your program opens a file, it gets an integer called a **file descriptor**\.

For example\:

```c
int fd = open("data.txt", O_RDONLY);
```

Suppose `open()` returns `3`\.

That means\:

```text
fd = 3
```

Your program uses this integer whenever it wants to read from or interact with that file.

But why does the OS use an integer instead of making your program directly access the file?

Because the operating system manages resources internally. The file descriptor is simply a handle through which your program requests operations on those resources.

A simplified view\:

[![PPT - Linux Programming PowerPoint Presentation, free download - ID:4321241](https://images.openai.com/static-rsc-4/MAL5iq1njhTaujEUUTOfcM9CUNJ0qNy9r0LlwEiVJzyJYmyQhy_m1z0aVfv9hN6dwmD3O6bjN25Df-cWFQ4PO_z1fHZ2OF2xv00Zg7h1NaretlWwYaBvQuhE5t-HpcnXP5TwSMLMskVZJHxA5eJRABeOAV_8aZFRJQPwXrv9wO8Hpt1dK_F7zssMzYpAUwb8?purpose=fullsize)](https://www.slideserve.com/olaf/linux-programming-powerpoint-ppt-presentation)

For example, a process might have\:

| File descriptor | Resource |
|--------|--------|
| 0 | Standard input |
| 1 | Standard output |
| 2 | Standard error |
| 3 | File: data.txt |

Notice that standard input, output and error also use file descriptors.

Now here is the important part\:

**Sockets also use file descriptors\!**

For example\:

```c
int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
```

Suppose this returns `4`\.

Now\:

```text
sock_fd = 4
```

This descriptor refers to a socket managed by the operating system.

You can use it with functions such as\:

```c
send(sock_fd, ...);
recv(sock_fd, ...);
close(sock_fd);
```

So remember\:

- A socket is the communication endpoint.
- A file descriptor is the integer handle your program uses to access that socket.

Linux represents sockets through file descriptors, allowing programs to use familiar system-call interfaces for different kinds of resources.

---

# 2\. How does a TCP server communicate with clients?

Let's build a TCP server step by step.

Imagine you are writing a chat application.

Your server runs on a machine with IP address\:

```text
192.168.1.10
```

And it listens on port\:

```text
8080
```

A client needs to connect to this server.

## 2.1 What are IP addresses and ports?

Think of an IP address as the address of a building.

A port is like a particular department or service inside that building.

For example\:

```text
IP address:  192.168.1.10
Port:        8080
```

Together, they identify the network endpoint that the client wants to contact.

A machine can run multiple network applications, each listening on different ports.

For example\:

| Application | Port |
|--------|--------|
| HTTP server | 80 |
| HTTPS server | 443 |
| Your test server | 8080 |

These are conventional port assignments; your test server can use another available port.

## 2.2 Creating a TCP socket

A server begins by creating a socket\:

```c
int server_fd = socket(AF_INET, SOCK_STREAM, 0);
```

Let's understand the arguments\:

- `AF_INET`\: Use IPv4 addressing.
- `SOCK_STREAM`\: Use a stream-oriented socket (TCP in this example).
- `0`\: Let the OS select the default protocol for this socket type and address family.

The OS creates a socket and returns its file descriptor.

Suppose\:

```text
server_fd = 3
```

At this point, we have a socket, but it is not yet listening for incoming connections.

## 2.3 Binding the socket

Next\:

```c
bind(server_fd, ...);
```

Binding associates the socket with a local IP address and port.

For example\:

```text
192.168.1.10:8080
```

This tells the OS that our server wants to use port 8080 on that address.

## 2.4 Listening for connections

Next\:

```c
listen(server_fd, 128);
```

This converts the socket into a listening socket.

It tells the OS\:

> I am a server. Start accepting incoming connection requests on this socket.

The `128` is the requested listening backlog, which relates to how many pending connections the OS can queue. It is not a limit of 128 total connected clients.

Our server is now waiting for clients.

## 2.5 Accepting a client

Suppose a client wants to connect.

The server calls\:

```c
int client_fd = accept(server_fd, NULL, NULL);
```

This is where something very important happens.

**`accept()` returns a new file descriptor for the client connection.**

The original `server_fd` is still the listening socket.

The new `client_fd` represents the connection with that particular client.

For example\:

```text
server_fd = 3       Listening socket
client_fd = 4       Client A's connection
```

The listening socket and the connected socket have different responsibilities.

| Socket | Responsibility |
|--------|--------|
| Listening socket | Wait for new connection requests |
| Client socket | Exchange data with an accepted client |

This distinction is fundamental to understanding network servers.

[![IP Sockets - Networking Fundamentals - Part 1 | Linux](https://images.openai.com/static-rsc-4/IIkEQ5Q0iS56J2lGrDltfUK2y3qfnsCW3gl5WHsTR7AfDrlsc-oqyrvka0U39TUAF2nC_DTH-6UoLKQYJxqC1oUVf3qEMgAB7UQqurZwdq_KRUD3LYSQn_pMFXh4MmpEp6gN2zIq58yiV-JKyeWl3jQSw9J9MK6j7Llhy7oXG1oRONCVk2XQMSMA97RyXNRG?purpose=fullsize)](https://tusharf5.com/posts/ip-sockets-unix-nodejs-1/)

## 2.6 Sending and receiving data

Once the connection is established, the server can communicate with the client.

Receiving data\:

```c
char buffer[1024];

int n = recv(client_fd, buffer, sizeof(buffer), 0);
```

Sending data\:

```c
send(client_fd, "Hello Client", 12, 0);
```

The client also has its own socket and can send and receive data.

TCP provides a reliable, ordered **byte stream**\.

One important detail: TCP does not preserve application message boundaries.

For example, suppose a client sends\:

```text
"Hello"
"World"
```

The server might receive\:

```text
"HelloWorld"
```

or\:

```text
"Hel"
"loWorld"
```

The application must decide how to identify complete messages, perhaps using a delimiter or a length field.

---

# 3\. How does one server handle multiple clients?

This is where things become interesting.

Suppose three clients want to connect to our server\:

```text
Client A
Client B
Client C
```

All three connect to\:

```text
192.168.1.10:8080
```

Does the server need three different ports?

**No\!**

All three connections can use the same server IP address and port.

TCP distinguishes individual connections using a combination of four values\:

```text
Client IP
Client port
Server IP
Server port
```

This is commonly called the TCP four-tuple.

For example\:

| Connection | Client endpoint | Server endpoint |
|--------|--------|--------|
| A | 192.168.1.20:5001 | 192.168.1.10:8080 |
| B | 192.168.1.21:5002 | 192.168.1.10:8080 |
| C | 192.168.1.22:5003 | 192.168.1.10:8080 |

Even though all connections use server port 8080, they are different TCP connections.

## 3.1 Multiple clients mean multiple file descriptors

Our server might now have\:

```text
FD 3 → Listening socket

FD 4 → Client A
FD 5 → Client B
FD 6 → Client C
```

The OS maintains the state of each connection separately.

Our server can communicate with each client using its corresponding descriptor.

This is how one server process can manage multiple TCP connections.

## 3.2 But how does the server actually handle them?

Let's first write a very simple server.

```c
int server_fd = socket(AF_INET, SOCK_STREAM, 0);

bind(server_fd, ...);
listen(server_fd, 128);

while (1) {
    int client_fd = accept(server_fd, NULL, NULL);

    char buffer[1024];

    int n = recv(client_fd, buffer, sizeof(buffer), 0);

    if (n > 0) {
        send(client_fd, buffer, n, 0);
    }

    close(client_fd);
}
```

This server accepts a client, receives some data, sends it back, and closes the connection.

But notice something.

The server processes clients **one at a time**\.

Consider this situation\:

1. Client A connects.
2. The server accepts A.
3. A sends nothing.
4. The server reaches `recv()`\.
5. The server waits for A to send data.
6. Meanwhile, Client B connects.

What happens to Client B?

The server cannot move on to another `accept()` call because it is stuck waiting for Client A's data.

The OS may queue Client B's connection request, but our application is not processing it yet.

This is a major limitation.

[![PPT - Concurrent Programming: Challenges and Solutions PowerPoint Presentation - ID:8818961](https://images.openai.com/static-rsc-4/pSZ0FY5dHYFkLRGlsgWttYpV64Ph6wGYUJGMpmxKlBzxku6f_S5A0-SpiUrio5Db3IM6AH5gVFdlVikaBTRHbSzsWpA-h33uwmW0ADkGATnS4uTletGExmwakia2pXEn1e31NQlDn7bz46ueSfpNRvyt0lAcp7QA7w7WE9T2kmNTHNGtihb6tmOOYwO0bwFb?purpose=fullsize)](https://www.slideserve.com/rebeccathomas/concurrent-programming-powerpoint-ppt-presentation)

We need to understand why the server gets stuck.

That brings us to blocking I/O.

---

# 4\. What is blocking I/O?

Suppose you call\:

```c
recv(client_fd, buffer, 1024, 0);
```

You are asking the OS\:

> Give me data received from this client.

But what happens when the client has not sent anything yet?

On a blocking socket, the `recv()` call waits until data becomes available, the connection closes, or an error occurs.

Your program cannot continue past that call while it is waiting.

This is called **blocking I/O**\.

Think of ordering food at a restaurant\:

- You place your order.
- You stand at the counter.
- You wait until the food is ready.
- You cannot do anything else while waiting.

That's similar to a blocking system call.

## 4.1 Which operations can block?

There are several common examples\:

| Operation | When it can block |
|--------|--------|
| `accept()` | No incoming connection is available |
| `recv()` | No data is available to receive |
| `send()` | The socket's send buffer cannot currently accept more data |
| `connect()` | Waiting for the connection establishment to complete |

One important clarification: a blocking `recv()` does not necessarily wait until all 1024 requested bytes arrive. It can return after receiving some available data.

Similarly, even a blocking `send()` can send fewer bytes than requested. Your program must handle partial sends.

## 4.2 Why is blocking I/O a problem?

Return to our three clients\:

```text
Client A → Connected but sends nothing
Client B → Wants to send a message
Client C → Wants to send a message
```

Our server is stuck here\:

```c
recv(client_A_fd, buffer, sizeof(buffer), 0);
```

It cannot serve B or C.

Now, you might suggest\:

> Why not create a separate thread for every client?

That's a valid solution.

For example\:

```text
Main thread
    |
    |-- accept Client A
    |       |
    |       └── Thread A handles Client A
    |
    |-- accept Client B
    |       |
    |       └── Thread B handles Client B
    |
    |-- accept Client C
            |
            └── Thread C handles Client C
```

Now, if Thread A is blocked waiting for Client A, the other threads can continue working.

This solves the sequential processing problem.

However, imagine 10,000 clients.

Creating and maintaining 10,000 threads can consume substantial memory and scheduling resources.

We need a more scalable approach.

---

# 5\. What is non-blocking I/O?

Now let's make a small change.

Instead of allowing a socket operation to wait, we configure the socket as **non-blocking**\.

For example, on Linux\:

```c
int flags = fcntl(client_fd, F_GETFL, 0);

fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
```

Now let's call\:

```c
int n = recv(client_fd, buffer, sizeof(buffer), 0);
```

Suppose no data has arrived.

What happens?

Instead of waiting, the call returns immediately with an error indication\:

```text
recv() = -1
errno = EAGAIN
```

\(`EWOULDBLOCK` is also commonly used for this condition.)

This means\:

> There is currently no data available. Try again later.

The program can continue executing other work.

This is **non-blocking I/O**\.

## 5.1 Blocking vs non-blocking example

Imagine two clients\:

```text
Client A: No data available
Client B: Data available
```

### Blocking approach

```text
Server
  |
  |-- recv(Client A)
  |
  |-- WAIT ..............
  |
  |-- Cannot reach Client B yet
```

### Non-blocking approach

```text
Server
  |
  |-- recv(Client A)
  |       |
  |       └── No data → EAGAIN
  |
  |-- recv(Client B)
          |
          └── Data available → Process it
```

The server is no longer forced to wait for Client A.

[![\[Network\] Blocking I/O 와 Non-Blocking I/O 에 대해서 알아보자](https://images.openai.com/static-rsc-4/-_M89cRjycpBrMS2oGbnjyEQ-6ptxVXVYTvxoeZpsoEp5bQsGH8tpj50NoSmP1CZFFfHVTkl8t1RP8e0MpYYgt-7anrz2hx-ew2DErXDyh3aH-6XzZL_hQvkgXP0Ov3i-LyUfpAbAIrdlp_uZWseX-WUf_QXQbbEEK-SuGS4IYQ2wADxEVqCnubtVizrIdYC?purpose=fullsize)](https://didu-story.tistory.com/307)

## 5.2 But there is a new problem\!

Let's say we have 10,000 connected clients.

Our server does this\:

```c
while (1) {
    for (int i = 0; i < number_of_clients; i++) {
        int n = recv(client_fds[i], buffer, sizeof(buffer), 0);

        if (n > 0) {
            // Process received data
        }
    }
}
```

Suppose only Client 7 has sent data.

What does the server do?

```text
Client 1 → EAGAIN
Client 2 → EAGAIN
Client 3 → EAGAIN
Client 4 → EAGAIN
Client 5 → EAGAIN
Client 6 → EAGAIN
Client 7 → Data available!
Client 8 → EAGAIN
...
Client 10000 → EAGAIN
```

It repeatedly checks all clients, even when most of them have no data.

Then it starts again\:

```text
Check Client 1
Check Client 2
Check Client 3
...
Check Client 10000
Repeat...
```

This can waste a lot of CPU time.

This behavior is called **busy polling**\.

So non-blocking I/O solves one problem but creates another.

---

# 6\. What problem are scalable I/O mechanisms trying to solve?

Let's compare the approaches we have seen.

| Approach | Main problem |
|--------|--------|
| Blocking I/O, one thread | One slow client can stop other clients from being served |
| Blocking I/O, one thread per client | Large numbers of threads consume resources |
| Non-blocking I/O with repeated checking | CPU is wasted checking sockets that have no data |

We need something better.

Imagine the OS could tell our server\:

> You have 10,000 connected clients. Only Clients 7, 42 and 900 currently have data available. Focus on those clients.

That would be much more efficient\!

And this is the central idea behind I/O multiplexing mechanisms.

We will study these next\:

## The evolution of network I/O

1. **Blocking I/O**
   
   Wait for an operation to make progress.
2. **Non-blocking I/O**
   
   Return immediately when an operation cannot make progress.
3. **select() and poll()**
   
   Ask the OS which file descriptors are ready for I/O.
4. **epoll**
   
   Efficiently monitor large numbers of file descriptors on Linux.
5. **io\_uring**
   
   A newer Linux interface that lets applications submit I/O requests and receive their completions through shared ring buffers.

We haven't discussed the internals of `select()`, `poll()`, `epoll()` or `io_uring` yet. First, make sure the foundation is clear.

---

# 7\. Check your understanding

Try these questions before moving forward. They will help identify any gaps in the mental model.

**Foundation quiz** (/ 4 answered)

**The most important takeaway\:** Blocking I/O makes your program wait. Non-blocking I/O lets your program continue, but it doesn't tell you when a socket becomes ready. Efficiently discovering which sockets need attention is the next problem we need to solve.

Our next lesson should be `select()` and `poll()`-starting with how a server can monitor multiple file descriptors without creating one thread per client.

Want a short networking exercise each day to reinforce these concepts?

Daily networking mini-exercise

## Prompt:
08/11/2026, 1:16:57 am

Okay, let's move on to select() and poll() now.
I want to understand how these actually help a server handle multiple clients using a single thread.

start with select(). explain how it monitors multiple file descriptors.

It would be helpful to take a small example with maybe 3 clients and show how the server detects which client has sent data and how it handles them, then explain poll() in a similar way.

Finally, compare select() and poll(). Why was poll() introduced when select() already existed? What are their limitations and why do they become inefficient with a large number of connections?

Please explain everything briefly. I want to understand what is happening inside the OS as well, not just learn the function syntax. Don't move to epoll, we will cover that separately.

## Response:
08/11/2026, 1:17:06 am · gpt-6-t-mini-mini

> **Explaining readiness**
>
> I’m treating this as a focused lesson on `select.select()`. I’ll clarify its three lists, explain blocking behavior, then connect readiness checks to responding without crashing.
>
> **Clarifying readiness**
>
> I’m covering how descriptor sets change, how servers handle listeners and clients, and why readiness means “some operation may proceed,” not “the entire message is available.”
>
> **Explained and clarified readiness**
>
> 
>
> Worked for 36s

# 1\. Why do we need `select()`?

Let's revisit the problem we had with non-blocking I/O.

Suppose our server has three connected clients\:

```text
Client A → FD 4
Client B → FD 5
Client C → FD 6
```

All sockets are non-blocking.

Our server could repeatedly call `recv()` on all three clients, but that would cause busy polling.

What we actually want is\:

> OS, tell me whenever any of these sockets becomes ready for reading. Until then, let my process sleep.

This is exactly the problem `select()` helps solve.

**`select()` allows one thread to monitor multiple file descriptors and wait until one or more become ready for I/O.**

## 1.1 How does `select()` work?

The basic syntax is\:

```c
select(nfds, &readfds, &writefds, &exceptfds, &timeout);
```

There are three important sets\:

| Set | What it monitors |
|--------|--------|
| `readfds` | File descriptors ready for reading |
| `writefds` | File descriptors ready for writing |
| `exceptfds` | Exceptional conditions |

For now, we will primarily focus on `readfds`\.

For example, suppose we want to monitor\:

```text
FD 3 → Listening socket
FD 4 → Client A
FD 5 → Client B
FD 6 → Client C
```

We put all four descriptors into the read set.

Conceptually\:

```text
readfds = {3, 4, 5, 6}
```

Then we call\:

```c
select(7, &readfds, NULL, NULL, NULL);
```

Why `7`?

Because `nfds` must be the highest file descriptor we want to monitor plus one.

Here, the highest FD is 6, so\:

```text
nfds = 6 + 1 = 7
```

The last `NULL` means we don't specify a timeout, so `select()` can wait indefinitely.

---

# 2\. Let's understand it with three clients

Suppose we have this situation\:

**Initial server state**

**Server - Single thread**

Monitoring read readiness

**Listening socket**

(FD 3)

Waiting for connections

**Client A**

(FD 4)

No data

**Client B**

(FD 5)

No data

**Client C**

(FD 6)

No data

### Step 1: Register descriptors

Our server creates a read set\:

```c
fd_set readfds;

FD_ZERO(&readfds);

FD_SET(server_fd, &readfds);
FD_SET(client_A, &readfds);
FD_SET(client_B, &readfds);
FD_SET(client_C, &readfds);
```

`FD_ZERO()` clears the set.

`FD_SET()` adds a descriptor to it.

### Step 2: Call select()

```c
int activity = select(max_fd + 1,
                      &readfds,
                      NULL,
                      NULL,
                      NULL);
```

What happens now?

The server thread enters the `select()` system call.

**What happens inside the OS?**

1. The kernel examines the requested file descriptors.
2. It checks whether they are ready for the requested operations.
3. If none are ready, the process can be put to sleep.
4. When an event occurs, the kernel wakes the process.
5. The kernel checks readiness again and returns the ready descriptors.

For example, suppose Client B sends\:

```text
"Hello Server"
```

The networking stack receives the TCP data and places it in Client B's socket receive buffer.

Now FD 5 is readable.

The kernel wakes our server process, and `select()` returns.

### Step 3: Which client sent data?

After returning, the read set contains only descriptors that are ready.

For example\:

```text
Before select():

readfds = {3, 4, 5, 6}

After select():

readfds = {5}
```

The return value is\:

```text
activity = 1
```

Why 1? Because one descriptor is ready for reading.

Our server can check\:

```c
if (FD_ISSET(client_A, &readfds)) {
    // Read from Client A
}

if (FD_ISSET(client_B, &readfds)) {
    // Read from Client B
}

if (FD_ISSET(client_C, &readfds)) {
    // Read from Client C
}
```

Only the Client B condition is true.

So we call\:

```c
recv(client_B, buffer, sizeof(buffer), 0);
```

And process its data.

### Step 4: What if multiple clients send data?

Suppose Client A and Client C send data almost simultaneously.

The OS may return\:

```text
readfds = {4, 6}
```

And\:

```text
activity = 2
```

Our server processes both\:

```text
FD 4 → Read Client A
FD 6 → Read Client C
```

Remember, it is still a **single thread**\.

It processes the ready clients sequentially, but it doesn't waste time waiting on clients that have no data.

[![Your final station of understanding multiplexed I/O | Codes & Beyond](https://images.openai.com/static-rsc-4/OH4FyAYodenuOMGGHTy4ii1RvxsIAUgq0Vo0gzyOKN0Vx4r-YcVi2b5kllaJnoDPbxAybyuu9hSSgYLSkOjk3D_hojFa2Z8-Zwt3WmLSys_Ud27h-sN0fmwuv32qArOrFlT-XWbZE42Qutz59-l07Oj_zDoTqqwCA0eSQhuM7fqjRQsUCW41Y6OqAOj0KWET?purpose=fullsize)](https://www.codesandbeyond.com/posts/multiplexedio/)

### Step 5: Call select() again

Here is an important detail.

`select()` modifies the descriptor sets you pass to it.

Therefore, before every new call, you must reconstruct the original set of descriptors you want to monitor.

Conceptually\:

```text
Create read set
      |
      v
Call select()
      |
      v
Identify ready FDs
      |
      v
Process ready clients
      |
      v
Rebuild read set
      |
      v
Call select() again
```

This is how a single-threaded server can monitor multiple clients without continuously checking every socket itself.

One more important point: readiness does not mean that a complete application message has arrived. It means a read operation can currently make progress. You still need to handle partial reads and connection closures.

---

# 3\. What is poll()?

Now that we understand `select()`, let's look at `poll()`\.

It solves almost exactly the same problem\:

> Monitor multiple file descriptors and tell me which ones are ready for I/O.

However, it uses a different way of representing the descriptors.

## 3.1 How does poll() work?

Instead of using `fd_set`, `poll()` uses an array of structures.

Each structure represents one file descriptor.

```c
struct pollfd {
    int fd;
    short events;
    short revents;
};
```

Let's understand these fields\:

| Field | Meaning |
|--------|--------|
| `fd` | File descriptor to monitor |
| `events` | Events we are interested in |
| `revents` | Events that actually occurred |

Some common event flags\:

| Flag | Meaning |
|--------|--------|
| `POLLIN` | Ready for reading |
| `POLLOUT` | Ready for writing |
| `POLLERR` | Error condition |
| `POLLHUP` | Connection hung up |

## 3.2 Same three-client example

We have\:

```text
FD 3 → Listening socket
FD 4 → Client A
FD 5 → Client B
FD 6 → Client C
```

We create an array\:

```c
struct pollfd fds[4];

fds[0].fd = server_fd;
fds[0].events = POLLIN;

fds[1].fd = client_A;
fds[1].events = POLLIN;

fds[2].fd = client_B;
fds[2].events = POLLIN;

fds[3].fd = client_C;
fds[3].events = POLLIN;
```

Here, we are telling the OS that we are interested in read events for all four sockets.

Now call\:

```c
int activity = poll(fds, 4, -1);
```

Arguments\:

- `fds`\: Array of descriptors to monitor.
- `4`\: Number of entries in the array.
- `-1`\: Wait indefinitely.

### Step 1: OS checks the descriptors

Internally, the OS examines the requested descriptors and checks their readiness.

If none are ready, the process sleeps until an event occurs.

### Step 2: Client B sends data

Suppose Client B sends\:

```text
"Hello Server"
```

Its socket receive buffer gets data.

The OS wakes our process, and `poll()` returns.

The array now looks conceptually like this\:

| Index | FD | events | revents |
|--------|--------|--------|--------|
| 0 | 3 | POLLIN | 0 |
| 1 | 4 | POLLIN | 0 |
| 2 | 5 | POLLIN | POLLIN |
| 3 | 6 | POLLIN | 0 |

Notice the difference\:

- `events` tells the OS what we want to monitor.
- `revents` tells us what actually happened.

We can process the ready descriptors\:

```c
for (int i = 0; i < 4; i++) {
    if (fds[i].revents & POLLIN) {
        // This descriptor is ready for reading
    }
}
```

Only Client B will satisfy this condition.

### Step 3: Multiple clients send data

Suppose Clients A and C also send data.

The next `poll()` call could return\:

| FD | revents |
|--------|--------|
| 3 | 0 |
| 4 | POLLIN |
| 5 | POLLIN |
| 6 | POLLIN |

Now the server knows that all three clients are ready for reading.

It can process them sequentially.

[![Nonblocking I/O. What really are descriptors? | by Cindy Sridharan | Medium](https://images.openai.com/static-rsc-4/O6jg4KQ9Gug2xmyKaTTanz0fb46pO0XoeajWMp7q1kuwMs9_7G180F7-TvzFOLL2aJiWQt_A3oobyirTpg83jW1S-ZOKWEbajOSC_sklgV0E1H2nxAQKyXxTGPzBA-PPsWN_lsYo-6HBUfUfp4Qo-la9ojX-QW_ByfI50htUaiY8vM8EXGKRtAEj3yUB8WZ0?purpose=fullsize)](https://copyconstruct.medium.com/nonblocking-i-o-99948ad7c957)

Unlike `select()`, `poll()` does not overwrite the `events` field. It fills in `revents`, so you don't need to reconstruct the event interests in the same way. However, you still need to call `poll()` again to wait for further events.

---

# 4\. select() vs poll()

Now we understand both mechanisms. Let's compare them.

| Feature | select() | poll() |
|--------|--------|--------|
| Descriptor representation | Fixed-size bit sets | Array of `pollfd` structures |
| Maximum FD number | Commonly limited by `FD_SETSIZE` \(usually 1024) | No equivalent fixed `FD_SETSIZE` restriction |
| Monitoring events | Separate read/write/exception sets | Event flags |
| Returned readiness | Modifies the supplied sets | Fills `revents` |
| Rebuilding interests | Required every call | Usually not required if interests haven't changed |
| Kernel processing | Scans requested descriptors | Scans array entries |
| Complexity | O(n) scanning | O(n) scanning |

Both can monitor a large number of connections in principle, but neither is particularly efficient when the number becomes very large.

Let's understand why.

# 5\. Why was poll() introduced when select() already existed?

The biggest motivation was to improve the interface and overcome some limitations of `select()`\.

### Problem 1: FD\_SETSIZE limitation

`select()` uses a fixed-size bit set.

On many systems, this is limited to 1024 descriptors.

For example\:

```text
FD 4
FD 5
...
FD 1023
```

You cannot safely use `FD_SET()` with a descriptor outside the supported `fd_set` range.

An important distinction: the limitation applies to the numerical value of the descriptor, not just the number of connections.

`poll()` uses an array instead.

You can pass an array containing descriptors such as\:

```text
FD 4
FD 1500
FD 5000
FD 10000
```

Provided the descriptors are valid and you have sufficient resources.

### Problem 2: select() modifies the sets

Suppose we originally monitor\:

```text
{3, 4, 5, 6}
```

After `select()` returns\:

```text
{5}
```

We have lost the original set and must rebuild it.

With `poll()`, the `events` field remains unchanged while `revents` is updated.

This makes the interface more convenient.

### Problem 3: Less convenient descriptor management

In `select()`, we work with bit sets and must track the highest descriptor plus one.

In `poll()`, we simply maintain an array and specify its length.

This is easier to manage when descriptors are added and removed dynamically.

**So, poll() improved the interface and removed select()'s fixed-size descriptor-set limitation. But it did not fundamentally solve the scalability problem.**

---

# 6\. Why are BOTH select() and poll() inefficient for thousands of connections?

This is the most important part for your project.

Let's imagine a server has\:

```text
10,000 connected clients
```

But at a particular moment\:

```text
Only 5 clients have data.
```

What happens when we call `select()` or `poll()`?

### Problem 1: Linear scanning

The OS must examine the requested descriptors to determine which ones are ready.

For 10,000 descriptors, it potentially checks all 10,000.

Even though only 5 have data.

```text
FD 1      → Not ready
FD 2      → Not ready
FD 3      → Not ready
...
FD 5000   → READY
...
FD 10000  → Not ready
```

This is O(n) work per call, where n is the number of monitored descriptors.

### Problem 2: Repeated scanning

Suppose the server processes the five ready clients and calls `select()` or `poll()` again.

The OS has to examine the descriptor collection again.

This happens repeatedly.

```text
Call 1 → Scan 10,000 descriptors
Call 2 → Scan 10,000 descriptors
Call 3 → Scan 10,000 descriptors
Call 4 → Scan 10,000 descriptors
```

Even if only a handful of clients are active, the server keeps paying the cost of scanning the entire collection.

### Problem 3: User-kernel data transfer

There is another overhead.

The descriptor sets or poll arrays need to be communicated from user space to kernel space on each call.

The kernel examines them and returns readiness information to the application.

For large descriptor collections, repeatedly transferring and processing this information adds overhead.

[![How to Build a High-Performance TCP Server  Ep1 - How Can a Single Server Handle Tens of Thousands of Concurrent Connections?](https://images.openai.com/static-rsc-4/QeYOvJduCoEMa7OGkBF-tqM7XK1CrNwYtaUA4ioDdf74-HLXcbMejvkjVriy1J-pGhQa3WcVcBwx5tBz-8Um-f-rRx6ErQ04uXbXmZBi9rVitoMwMHrxxLm3rq7d6tMaTQP52zl98vA3cYhpjf7LHs56MDmAtPSkz2nFRfO6jXVYNgIkMRRoKPlP6HQD5vuZ?purpose=fullsize)](https://www.chillvic.dev/p/how-to-build-a-high-performance-tcp)

## 7\. Final mental model

Let's summarize the progression\:

**Blocking I/O**

Wait on an individual operation. A slow client can hold up the server.

---

**Non-blocking I/O**

Don't wait, but repeatedly checking every socket wastes CPU.

---

**select()**

Wait for readiness across multiple descriptors, using fixed-size descriptor sets.

---

**poll()**

Same basic readiness approach, but with a more flexible array-based interface.

The key distinction to remember\:

**`select()` and `poll()` tell you which sockets are ready. They do not receive the data for you.**

Your application still needs to call `recv()` or `send()` to perform the actual I/O.

Also, both are readiness-based mechanisms: they report that an operation can make progress, rather than reporting that a submitted operation has completed.

That distinction will become particularly important when we eventually discuss the I/O mechanisms used in your project.

## Prompt:
08/11/2026, 1:25:59 am

Ok, I understood how select() and poll() work and their limitations.
Before moving to io_uring,  I want to briefly understand epoll() as well. I don't need to go very deep into its implementation since my assigned part is io_uring.

explain the basic idea behind epoll, how epoll_create1(), epoll_ctl() and epoll_wait() work, and what is the difference between its interest list and ready list. also briefly explain level-triggered and edge-triggered modes.
help me understand how epoll differs from select() and poll(), and what limitations still remain.

I want to understand these concepts well enough to compare them with io_uring later. After this, let's move towards io_uring and spend considerably more time understanding its internals.

## Response:
08/11/2026, 1:26:04 am · gpt-6-t-mini-mini

> **Explaining epoll basics**
>
> I’m framing `epoll` as Linux’s scalable event multiplexer: registrations live in an interest list, while readiness notifications enter a ready list. I’ll compare level-triggered and edge-triggered behavior, emphasizing nonblocking I/O and draining until `EAGAIN`.
>
> **Clarifying readiness behavior**
>
> I’m emphasizing that `epoll` reports possible progress—it doesn’t perform `recv`, `send`, or `accept`. I’ll explain returned event data, readable-listener examples, edge-triggered caveats, per-event system calls, and why scalability still has limits.
>
> **Explained epoll basics and readiness behavior**
>
> 
>
> Worked for 10s

# 1\. Why was epoll() introduced?

Let's start with the limitation we identified in `select()` and `poll()`\.

Suppose our server has 10,000 connected clients, but only 5 clients have data available.

With `select()` or `poll()`, the OS needs to examine the requested file descriptors repeatedly to identify which ones are ready.

The problem is that we keep asking\:

> Out of these 10,000 sockets, which ones are ready now?

Wouldn't it be better if the OS maintained track of the sockets we are interested in and notified us when their readiness changes?

That's the basic idea behind **`epoll()`**\.

`epoll()` is a Linux-specific I/O event notification mechanism designed to efficiently monitor large numbers of file descriptors.

The important improvement is\:

**Unlike `select()` and `poll()`, epoll maintains the set of monitored file descriptors inside the kernel across multiple calls.**

Let's see how.

# 2\. How does epoll work?

There are three primary functions\:

1. `epoll_create1()` - Create an epoll instance.
2. `epoll_ctl()` - Add, modify or remove file descriptors to be monitored.
3. `epoll_wait()` - Wait for ready events.

Let's understand them through our three-client example.

## 2.1 epoll\_create1()

First, we create an epoll instance.

```c
int epfd = epoll_create1(EPOLL_CLOEXEC);
```

This returns another file descriptor.

For example\:

```text
FD 3 → Listening socket
FD 4 → epoll instance
```

What does creating an epoll instance actually mean?

The kernel creates an internal structure that maintains information about the file descriptors we want to monitor.

We can think of this as creating a monitoring system inside the kernel.

## 2.2 epoll\_ctl()

Now we need to tell the kernel which sockets we want to monitor.

We use\:

```c
epoll_ctl(epfd, operation, fd, &event);
```

The second argument specifies what we want to do\:

| Operation | Meaning |
|--------|--------|
| `EPOLL_CTL_ADD` | Add an FD to monitoring |
| `EPOLL_CTL_MOD` | Modify its monitoring events |
| `EPOLL_CTL_DEL` | Remove an FD |

For example\:

```c
struct epoll_event event;

event.events = EPOLLIN;
event.data.fd = client_fd;

epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &event);
```

Here\:

- `epfd`\: Our epoll instance.
- `EPOLL_CTL_ADD`\: Add this socket.
- `client_fd`\: The socket we want to monitor.
- `EPOLLIN`\: Notify us when it becomes ready for reading.
- `event.data.fd`\: Store the descriptor so we can identify it later.

We can register our listening socket and every accepted client socket.

For example\:

```text
epoll instance (FD 4)

Monitored descriptors:
    FD 3 → Listening socket
    FD 5 → Client A
    FD 6 → Client B
    FD 7 → Client C
```

Notice something important\:

We register the sockets using `epoll_ctl()`, rather than passing the complete list of descriptors again every time we wait.

The kernel remembers these registrations.

---

# 3\. Interest list vs Ready list

This is the most important internal concept of epoll.

Inside the kernel, conceptually, there are two important collections.

**Interest list**

(What we want to monitor)

Contains file descriptors registered through epoll\_ctl().

```text
FD 3 → Listening socket
FD 5 → Client A
FD 6 → Client B
FD 7 → Client C
```

---

**Ready list**

(What is currently ready)

Contains registered descriptors for which relevant events have occurred.

```text
FD 6 → Client B is readable
```

Let's understand how these two collections interact.

### Step 1: Register clients

Initially, we have\:

```text
Interest list:
{3, 5, 6, 7}

Ready list:
{}
```

We are monitoring all four descriptors, but none currently has an event to report.

### Step 2: Client B sends data

Suppose Client B sends\:

```text
"Hello Server"
```

The kernel's networking stack receives the TCP data and places it in Client B's socket receive buffer.

The socket becomes readable.

The kernel's event notification mechanism detects this readiness and makes the corresponding epoll event available through the ready list.

Now\:

```text
Interest list:
{3, 5, 6, 7}

Ready list:
{6}
```

Notice that FD 6 remains in the interest list.

It hasn't been removed just because it became ready.

### Step 3: Server calls epoll\_wait()

```c
struct epoll_event events[100];

int n = epoll_wait(epfd, events, 100, -1);
```

Arguments\:

- `epfd`\: Our epoll instance.
- `events`\: Array where ready events will be returned.
- `100`\: Maximum number of events to return.
- `-1`\: Wait indefinitely.

The kernel waits until relevant events are available.

When Client B becomes readable, `epoll_wait()` returns.

For example\:

```text
n = 1

events[0].data.fd = 6
events[0].events  = EPOLLIN
```

The server now knows that Client B is ready.

It calls\:

```c
recv(6, buffer, sizeof(buffer), 0);
```

And processes the received data.

### Step 4: More clients become ready

Suppose Clients A and C also send data.

The ready events can now include\:

```text
FD 5 → EPOLLIN
FD 7 → EPOLLIN
```

The next `epoll_wait()` can return both events.

The application processes them sequentially.

[![the spatula](https://images.openai.com/static-rsc-4/TlJDy_LlHdvK8UQP2XV526jxhsBKHIengaRaweNcXkGVBX0SiqitKBcOzwJviy3qkR_3d0R6LOiKZwKPDJ3ahrOe6y8gA85NEMxeLQjAbD3eLFIHxExrR2UGR0CqN4BnLVsSsfUg9VDxHx7M42aKSFgpPlPcqIUsjCK2LR1b6l_KwbeJ0jn28kPIf_xX5jhb?purpose=fullsize)](https://www.thespatula.io/rust/rust_io_uring_bindings/)

**The key insight\:** The kernel maintains the interest list and tracks readiness events. We don't need to scan the entire collection of monitored descriptors ourselves on every wait.

This is why epoll generally scales better than `select()` and `poll()` for large numbers of connections with relatively few active events.

---

# 4\. Level-triggered vs Edge-triggered

These are two ways epoll can notify us about readiness.

This distinction is particularly important when writing an event-driven server.

## 4.1 Level-triggered (LT)

This is the default behavior of epoll.

Suppose Client A sends 10 KB of data.

The data is placed in its receive buffer.

Our application reads only 4 KB\:

```c
recv(client_fd, buffer, 4096, 0);
```

What happens to the remaining 6 KB?

It stays in the socket receive buffer.

The socket is still readable.

With level-triggered mode, epoll will continue reporting the socket as ready while unread data remains.

```text
Client A sends 10 KB
        |
        v
Receive buffer: 10 KB
        |
        v
Application reads 4 KB
        |
        v
Remaining data: 6 KB
        |
        v
Socket is still readable
        |
        v
epoll_wait() reports it again
```

In other words\:

**As long as the socket remains ready, epoll continues notifying us.**

This is easier to work with because even if we don't read all available data in one attempt, we can be notified again.

## 4.2 Edge-triggered (ET)

Edge-triggered mode works differently.

We enable it using\:

```c
event.events = EPOLLIN | EPOLLET;
```

Here, `EPOLLET` enables edge-triggered notification.

Instead of repeatedly notifying us while a socket remains readable, epoll notifies us when its readiness changes, such as transitioning from not-readable to readable.

Let's use the same example.

Client A sends 10 KB.

```text
Receive buffer: 10 KB
```

Epoll notifies us.

Now suppose we read only 4 KB.

```text
Remaining data: 6 KB
```

The socket is still readable.

But there might not be another readiness transition, so we cannot rely on receiving another notification just because the remaining data is still there.

**This can cause a problem if we don't read all available data.**

That's why edge-triggered mode is normally used with non-blocking sockets.

The usual approach is\:

```c
while (1) {
    int n = recv(client_fd, buffer, sizeof(buffer), 0);

    if (n > 0) {
        // Process received data
    }
    else if (n == -1 && errno == EAGAIN) {
        break;
    }
    else {
        // Handle connection closure or error
        break;
    }
}
```

We keep reading until `EAGAIN` tells us that no more data is currently available.

This ensures that we don't leave unread data waiting without a new edge notification.

### LT vs ET comparison

| Feature | Level-triggered | Edge-triggered |
|--------|--------|--------|
| Notification | While socket remains ready | When readiness changes |
| Repeated notification | Yes | Not simply because readiness remains |
| Must drain data? | Not necessarily in one attempt | Yes, typically until EAGAIN |
| Complexity | Easier | More careful handling required |
| Non-blocking sockets | Recommended | Essential for the usual safe pattern |

For now, remember\:

- LT: Tell me while the socket is ready.
- ET: Tell me when the socket becomes ready.

---

# 5\. epoll vs select vs poll

Let's compare all three now.

| Feature | select() | poll() | epoll() |
|--------|--------|--------|--------|
| Maintains monitoring state | User passes sets each time | User passes array each time | Kernel maintains interest list |
| Descriptor-set limitation | Fixed FD\_SETSIZE | No equivalent fixed limit | No FD\_SETSIZE restriction |
| Repeated full scan | Yes | Yes | No full interest-list scan on every wait |
| Returns | Modified FD sets | `revents` array | Ready events |
| Registration | Every call through sets | Every call through array | `epoll_ctl()` |
| Scalability | Less suitable for large sets | Less suitable for large sets | Generally better for large sets |

### Why is epoll more efficient?

Imagine\:

```text
10,000 monitored connections
Only 5 currently ready
```

With `select()` and `poll()`\:

```text
Repeatedly examine the 10,000 descriptors
        |
        v
Identify the 5 ready descriptors
```

With epoll\:

```text
Register descriptors once
        |
        v
Kernel tracks readiness events
        |
        v
Return the 5 ready events
```

The work of waiting is now more closely related to the number of ready events rather than the total number of monitored descriptors.

This is the primary scalability improvement.

---

# 6\. What limitations still remain in epoll?

Epoll is a significant improvement, but it doesn't eliminate every I/O overhead.

**1\. It is still readiness-based.**

Epoll tells us that a socket is ready for reading or writing.

We still need to call\:

```c
recv();
send();
accept();
```

to perform the actual operations.

Epoll doesn't perform these operations on our behalf.

**2\. System calls are still involved.**

We still use\:

- `epoll_ctl()` to register or modify descriptors.
- `epoll_wait()` to retrieve events.
- `recv()` and `send()` to perform I/O.

These involve user-kernel transitions.

**3\. Event handling still takes resources.**

The application still needs to process returned events, and a server with thousands of active connections can have substantial event-processing work.

**4\. It is Linux-specific.**

Unlike the more widely available `select()` and `poll()` interfaces, epoll is specific to Linux.

**5\. Edge-triggered mode adds complexity.**

The application must carefully drain sockets, handle partial operations, and avoid losing progress when it fails to process all available data.

Also, epoll is not unlimited: monitored descriptors consume kernel resources, and process/system resource limits still apply.

---

# 7\. What should you remember before moving to io\_uring?

This distinction is particularly important for understanding your project.

## select() / poll()

(Readiness notification)

The application asks the OS which file descriptors are ready.

---

## epoll()

(More efficient readiness notification)

The kernel maintains the monitored descriptors and makes ready events available to the application.

---

## io\_uring

(Submission and completion model)

The application submits I/O requests, and the kernel reports their completions.

There is one fundamental difference to keep in mind\:

With epoll\:

```text
epoll_wait()
     |
     v
Client socket is readable
     |
     v
Application calls recv()
     |
     v
Data is received
```

Conceptually, with io\_uring\:

```text
Application submits a receive request
     |
     v
Kernel performs the requested operation
     |
     v
Kernel reports its completion
     |
     v
Application processes the completion
```

This is a conceptual comparison; we'll examine the actual io\_uring submission and completion mechanisms in detail later.

**My recommendation for your project preparation\:** We have covered enough about select, poll and epoll for now. We should spend considerably more time on io\_uring, especially its shared ring buffers, SQEs, CQEs, submission/completion flow, kernel interaction and how your C implementation handles multiple clients.

That is where we'll focus next.

For continued practice, a short daily networking question could help reinforce these concepts.

Daily network I/O practice question

## Prompt:
08/11/2026, 1:34:17 am

now let's move to io_uring. it is the most important part for my project because I was responsible for implementing the io_uring engine.

I am uploading my actual implementation (io_uring_server.c), along with the project README and relevant framework files.

I want you to use these files as the primary reference throughout our discussion. My goal is to understand everything that I have implemented so that I can confidently explain the code.

1. Understand io_uring internally.

Start with the basic idea behind io_uring and how it differs from readiness-based mechanisms like epoll. Then explain its architecture, shared memory, Submission Queue (SQ), Completion Queue (CQ), SQEs, CQEs, user_data, batching, io_uring_enter() and the overall submission-completion cycle.

Use simple examples to explain what happens inside the kernel and userspace. Don't jump into my code immediately. First make sure I understand the underlying mechanism.

2. Understand my actual implementation.

Once the fundamentals are clear, start going through my io_uring_server.c. I want to understand every function, important variable, flag, condition and operation that I have implemented.

For each part:

- Explain what the code does, step by step.
- Explain why we need it and what problem it solves.
- Explain what happens internally when that particular operation is performed.
- Show how it connects with the SQ, CQ and the overall event loop.
- Use small examples wherever helpful.
- Explain important edge cases and what could go wrong without particular checks.
- Also explain alternative approaches and why this implementation might have chosen the current approach.

Please don't just paraphrase the comments in my code. I want to understand the actual logic.

Pay particular attention to the TCP implementation, UDP implementation, ring initialization, submission and completion handling, syscall counting, SQPOLL, busy polling, queue sizing, cancellation and shutdown.

Whenever my code uses something defined in framework.h or framework.c, refer to those files to explain how the components interact. Don't assume I already know the framework

keep in mind:

1. My priority is understanding my own io_uring implementation in detail.&#x20;
2. Don't skip small implementation details just because they seem obvious.
3. Don't dump the entire code explanation in one response. Teach me one concept or function at a time and let me ask doubts.
4. After explaining an important section, ask me a few conceptual questions to check whether I have understood it.
5. Gradually increase the difficulty towards questions that might be asked during a viva.
6. Don't generate a new implementation or rewrite my code unless I specifically ask. I want to understand and defend the implementation I already have.

Let's begin with the fundamentals of io_uring.

[Attachment: io_uring_server.c]

[Attachment: README.md]

[Attachment: framework.c]

[Attachment: framework.h]

## Response:
08/11/2026, 1:34:19 am · gpt-6-t-mini-mini

> **Building fundamentals first**
>
> I’m grounding this in the README’s core distinctions: io_uring is operation-based, not readiness-based; applications submit SQEs, the kernel returns CQEs, and `user_data` connects completions to requests.
>
> **Starting with completions**
>
> I’m beginning with the central idea: readiness APIs say “try now,” while io_uring represents whole operations—submit a read or write, then receive its result through a completion queue.
>
> **Connecting queues and operations**
>
> I’m mapping the model onto the project: userspace places operation requests into the submission queue, the kernel performs them, and completion entries report results. I’ll next unpack shared memory and queue movement.
>
> Worked for 12s

# io\_uring - Lesson 1: The fundamental idea

We'll approach this in two stages.

First, we'll build a mental model of how `io_uring` works internally, independently of your implementation. Then we'll revisit each mechanism in `io_uring_server.c` and understand exactly how you used it.

I'll use your project's README, `framework.h`, `framework.c`, and `io_uring_server.c` as the primary references. In particular, your README already establishes an important distinction: readiness-based I/O versus completion-based I/O. fileciteturn0file1L125-L149

## 1\. The basic idea behind io\_uring

Let's start with a problem.

Imagine our TCP server has 1,000 connected clients.

Client A sends a message. Our server needs to\:

1. Receive the message.
2. Process it.
3. Send a response.

With `epoll()`, the process looks like this\:

(epoll - readiness-based)

**Application calls epoll\_wait()**

Wait for ready sockets

**Kernel reports Client A is readable**

**Application calls recv()**

Another system call to perform the actual read

**Application processes the data**

**Application calls send()**

Another system call to send the response

Notice something: `epoll()` doesn't actually receive the data.

It only tells us\:

> Client A's socket is ready. You can attempt to read from it.

The application is responsible for performing the actual operation.

### What does io\_uring change?

Instead of asking the kernel whether a socket is ready, we can directly give it an instruction\:

> Receive up to 4096 bytes from Client A into this buffer.

We describe the entire operation and submit it to the kernel.

The kernel performs the requested operation and later tells us what happened.

(io\_uring - completion-based)

**Application prepares a receive request**

FD, buffer, length, operation type

**Application submits the request**

**Kernel performs the receive operation**

Waits for data if it cannot complete immediately

**Kernel reports completion**

Number of bytes received, or an error

**Application processes the result**

The fundamental difference\:

| epoll | io\_uring |
|--------|--------|
| Tells us an operation can make progress | Tells us an operation has completed |
| Application performs `recv()` | Kernel performs the submitted receive |
| Application performs `send()` | Kernel performs the submitted send |
| Readiness-based | Completion-based |

Your own `io_uring_server.c` describes this distinction in its opening comments: instead of asking whether it can read and then reading, it describes the whole operation in a submission queue entry and collects the result from the completion queue. fileciteturn0file0L2-L11

One qualification: io\_uring doesn't guarantee that every operation completes immediately or without further waiting. The kernel can keep an operation pending until it can make progress.

---

## 2\. How does io\_uring communicate with the kernel?

This is where the architecture becomes interesting.

Normally, when an application wants the kernel to perform an operation, it makes a system call.

For example\:

```c
recv(fd, buffer, 4096, 0);
```

The CPU transitions from user space into kernel space, the kernel performs the operation, and control returns to the application.

This transition is necessary, but making a separate system call for every operation creates overhead.

Imagine our server has received 100 requests that need to be submitted.

A naive approach might involve\:

```text
Submit Request 1 → syscall
Submit Request 2 → syscall
Submit Request 3 → syscall
...
Submit Request 100 → syscall
```

That is potentially 100 kernel entries just to submit 100 operations.

io\_uring introduces a way to reduce this overhead.

**It uses shared memory between user space and kernel space.**

Instead of transferring every request through a separate system call, the application and kernel communicate through shared ring structures.

Let's understand those.

## 3\. The architecture: SQ and CQ

io\_uring has two primary queues.

## 1\. Submission Queue (SQ)

Application → Kernel

Contains requests that the application wants the kernel to execute.

```text
Receive from Client A
Send to Client B
Accept a new connection
```

---

## 2\. Completion Queue (CQ)

Kernel → Application

Contains the results of operations that have completed.

```text
Receive completed: 128 bytes
Send completed: 256 bytes
Accept completed: FD 12
```

Think of it like a restaurant\:

- SQ: The orders placed by customers.
- Kernel: The kitchen preparing those orders.
- CQ: The completed orders waiting for collection.

The application submits operations through SQ and collects their results through CQ.

Importantly, the kernel and application can access the relevant shared ring memory directly.

There are also separate arrays of entries\:

- **SQEs (Submission Queue Entries)\:** Describe individual operations.
- **CQEs (Completion Queue Entries)\:** Describe the results of individual operations.

We'll examine their internal structures and memory layout separately.

### Why are they called ring queues?

Because they use circular indexing.

For example, imagine a queue with four slots\:

```text
       0       1
     ┌───┬───┬───┬───┐
     │ A │ B │ C │ D │
     └───┴───┴───┴───┘
       3       2
```

After the last slot, the next position wraps around to slot zero.

This allows the queues to reuse memory instead of constantly allocating and freeing new queue entries.

---

## 4\. The most important optimization: batching

Let's return to our example of 100 operations.

Without batching\:

```text
Application             Kernel

Submit request 1  ----> Execute request 1
Submit request 2  ----> Execute request 2
Submit request 3  ----> Execute request 3
...
Submit request 100 ---> Execute request 100
```

With io\_uring, the application can prepare multiple SQEs first.

```text
Application

SQ:
┌──────────────────────────┐
│ SQE 1: Receive Client A  │
│ SQE 2: Receive Client B  │
│ SQE 3: Send Client C     │
│ SQE 4: Accept connection │
└──────────────────────────┘
             |
             | One submission operation
             v
           Kernel
```

The application can submit multiple operations together, potentially using one `io_uring_enter()` system call.

This is called **batching**\.

For example\:

```text
100 operations

Without batching:
Potentially 100 submission syscalls

With batching:
Potentially 1 submission syscall
```

This is illustrative; the actual number of kernel entries depends on how the ring is configured and how it is submitted.

Why does this matter?

Because one system call can tell the kernel about several operations rather than entering the kernel separately for every operation.

And that's one of the main reasons io\_uring can reduce system-call overhead.

Your README explicitly identifies batching as one of the advantages of the completion-based design. fileciteturn0file1L125-L149

---

## 5\. What happens during the overall submission-completion cycle?

Let's put everything together.

Suppose our application wants to receive data from Client A.

The conceptual sequence is\:

1. **Prepare an SQE**
   
   Describe the receive operation: socket FD, destination buffer and requested length.
2. **Place it in the SQ**
   
   The application makes the submission entry available through the shared queue.
3. **Notify the kernel**
   
   The application may use `io_uring_enter()` \(through liburing's submission functions) to tell the kernel to consume submitted entries.
4. **Kernel executes the operation**
   
   The kernel tries to receive data. The operation may complete immediately or remain pending.
5. **Kernel produces a CQE**
   
   The completion includes the result, such as the number of bytes received.
6. **Application retrieves the CQE**
   
   It examines the result, processes the data and decides what operation should happen next.

One important distinction\:

`io_uring_enter()` is the system call used to interact with the ring, including submitting requests and optionally waiting for completions.

But **you do not necessarily need a system call for every submission or completion**\.

For example\:

- SQEs can be placed in shared memory before submission.
- CQEs can be read directly from shared memory if they are already available.
- Several operations can be submitted together.
- Some configurations, particularly SQPOLL, can allow the kernel to consume SQ entries without a submission syscall on every batch.

We'll examine exactly how this works when we study SQ and CQ memory management.

---

## 6\. One important point before going further

You might be thinking\:

> Does io\_uring eliminate all system calls and all kernel-user data copying?

No.

It is important not to make this mistake during your viva.

io\_uring can reduce the number of system calls required to submit and collect I/O operations. However\:

- Actual network I/O still involves kernel networking code.
- Normal TCP send and receive operations can still copy data between user-space buffers and kernel-managed socket buffers.
- Some operations still require system calls.
- Shared ring memory does not mean that all network data is automatically transferred without copying.

The optimization is primarily about **how applications communicate I/O requests and receive their results**, not eliminating the entire operating-system networking stack.

---

# 7\. Let's check your understanding

Before we get into the actual queue internals, answer these four questions. Try explaining them in your own words rather than just selecting an option.

**Concept check** (/4 answered)

## What we'll study next

I suggest we take the next topic slowly because it is the foundation of understanding your implementation\:

**Lesson 2: Shared memory and the internal architecture of the SQ and CQ.**

We'll understand\:

- How user space and kernel space map the same memory.
- SQ head, tail and ring indices.
- How the kernel knows which SQEs are new.
- How the application knows which CQEs are available.
- Why memory ordering and acquire/release operations matter.

Only after that should we study the actual SQE and CQE structures, `user_data`, and `io_uring_enter()` in detail.

Then we'll begin your `io_uring_server.c`, function by function.

## Prompt:
08/11/2026, 1:39:35 am

Okay, let's start lesson 2

## Response:
08/11/2026, 1:39:41 am · gpt-6-t-mini-mini

> **Planning the lesson**
>
> I’ll anchor the explanation in the provided files, then verify low-level ring mechanics against authoritative documentation. I’m focusing on shared memory, submission and completion queues, ownership, ordering, and how the example server uses them.
>
> **Planned the lesson**
>
> 
>
> **Read Ring setup and I/O engine documentation**
>
> 
>
> **Explaining queue mechanics**
>
> I’m clarifying how userspace and the kernel share ring metadata: producers advance tails, consumers advance heads, and monotonic counters map onto reusable slots. I’ll also distinguish SQ entries, submission indexing, and completion records.
>
> **Clarifying ownership**
>
> I’m emphasizing that queue entries are shared memory, not messages copied between processes. Userspace publishes submissions, the kernel consumes them, and completion records travel back through the same ring-based coordination model.
>
> **Mapping the ring model**
>
> I’m building a diagram around shared memory: submission metadata and entries flow toward the kernel, while completion records flow back. I’ll note that ordinary operations produce one completion, with advanced multishot operations as an exception.
>
> **Modeling ring behavior**
>
> I’m connecting the mechanics to the example: counters track pending work, masks reuse fixed slots, and ownership alternates between producers and consumers. I’m also noting that advanced multishot operations can generate multiple completions from one submission.
>
> **Explained queue mechanics, ownership, and ring behavior**
>
> 
>
> **Searched 8 websites**
>
> 
>
> Worked for 1m 5s

# Lesson 2: Shared memory and the internal architecture of SQ and CQ

This is one of the most important concepts in `io_uring`\. Once you understand how the queues work internally, SQEs, CQEs, batching, `io_uring_enter()`, and eventually your server's event loop will become much easier to understand.

Let's focus on one question\:

**How can the application and kernel exchange thousands of I/O requests and results without making a separate system call for every operation?**

The answer is shared memory and circular queues.

We'll build this from the memory layout upwards, rather than jumping directly into structure definitions.

## 1\. What does shared memory actually mean?

First, understand the normal situation.

Your application runs in **user space**, while the operating system manages I/O in **kernel space**\.

Normally, these are protected from directly accessing each other's memory.

For example\:

```text
USER SPACE                  KERNEL SPACE

Your application            Linux kernel
      |                           |
      |  System call              |
      |-------------------------->|
      |                           |
      |  Result                   |
      |<--------------------------|
```

This separation provides protection. An application cannot freely access kernel memory.

But repeated communication across this boundary creates overhead.

### How io\_uring changes this

Instead of communicating every request through a separate system call, the kernel creates memory regions that are mapped into both address spaces.

Conceptually\:

## User space

**Your application**

Prepares requests and collects results

(Same shared memory pages)

## Kernel space

**Linux io\_uring subsystem**

Consumes requests and publishes completions

The important detail is that these are mappings of the same underlying memory, not two separate copies of the queues.

Both sides can access the shared data through their respective virtual memory mappings.

Linux establishes these mappings during ring initialization using `mmap()`\. The `io_uring_setup()` interface provides the information required to map the SQ and CQ regions. \[[io\_uring\_setup(2)](https://www.man7.org/linux/man-pages/man2/io_uring_setup.2.html?utm_source=chatgpt.com); [io\_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html?utm_source=chatgpt.com)\]

**Why is this useful?**

Suppose you have 100 SQEs ready to submit.

Your application can write all 100 descriptions into shared memory before entering the kernel.

Similarly, when the kernel completes an operation, it can write the result into shared memory. Your application can read it directly.

That's how we reduce the number of system calls needed for communication.

One clarification: shared memory avoids copying the ring entries between user and kernel space. It does not mean that the actual TCP network data is never copied.

---

## 2\. Understand the complete memory layout

Now let's look at how these shared regions are organized.

There is a subtle detail here that is important for understanding the actual implementation.

The architecture contains\:

1. SQ ring metadata and an SQ index array.
2. An SQE array containing the actual operation descriptions.
3. CQ ring metadata and a CQE array containing the results.

Conceptually\:

## Submission side

**SQ ring**

**Head / Tail**

Track consumption and publication

**SQ index array**

Points to SQE slots

**SQE array**

SQE 0

SQE 1

SQE 2

Contains actual I/O instructions

---

## Completion side

**CQ ring**

**Head / Tail**

Track consumption and publication

**CQE array**

Contains completed results

Why is the SQ slightly more complicated?

The SQ index array tells the kernel which position in the separate SQE array contains each submitted request.

For learning purposes, you can initially think of one SQ slot as corresponding to one SQE. But internally, the index array provides this indirection.

The CQ is simpler: its CQE entries are contained directly in its ring region. \[[io\_uring\_setup(2)](https://www.man7.org/linux/man-pages/man2/io_uring_setup.2.html?utm_source=chatgpt.com); [man/io\_uring\_setup.2 - pub/scm/linux/kernel/git/axboe/liburing](https://kernel.googlesource.com/pub/scm/linux/kernel/git/axboe/liburing/%2B/refs/heads/master/man/io_uring_setup.2?utm_source=chatgpt.com)\]

Now let's understand how these queues actually work.

---

# 3\. Understanding head and tail

This is the most important part of today's lesson.

Both queues use two counters\:

- `head`
- `tail`

These counters tell the application and kernel which entries have been consumed and which have been published.

The ownership is different for the two queues.

| Queue | Head is updated by | Tail is updated by |
|--------|--------|--------|
| SQ | Kernel | Application |
| CQ | Application | Kernel |

Think of head as the position up to which entries have been consumed, and tail as the position up to which entries have been published.

Let's take a very small example.

Suppose the SQ has only four slots\:

```text
SQ capacity = 4
```

Initially\:

```text
head = 0
tail = 0
```

No requests have been submitted.

## 3.1 Application submits two requests

Suppose the application wants to perform\:

- Request A: Receive data from Client A.
- Request B: Send data to Client B.

It prepares two SQEs and makes them available.

Now\:

```text
head = 0
tail = 2
```

The queue looks like\:

**SQ - Two requests published**

**0**

Request A

**1**

Request B

**2**

Empty

**3**

Empty

(Head = 0) (Tail = 2)

Two entries are available for the kernel to consume.

The kernel can determine the number of pending entries\:

$$
\text{Pending SQ entries} = \text{tail} - \text{head}
$$

Here\:

$$
2-0=2
$$

So the kernel knows there are two new requests.

## 3.2 Kernel consumes the requests

The kernel reads the two requests and starts processing them.

After consuming both\:

```text
head = 2
tail = 2
```

Now\:

$$
2-2=0
$$

There are no unconsumed submissions.

The two slots can be reused.

## 3.3 What happens when we submit again?

Suppose we now submit\:

- Request C: Receive from Client C.
- Request D: Accept a new connection.

The counters become\:

```text
head = 2
tail = 4
```

The important thing is that we reuse the physical slots\:

```text
Slot 0 → Request C
Slot 1 → Request D
Slot 2 → Empty
Slot 3 → Empty
```

Why slots 0 and 1 again?

Because this is a circular queue.

The counters keep advancing, but their physical positions wrap around.

### How does wrapping work?

Suppose the queue has four entries.

Its ring mask is\:

```text
ring_mask = 4 - 1 = 3
```

To calculate the actual slot\:

$$
\text{slot} = \text{counter} \ \& \ \text{ring\_mask}
$$

For example\:

| Counter | Calculation | Physical slot |
|--------|--------|--------|
| 0 | 0 &amp; 3 | 0 |
| 1 | 1 &amp; 3 | 1 |
| 2 | 2 &amp; 3 | 2 |
| 3 | 3 &amp; 3 | 3 |
| 4 | 4 &amp; 3 | 0 |
| 5 | 5 &amp; 3 | 1 |
| 6 | 6 &amp; 3 | 2 |

Thus the queue can reuse a fixed number of physical slots while the counters advance.

Actual io\_uring ring sizes use this power-of-two arrangement, with the kernel providing the ring size and mask to userspace. \[[man/io\_uring\_setup.2 - pub/scm/linux/kernel/git/axboe/liburing](https://kernel.googlesource.com/pub/scm/linux/kernel/git/axboe/liburing/%2B/refs/heads/master/man/io_uring_setup.2?utm_source=chatgpt.com); [io\_uring\_setup(2)](https://www.man7.org/linux/man-pages/man2/io_uring_setup.2.html?utm_source=chatgpt.com)\]

---

# 4\. The CQ works in the opposite direction

Now let's understand the Completion Queue.

The SQ was\:

```text
Application → Kernel
```

The CQ is\:

```text
Kernel → Application
```

The ownership of head and tail is therefore reversed.

| Counter | Who updates it? | What does it mean? |
|--------|--------|--------|
| CQ tail | Kernel | New completions have been published |
| CQ head | Application | Completions have been consumed |

Let's take an example.

Suppose our application submitted three operations\:

```text
Request A → Receive from Client A
Request B → Receive from Client B
Request C → Send to Client C
```

The kernel starts executing them.

An important point: **the operations do not necessarily complete in submission order.**

Suppose Client B sends data first, then Client C's send completes, and finally Client A sends data.

The completion order could be\:

```text
1. Request B completed
2. Request C completed
3. Request A completed
```

io\_uring does not force completion order to match submission order. \[[io\_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html?utm_source=chatgpt.com)\]

## 4.1 Kernel publishes completions

Initially\:

```text
CQ head = 0
CQ tail = 0
```

After Client B's receive finishes, the kernel writes its result into a CQE slot and advances the tail\:

```text
CQ head = 0
CQ tail = 1
```

The CQ now contains\:

```text
Slot 0 → Client B: 128 bytes received
```

After Client C's send completes\:

```text
CQ head = 0
CQ tail = 2
```

And\:

```text
Slot 0 → Client B: 128 bytes received
Slot 1 → Client C: 128 bytes sent
```

After Client A's receive completes\:

```text
CQ head = 0
CQ tail = 3
```

The queue now has three available completions.

**CQ - Three completions published**

**CQE 0**

Client B

128 bytes

**CQE 1**

Client C

128 bytes

**CQE 2**

Client A

128 bytes

(Head = 0) (Tail = 3)

Three completions are available to the application.

The number of available completions is\:

$$
\text{Available CQEs} = \text{tail} - \text{head}
$$

Here\:

$$
3-0=3
$$

## 4.2 Application consumes the completions

Our application checks the CQ and processes the three completions.

After processing the first one, it marks that CQE as consumed.

Conceptually\:

```text
Before consuming:

head = 0
tail = 3

After consuming one:

head = 1
tail = 3
```

Now two completions remain available.

After consuming all three\:

```text
head = 3
tail = 3
```

The application has consumed everything.

This is how the CQ slots become available for reuse.

### How does this happen in liburing?

There are two functions worth remembering\:

```c
io_uring_peek_cqe(ring, &cqe);
```

Checks whether a CQE is already available. It does not need a system call when a completion is present in shared memory.

And\:

```c
io_uring_cqe_seen(ring, cqe);
```

Marks that completion as consumed, advancing the CQ head as appropriate.

These are directly used in your implementation.

For example, your `run_tcp()` loop does\:

```c
void *data = io_uring_cqe_get_data(cqe);
int n = cqe->res;

io_uring_cqe_seen(ring, cqe);
```

We will understand these operations in detail when we reach your completion-handling logic.

---

# 5\. An important internal detail: Why memory ordering matters

This might seem like a small implementation detail, but it is actually quite important in understanding io\_uring internally.

Remember, the application and kernel are accessing the same memory.

But sharing memory doesn't automatically guarantee that they observe writes in the correct order.

Modern CPUs and compilers can reorder memory operations when allowed.

Let's see why this matters.

## 5.1 Imagine the application submitting an SQE

Suppose the application wants to submit\:

```text
Receive from Client A
Buffer = 0x1000
Length = 4096
```

The logical operation should be\:

1. Write the SQE contents.
2. Publish the new SQ tail.

Why must this order be maintained?

Imagine the CPU makes the updated tail visible before the SQE contents are fully written.

The kernel might observe\:

```text
SQ tail = 1
```

And conclude\:

> A new request is available\!

But the SQE might still contain old or incomplete data.

The kernel could consume an invalid request.

That would be a serious correctness problem.

## 5.2 Release and acquire semantics

io\_uring uses memory-ordering guarantees to prevent this.

The conceptual sequence is\:

**Application (SQ producer)**

1\. Write all SQE fields

2\. Publish SQ tail using release semantics

Shared memory

**Kernel (SQ consumer)**

3\. Read SQ tail using acquire semantics

4\. Safely read the published SQE

The reverse producer-consumer relationship applies to the CQ\:

- Kernel writes the CQE.
- Kernel publishes the CQ tail.
- Application reads the CQ tail with appropriate acquire semantics.
- Application then reads the CQE contents.
- Application publishes its updated CQ head after consuming it.

**Release** ensures earlier writes are published before the corresponding index update.

**Acquire** ensures that after observing the published index, the consumer also observes the preceding data writes.

The Linux kernel source explicitly documents these synchronization requirements for both SQ and CQ. \[[io\_uring/io\_uring.c - pub/scm/linux/kernel/git/next/linux-next](https://kernel.googlesource.com/pub/scm/linux/kernel/git/next/linux-next/%2B/master/io_uring/io_uring.c?utm_source=chatgpt.com)\]

You don't need to memorize the CPU-level implementation yet. Just understand the principle\:

> The queue must never tell the consumer that an entry is available before the actual entry has been safely written.

This is why ring head/tail management is more than just incrementing two integers.

---

# 6\. How does all this map to your actual implementation?

Now let's connect today's concepts to your `io_uring_server.c`\.

You don't explicitly create the shared memory mappings yourself. You use **liburing**, which handles the low-level setup.

Let's look at your code.

### 6.1 Ring initialization

In your implementation\:

```c
#define RING_DEPTH 256

static int ring_setup(server_t *s, struct io_uring *ring)
{
    struct io_uring_params params = {0};

    params.flags = IORING_SETUP_CQSIZE;
    params.cq_entries = completion_entries(s);

    if (s->config->sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = SQPOLL_IDLE_MS;
        // ...
    }

    return io_uring_queue_init_params(RING_DEPTH, ring, &params);
}
```

The important call is\:

```c
io_uring_queue_init_params(RING_DEPTH, ring, &params);
```

What does liburing do internally?

Conceptually\:

1. Requests the kernel to create an io\_uring instance.
2. The kernel allocates the SQ and CQ structures.
3. The required shared memory regions are mapped into your process.
4. Liburing initializes its internal pointers and queue-management information.
5. Your application can now obtain SQEs and access CQEs.

Your code doesn't need to manually call `mmap()` or manipulate all the ring offsets.

That's handled by liburing. \[[io\_uring\_setup(2)](https://www.man7.org/linux/man-pages/man2/io_uring_setup.2.html?utm_source=chatgpt.com); [io\_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html?utm_source=chatgpt.com)\] fileciteturn0file0

### 6.2 Why is SQ depth 256 but CQ depth configurable?

This is an important design choice in your implementation.

Your code defines\:

```c
#define RING_DEPTH 256
```

So your SQ has 256 entries.

But your CQ size is calculated separately\:

```c
params.cq_entries = completion_entries(s);
```

Why?

Because the number of requests waiting to be submitted and the number of operations that can be completed without being collected are different concerns.

Your README explains that the original CQ size of 512 overflowed when running with 1,000 clients.

The completion queue needed to accommodate the outstanding operations from many clients.

So your implementation calculates a sufficiently large CQ based on\:

```text
max_clients + udp_depth + 64
```

and rounds up to a power of two.

For the default configuration\:

```text
max_clients = 10000
udp_depth   = 32

needed = 10000 + 32 + 64
       = 10096

CQ entries = 16384
```

This is why SQ and CQ do not necessarily have the same capacity.

We'll examine the CQ overflow problem and its consequences in much more detail when we study `completion_entries()`\.

fileciteturn0file0 fileciteturn0file1

### 6.3 Where will we see these concepts in your event loop?

Three functions from your implementation are particularly relevant\:

| Your function | What it does |
|--------|--------|
| `counted_submit()` | Submits available SQ entries |
| `counted_wait()` | Checks for available completions and waits if necessary |
| `run_tcp()` | Uses these mechanisms to manage TCP clients |

For example, your `counted_wait()` first executes\:

```c
io_uring_peek_cqe(ring, cqe)
```

Why?

Because the completion might already be present in shared memory.

There's no reason to enter the kernel and wait when the result is already available.

Likewise, your TCP event loop calls\:

```c
io_uring_cqe_seen(ring, cqe);
```

after collecting a completion.

This allows the CQ slot to be reused.

We'll come back to both functions when we study submission and completion handling. fileciteturn0file0

---

# 7\. Summary: What you should understand from Lesson 2

**The complete mental model**

(Submission Queue)

Application produces requests; kernel consumes them.

- Application updates SQ tail.
- Kernel updates SQ head.
- SQE array contains operation descriptions.

---

(Completion Queue)

Kernel produces results; application consumes them.

- Kernel updates CQ tail.
- Application updates CQ head.
- CQE array contains operation results.

---

(Shared memory)

Both user space and kernel space access the same underlying ring memory through their respective mappings.

---

(Memory ordering)

Release/acquire synchronization ensures that queue entries are safely published and consumed.

The biggest distinction to remember\:

**SQ head/tail track the movement of requests into the kernel. CQ head/tail track the movement of completed results back to the application.**

## 8\. Check your understanding

Try answering these in your own words. Don't worry about using technically perfect terminology yet.

**Q1.** Your SQ has a capacity of 256. Its current values are\:

```text
head = 100
tail = 150
```

How many requests are waiting for the kernel to consume? Who updates the head and who updates the tail?

**Q2.** Your CQ has\:

```text
head = 20
tail = 28
```

How many completions are available? What happens when the application processes three CQEs and calls `io_uring_cqe_seen()` for each?

**Q3.** Why do we need release/acquire memory ordering? What could happen if the application publishes the SQ tail before the SQE is completely written?

**Q4. (Viva level)** Suppose your application submits 256 operations, and the kernel completes all of them. But your application doesn't collect any CQEs. What happens when more operations start completing? Why does CQ sizing matter even though the SQ has sufficient capacity?

Once you answer these, we'll move to\:

**Lesson 3: SQEs, CQEs and `user_data` - understanding exactly how an I/O operation is described and how its result is associated with the correct client.**

That will be our first major step towards understanding your actual TCP event loop.

Want a short io\_uring viva question delivered daily as well?

Daily io\_uring viva question

## Prompt:
08/11/2026, 1:48:04 am

okay, let's move to Lesson 3

## Response:
08/11/2026, 1:48:14 am · gpt-6-t-mini

Lesson 3 will focus on the two structures that carry each operation through the ring: the SQE that describes what to do, and the CQE that reports what happened. I’ll also connect `user_data` to your implementation, where it is used to identify which client or accept operation a completion belongs to.

> **Explaining SQE and CQE fields**
>
> I’m explaining how submission entries describe operations and how completion entries report results. I’ll connect `user_data`, `res`, accept tags, connection pointers, buffer lifetime, and `pending` state directly to the code examples.
>
> **Clarifying operation flow**
>
> I’m distinguishing operation descriptions from completion results: SQEs hold opcodes, descriptors, buffers, lengths, flags, and correlation tokens; CQEs return `res`, flags, and the unchanged token. I’ll emphasize asynchronous buffer lifetime, partial sends, EOF, accept results, out-of-order completion, and queue handling.
>
> **Explained SQE and CQE fields and clarified operation flow**
>
> 
>
> **Computed SQEs, CQEs, and user data; crafted a sourced lesson**
>
> 
>
> Worked for 34s

# 1\. What is an SQE (Submission Queue Entry)?

An SQE is a structure that describes **one I/O operation that the application wants the kernel to perform**\.

For example, suppose our server wants to receive data from Client A.

It needs to tell the kernel\:

- Which socket should be used?
- What operation should be performed?
- Where should the received data be stored?
- How many bytes should be received?
- What additional flags should be applied?

An SQE carries this information.

Think of it as an instruction card that the application prepares for the kernel.

## 1.1 What does an SQE contain?

The Linux kernel's `io_uring_sqe` structure contains several fields.

We don't need to memorize every field yet, but the important ones are\:

| Field | Purpose |
|--------|--------|
| `opcode` | Type of operation: RECV, SEND, ACCEPT, etc. |
| `fd` | File descriptor on which to perform the operation |
| `addr` | Address of the buffer or relevant memory |
| `len` | Length of the buffer or requested data |
| `off` | Operation-specific offset or argument |
| `flags` | Additional operation/submission options |
| `user_data` | Application-defined 64-bit value returned with the completion |

There are also additional fields for specialized operations, fixed buffers, registered files, and other features.

Let's understand this with an example.

Suppose\:

```text
Client A FD = 5
Buffer address = 0x1000
Maximum receive length = 4096
```

We want the kernel to receive data from Client A.

Conceptually, our SQE describes\:

**SQE: Receive request**

opcode **IORING\_OP\_RECV**

---

fd **5**

---

addr **0x1000**

---

len **4096**

---

user\_data **Application's identifier**

The kernel can interpret this instruction as\:

> Receive up to 4096 bytes from FD 5 and place the received data into the buffer at address 0x1000.

Notice something important.

**The SQE doesn't contain the actual network data. It contains a description of the operation and a reference to the buffer where the data should go.**

The kernel accesses that buffer when performing the operation.

---

# 2\. How do we prepare an SQE in C?

You generally don't fill every field manually.

liburing provides helper functions that prepare SQEs for common operations.

For receiving\:

```c
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);

io_uring_prep_recv(
    sqe,
    client_fd,
    buffer,
    4096,
    0
);
```

Let's understand each step.

### Step 1: Get an available SQE slot

```c
sqe = io_uring_get_sqe(&ring);
```

This asks liburing for an available submission entry.

If an entry is available, it returns a pointer to it.

If the submission queue is full, it returns `NULL`\.

Important: getting an SQE does not mean the kernel has received or executed the request. We are only obtaining a slot in which to prepare it.

### Step 2: Prepare the operation

```c
io_uring_prep_recv(sqe, client_fd, buffer, 4096, 0);
```

This fills the appropriate SQE fields.

Conceptually\:

```text
opcode = IORING_OP_RECV
fd     = client_fd
addr   = buffer
len    = 4096
flags  = 0
```

### Step 3: Make the SQE available for submission

liburing manages the SQE array and submission queue bookkeeping.

When we call\:

```c
io_uring_submit(&ring);
```

the prepared entries are made available to the kernel.

This is the point at which our requests are submitted, subject to the ring's configuration.

We will study `io_uring_submit()` and `io_uring_enter()` separately in the next lesson.

---

# 3\. What is a CQE (Completion Queue Entry)?

Now suppose the kernel executes our receive operation.

The client sends\:

```text
"Hello Server"
```

That's 12 bytes.

The kernel receives those bytes and places them into our specified buffer.

But how does our application know that the operation has finished?

How does it know how many bytes were received?

This is where the CQE comes in.

A **CQE is a structure containing the result of a completed I/O operation.**

The important fields are\:

| Field | Purpose |
|--------|--------|
| `user_data` | The identifier associated with the original SQE |
| `res` | Result of the operation |
| `flags` | Additional completion information |

Let's understand each.

## 3.1 The `res` field

Suppose our receive operation requested 4096 bytes.

But the client sent only 12 bytes.

The CQE might contain\:

```text
res = 12
```

This means the operation successfully received 12 bytes.

The `res` field has several possible meanings depending on the operation.

| Operation | Possible `res` |
|--------|--------|
| `recv` | Number of bytes received |
| `send` | Number of bytes sent |
| `accept` | Newly accepted socket FD |
| Failed operation | Negative error code |
| `recv` returns EOF | 0 |

Notice that a CQE does not always mean bytes transferred.

For example\:

```text
Accept completes → res = 12
```

This could mean the newly accepted connection received FD 12.

Another example\:

```text
Receive fails → res = -ECONNRESET
```

The result is a negative error code.

Unlike ordinary system calls, liburing exposes operation errors in the CQE's `res` field as negative error numbers. You generally don't inspect `errno` to interpret an already returned CQE.

## 3.2 The `flags` field

A CQE can contain additional information in `flags`\.

For example, multishot operations can use `IORING_CQE_F_MORE` to indicate that more completions are expected for the same submitted request.

Your current implementation doesn't use this flag, but it's useful to know that `res` is not the entire completion information.

We'll revisit CQE flags when discussing advanced operations.

---

# 4\. The most important connection: user\_data

Now we reach one of the most important concepts for your implementation.

Imagine our server has 1,000 clients.

We submit receive operations for all 1,000 clients.

The kernel performs them asynchronously.

Suppose the following happens\:

```text
Request 1 → Client A
Request 2 → Client B
Request 3 → Client C
```

But their completion order is\:

```text
1. Client C completes
2. Client A completes
3. Client B completes
```

How does our application know which client a CQE belongs to?

We cannot assume that the first CQE belongs to the first SQE.

This is why we have `user_data`\.

## 4.1 What is user\_data?

`user_data` is a 64-bit value supplied by the application when preparing a request.

The kernel returns the same value in the corresponding CQE.

Think of it as an identification tag.

For example\:

**Application submits three operations**

**SQE 1** (Client A)

user\_data = A

**SQE 2** (Client B)

user\_data = B

**SQE 3** (Client C)

user\_data = C

---

**Kernel completes operations in a different order**

**CQE 1** (Client C)

user\_data = C, res = 128

**CQE 2** (Client A)

user\_data = A, res = 256

**CQE 3** (Client B)

user\_data = B, res = 64

**The application can identify every completion regardless of its order.**

The important property is that the kernel does not need to understand what your identifier means.

You decide what to store there.

It could be\:

- An integer request ID.
- A pointer to a client structure.
- A pointer to a request structure.
- Another application-defined identifier.

In your implementation, you primarily use pointers.

---

# 5\. How does your implementation use user\_data?

Now let's connect this concept to `io_uring_server.c`\.

Your TCP implementation uses\:

```c
io_uring_sqe_set_data(sqe, c);
```

Here, `c` is a pointer to the corresponding `conn_t` structure.

Let's understand why this is useful.

Your `framework.h` defines\:

```c
struct conn_t {
    int fd;
    stage_t stage;
    size_t done, want;
    uint64_t length;
    uint64_t body_left;
    uint64_t response_total;
    uint64_t response_left;
    server_t *server;
    conn_t *prev, *next;
    conn_t *ready_next;
    int pending;
    void *app_state;
    unsigned char buffer[];
};
```

Each client has its own `conn_t`\.

It stores information about\:

- Its socket FD.
- Whether it is reading or sending.
- How many bytes have been processed.
- How much data remains.
- Its application buffer.
- Whether an I/O operation is currently pending.

fileciteturn0file3L61-L75

So instead of storing a client number, you store the actual address of its state structure.

### Example

Suppose\:

```text
Client A → conn_t address 0x1000
Client B → conn_t address 0x2000
Client C → conn_t address 0x3000
```

You prepare three receive operations\:

```c
io_uring_sqe_set_data(sqe_A, client_A);
io_uring_sqe_set_data(sqe_B, client_B);
io_uring_sqe_set_data(sqe_C, client_C);
```

Conceptually\:

```text
SQE A → user_data = 0x1000
SQE B → user_data = 0x2000
SQE C → user_data = 0x3000
```

When the kernel completes an operation, the associated pointer comes back through the CQE.

You retrieve it using\:

```c
conn_t *c = io_uring_cqe_get_data(cqe);
```

Now you have the exact client whose operation has completed.

You don't need to search through all your connected clients to identify it.

**This is a major part of how your implementation handles many clients efficiently.**

## 5.1 Why must the pointer remain valid?

This is a particularly important implementation detail.

Suppose Client A has a receive operation in flight.

The SQE contains\:

```text
buffer = Client A's buffer
user_data = Client A's conn_t pointer
```

The kernel may still access that buffer while the operation is pending.

And when it completes, the CQE returns the pointer.

What happens if we free Client A's `conn_t` before collecting the completion?

Two problems could occur\:

1. The kernel could write received data into memory that has already been freed.
2. The application could receive the CQE and interpret the old pointer as a valid connection.

This is why your implementation tracks\:

```c
c->pending = 1;
```

The connection structure and its buffer must remain valid until the operation has completed and its CQE has been handled.

We'll study buffer ownership and the importance of `pending` in depth when we reach the TCP event loop and shutdown logic.

---

# 6\. How does your TCP implementation prepare SQEs?

Let's examine the actual code now, but only this small portion.

From your `run_tcp()`\:

```c
if (conn_is_reading(c)) {
    io_uring_prep_recv(
        sqe,
        c->fd,
        conn_buffer(c),
        conn_length(c),
        0
    );
} else {
    io_uring_prep_send(
        sqe,
        c->fd,
        conn_buffer(c),
        conn_length(c),
        MSG_NOSIGNAL
    );
}

io_uring_sqe_set_data(sqe, c);
c->pending = 1;
outstanding++;
```

fileciteturn0file0L329-L342

Let's break down the logic.

### Case A: Client needs to receive data

```c
conn_is_reading(c)
```

This checks the client's current stage.

Your framework defines\:

```text
HEADER
RECEIVE
SEND
```

If the client is in `HEADER` or `RECEIVE`, it needs to receive data.

So we prepare\:

```c
io_uring_prep_recv(
    sqe,
    c->fd,
    conn_buffer(c),
    conn_length(c),
    0
);
```

Two framework functions are important here.

**`conn_buffer(c)`**

Returns\:

```c
c->buffer + c->done
```

This points to the next position where data should be placed.

**`conn_length(c)`**

Returns\:

```c
c->want - c->done
```

This gives the number of bytes still expected in the current portion of the request.

For example\:

```text
want = 4096
done = 1000
```

Then\:

```text
Buffer address = c->buffer + 1000
Receive length = 3096
```

So the kernel receives into the remaining portion of the client buffer.

fileciteturn0file3L61-L67 fileciteturn0file2L154-L167

### Case B: Client needs to send data

If `conn_is_reading(c)` is false, the client is in `SEND`\.

We prepare\:

```c
io_uring_prep_send(
    sqe,
    c->fd,
    conn_buffer(c),
    conn_length(c),
    MSG_NOSIGNAL
);
```

Here the kernel is instructed to send data from the client's buffer.

Notice the flag\:

```c
MSG_NOSIGNAL
```

This prevents a broken connection from generating a `SIGPIPE` signal that could terminate the process. The send operation instead reports an error such as `-EPIPE`\.

The framework and the other engines also use this protection.

### Case C: Associate the operation with the client

```c
io_uring_sqe_set_data(sqe, c);
```

This stores the client's pointer in `user_data`\.

Then\:

```c
c->pending = 1;
```

Marks that an operation is now pending for this client.

Finally\:

```c
outstanding++;
```

Increments the number of operations that the engine is tracking as outstanding.

These three operations are related, but they do different things\:

| Code | Purpose |
|--------|--------|
| `io_uring_sqe_set_data()` | Identify the client when the completion returns |
| `c->pending = 1` | Track the client's buffer/operation ownership |
| `outstanding++` | Track operations that have not yet had their completion handled |

We'll revisit `outstanding` separately because it is particularly important during shutdown.

---

# 7\. What happens when a CQE comes back?

Now let's look at the corresponding completion-handling code.

Your implementation does\:

```c
void *data = io_uring_cqe_get_data(cqe);
int n = cqe->res;

io_uring_cqe_seen(ring, cqe);
outstanding--;

if (data == &accept_tag) {
    // Handle accept completion
} else {
    conn_t *c = data;
    c->pending = 0;

    int status = conn_complete(c, n);

    if (status) {
        conn_close(c, status);
    } else {
        enqueue(&head, &tail, c);
    }
}
```

fileciteturn0file0L372-L405

Let's follow one example.

Suppose Client B has a receive request in flight.

The kernel receives 128 bytes.

Its CQE contains\:

```text
user_data = Client B's conn_t pointer
res       = 128
```

Your application executes\:

### Step 1: Retrieve user\_data

```c
void *data = io_uring_cqe_get_data(cqe);
```

It gets Client B's connection pointer.

### Step 2: Retrieve the result

```c
int n = cqe->res;
```

Here\:

```text
n = 128
```

### Step 3: Mark the CQE as consumed

```c
io_uring_cqe_seen(ring, cqe);
```

The application has finished reading this completion and makes the CQ slot reusable.

### Step 4: Decrease outstanding

```c
outstanding--;
```

We have now handled one completed operation.

### Step 5: Release the client's pending state

```c
c->pending = 0;
```

This operation has finished. The application can now safely manage the client's buffer again, assuming there are no other operations referencing it.

### Step 6: Update the framework's request state machine

```c
int status = conn_complete(c, n);
```

This is an especially important design decision.

Your io\_uring engine does not independently implement TCP request processing.

It passes the result to the common framework.

The framework updates\:

- `done`
- `body_left`
- `response_left`
- `stage`

It also decides whether the client should receive more data or start sending a response.

Your README explains that all four networking engines share this same state machine. fileciteturn0file1L345-L370

### Step 7: Schedule the next operation

If\:

```c
status == 0
```

The client remains valid.

So your engine calls\:

```c
enqueue(&head, &tail, c);
```

This adds the client to the engine's ready queue.

On the next event-loop iteration, the engine prepares its next SQE.

If the status is nonzero, the client is closed instead.

This is how your implementation maintains the sequence\:

```text
Prepare SQE
    |
    v
Submit operation
    |
    v
Kernel executes operation
    |
    v
CQE arrives
    |
    v
Retrieve user_data
    |
    v
Update client state
    |
    v
Prepare next operation
```

---

# 8\. A special case: Why does your code use accept\_tag?

There is one more interesting use of `user_data` in your TCP implementation.

When you submit an accept operation, you don't associate it with a `conn_t`, because the new client doesn't have a `conn_t` yet\!

Your code uses\:

```c
static __thread int accept_tag;
```

And\:

```c
io_uring_prep_accept(
    sqe,
    s->listener,
    NULL,
    NULL,
    SOCK_NONBLOCK | SOCK_CLOEXEC
);

io_uring_sqe_set_data(sqe, &accept_tag);
```

Why?

Because the completion could belong to either\:

- The listening socket's accept operation.
- An already connected client's receive/send operation.

We need to distinguish them.

Your code compares\:

```c
if (data == &accept_tag)
```

If true, it knows the completion belongs to the accept operation.

In that case\:

```c
int accepted_fd = cqe->res;
```

If the result is non-negative, the kernel has returned a new connected socket FD.

Your engine then\:

1. Enables `TCP_NODELAY`\.
2. Calls `conn_add()`\.
3. Adds the newly created connection to the ready queue.

If the completion is not the accept tag, it treats `data` as a `conn_t *`\.

This is a neat use of `user_data`\: **you use the same completion mechanism to distinguish different types of operations.**

The unique address of the accept tag acts as a sentinel. It must remain valid while the accept operation can still complete.

fileciteturn0file0L313-L327 fileciteturn0file0L378-L396

---

# 9\. One more connection: Your UDP implementation

Your UDP implementation also uses `user_data`, but with a slightly different design.

You define\:

```c
typedef struct udp_slot_t {
    struct sockaddr_in peer;
    struct iovec iov;
    struct msghdr msg;
    unsigned char in[MAX_DATAGRAM], out[MAX_DATAGRAM];
    int sending;
} udp_slot_t;
```

fileciteturn0file0L195-L203

Each UDP slot has\:

- An input buffer.
- An output buffer.
- Sender address.
- Message metadata.
- A `sending` flag.

When preparing a receive\:

```c
io_uring_prep_recvmsg(sqe, s->listener, &slot->msg, 0);
io_uring_sqe_set_data(sqe, slot);
```

When preparing a send\:

```c
io_uring_prep_sendmsg(sqe, s->listener, &slot->msg, MSG_NOSIGNAL);
io_uring_sqe_set_data(sqe, slot);
```

Notice that both operations store the same `slot` pointer in `user_data`\.

So how does your engine distinguish a receive completion from a send completion?

It uses\:

```c
if (slot->sending)
```

- `sending = 0`\: The completion corresponds to a receive.
- `sending = 1`\: The completion corresponds to a send.

This is a little different from TCP.

In TCP, your code distinguishes the accept operation through a special tag and treats the other completions as client operations.

In UDP, it associates a slot with both operations and uses the slot's state to distinguish them.

We'll revisit this when we study `run_udp()` in detail.

---

# 10\. Important takeaways

| Concept | Remember this |
|--------|--------|
| SQE | Describes an operation to the kernel |
| SQE opcode | Specifies which operation to execute |
| SQE buffer address | Points to the memory used by the operation |
| CQE | Reports an operation's completion |
| `cqe->res` | Contains the result, including byte counts, accepted FD or negative error |
| `user_data` | Connects a completion to its original application request |
| `io_uring_sqe_set_data()` | Associates user-defined data with an SQE |
| `io_uring_cqe_get_data()` | Retrieves that data from the corresponding CQE |
| `pending` | Your implementation's buffer/operation ownership tracking |

The most important thing for your viva\:

**SQEs and CQEs are not necessarily processed in the same order. `user_data` allows the application to associate every completion with the correct client or operation, regardless of completion order.**

---

# 11\. Check your understanding

Let's check whether you've understood the concepts. These are a little more challenging than the previous lesson.

Try answering in your own words.

**Q1.** Suppose you submit three receive requests\:

```text
SQE 1 → Client A, user_data = A
SQE 2 → Client B, user_data = B
SQE 3 → Client C, user_data = C
```

The kernel returns completions in this order\:

```text
CQE 1 → user_data = C, res = 100
CQE 2 → user_data = A, res = 200
CQE 3 → user_data = B, res = 300
```

How does the application know which client to process for each completion? Why can't it simply assume that CQE 1 belongs to SQE 1?

**Q2.** In your implementation, why do we use\:

```c
io_uring_sqe_set_data(sqe, c);
```

instead of storing just the client FD?

What additional information becomes immediately accessible when we retrieve the `conn_t *`?

**Q3.** Suppose Client A has a receive operation pending. The application decides to close Client A and immediately frees its `conn_t`\.

What two problems could occur when the kernel eventually completes that receive operation?

**Q4. (Viva level)** Your implementation uses\:

```c
io_uring_sqe_set_data(sqe, &accept_tag);
```

for accepting connections, and\:

```c
io_uring_sqe_set_data(sqe, c);
```

for client operations.

Why is this distinction necessary? How would the completion-handling code know whether `cqe->res` represents a new socket FD or a byte count?

After we discuss your answers, we'll move to **Lesson 4: `io_uring_enter()`, submission mechanics, batching and how liburing actually sends SQEs to the kernel**\.

That lesson will be particularly important because it leads directly into your `counted_submit()` function and the syscall-counting logic you implemented.

## Prompt:
08/11/2026, 1:58:57 am

Lesson 4

## Response:
08/11/2026, 1:59:07 am · gpt-6-t-mini

Lesson 4 connects the queue mechanics to the actual kernel entry point: how prepared SQEs become visible to the kernel, when `io_uring_enter()` is needed, and how your `counted_submit()` distinguishes ordinary submission from SQPOLL. I’ll keep the examples aligned with the calls your code actually uses—especially `io_uring_submit()` and `io_uring_wait_cqe_timeout()`—rather than introducing a different submission pattern.

> **Explaining submission mechanics**
>
> I’m connecting the lesson to the supplied helpers: `io_uring_submit()` publishes prepared SQEs, while `io_uring_enter()` controls submission and optional waiting. I’ll distinguish batching, SQPOLL wakeups, return values, and why separate submit and wait paths matter.
>
> **Explained submission mechanics**
>
> 
>
> **Tracing ring submission**
>
> I’m mapping the path from SQE preparation to kernel entry: liburing publishes pending SQEs, then submits them, while waiting is separate. I’ll emphasize that kernel entry doesn’t always mean a context switch, except when an operation actually blocks.
>
> **Understanding completion flow**
>
> I’m separating submission from execution and completion: the kernel consumes published SQEs, operations may finish later, and CQE polling can avoid entry entirely. SQPOLL can submit without a syscall unless its polling thread needs waking.
>
> **Applying the submission model**
>
> I’m now tying the mechanics to the benchmark’s counted paths: batching multiple SQEs can use one kernel entry, while completion polling may use none. I’ll explain SQPOLL as a separate optimization rather than mixing every detail together.
>
> **Traced ring submission and applied the completion flow model**
>
> 
>
> **Explained io_uring submission mechanics**
>
> 
>
> Worked for 3m 51s

# 1\. What is `io_uring_enter()`?

Let's start with the main question\:

We understood that the application prepares SQEs in shared memory. But how does the kernel know that new requests have been placed in the Submission Queue?

In a normal io\_uring configuration, the application uses a system call called\:

```c
io_uring_enter()
```

This is the interface through which userspace can ask the kernel to consume submitted SQEs and, optionally, wait for completions.

Think of the process like this\:

```text
Application                   Kernel

Prepare SQE 1
Prepare SQE 2
Prepare SQE 3
     |
     v
Publish SQ tail
     |
     v
io_uring_enter()  --------->  Kernel sees new requests
                              Kernel consumes SQEs
                              Kernel starts operations
     |                              |
     |<-----------------------------|
     |
Application continues
```

The important point is that **the application can submit multiple operations through one kernel entry.**

Let's understand why.

## 1.1 What does io\_uring\_enter() actually do?

Conceptually, its system-call interface looks like\:

```c
io_uring_enter(
    ring_fd,
    to_submit,
    min_complete,
    flags,
    sig,
    sigsz
);
```

You don't need to memorize the entire signature right now. Understand these three important arguments\:

| Argument | Meaning |
|--------|--------|
| `to_submit` | Number of submission entries to process |
| `min_complete` | Minimum number of completions to wait for when requesting completion waiting |
| `flags` | Controls additional behavior |

Some important flags include\:

- `IORING_ENTER_GETEVENTS`\: Ask the kernel to wait for completion events, when needed.
- `IORING_ENTER_SQ_WAKEUP`\: Wake an idle SQPOLL thread.

Notice that `io_uring_enter()` has two related responsibilities\:

1. Submission: Tell the kernel about operations it should process.
2. Waiting: Optionally wait until completions become available.

These responsibilities can be combined in one call, but they don't have to be.

**Your implementation generally handles submission and waiting separately.** It uses `io_uring_submit()` for submission and `io_uring_wait_cqe_timeout()` for waiting.

---

# 2\. How does liburing submit SQEs?

Remember, your project uses liburing\:

```c
#include <liburing.h>
```

liburing provides convenient functions so that we don't have to manually manage every ring index and call the raw system call ourselves.

Let's follow a small example.

Suppose the server has three clients\:

```text
Client A → FD 5
Client B → FD 6
Client C → FD 7
```

We want to receive data from all three.

## Step 1: Get SQE slots

```c
struct io_uring_sqe *sqe1 = io_uring_get_sqe(&ring);
struct io_uring_sqe *sqe2 = io_uring_get_sqe(&ring);
struct io_uring_sqe *sqe3 = io_uring_get_sqe(&ring);
```

liburing gives us three available submission entries.

At this point\:

```text
SQ:
┌─────────────┐
│    SQE 1    │
│    SQE 2    │
│    SQE 3    │
└─────────────┘
```

The kernel has not necessarily been notified yet.

## Step 2: Prepare all three operations

```c
io_uring_prep_recv(sqe1, 5, bufferA, 4096, 0);
io_uring_prep_recv(sqe2, 6, bufferB, 4096, 0);
io_uring_prep_recv(sqe3, 7, bufferC, 4096, 0);
```

Now our SQEs contain three different receive requests.

Conceptually\:

| SQE | Operation | FD | Buffer |
|--------|--------|--------|--------|
| 1 | RECV | 5 | bufferA |
| 2 | RECV | 6 | bufferB |
| 3 | RECV | 7 | bufferC |

Still, we haven't explicitly entered the kernel.

## Step 3: Submit all three together

```c
int ret = io_uring_submit(&ring);
```

This is the important step.

In the ordinary, non-SQPOLL configuration, liburing\:

1. Publishes the prepared SQE references into the shared SQ index array.
2. Updates the shared SQ tail so the kernel can see the new entries.
3. Calls `io_uring_enter()` to notify the kernel and have it process the submissions.

Instead of making three separate kernel entries, we can submit all three requests together.

**Batch submission**

Receive A

Receive B

Receive C

(One submission batch)

**io\_uring\_enter()**

Kernel consumes the available SQEs

This is called batching.

### What does the kernel do after submission?

For each SQE, the kernel examines its opcode and other fields.

For our three operations\:

```text
SQE 1 → Attempt receive from Client A
SQE 2 → Attempt receive from Client B
SQE 3 → Attempt receive from Client C
```

There are two possibilities.

**Case A: Data is already available.**

The kernel may perform the receive immediately and post a CQE.

**Case B: Data is not available.**

The operation remains pending.

For socket operations, io\_uring normally uses an internal readiness-polling mechanism called *fast poll*\. When the socket becomes ready, the operation is retried and its result is eventually published as a CQE.

Your README describes this exact sequence: the kernel initially issues the receive non-blockingly, and if data isn't available, it arms an internal poll on the socket. fileciteturn0file1L460-L505

### A very important distinction

Suppose\:

```c
int ret = io_uring_submit(&ring);
```

returns\:

```text
ret = 3
```

This generally tells us that three submission entries were submitted.

It does **not** mean that all three receives have completed.

For example\:

```text
Submitted: 3
Completed: 1
Pending:  2
```

The submission result and the actual I/O result are different things.

The actual receive results will arrive through CQEs.

This distinction is frequently tested in vivas.

---

# 3\. What is batching, and why is it important?

Let's compare two approaches.

Suppose our server needs to submit 100 receive operations.

### Approach A: One system call per operation

```text
Prepare request A → Enter kernel
Prepare request B → Enter kernel
Prepare request C → Enter kernel
...
Prepare request 100 → Enter kernel
```

Potentially 100 kernel entries.

### Approach B: Batch submission

```text
Prepare request A
Prepare request B
Prepare request C
...
Prepare request 100
          |
          v
     io_uring_submit()
          |
          v
      Kernel entry
```

Potentially one kernel entry for the entire batch.

Why does this matter?

A system call involves\:

- Transitioning from user mode to kernel mode.
- Saving/restoring the necessary execution state.
- Running the kernel-side system-call logic.
- Returning to user mode.

These operations introduce overhead.

By submitting multiple requests together, we amortize that overhead across many operations.

One clarification\:

A system call is not automatically a context switch. The kernel can process submissions and return to the same application thread without scheduling a different thread.

Also, batching reduces submission overhead; it doesn't guarantee that all operations complete together or immediately.

## 3.1 Does batching happen automatically?

Not entirely.

Your application decides how many SQEs to prepare before calling `io_uring_submit()`\.

For example\:

```c
for (int i = 0; i < 100; i++) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);

    // Prepare operation
}

io_uring_submit(&ring);
```

This prepares multiple operations and submits them together.

In your implementation, the batching is created by the TCP event loop itself. It prepares operations for multiple clients before calling `counted_submit()`\.

We'll study that exact logic later.

---

# 4\. Does io\_uring\_submit() wait for operations to complete?

No. This is another important distinction.

Consider this sequence\:

```text
Application                  Kernel

Prepare receive request
       |
       v
io_uring_submit()
       |
       |--------------------> Kernel accepts request
       |                     No data available
       |                     Arms fast poll
       |
       |<-------------------- Submission returns
       |
Application can do other work
       |
       |                     Client sends data
       |                     Kernel retries receive
       |                     CQE is generated
       |
       v
Application collects CQE
```

The submission operation and the completion of the actual I/O are separate events.

This means your application doesn't have to stay blocked inside `io_uring_submit()` until the client sends data.

That is one of the fundamental properties of asynchronous I/O.

## 4.1 How does the application collect completions?

There are two relevant liburing functions in your project.

**A. `io_uring_peek_cqe()`**

```c
struct io_uring_cqe *cqe;

int rc = io_uring_peek_cqe(&ring, &cqe);
```

This checks whether a completion is already available.

If a CQE is present, liburing returns it.

If none is available, it returns an error indicating that no completion is currently available.

Importantly, this doesn't need a system call when the completion is already in the shared CQ.

**B. `io_uring_wait_cqe_timeout()`**

```c
struct __kernel_timespec timeout = {
    .tv_sec = 0,
    .tv_nsec = 100 * 1000000L
};

int rc = io_uring_wait_cqe_timeout(&ring, &cqe, &timeout);
```

This waits for a completion, with a timeout.

If no completion is available, the application can enter the kernel and wait instead of continuously checking the CQ.

Conceptually\:

```text
Check CQ
   |
   v
CQE available?
   |
   |-- YES --> Process CQE
   |
   |-- NO --> Wait in kernel
                 |
                 v
            CQE available
                 |
                 v
            Return to application
```

The wait operation normally uses the `io_uring_enter()` completion-waiting functionality.

The important distinction\:

| Function | Behavior | Kernel syscall when CQE exists? |
|--------|--------|--------|
| `io_uring_peek_cqe()` | Check immediately | No |
| `io_uring_wait_cqe_timeout()` | Wait for a completion or timeout | May enter kernel to wait |

## 4.2 What happens after retrieving a CQE?

Once you process a completion, you must mark it as consumed\:

```c
io_uring_cqe_seen(&ring, cqe);
```

This advances the CQ head through liburing's ring-management logic.

The slot can now be reused for a future completion.

Remember the sequence\:

```text
SQE prepared
    |
    v
io_uring_submit()
    |
    v
Kernel executes operation
    |
    v
CQE published
    |
    v
io_uring_peek_cqe() / wait_cqe()
    |
    v
Application reads result
    |
    v
io_uring_cqe_seen()
```

This is the basic submission-completion lifecycle.

---

# 5\. Now let's connect this to your actual implementation

This is where things become particularly interesting.

Your `io_uring_server.c` doesn't directly call the raw `io_uring_enter()` system call.

Instead, you use liburing and have created wrapper functions to count actual kernel entries.

Let's understand the first one.

## 5.1 Your `counted_submit()` function

Here is your actual implementation\:

```c
static int counted_submit(server_t *s, struct io_uring *ring)
{
    unsigned ready = io_uring_sq_ready(ring);

    if (s->config->sqpoll) {
        if (IO_URING_READ_ONCE(*ring->sq.kflags) &
            IORING_SQ_NEED_WAKEUP) {
            s->sc_enter++;
        }
    } else if (ready) {
        s->sc_enter++;
    }

    return io_uring_submit(ring);
}
```

fileciteturn0file0L39-L51

First, let's understand why you created this function.

Your project measures system calls per message.

But there is a complication\:

**Calling a liburing function does not necessarily mean that the function made a kernel system call.**

For example\:

```c
io_uring_peek_cqe()
```

can inspect shared memory without entering the kernel.

Similarly, under SQPOLL, submission can happen without a syscall.

Therefore, simply counting how many times the application calls `io_uring_submit()` would give incorrect syscall statistics.

Your `counted_submit()` attempts to count only the cases where liburing is expected to enter the kernel.

Let's examine it line by line.

### Line 1: Count SQEs ready for submission

```c
unsigned ready = io_uring_sq_ready(ring);
```

This tells us how many submission entries are currently available to be submitted.

Suppose\:

```text
SQ depth = 256

Prepared SQEs = 10
```

Then\:

```text
ready = 10
```

If no SQEs have been prepared\:

```text
ready = 0
```

This is important because, in ordinary io\_uring mode, there is no reason to make a submission syscall when there are no SQEs to submit.

### Line 2: Check whether SQPOLL is enabled

```c
if (s->config->sqpoll)
```

Your project supports two different submission mechanisms\:

1. Ordinary io\_uring.
2. io\_uring with SQPOLL.

Their syscall behavior is different.

Let's understand both separately.

---

# 6\. Ordinary io\_uring submission

Suppose SQPOLL is disabled.

Your code follows\:

```c
else if (ready) {
    s->sc_enter++;
}
```

Why?

Because when there are SQEs ready in the ordinary configuration, liburing needs to enter the kernel to notify it and submit those operations.

For example\:

```text
ready = 5
```

Your counter becomes\:

```text
sc_enter = sc_enter + 1
```

Then\:

```c
return io_uring_submit(ring);
```

liburing performs the actual submission.

Notice that the increment happens once, not five times.

Why?

Because five SQEs can be submitted through one `io_uring_enter()`\.

Consider\:

| Situation | Expected submission syscalls |
|--------|--------|
| 1 SQE ready | 1 |
| 5 SQEs ready | 1 |
| 100 SQEs ready | 1 |
| 0 SQEs ready | 0 |

This describes the ordinary submission path in your implementation.

Your README documents the same accounting rule: without SQPOLL, `sc_enter` is incremented when `io_uring_sq_ready()` is nonzero. fileciteturn0file1L545-L559

# 7\. SQPOLL: Can we submit without a system call?

Now let's understand the second branch of your `counted_submit()`\.

This is particularly important because your project implements three modes\:

1. Plain io\_uring.
2. SQPOLL.
3. SQPOLL with busy polling.

SQPOLL stands for **Submission Queue Polling**\.

The basic idea is simple\:

> Instead of making the application enter the kernel every time it submits operations, create a kernel thread that continuously checks the Submission Queue.

Let's understand this.

## 7.1 Normal submission vs SQPOLL

### Normal io\_uring

The application prepares SQEs and then invokes a submission operation.

```text
Application                  Kernel

Prepare SQEs
    |
    v
io_uring_submit()
    |
    |---- io_uring_enter() ---> Process submissions
    |
    v
Continue
```

The kernel learns about the submissions because the application enters the kernel.

### SQPOLL

Now imagine that a separate kernel thread is continuously monitoring the submission queue.

```text
Application                 Kernel SQPOLL thread

Prepare SQEs
    |
    v
Publish SQ tail
    |
    |                         Checks SQ tail
    |                         Notices new entries
    |                         Consumes SQEs
    |                         Processes operations
    |
    v
Continue
```

The application doesn't need a submission syscall every time.

The SQPOLL thread notices the new entries through shared memory.

[![io_uring kernel - OpenAnolis code repository](https://images.openai.com/static-rsc-4/VqTmFYK-TDx8h9EAhkqxgGr5jAgeZ5PVME400KM0XXLvWNEDgahl6VL4ZskOHreACc_W1U2F5Yp8az8K4X4E2dO_EDCyIJpvXktPf5-s-TRzRZRFSYxhB0uhs-TmW17L7FQ5Zk_8dbvRGQ_oBaqngp7VX1aibzXwli_3POT6pFfkvYdVGZ6LAAQdvwZwFEPM?purpose=fullsize)](https://openanolis.cn/sig/high-perf-storage/doc/218455073889779745)

This is useful because the kernel thread can observe new submissions without requiring the application to make a separate syscall for each batch.

But there is a trade-off\:

**The SQPOLL thread consumes CPU while it polls the submission queue.**

It is effectively busy waiting for new submissions while awake.

## 7.2 What if the SQPOLL thread goes to sleep?

Keeping a kernel thread continuously active is expensive when there is no work.

So io\_uring allows the SQPOLL thread to sleep after a configured idle period.

Your implementation uses\:

```c
#define SQPOLL_IDLE_MS 100
```

And in `ring_setup()`\:

```c
params.flags |= IORING_SETUP_SQPOLL;
params.sq_thread_idle = SQPOLL_IDLE_MS;
```

This means the kernel SQPOLL thread is configured to become idle after approximately 100 ms without submissions.

When it sleeps, the kernel sets a flag\:

```c
IORING_SQ_NEED_WAKEUP
```

This flag tells userspace\:

> The submission polling thread is asleep. You need to enter the kernel to wake it before relying on it to consume new submissions.

The liburing submission function handles this wake-up mechanism.

Now let's connect this to your code.

## 7.3 Understanding your SQPOLL condition

Your implementation\:

```c
if (s->config->sqpoll) {
    if (IO_URING_READ_ONCE(*ring->sq.kflags) &
        IORING_SQ_NEED_WAKEUP) {
        s->sc_enter++;
    }
}
```

Let's break this down.

**First\:**

```c
ring->sq.kflags
```

This points to the shared SQ flags maintained by the kernel.

Your application can inspect these flags to understand the state of the SQPOLL thread.

**Second\:**

```c
IO_URING_READ_ONCE(*ring->sq.kflags)
```

This reads the shared flags without allowing the compiler to treat the value as an ordinary cached variable.

We need to observe the current shared state rather than rely on an old value.

**Third\:**

```c
& IORING_SQ_NEED_WAKEUP
```

This checks whether the corresponding bit is set.

There are two situations\:

| State | Meaning | Submission syscall needed? |
|--------|--------|--------|
| Flag not set | SQPOLL thread is awake and polling | Normally no |
| Flag set | SQPOLL thread needs waking | Yes, to wake it |

Finally\:

```c
s->sc_enter++;
```

Your program increments its kernel-entry counter when it expects liburing to perform the wake-up entry.

The actual wake-up is handled inside\:

```c
io_uring_submit(ring);
```

### Important viva question

**Does SQPOLL eliminate all system calls?**

No.

It avoids the ordinary submission syscall while the polling thread is awake.

However\:

- The thread may need to be woken after sleeping.
- The application may still enter the kernel to wait for completions.
- Other system calls, such as socket configuration and shutdown operations, still exist.

Your README explicitly highlights this distinction. fileciteturn0file1L570-L599

---

# 8\. Your `counted_wait()` function

We have understood submission.

Now let's understand how your application waits for completed operations.

Here is your actual function\:

```c
static int counted_wait(
    server_t *s,
    struct io_uring *ring,
    struct io_uring_cqe **cqe
)
{
    if (io_uring_peek_cqe(ring, cqe) == 0) {
        return 0;
    }

    if (s->config->sqpoll_busy) {
        int64_t deadline = monotonic_ns() + WAIT_MS * 1000000LL;

        while (!stop_requested) {
            if (io_uring_peek_cqe(ring, cqe) == 0) {
                return 0;
            }

            if (monotonic_ns() >= deadline) {
                return -ETIME;
            }
        }

        return -EINTR;
    }

    struct __kernel_timespec timeout = {
        .tv_sec = 0,
        .tv_nsec = WAIT_MS * 1000000L
    };

    s->sc_enter++;
    s->sc_wait++;

    return io_uring_wait_cqe_timeout(ring, cqe, &timeout);
}
```

fileciteturn0file0L60-L84

This function has three distinct paths.

## Path A: Completion already exists

```c
if (io_uring_peek_cqe(ring, cqe) == 0) {
    return 0;
}
```

Suppose the kernel already completed a receive operation.

The CQ looks like\:

```text
CQ:
┌──────────────────────┐
│ Client A: 128 bytes  │
│ Empty                │
│ Empty                │
└──────────────────────┘
```

Your application doesn't need to enter the kernel and wait.

It can simply retrieve the CQE from shared memory.

Therefore\:

```text
Additional submission/wait syscall = 0
```

This is an important optimization.

## Path B: SQPOLL with busy polling

If there is no completion and\:

```c
s->config->sqpoll_busy
```

is true, your code enters a loop\:

```c
while (!stop_requested) {
    if (io_uring_peek_cqe(ring, cqe) == 0) {
        return 0;
    }
}
```

The application repeatedly checks the completion queue.

It does not sleep.

It does not call `io_uring_wait_cqe_timeout()`\.

It keeps checking\:

```text
Is there a CQE?
No.

Is there a CQE?
No.

Is there a CQE?
Yes!
Process it.
```

This is called **busy polling**\.

Why would someone do this?

Because sleeping and waking up a thread involves scheduling and wake-up overhead.

By continuously checking the CQ, the application can observe a completion as soon as it becomes visible.

But the trade-off is CPU usage.

In your project\:

- The SQPOLL kernel thread busy-polls submissions.
- Your application thread busy-polls completions.

Therefore, both can consume an entire CPU core.

This explains the high CPU consumption observed in your SQPOLL-busy experiments.

Your README explicitly discusses this two-core trade-off. fileciteturn0file1L599-L622

One small detail: your loop has a deadline of `WAIT_MS`, which is 100 ms. When it expires, the function returns `-ETIME`, allowing the outer event loop to check its shutdown condition and continue.

## Path C: Ordinary waiting

If neither of the above paths applies, your code reaches\:

```c
s->sc_enter++;
s->sc_wait++;
```

Then\:

```c
return io_uring_wait_cqe_timeout(ring, cqe, &timeout);
```

Here the application enters the kernel and waits for a completion, with a 100 ms timeout.

Why does the timeout matter?

Remember the framework's shutdown mechanism.

When the user sends SIGTERM or SIGINT, the signal handler sets\:

```c
stop_requested = 1;
```

But the event loop might be waiting for a network operation to complete.

The timeout ensures that the application periodically wakes up and checks whether it has been asked to stop.

The timeout comes from your shared framework\:

```c
#define WAIT_MS 100
```

fileciteturn0file3L22-L31

---

# 9\. How submission and waiting fit together in your TCP event loop

Now let's connect both wrapper functions to the actual `run_tcp()` implementation.

Your event loop follows this structure\:

```c
while (!stop_requested) {

    // 1. Prepare an accept request if needed

    // 2. Prepare operations for ready clients

    // 3. Submit everything
    int rc = counted_submit(s, ring);

    // 4. Wait or peek for completions

    // 5. Process completed operations
}
```

fileciteturn0file0L311-L405

Let's use a practical example.

Suppose the server currently has four clients\:

| Client | Current state | Next operation |
|--------|--------|--------|
| A | Receiving request data | RECV |
| B | Sending response | SEND |
| C | Receiving request data | RECV |
| D | Receiving request data | RECV |

Your ready queue contains\:

```text
head
 |
 v
A → B → C → D
```

### Step 1: Prepare SQEs

Your event loop removes each client from the ready queue and prepares an appropriate SQE.

```text
SQE 1 → RECV Client A
SQE 2 → SEND Client B
SQE 3 → RECV Client C
SQE 4 → RECV Client D
```

It also sets the `user_data` pointer for each client and increments `outstanding`\.

### Step 2: Submit all operations

```c
int rc = counted_submit(s, ring);
```

In ordinary mode, these four operations can be submitted with one kernel entry.

The kernel starts processing them.

Suppose\:

- Client A has data available.
- Client B's send can proceed.
- Client C has no data.
- Client D has no data.

The kernel may complete A and B immediately, while C and D remain pending.

### Step 3: Collect completions

Your application calls `counted_wait()`\.

If the completions for A and B are already available, `io_uring_peek_cqe()` retrieves them without another syscall.

Otherwise, the application waits for a completion.

### Step 4: Process the results

Suppose the CQEs are\:

```text
CQE 1 → Client B → Send completed
CQE 2 → Client A → Receive completed
```

Your code processes them, updates the framework's client states and puts the clients back into the ready queue if they need another operation.

For example\:

```text
Client A: RECEIVE → SEND
Client B: SEND → HEADER
```

The next iteration prepares new SQEs for their next operations.

### The complete flow

**Application ready queue**

Clients needing their next operation

**Prepare SQEs**

RECV / SEND / ACCEPT

**counted\_submit()**

Submit the batch

Kernel performs operations asynchronously

**counted\_wait()**

Peek or wait for CQEs

**Process CQEs**

Recover user\_data, process results, update clients

**Next iteration**

This is the architecture of your engine.

Notice that your application doesn't need to maintain an epoll-style registration for every connected client. Each client has an operation submitted when it needs to receive or send data.

---

# 10\. An important issue: What if the SQ becomes full?

Your implementation has\:

```c
#define RING_DEPTH 256
```

This means your submission queue has 256 entries.

Suppose there are 1,000 clients ready for I/O.

Can you prepare 1,000 SQEs simultaneously?

No. Your SQ only has 256 entries.

Your code uses\:

```c
struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
```

If the SQ has no available entry, this returns `NULL`\.

The application must submit the already-prepared operations, process completions and make progress before preparing more.

Your `run_tcp()` handles this by submitting available work and then collecting completions.

There is also an interesting stall problem that you encountered during testing.

Suppose\:

- SQ has become full.
- Several clients remain in your application-side ready queue.
- Submitted operations are receives.
- The clients corresponding to those receives are waiting for responses.
- Those responses are queued but haven't yet been submitted.

If the application waits for a completion before submitting the remaining operations, it can become stuck waiting for an operation whose necessary follow-up work is still sitting in the application queue.

Your implementation fixes this by peeking instead of blocking whenever the ready queue still contains clients.

This is a particularly good viva topic because it demonstrates that understanding queue capacity isn't enough; you also need to reason about progress through the event loop.

We'll examine this in depth when we reach the submission loop.

fileciteturn0file1L585-L599 fileciteturn0file0L344-L364

# 11\. Putting everything together: Three submission modes

Before we finish, let's compare how the same event loop behaves in your three configurations.

| Feature | Plain io\_uring | SQPOLL | SQPOLL + busy polling |
|--------|--------|--------|--------|
| Who consumes SQ? | Kernel on submission entry | Kernel polling thread | Kernel polling thread |
| Submission syscall | When submitting entries | Only when wake-up is needed | Only when wake-up is needed |
| How application checks CQ | Peek, then wait | Peek, then wait | Continuously peek |
| Application sleeps? | Yes, when waiting | Yes, when waiting | No, during busy polling |
| CPU consumption | Work-dependent | Additional polling CPU | Two busy polling threads |

The important insight\:

- SQPOLL optimizes the **submission side**\.
- Busy polling in your implementation optimizes the **completion-waiting side**\.
- Combining them avoids sleeping on either side, but costs CPU resources.

We'll dedicate a separate lesson to understanding SQPOLL internally, its flags, CPU affinity and the high CPU usage in your experiments.

---

# 12\. Lesson 4 - What you should remember

**Submission**

1. Application prepares SQEs.
2. liburing publishes them to the SQ.
3. `io_uring_submit()` submits them, ordinarily through `io_uring_enter()`\.
4. The kernel consumes and processes the requests.

---

**Completion**

1. Kernel finishes an operation.
2. Kernel publishes a CQE.
3. Application checks the CQ using `io_uring_peek_cqe()` or waits using `io_uring_wait_cqe_timeout()`\.
4. Application processes the result.
5. `io_uring_cqe_seen()` marks the completion as consumed.

---

**Optimizations**

- Batching reduces submission syscall frequency.
- Shared memory enables direct queue communication.
- SQPOLL reduces submission syscalls further.
- CQ polling can avoid waiting syscalls when completions are already available.

# 13\. Conceptual questions - test your understanding

These are slightly more viva-oriented. Try to explain the reasoning, not just give one-word answers.

**Q1.** Suppose you prepare 50 SQEs and call `io_uring_submit()` once.

Does the kernel necessarily complete all 50 operations before the function returns? Explain the difference between submitting and completing an operation.

**Q2.** Your application prepares five SQEs, but doesn't call `io_uring_submit()`\.

In ordinary io\_uring mode, will the kernel automatically start executing these operations simply because the SQEs have been prepared in memory? Why?

**Q3.** Your implementation uses\:

```c
unsigned ready = io_uring_sq_ready(ring);

if (ready) {
    s->sc_enter++;
}

return io_uring_submit(ring);
```

Why does it increment the counter once when `ready = 50`, rather than incrementing it 50 times?

**Q4.** (Viva level)

Suppose you enable SQPOLL. The SQPOLL thread is awake, and you prepare 20 SQEs.

Why can the application submit these without making a syscall? How does the kernel thread discover the new entries?

**Q5.** Your server uses SQPOLL, but does not enable busy polling.

Does that mean your application thread never sleeps or enters the kernel? Explain what happens on the completion side.

---

## Next lesson: Understanding the kernel's execution of I/O requests

After these questions, we'll study something particularly important\:

**Lesson 5: What actually happens inside the kernel when an SQE is submitted?**

We'll follow a TCP receive request through these stages\:

1. Kernel consumes the SQE.
2. Kernel tries to execute the receive operation.
3. What happens if data is already available?
4. What happens if data is not available?
5. How fast poll works and how the socket wake-up mechanism works.
6. How the operation gets retried and how the CQE is generated.

This will help you understand an important viva question\:

*If io\_uring is completion-based, why does it still use readiness polling internally?*

That question is particularly relevant to your comparison of epoll and io\_uring.

## Prompt:
08/11/2026, 2:11:38 am

Lesson 5

## Response:
08/11/2026, 2:11:46 am · gpt-6-t-mini

Lesson 5 follows one submitted TCP receive all the way through the kernel: the immediate attempt, what happens when the socket has no data, how fast poll arranges a retry, and how the CQE gets published. I’ll use the flow described in your project README as the main reference, and distinguish that conceptual path from the parts your code itself directly controls.

> **Explaining kernel execution**
>
> I’m using the README as the primary reference and will explain its conceptual flow: submission, nonblocking attempt, readiness polling, retry, completion, and how `DEFER_TASKRUN` shifts retry work toward the submitting thread.
>
> **Explained kernel execution**
>
> 
>
> **Explained io_uring receive execution flow**
>
> 
>
> **Tracing kernel execution**
>
> I’m supplementing the README’s model with official kernel documentation, while separating conceptual behavior from version-specific internals. I’ll trace a TCP receive from submission through readiness notification, retry, and CQE publication.
>
> **Clarifying retry mechanics**
>
> I’m distinguishing internal readiness polling from application-managed readiness. A receive may complete immediately; otherwise the kernel waits, retries when data arrives, and posts a CQE. Exact scheduling details vary by operation and kernel version.
>
> **Following receive completion**
>
> I’m emphasizing that the application thread isn’t blocked while a socket lacks data. The kernel arms internal readiness handling, later retries in suitable task context, and finally publishes bytes, EOF, or an error through the CQE.
>
> **Traced kernel execution, retry mechanics, and receive completion**
>
> 
>
> **Tracing kernel execution**
>
> I’m ready to explain the receive path using the README’s model: the application submits a receive, the kernel tries immediately, then internally waits and retries if the socket has no data.
> 
> The key distinction is that readiness polling happens inside the kernel; the application receives only the final operation result, not a separate readiness notification.
> 
> 1. `io_uring_prep_recv` describes the receive in an SQE:
> 
>    - target socket;
>    - destination buffer;
>    - maximum length;
>    - receive flags.
> 
>    The application submits that SQE and must keep the socket and buffer valid until the receive completes.
> 
> 2. The kernel consumes the SQE and attempts the socket receive without blocking the application thread.
> 
>    If TCP data is already available, the receive can complete immediately. The kernel copies available bytes into the supplied buffer and publishes a CQE whose result contains the byte count.
> 
> 3. If the socket has no data, the operation encounters the equivalent of “try again later.”
> 
>    Instead of returning that temporary condition directly to the application, `io_uring` can attach internal polling to the socket. Conceptually, this is similar to waiting for readable readiness, but the kernel keeps ownership of the original receive request.
> 
> 4. When network data arrives, the normal networking path places it into the socket’s receive queue and wakes readers waiting for readability.
> 
>    The internal polling mechanism notices that transition. The kernel then arranges for the receive operation to be retried in suitable task context rather than attempting the full operation directly from a low-level wakeup callback.
> 
> 5. The retry performs the actual receive again.
> 
>    If bytes are now available, the kernel copies them into the original buffer and posts the CQE. The application later sees the completion through its normal completion-processing path.
> 
> The CQE represents completion of the receive operation, not merely “the socket became readable.” Its result may be:
> 
> - a positive byte count;
> - zero for orderly end-of-stream;
> - a negative error value.
> 
> That distinction is central: with traditional readiness APIs, the application receives readiness and must call `recv`. With this `io_uring` pattern, the kernel internally handles readiness and retry, while the application waits for the completed receive result.
>
> **Explaining task context**
>
> I’m refining one detail: readiness wakes the request, but the full retry runs in suitable task context. Depending on timing, that may occur during kernel entry, wakeup, or completion waiting, then the CQE is published.
>
> **Contrasting readiness and completion**
>
> I’m connecting this to your event loop: one client operation remains pending until completion, and the kernel—not your code—handles readiness, retries, and CQE publication.
> 
> With `epoll`, the sequence is:
> 
> ```text
> socket becomes readable
>         ↓
> epoll_wait returns readiness
>         ↓
> application calls recv()
>         ↓
> application handles bytes
> ```
> 
> With the receive operation in your framework:
> 
> ```text
> submit recv request
>         ↓
> kernel tries recv immediately
>         ↓
> if unavailable, kernel waits internally
>         ↓
> socket becomes readable
>         ↓
> kernel retries recv
>         ↓
> CQE reports bytes, EOF, or error
> ```
> 
> That distinction matters. Your application isn’t receiving “the socket is readable.” It’s receiving “the requested operation completed.”
> 
> The README describes this as a fast-poll path: when the socket operation would block, `io_uring` attaches internal polling to the operation, waits for readiness, and retries it. The application doesn’t manually register an `epoll` interest or issue a second `recv` call. fileciteturn0file0
> 
> For a receive request, the buffer remains owned by the outstanding operation until the CQE appears. That’s why your connection state tracks whether an operation is pending and only requeues the connection after completion. The next receive may use a different buffer offset when a message arrives in pieces. fileciteturn0file0
> 
> A simplified kernel-side sequence is:
> 
> 1. The SQE describes the socket, buffer, length, and flags.
> 2. The kernel attempts the socket operation without sleeping the application inside `recv`.
> 3. If data is available, bytes are copied immediately and a CQE is posted.
> 4. If the operation would block, internal readiness handling is attached.
> 5. Network activity makes the socket readable.
> 6. A retry is scheduled in suitable task context.
> 7. The receive runs again.
> 8. The result is written to the CQE.
> 
> The retry may be arranged through deferred task work associated with the request’s task. If the application is waiting for completions, that wait can be awakened after the CQE is produced. If the application is busy polling, it can observe the CQE during its next completion check.
> 
> I’m being careful not to describe this as one universal implementation path: exact scheduling varies with the operation and kernel version. The stable conceptual model is that readiness is an internal mechanism, while the application sees operation completion.
> 
> Your send path follows the same general idea in the opposite direction. If the socket send buffer has room, the send progresses immediately. If it cannot accept more data, the kernel can wait for writability, retry the send, and eventually publish the number of bytes sent.
> 
> One subtle but important consequence is partial progress. A receive may return fewer bytes than requested, and a send may accept fewer bytes than supplied. Your connection state keeps the accumulated byte count and submits the next operation for the remaining portion.
>
> **Explained kernel execution, task context, readiness, and completion**
>
> 
>
> **Searched 4 websites**
>
> 
>
> Worked for 1m 16s

# Lesson 5: What happens inside the kernel when an SQE is submitted?

This lesson is particularly important because it answers a question that might seem confusing\:

**If io\_uring is completion-based, why does it still use readiness polling internally?**

To understand this, let's follow an actual TCP receive operation through the kernel.

We'll use the same example throughout.

Suppose\:

```text
Server FD       = 5
Client          = A
Receive buffer  = 4096 bytes
Available data  = Initially 0 bytes
```

Our application wants to receive data from Client A.

---

## 1\. Step 1: Application prepares and submits the receive request

Our application executes\:

```c
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);

io_uring_prep_recv(
    sqe,
    5,
    buffer,
    4096,
    0
);
```

Then\:

```c
io_uring_submit(&ring);
```

Conceptually, the SQE contains\:

```text
Operation: RECV
FD:        5
Buffer:    buffer
Length:    4096
```

The kernel consumes this SQE and tries to execute the receive operation.

Remember, submitting an operation doesn't mean that the receive has completed.

The kernel now owns responsibility for executing the operation.

Your README describes the initial socket operation as being issued non-blockingly. fileciteturn0file1L460-L479

There are two possibilities.

# 2\. Case A: Data is already available

Suppose Client A has already sent\:

```text
"Hello Server"
```

The TCP networking stack has processed the incoming data, and it is available in the socket's receive buffer.

Our receive request asks for up to 4096 bytes.

The kernel attempts the receive operation.

Since data is already available\:

1. The kernel reads the available data from the socket's receive buffer.
2. It copies the received bytes into our application's specified buffer.
3. It determines the number of bytes received.
4. It creates a CQE containing the result.
5. It publishes the CQE to the Completion Queue.

For example\:

```text
Requested: 4096 bytes
Received:  12 bytes
```

The CQE contains\:

```text
res = 12
```

The application can now retrieve the completion.

(Immediate completion)

Application submits RECV SQE

Kernel checks socket receive buffer

(Data available)

Kernel copies data into application buffer

**CQE published**

# res = 12

Notice something\:

The application did not need to call `recv()` separately.

It already submitted the receive operation as an SQE.

The kernel performed the operation and reported its result.

But what happens when data is not available?

That's where things become interesting.

---

# 3\. Case B: No data is available - enter fast poll

Let's return to our original situation.

Client A has not sent anything.

Our application submits\:

```c
io_uring_prep_recv(sqe, client_fd, buffer, 4096, 0);
```

The kernel tries to perform the receive.

But the socket receive buffer is empty.

What would happen with an ordinary blocking `recv()`?

The application thread would sleep waiting for data.

But io\_uring doesn't want to block our application thread inside the receive operation.

Instead, it uses an internal mechanism called **fast poll**\.

## 3.1 What is fast poll?

Fast poll is an internal mechanism used by io\_uring to monitor whether an asynchronous I/O operation can make progress.

In this case, the kernel needs to know\:

> When does Client A's socket become readable?

So it arms an internal poll request on that socket.

This resembles the readiness notification mechanism used by epoll.

But there is a very important difference.

With epoll\:

```text
Socket becomes readable
       |
       v
epoll_wait() returns
       |
       v
Application calls recv()
```

With io\_uring\:

```text
Socket becomes readable
       |
       v
Internal poll detects readiness
       |
       v
Kernel retries the receive operation
       |
       v
CQE is generated
```

The application doesn't have to perform the receive after readiness notification.

**The kernel handles that next step itself.**

Your project README explicitly describes io\_uring arming an internal poll when the initial receive cannot complete. fileciteturn0file1L463-L479

## 3.2 What happens to the receive buffer?

This is an important detail.

Remember our SQE\:

```text
FD = 5
Buffer = 0x1000
Length = 4096
```

The kernel has received a pointer to our application's buffer.

Since the receive operation is still pending, the kernel may need to write into that buffer later.

Therefore\:

**The application must not free or reuse that buffer while the operation is in flight.**

For example, this is unsafe\:

```text
Submit receive operation
        |
        v
Operation becomes pending
        |
        v
Free the buffer
        |
        v
Data arrives
        |
        v
Kernel attempts to write into freed memory
```

This can cause memory corruption or a use-after-free bug.

This is directly relevant to your implementation's `pending` variable\:

```c
c->pending = 1;
```

Your framework also documents this ownership concern.

The connection's buffer must remain valid while the kernel owns the pending receive operation. fileciteturn0file3L61-L75 fileciteturn0file1L585-L599

---

# 4\. What happens when data finally arrives?

Now suppose Client A sends\:

```text
"Hello Server"
```

We need to understand how this event travels through the OS.

## 4.1 Data arrives at the socket

The incoming packet is processed by the Linux networking stack.

For a TCP connection, the networking stack handles things such as\:

- TCP sequence numbers.
- Retransmissions and ordering.
- TCP segment processing.
- Placing received data into the socket's receive queue.

Once data becomes available, the socket can be considered readable.

This causes the socket's wait-queue notification mechanism to wake interested internal poll waiters.

Remember, the io\_uring fast-poll mechanism has registered interest in this socket.

It can now react to the readiness notification.

## 4.2 The poll callback queues task work

This is the next important internal concept.

The fast-poll notification does not simply copy the network data into our application buffer directly inside the socket wake-up callback.

Instead, the wake-up mechanism schedules **task work** associated with the io\_uring request and its originating task.

What is task work?

Think of it as deferred work that the kernel arranges to execute in the context of a particular task.

Why is this needed?

Because handling the actual receive operation requires more than merely detecting that data is available.

The kernel needs to retry the receive operation and handle its result.

The simplified sequence is\:

```text
Data arrives
     |
     v
TCP stack processes data
     |
     v
Socket receive buffer becomes readable
     |
     v
Socket wake-up notification
     |
     v
Fast-poll callback
     |
     v
Task work is queued
```

Your README describes this sequence and explains that task work causes the receive operation to be retried. fileciteturn0file1L466-L479

## 4.3 The kernel retries the receive

The scheduled task work eventually runs in an appropriate task context.

The kernel retries the receive operation.

This time, data is available.

So\:

1. The kernel reads the data from the socket receive buffer.
2. It copies the data into the application's original buffer.
3. It obtains the result.
4. It prepares a CQE.

For example\:

```text
Requested receive length = 4096

Actual received data = 12 bytes

CQE:
    res = 12
```

One subtlety: the task-work execution and wake-up timing depend on the ring configuration and kernel execution path. Your README gives the useful conceptual model: task work is queued, and it runs when the originating task next enters the relevant kernel processing path or is woken from an io\_uring wait.

## 4.4 CQE gets published

The kernel now writes the completion entry into the CQ.

Conceptually\:

```text
CQE:
    user_data = Client A's conn_t pointer
    res       = 12
    flags     = 0
```

The kernel updates the CQ tail to publish the entry.

If our application is sleeping in a completion wait, the completion can wake it.

If it is already busy polling the CQ, it can simply observe the new CQE.

(Delayed completion)

Application submits RECV SQE

Kernel attempts receive

(No data available)

Kernel arms fast poll

Receive operation stays in flight

Time passes; client sends data

Socket becomes readable

Wake-up notification and task work

Kernel retries the receive

**CQE published**

# res = 12

This is the complete conceptual journey of a TCP receive operation.

---

# 5\. So does io\_uring still use readiness polling?

Yes\!

This is probably the most important conceptual takeaway from this lesson.

You might have initially thought\:

> epoll uses readiness, while io\_uring uses completion. So io\_uring must have completely eliminated readiness checking.

But that's not quite correct.

Internally, io\_uring can use readiness notification to determine when an operation can make progress.

The difference is who handles the operation after readiness is detected.

Let's compare.

## epoll

```text
Application submits interest in FD
         |
         v
Socket becomes readable
         |
         v
Kernel notifies application
         |
         v
Application wakes up
         |
         v
Application calls recv()
         |
         v
Receive operation completes
```

## io\_uring

```text
Application submits receive operation
         |
         v
Kernel attempts recv
         |
         v
No data available?
         |
         v
Kernel internally arms fast poll
         |
         v
Socket becomes readable
         |
         v
Kernel retries recv
         |
         v
Kernel publishes CQE
         |
         v
Application wakes up / collects CQE
```

The key difference\:

**epoll reports readiness to the application. io\_uring uses readiness internally to complete the operation that the application already requested.**

This is why we call io\_uring completion-based even though its internals can rely on readiness mechanisms.

---

# 6\. Does the same thing happen with send operations?

Yes, the general idea is similar, but the condition is different.

For a receive operation\:

```text
Socket receive buffer empty
       |
       v
Wait for readable condition
```

For a send operation\:

```text
Socket send buffer has insufficient space
       |
       v
Wait for writable condition
```

Let's understand a send operation.

Suppose our application submits\:

```c
io_uring_prep_send(
    sqe,
    client_fd,
    buffer,
    4096,
    MSG_NOSIGNAL
);
```

The kernel attempts to send the data.

### Case A: Send buffer has space

The kernel accepts some or all of the data for transmission.

A CQE is generated with the number of bytes accepted.

For example\:

```text
Requested = 4096
Sent      = 4096

CQE res = 4096
```

### Case B: Send buffer is full

The send cannot currently make progress.

The kernel can arm internal poll for writable readiness.

When sufficient space becomes available, the kernel retries the send and generates a CQE.

For example\:

```text
Requested = 4096

First attempt:
No sufficient space

Later:
Space becomes available

Retry:
3072 bytes accepted

CQE res = 3072
```

Notice that a send can complete partially.

Your application must handle the remaining bytes.

The Linux `io_uring_enter(2)` documentation describes the send and receive operations as equivalents of the corresponding socket operations and documents their internal polling options. \[[io\_uring\_enter(2)](https://man7.org/linux/man-pages/man2/io_uring_enter.2.html?utm_source=chatgpt.com)\]

---

# 7\. What if the operation cannot be completed using this approach?

There's one more internal mechanism worth knowing.

The kernel has a worker mechanism called **io-wq**\.

Some operations may not be suitable for execution in a non-blocking way.

For those operations, io\_uring can delegate work to kernel worker threads.

These threads can appear with names such as\:

```text
iou-wrk-<pid>
```

However, don't confuse these with SQPOLL.

| Thread | Responsibility |
|--------|--------|
| SQPOLL thread (`iou-sqp-*`\) | Polls the Submission Queue and consumes new submissions |
| io-wq worker (`iou-wrk-*`\) | Executes operations that need asynchronous worker execution |

For the TCP socket receive/send operations used in your implementation, the normal path described above is non-blocking issue plus fast poll. You should not assume that every socket operation is sent to an io-wq worker.

Your project README also explicitly distinguishes these two mechanisms. fileciteturn0file1L463-L479

---

# 8\. How does this connect with your implementation?

Now let's map this internal behavior to your actual code.

Your TCP engine prepares a receive request here\:

```c
if (conn_is_reading(c)) {
    io_uring_prep_recv(
        sqe,
        c->fd,
        conn_buffer(c),
        conn_length(c),
        0
    );
}
```

Then it associates the request with its connection\:

```c
io_uring_sqe_set_data(sqe, c);
```

And marks the operation as pending\:

```c
c->pending = 1;
outstanding++;
```

fileciteturn0file0L329-L342

Let's relate each line to what we've learned.

| Your code | What happens internally |
|--------|--------|
| `io_uring_prep_recv()` | Describes the receive operation |
| `io_uring_submit()` through `counted_submit()` | Makes the submission available to the kernel |
| `user_data = c` | Associates the operation with the client |
| `pending = 1` | Application tracks that the kernel may access the client's buffer |
| Kernel attempts receive | Reads from socket if data is available |
| Fast poll | Waits internally if receive cannot currently proceed |
| CQE published | Kernel reports the receive result |
| `io_uring_cqe_get_data()` | Retrieves the client pointer |
| `conn_complete(c, n)` | Framework updates the TCP request state |

The most important detail is that **your engine does not call `recv()` itself**\.

Instead, after the kernel publishes a completion, your engine passes the result to\:

```c
conn_complete(c, n);
```

Your framework then updates the client's state and decides whether the next operation should be another receive or a send.

For example\:

```text
Client A is in RECEIVE stage

RECV operation submitted
        |
        v
Kernel completes receive: 4096 bytes
        |
        v
conn_complete(c, 4096)
        |
        v
More body bytes remaining?
        |
       YES
        |
        v
Client A goes back into ready queue
        |
        v
Another RECV operation submitted
```

Or, once the complete request body has arrived\:

```text
Client A is in RECEIVE stage

RECV operation completes
        |
        v
conn_complete(c, n)
        |
        v
Body completely received
        |
        v
Framework changes stage to SEND
        |
        v
Next SQE will be a SEND operation
```

This is how your io\_uring engine integrates with the common framework rather than implementing a separate TCP protocol state machine.

fileciteturn0file2L190-L255 fileciteturn0file0L395-L405

---

# 9\. Important edge cases to understand

Before the viva questions, let's cover four cases.

**1\. What if the client closes the connection?**

A receive can complete with\:

```text
res = 0
```

This indicates EOF for the stream socket.

Your framework handles this in `conn_complete()`\:

- If the client closed cleanly between requests, it returns `1`\.
- If the connection closes during an incomplete request, it returns `-ECONNRESET`\.

**2\. What if only some bytes arrive?**

Suppose the receive operation requested 4096 bytes, but only 1024 bytes were received.

The CQE can contain\:

```text
res = 1024
```

The application must account for the partial receive and submit another receive if more data is needed.

Your framework tracks this using `done` and `want`\.

**3\. What if the operation fails?**

A CQE can contain a negative error\:

```text
res = -ECONNRESET
```

Your framework interprets negative results and decides whether to retry or close the client.

**4\. What if the server is shutting down while a receive is pending?**

The kernel may still be using the client's buffer.

Therefore, your implementation must cancel or drain outstanding operations and collect their completions before freeing client structures.

This is exactly why your `drain()` function exists.

We'll dedicate a separate lesson to cancellation and shutdown because this is a particularly important correctness issue in your implementation.

---

# 10\. The most important comparison for your viva

| Question | epoll | io\_uring |
|--------|--------|--------|
| What does the application submit? | Interest in socket readiness | Actual I/O operation |
| What happens when socket becomes readable? | Application gets notified | Kernel retries pending receive |
| Who performs `recv()`? | Application | Kernel, as part of submitted operation |
| What does the application receive? | Readiness event | Completion result |
| Does it use readiness internally? | Yes, directly | Yes, for mechanisms such as fast poll |
| Does the application need separate `recv()` after notification? | Yes | No |

**A strong viva answer\:**

"Although io\_uring exposes a completion-based interface, it can use readiness polling internally. When a submitted socket receive cannot complete immediately, the kernel arms an internal fast-poll request. Once the socket becomes readable, the wake-up mechanism schedules task work to retry the receive. The kernel then places the actual result in the completion queue. Unlike epoll, the application doesn't have to receive a readiness notification and issue a separate recv system call."

That is the key concept I want you to be able to explain confidently.

---

# 11\. Check your understanding

Let's test whether you've understood what happens internally.

**Lesson 5 quiz** (/4 answered)

## What comes next?

We've now covered the fundamental io\_uring architecture, the queues, SQEs, CQEs, submission, batching, and what happens internally when an operation executes.

I suggest we now move from the general mechanism to the optimizations that are especially important in your project\:

**Lesson 6: SQPOLL and busy polling - how your implementation uses them, how they reduce kernel entries, how they affect context switches, and why your experiments showed approximately 90% CPU usage.**

After that, we'll begin the detailed walkthrough of `io_uring_server.c`, starting with the ring setup and initialization functions.

## Prompt:
08/11/2026, 2:24:05 am

Lesson 6

## Response:
08/11/2026, 2:24:12 am · gpt-6-t-mini

Lesson 6 has two distinct mechanisms in your project: SQPOLL changes how submissions reach the kernel, while your `--sqpoll-busy` option changes how the application waits for completions. I'll explain them separately first, then show how they combine and why the README reports high CPU usage.

> **Clarifying SQPOLL behavior**
>
> I’m refining the explanation: SQPOLL uses a kernel-created, process-associated polling thread, while the optional busy loop keeps the application thread checking completions. I’ll distinguish their roles, timing, wakeups, and syscall measurements carefully.
>
> **Refining SQPOLL details**
>
> I’m separating CPU usage, wakeups, syscalls, and context switches. SQPOLL can avoid submission syscalls while active, but sleeping may require a wakeup; busy completion polling adds application-thread CPU usage.
>
> **Distinguishing polling modes**
>
> I’m emphasizing that this code’s busy mode is a userspace completion spin, not kernel I/O polling. SQPOLL keeps a kernel thread active, while normal waits still sleep and may require wakeups.
>
> **Clarified SQPOLL behavior and distinguished polling modes**
>
> 
>
> **Configured io_uring server setup and benchmarking**
>
> 
>
> Worked for 1m 8s

# Lesson 6: SQPOLL and Busy Polling

This lesson is particularly important for your project because you implemented three io\_uring configurations and the professor/TA specifically questioned why io\_uring consumes approximately 90% CPU even at low loads.

We need to understand two separate things\:

1. **SQPOLL\:** How the kernel consumes submission requests without requiring a syscall for every submission.
2. **Busy polling\:** How the application checks for completions without sleeping.

These optimize two different sides of io\_uring.

---

# 1\. First, understand the problem with normal io\_uring

In ordinary io\_uring, remember our submission process\:

```text
Application                    Kernel

Prepare SQEs
    |
    v
io_uring_submit()
    |
    |------ io_uring_enter() --> Consume SQEs
    |                            Process requests
    |
    v
Continue
```

Suppose our application continuously submits batches of operations.

Every time it needs to notify the kernel about new submissions, it normally uses `io_uring_enter()`\.

Even though batching reduces the number of syscalls, these kernel entries still have a cost.

Can we eliminate the submission syscall in the common case?

That's exactly what SQPOLL attempts to do.

# 2\. SQPOLL - Submission Queue Polling

SQPOLL stands for **Submission Queue Polling**\.

The central idea\:

Instead of requiring the application to enter the kernel to submit every batch, we create a separate kernel thread that continuously monitors the Submission Queue.

Let's understand how.

## 2.1 Without SQPOLL

Suppose the application prepares three SQEs.

```text
Application

SQ:
┌───────────────┐
│ SQE 1: RECV   │
│ SQE 2: SEND   │
│ SQE 3: ACCEPT │
└───────────────┘
       |
       v
io_uring_submit()
       |
       v
io_uring_enter()
       |
       v
Kernel consumes the submissions
```

The application needs to enter the kernel to notify it.

## 2.2 With SQPOLL

Now we introduce another thread.

This is a kernel thread created for the io\_uring instance.

It continuously checks the Submission Queue.

## User space

**Your application thread**

Prepares SQEs and publishes SQ tail

Shared SQ memory

## Kernel side

**SQPOLL kernel thread**

(Continuously polls SQ)

Notices new entries and consumes them

Now consider the sequence\:

1. Your application prepares SQEs.
2. It publishes the new SQ tail.
3. The SQPOLL thread notices that new entries are available.
4. It consumes the submissions.
5. The application doesn't need a submission syscall in this common case.

**The application and SQPOLL thread communicate through shared memory.**

This is why SQPOLL can reduce submission syscalls.

Your README describes this exact mechanism: the kernel thread watches the SQ tail, while the application writes submissions into shared memory. fileciteturn0file1L570-L585

---

# 3\. But why does SQPOLL consume so much CPU?

Here's the trade-off.

How does the SQPOLL thread know whether a new submission has arrived?

It repeatedly checks the Submission Queue.

Conceptually\:

```text
Check SQ tail
     |
     v
New submissions?
     |
   No
     |
     v
Check again
     |
     v
New submissions?
     |
   No
     |
     v
Check again
     |
     v
New submissions?
     |
   Yes
     |
     v
Consume submissions
```

It keeps checking while it is awake.

This is called **busy waiting** or **busy polling**\.

The problem is obvious\:

Even when there is no work, the thread can keep consuming CPU cycles.

Imagine this\:

```text
Your application thread → CPU 2
SQPOLL kernel thread    → CPU 4
```

Even if your application submits only 10 requests per second, the SQPOLL thread may continuously consume CPU while awake.

This is the trade-off\:

**SQPOLL**

Fewer submission syscalls

High CPU consumption from polling

Requires an additional kernel thread

## 3.1 Does SQPOLL always spin?

No.

Linux provides an idle timeout for the SQPOLL thread.

When no submissions arrive for the configured period, the thread can go to sleep.

This is useful when the server is inactive.

For example\:

```text
SQPOLL thread is awake

0 ms    → Checks SQ
10 ms   → Checks SQ
20 ms   → Checks SQ
...
100 ms  → No submissions during idle period
        → Goes to sleep
```

When the application later needs to submit work, it must wake the thread.

Let's see how your implementation configures this.

---

# 4\. SQPOLL in your actual implementation

Let's revisit your `ring_setup()` function.

```c
#define SQPOLL_IDLE_MS 100
```

And\:

```c
static int ring_setup(server_t *s, struct io_uring *ring)
{
    struct io_uring_params params = {0};

    params.flags = IORING_SETUP_CQSIZE;
    params.cq_entries = completion_entries(s);

    if (s->config->sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = SQPOLL_IDLE_MS;

        if (s->config->sqpoll_cpu >= 0) {
            params.flags |= IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu =
                (unsigned)s->config->sqpoll_cpu + (unsigned)s->index;
        }
    }

    return io_uring_queue_init_params(RING_DEPTH, ring, &params);
}
```

fileciteturn0file0L111-L125

Let's understand each important part.

### A. `IORING_SETUP_SQPOLL`

```c
params.flags |= IORING_SETUP_SQPOLL;
```

This requests SQPOLL mode during ring initialization.

The kernel creates the SQ polling thread.

Without this flag, you get ordinary io\_uring submission behavior.

### B. `sq_thread_idle`

```c
params.sq_thread_idle = SQPOLL_IDLE_MS;
```

You configured\:

```text
SQPOLL_IDLE_MS = 100
```

The polling thread is allowed to go idle after approximately 100 ms without work.

Why choose this?

Because continuously spinning when there is no traffic is wasteful.

The idle period provides a compromise\:

- Keep the thread active during bursts.
- Allow it to sleep when the server becomes idle.

### C. `IORING_SETUP_SQ_AFF`

```c
params.flags |= IORING_SETUP_SQ_AFF;
```

This is a CPU affinity option.

It allows the SQPOLL thread to be pinned to a particular CPU.

Your code then specifies\:

```c
params.sq_thread_cpu =
    (unsigned)s->config->sqpoll_cpu + (unsigned)s->index;
```

For example\:

```text
--sqpoll-cpu 4
```

With one server thread\:

```text
Server thread → CPU 2 (as configured separately)
SQPOLL thread → CPU 4
```

With multiple engine threads, the code offsets the specified SQPOLL CPU by the server thread's index.

Note that SQPOLL CPU affinity is only enabled when `sqpoll_cpu >= 0`\.

This helps control CPU placement and reduce interference between the application thread and its SQPOLL thread.

---

# 5\. What happens when the SQPOLL thread sleeps?

This is a particularly important detail.

After being idle for the configured period, the kernel sets\:

```c
IORING_SQ_NEED_WAKEUP
```

This is a flag in the shared SQ flags.

It indicates that userspace must wake the polling thread before relying on it to consume new entries.

Suppose\:

```text
SQPOLL thread → Sleeping
```

Your application prepares new SQEs.

The application must now enter the kernel to wake the SQPOLL thread.

The sequence becomes\:

```text
Application                  Kernel

Prepare SQEs
    |
    v
Publish SQ tail
    |
    v
Check SQ flags
    |
    v
NEED_WAKEUP is set
    |
    v
io_uring_submit()
    |
    |------ io_uring_enter() --> Wake SQPOLL thread
                               |
                               v
                          Consume SQEs
```

Notice that we have returned to using a syscall\!

But this happens when the polling thread needs waking, rather than on every submission.

This is why SQPOLL reduces syscalls instead of eliminating them completely.

fileciteturn0file1L570-L585

## 5.1 How does your code handle this?

Your `counted_submit()` contains\:

```c
unsigned ready = io_uring_sq_ready(ring);

if (s->config->sqpoll) {
    if (IO_URING_READ_ONCE(*ring->sq.kflags) &
        IORING_SQ_NEED_WAKEUP) {
        s->sc_enter++;
    }
} else if (ready) {
    s->sc_enter++;
}

return io_uring_submit(ring);
```

Let's look at the logic\:

| Condition | What your code expects |
|--------|--------|
| SQPOLL disabled, SQ has entries | Submission kernel entry |
| SQPOLL enabled, thread awake | No submission kernel entry |
| SQPOLL enabled, NEED\_WAKEUP set | Kernel entry to wake the thread |

You increment `sc_enter` only when you expect liburing to actually enter the kernel.

This is important because your project is measuring actual syscall costs, not simply counting function calls.

Your README states that these syscall counts were checked against `strace` for multiple configurations. fileciteturn0file1L545-L559

---

# 6\. Important: SQPOLL does not mean your application never sleeps

This is probably the most common misconception.

We have optimized the submission side.

But what about the completion side?

Remember\:

```text
SQ → Kernel
CQ ← Kernel
```

The SQPOLL thread is responsible for polling the SQ.

It does not mean that your application automatically knows when a CQE is available.

Your application still needs to collect completions.

Let's see what happens in plain SQPOLL mode.

```text
Application thread            SQPOLL thread

Prepare SQEs
    |
    |--------------------------|
                               |
                         Poll SQ
                         Consume SQEs
                         Execute operations
                         Publish CQEs
                               |
    |<-------------------------|
    |
Wait for completion
    |
Sleep
    |
Kernel wakes application thread
    |
Collect CQE
```

So we have two threads with different responsibilities\:

| Thread | Responsibility |
|--------|--------|
| Application thread | Prepare operations and process completions |
| SQPOLL kernel thread | Monitor and consume submissions |

The application may still sleep while waiting for completions.

That introduces another optimization opportunity.

---

# 7\. Busy polling: Optimizing the completion side

Now let's understand your second configuration.

Your project provides\:

```text
--sqpoll-busy 1
```

Remember, this option is different from SQPOLL itself.

SQPOLL optimizes submission.

Your busy-polling option optimizes waiting for completions.

## 7.1 Normal waiting for CQEs

Suppose our application has submitted some requests.

It now wants to wait for a completion.

Normally it does\:

```c
io_uring_wait_cqe_timeout(&ring, &cqe, &timeout);
```

If there is no completion, the application enters the kernel and sleeps.

When a completion becomes available\:

1. The kernel makes the CQE available.
2. The waiting application thread is woken.
3. It becomes runnable.
4. The scheduler eventually runs it.
5. The application collects the CQE.

There is wake-up and scheduling overhead.

## 7.2 What if we never sleep?

Instead of waiting for a completion using a system call, what if the application repeatedly checks the CQ?

```c
while (1) {
    if (io_uring_peek_cqe(&ring, &cqe) == 0) {
        break;
    }
}
```

Now the application continuously checks\:

```text
Is CQE available?
    No

Is CQE available?
    No

Is CQE available?
    No

Is CQE available?
    Yes!
```

This is busy polling on the completion side.

The application doesn't need to sleep and be woken by the kernel.

It can observe a completion directly from shared memory.

[![现代 io api 的使用和性能对比 | Yibo Zhang's site](https://images.openai.com/static-rsc-4/xpFbtk7V9MZZj2_L01Tgy6d9aVHd6aHhMoT3SoKGbKZprgFkH39daNNll47v0kg9BZr4XrRSzpdk4NMUmfH6MVgrOOc_Sq41ip1STMhUg2wrwQ8bbPxhkJBe_Akg24UrLjLXSQVHM8FOxa_JB-HEkx9VRgH5-NFb8nSsfRJ7fZ6SKgOj5Jyq57jL16xTwKB7?purpose=fullsize)](https://zybzzz.github.io/ybsite/notes/os-dev-use/kernel/io/ioapi.html)

## 7.3 Your actual `counted_wait()` implementation

This is the relevant part\:

```c
if (io_uring_peek_cqe(ring, cqe) == 0) {
    return 0;
}

if (s->config->sqpoll_busy) {
    int64_t deadline = monotonic_ns() +
                       WAIT_MS * 1000000LL;

    while (!stop_requested) {
        if (io_uring_peek_cqe(ring, cqe) == 0) {
            return 0;
        }

        if (monotonic_ns() >= deadline) {
            return -ETIME;
        }
    }

    return -EINTR;
}
```

fileciteturn0file0L60-L78

Let's understand the exact behavior.

**First\:**

```c
io_uring_peek_cqe(ring, cqe)
```

The function checks whether a completion already exists.

If yes, return immediately.

No syscall is needed.

**Second\:**

```c
if (s->config->sqpoll_busy)
```

If busy polling is enabled, we enter the loop.

**Third\:**

```c
monotonic_ns() + WAIT_MS * 1000000LL
```

You calculate a deadline 100 ms into the future.

Why?

Because your event loop needs to periodically check `stop_requested`\.

You don't want it to spin forever without checking whether the server is shutting down.

**Fourth\:**

```c
io_uring_peek_cqe(ring, cqe)
```

Repeatedly checks the CQ until\:

- A completion appears.
- The deadline expires.
- A shutdown signal is observed.

When the deadline expires\:

```c
return -ETIME;
```

The caller recognizes this and continues its outer loop.

Notice that this path does not invoke\:

```c
io_uring_wait_cqe_timeout()
```

And it does not increment `sc_enter` or `sc_wait`\.

Therefore, the busy-poll waiting loop itself does not make a waiting syscall.

---

# 8\. Combining SQPOLL and busy polling

Now we can understand your third configuration.

You combine both optimizations.

## SQPOLL + busy polling

**Application thread**

(Busy-polls CQ)

Continuously checks for completed operations

Shared SQ and CQ memory

**SQPOLL kernel thread**

(Busy-polls SQ)

Continuously checks for new submissions

---

### Result

No ordinary submission syscall while SQPOLL is awake

No completion-wait syscall in the busy-poll loop

Two CPU cores can be continuously occupied

You have effectively created a setup where\:

- One thread watches the submission side.
- Another thread watches the completion side.

Neither needs to sleep while doing its respective polling.

This can reduce latency because we avoid waiting for thread wake-ups and scheduling.

But it consumes considerably more CPU.

---

# 9\. Why did your TA observe approximately 90% CPU usage?

This is the most important project-specific discussion.

Your README mentions the TA's question\:

> Why is io\_uring at about 90% CPU at every load?

Your implementation's SQPOLL mode explains this.

Let's compare your three configurations.

## A. Plain io\_uring

```text
Application thread:

Prepare SQEs
    |
Submit through syscall
    |
Wait for CQE
    |
Sleep
    |
Wake up
    |
Process completion
```

The thread sleeps when it has nothing to process.

CPU usage depends more directly on the actual workload.

## B. SQPOLL

```text
Application thread:

Prepare SQEs
    |
Publish SQ tail
    |
Wait for CQE
    |
Sleep
    |
Wake up
    |
Process completion

SQPOLL thread:

Poll SQ
Poll SQ
Poll SQ
Poll SQ
...
```

Now the SQPOLL thread continuously consumes CPU while active.

Your README reports approximately 100% CPU consumption for this thread even at a relatively low workload such as 1,000 requests per second.

The application thread can still sleep, but the SQPOLL thread keeps consuming CPU.

This explains why the total CPU consumption remains high even when the workload is relatively small.

The 100 ms idle timeout can allow the SQPOLL thread to sleep during sufficiently long idle periods. But during sustained traffic, it remains active.

## C. SQPOLL + busy polling

```text
Application thread:

Peek CQ
Peek CQ
Peek CQ
Peek CQ
...

SQPOLL thread:

Poll SQ
Poll SQ
Poll SQ
Poll SQ
...
```

Now both sides continuously poll.

Your README reports approximately 200% CPU usage because two cores are occupied.

The 90% and 200% figures are approximate observations from your project's experiments. The README specifically attributes this behavior to the SQPOLL and userspace busy-polling threads. fileciteturn0file1L570-L622

**A strong viva answer\:**

"SQPOLL reduces submission syscall overhead by using a dedicated kernel thread to poll the submission queue. However, that thread continuously consumes CPU while awake. In our implementation, the SQPOLL thread is active during traffic, which explains why CPU usage remains around 90% even at relatively low loads. With our additional busy-polling mode, the application thread also continuously polls the completion queue, so both cores can be occupied, resulting in approximately 200% CPU utilization."

---

# 10\. Another interesting result: Why did SQPOLL have more context switches?

Your README contains another observation\:

```text
Plain io_uring: Approximately 1 voluntary context switch/request

SQPOLL:         Approximately 3 voluntary context switches/request
```

Why would SQPOLL have more context switches if it was designed to reduce overhead?

Remember that there are now two threads.

The SQPOLL thread runs separately from the application thread.

In ordinary SQPOLL mode\:

1. The application publishes SQEs.
2. The SQPOLL thread consumes them and starts the operations.
3. The application thread waits for completions.
4. The kernel needs to wake the application thread when completions become available.
5. The scheduler must run that thread again.

This creates additional wake-up and scheduling activity.

There may also be cross-CPU wake-ups, particularly when the threads are pinned to different CPUs.

So reducing submission syscalls doesn't automatically mean reducing context switches.

**Syscall count, context-switch count and CPU utilization are three different metrics.**

This is an excellent distinction to remember for your project's performance analysis.

fileciteturn0file1L585-L599

---

# 11\. One subtle distinction: Your busy polling is userspace CQ polling

I want to highlight one detail so that you don't accidentally explain your implementation incorrectly in a viva.

Your code enables\:

```c
IORING_SETUP_SQPOLL
```

But it does not set\:

```c
IORING_SETUP_IOPOLL
```

Your `--sqpoll-busy` feature is implemented manually using\:

```c
io_uring_peek_cqe()
```

inside a userspace loop.

So there are two separate things\:

| Mechanism | What it polls |
|--------|--------|
| SQPOLL | Submission Queue, using a kernel thread |
| Your busy-poll mode | Completion Queue, using the application thread |

You should not describe your `--sqpoll-busy` option as enabling `IORING_SETUP_IOPOLL`\.

That would be incorrect.

The code confirms that your busy-poll mode is implemented in `counted_wait()`, not through an additional ring setup flag. fileciteturn0file0L60-L84 fileciteturn0file0L111-L125

---

# 12\. Quick comparison of all three modes

| Feature | Plain | SQPOLL | SQPOLL + busy |
|--------|--------|--------|--------|
| Kernel SQ polling thread | No | Yes | Yes |
| Userspace CQ busy loop | No | No | Yes |
| Normal submission syscall | Yes, when submitting | Avoided while SQPOLL awake | Avoided while SQPOLL awake |
| Waiting syscall | When no CQE is available | When no CQE is available | Avoided during busy-poll loop |
| Application sleeps | Yes | Yes | No during busy wait |
| CPU overhead | Work-dependent | High | Very high |

---

# 13\. Check your understanding - viva questions

These questions are deliberately more focused on your actual implementation.

**Lesson 6 viva quiz** (/6 answered)

## What comes next: Starting your actual implementation

We've now covered the major io\_uring fundamentals\:

1. Architecture and shared memory.
2. SQ and CQ.
3. SQEs, CQEs and `user_data`\.
4. Submission, batching and `io_uring_enter()`\.
5. Kernel execution and fast poll.
6. SQPOLL and busy polling.

Now I recommend moving into your actual `io_uring_server.c`\.

**Lesson 7: Ring initialization - `engine_run()`, `ring_setup()`, `completion_entries()` and the configuration flags.**

We'll understand why your SQ depth is 256, why you deliberately configure a larger CQ, how `IORING_SETUP_CQSIZE` works, and how the kernel and liburing establish the rings.

We'll take one function at a time, without rewriting your implementation.

## Prompt:
08/11/2026, 2:32:34 am

ok, proceed and explain my code now

## Response:
08/11/2026, 2:32:46 am · gpt-6-t-mini

> **Planning the code lesson**
>
> I’m continuing with ring initialization, starting at the engine entry point and then following setup into completion handling. I’ll verify the relevant source before explaining each function clearly.
>
> **Planned the code lesson**
>
> 
>
> **Explained io_uring engine and ring initialization**
>
> 
>
> **Starting ring setup**
>
> I’m confirming how the code creates the submission and completion queues, then I’ll connect those choices to `engine_run`. I’m also checking queue-size rules carefully so I don’t oversimplify the kernel behavior.
>
> **Explaining ring ownership**
>
> I’m tracing `engine_run` first: each engine thread creates its own ring, passes it to the selected transport, and destroys it afterward. The queue’s struct is local, while its backing memory is managed separately.
>
> **Explained ring setup and ownership**
>
> 
>
> **Assessing queue sizing**
>
> The completion queue starts at twice the submission depth, then grows to cover clients, UDP receives, and extra control operations. I’m noting the 65,536-entry ceiling because larger configurations may no longer satisfy that estimate.
>
> **Assessed queue sizing**
>
> 
>
> **Searched 18 websites**
>
> 
>
> **Mapping initialization**
>
> I’m connecting the initialization path now: `engine_run` creates one ring per worker, `ring_setup` configures it, and the transport loop later shares that ring until cleanup.
>
> **Connecting ring initialization**
>
> I’m following one concept at a time: `engine_run` owns the lifecycle, `completion_entries` sizes the completion queue, and `ring_setup` translates those choices into kernel configuration.
> 
> `engine_run` creates a local `struct io_uring ring`. Each worker thread executes this function, so each worker receives an independent ring rather than sharing one across threads. The actual queue memory is managed by liburing after initialization.
> 
> Its control flow is:
> 
> ```c
> struct io_uring ring;
> int rc = ring_setup(s, &ring);
> 
> if (rc < 0)
>     return rc;
> 
> if (s->config->udp)
>     rc = run_udp(s, &ring);
> else
>     rc = run_tcp(s, &ring);
> 
> io_uring_queue_exit(&ring);
> return rc;
> ```
> 
> I’m reading this as a lifecycle boundary:
> 
> 1. Create the ring.
> 2. Select TCP or UDP.
> 3. Run that transport’s event loop.
> 4. Release ring resources.
> 5. Return the transport result.
> 
> That final cleanup matters because the transport loop may return due to shutdown or an error. The ring still needs to be released before the worker exits.
> 
> `RING_DEPTH` is `256`. In `ring_setup`, it requests 256 submission-queue entries. Elsewhere, the same constant also limits how many completion entries a loop processes in one batch. It doesn’t define the completion-queue capacity.
> 
> ---
> 
> **Sizing completion capacity**
> 
> `completion_entries` begins with:
> 
> ```c
> unsigned entries = 2 * RING_DEPTH;
> ```
> 
> That produces 512 initially. It then calculates:
> 
> ```c
> needed = max_clients + udp_depth + 64;
> ```
> 
> The reasoning is that the completion queue may need to hold completions for many operations at once:
> 
> - one operation per connected TCP client,
> - several outstanding UDP receives,
> - extra accept, cancellation, or control operations,
> - a 64-entry margin.
> 
> The submission queue and completion queue serve different purposes. A submission queue with 256 entries doesn’t mean only 256 operations can exist overall; submissions are consumed and their slots become reusable while operations remain active. The completion queue therefore needs to tolerate a larger burst.
> 
> The loop repeatedly doubles the proposed completion size:
> 
> ```c
> while (entries < needed && entries < 65536)
>     entries *= 2;
> ```
> 
> For the default `max_clients = 10000` and `udp_depth = 32`, the calculation needs 10,096 entries and rounds up to 16,384.
> 
> One limitation is visible here: configurations above roughly 65,000 clients can request more than 65,536 completion entries, but this helper stops at 65,536. That may be adequate in practice if completions are reaped promptly, but it no longer guarantees the stated sizing target.
> 
> ---
> 
> **Configuring the ring**
> 
> `ring_setup` begins with:
> 
> ```c
> struct io_uring_params params = {0};
> ```
> 
> Zero-initializing the structure is important because it gives unspecified fields safe defaults and avoids passing accidental flag bits.
> 
> The function always enables custom completion sizing:
> 
> ```c
> params.flags = IORING_SETUP_CQSIZE;
> params.cq_entries = completion_entries(s);
> ```
> 
> Without this flag, the kernel normally derives the completion queue from the requested submission depth. Here, the project deliberately asks for a larger queue based on workload instead.
> 
> The requested completion size is also at least the submission size and is produced by power-of-two doubling, matching the usual queue-size expectations.
> 
> When submission polling is enabled, the function adds:
> 
> ```c
> params.flags |= IORING_SETUP_SQPOLL;
> params.sq_thread_idle = SQPOLL_IDLE_MS;
> ```
> 
> `SQPOLL` asks the kernel to maintain a polling thread that watches for submissions, reducing the need for the application thread to explicitly enter the kernel for every submission. `SQPOLL_IDLE_MS` sets how long that polling thread remains active while idle.
> 
> If a polling CPU was configured, the code additionally sets:
> 
> ```c
> params.flags |= IORING_SETUP_SQ_AFF;
> params.sq_thread_cpu = s->index + configured_cpu;
> ```
> 
> This assigns each worker’s polling thread to a different CPU offset from the configured base. The affinity flag is only meaningful alongside submission polling.
> 
> Finally:
> 
> ```c
> return io_uring_queue_init_params(
>     RING_DEPTH,
>     ring,
>     &params
> );
> ```
> 
> The first argument requests the submission-queue depth. The library and kernel create the ring, apply the parameters, and prepare the mapped queue memory. A negative return indicates initialization failure, so `engine_run` exits before starting TCP or UDP processing.
> 
> The project’s design is therefore:
> 
> ```text
> worker thread
>     -> engine_run
>         -> calculate CQ size
>         -> configure optional SQPOLL
>         -> initialize one ring
>         -> run TCP or UDP loop
>         -> destroy ring
> ```
> 
> That gives us a clean base for the next step: examining how the event loop acquires SQEs, submits them, and reaps CQEs.
> 
> A few quick checks for this lesson:
> 
> 1. Why does the completion queue need to be larger than the submission queue?
> 2. What does `IORING_SETUP_SQPOLL` change?
> 3. Why is `io_uring_queue_exit` called after the transport loop?
> 4. What happens when the computed completion size exceeds the helper’s 65,536-entry cap?
>
> **Mapped initialization and connected ring initialization**
>
> 
>
> **Evaluated io_uring CQ capacity limits and traced entry setup sizing**
>
> 
>
> Worked for 3m 35s

# Lesson 7: Understanding your ring initialization code

Now we start examining your actual `io_uring_server.c`\.

We'll follow the execution order of the program rather than simply reading the file from top to bottom.

Today's focus\:

1. How execution enters your engine (`engine_run()`\).
2. How you calculate the CQ size (`completion_entries()`\).
3. How you initialize the ring (`ring_setup()`\).
4. Why you chose these particular queue sizes and flags.

We won't get into TCP or UDP event-loop logic yet.

---

# 1\. Understanding the structure of your engine

Your engine implements one function\:

```c
int engine_run(server_t *s);
```

This is the interface defined in your `framework.h`\.

Remember the project's architecture\:

```text
Framework
    |
    | calls engine_run(server_t *s)
    v
io_uring_server.c
    |
    |-- Initialize io_uring
    |
    |-- TCP → run_tcp()
    |
    |-- UDP → run_udp()
    |
    |-- Destroy io_uring
```

The framework handles common functionality, such as socket creation, client state management, application plugins and statistics.

Your engine decides how I/O operations are submitted and completed.

This separation means all four engines use the same application logic and TCP request state machine. fileciteturn0file1L240-L303

## 1.1 Important: One ring per engine thread

Your `framework.c` supports multiple server threads.

Look at its `thread_main()`\:

```c
static void *thread_main(void *argument)
{
    thread_slot_t *slot = argument;
    server_t *s = &slot->server;

    // CPU affinity setup ...

    int rc = engine_run(s);

    // ...
}
```

Each worker calls `engine_run()` with its own `server_t`\.

Your `framework.h` explicitly says\:

```c
/* One server_t per engine thread; nothing below is shared between threads. */
struct server_t {
    // ...
};
```

fileciteturn0file2L450-L475 fileciteturn0file3L96-L108

This is important because your implementation creates a separate io\_uring instance for each engine thread.

For example, with `--threads 2`\:

```text
Worker Thread 0
    |
    |-- server_t 0
    |
    |-- io_uring instance 0
    |
    |-- run_tcp() / run_udp()

Worker Thread 1
    |
    |-- server_t 1
    |
    |-- io_uring instance 1
    |
    |-- run_tcp() / run_udp()
```

Each ring has its own submission and completion queues.

This avoids sharing one ring between multiple application threads and the additional synchronization that would require.

---

# 2\. Understanding the constants

Let's look at the beginning of your implementation.

```c
const char *const ENGINE_NAME = "io_uring";

#define RING_DEPTH 256
#define SQPOLL_IDLE_MS 100
```

There are three things to understand.

### A. ENGINE\_NAME

```c
const char *const ENGINE_NAME = "io_uring";
```

This identifies which engine is being built.

Your common framework uses this value to determine whether io\_uring-specific command-line options should be accepted.

For example\:

```text
--sqpoll 1
--sqpoll-busy 1
--udp-depth 32
```

These options are specific to your io\_uring engine.

### B. RING\_DEPTH

```c
#define RING_DEPTH 256
```

This is the requested Submission Queue depth.

In other words\:

```text
SQ capacity = 256 entries
```

Your application can prepare up to 256 SQEs in the queue before it needs to submit some of them to make space for more.

An important distinction\:

**RING\_DEPTH is not the maximum number of connected clients.**

For example, you can have 1,000 clients even though your SQ contains only 256 entries.

How?

The kernel consumes SQEs, freeing submission slots. Your application can then reuse those slots to submit additional operations.

Some previously submitted operations may still be pending while new operations are being submitted.

Therefore, the number of operations in flight can exceed the SQ depth.

We'll see why this is also important for CQ sizing.

One additional detail: your code also uses `RING_DEPTH` as the maximum number of CQEs to process in one completion-reaping batch.

We'll revisit that when studying `run_tcp()`\.

### C. SQPOLL\_IDLE\_MS

```c
#define SQPOLL_IDLE_MS 100
```

This is the idle timeout configured for the SQPOLL thread.

When SQPOLL is enabled, the polling thread can sleep after approximately 100 ms without new submission work.

This value is used later in `ring_setup()`\.

---

# 3\. Understanding completion\_entries()

(Important implementation detail)

This function is particularly important because your team encountered an actual CQ overflow problem during testing and fixed it through this function.

Here is your code\:

```c
static unsigned completion_entries(const server_t *s)
{
    size_t needed =
        s->config->max_clients +
        (size_t)s->config->udp_depth + 64;

    unsigned entries = 2 * RING_DEPTH;

    while (entries < needed && entries < 65536) {
        entries *= 2;
    }

    return entries;
}
```

fileciteturn0file0L97-L109

Let's understand why this function exists.

## 3.1 First, why can't we simply use 256 for both SQ and CQ?

Remember the distinction\:

- SQ holds requests waiting to be consumed by the kernel.
- CQ holds results waiting to be collected by the application.

These are not the same thing.

Imagine 1,000 clients.

Each client has one receive operation submitted.

```text
SQ:
256 slots

In-flight receive operations:
1,000

CQ:
Needs to accommodate potentially 1,000 completions
```

How can 1,000 operations be in flight if the SQ contains only 256 entries?

Because the kernel consumes SQEs as it takes ownership of the corresponding operations.

The submission slots become reusable.

But the operations themselves may remain pending.

This means\:

```text
SQE consumed ≠ I/O operation completed
```

And\:

```text
SQ slot reusable ≠ Corresponding CQE already collected
```

Consequently, the CQ may need to be much larger than the SQ.

## 3.2 Why did your original CQ size cause a problem?

Without custom CQ sizing, io\_uring normally creates a CQ with twice as many entries as the SQ.

For your configuration\:

```text
SQ = 256

Default CQ = 2 × 256 = 512
```

But consider 1,000 clients, each with a receive operation in flight.

Suppose many clients send data around the same time.

The kernel completes those operations and needs to publish their CQEs.

If the application hasn't collected the existing completions quickly enough, the CQ can become full.

Your README describes what happened during the first experiment\:

- 1,000 clients each had a receive operation in flight.
- The default 512-entry CQ overflowed.
- The kernel had to use a slower overflow-list path.
- Approximately 14,000 overflowed loop iterations were observed during a three-second run.

fileciteturn0file1L565-L581

This is why you introduced custom CQ sizing.

The goal is to provide enough completion capacity to avoid this expensive situation under the intended workload.

## 3.3 Line-by-line explanation

**First: Calculate the required number of entries.**

```c
size_t needed =
    s->config->max_clients +
    (size_t)s->config->udp_depth + 64;
```

Let's understand each component.

| Component | Why it is included |
|--------|--------|
| `max_clients` | One outstanding TCP operation per client |
| `udp_depth` | Number of outstanding UDP receives |
| `64` | Additional headroom for other operations and overhead |

Why are we considering only one operation per TCP client?

Because your TCP implementation submits at most one receive or send operation per client at a time.

Look at the framework's connection structure\:

```c
int pending;
```

Your engine sets this when it submits an operation and doesn't schedule another operation for that client until the completion is processed.

We will examine this in detail during `run_tcp()`\.

The UDP implementation is slightly different: it maintains a configurable number of receive slots.

So your formula accounts for both transports.

The additional 64 entries provide some margin for other operations, including accept and cancellation work.

**Second: Choose an initial CQ size.**

```c
unsigned entries = 2 * RING_DEPTH;
```

Since\:

```text
RING_DEPTH = 256
```

We start with\:

```text
entries = 512
```

This is the normal default CQ-to-SQ ratio.

**Third: Keep doubling until we have sufficient capacity.**

```c
while (entries < needed && entries < 65536) {
    entries *= 2;
}
```

Why multiply by two?

Because io\_uring uses power-of-two ring sizes.

Starting from 512, we get\:

```text
512
1024
2048
4096
8192
16384
32768
65536
```

We stop when the requested number of entries is sufficient or when we reach the configured upper limit.

## 3.4 Calculate the default CQ size

Your framework's defaults are\:

```c
.max_clients = DEFAULT_MAX_CLIENTS
```

where\:

```c
#define DEFAULT_MAX_CLIENTS 10000
```

And\:

```c
.udp_depth = 32
```

fileciteturn0file2L479-L483

Therefore\:

$$
needed = 10000 + 32 + 64
$$

$$
needed = 10096
$$

Now calculate the CQ size\:

| Candidate | Sufficient? |
|--------|--------|
| 512 | No |
| 1024 | No |
| 2048 | No |
| 4096 | No |
| 8192 | No |
| 16384 | Yes |

**Default ring configuration**

SQ capacity

# 256

Submission entries

CQ capacity

# 16,384

Completion entries

The CQ is deliberately 64 times larger than the SQ for this default configuration.

Why is this allowed?

Because `IORING_SETUP_CQSIZE` lets us explicitly configure a CQ size independent of the default two-times-SQ setting. The requested CQ size must satisfy the kernel's ring-size constraints. \[[io\_uring\_setup\_flags(7)](https://www.man7.org/linux/man-pages/man7/io_uring_setup_flags.7.html?utm_source=chatgpt.com); [io\_uring/io\_uring.c - linux/kernel/git/torvalds/linux](https://linux.googlesource.com/linux/kernel/git/torvalds/linux/%2B/cdb3033e191fd03da2d7da23b9cd448dfa180a8e/io_uring/io_uring.c?utm_source=chatgpt.com)\]

### An implementation detail worth knowing

Your loop stops at 65,536.

The framework accepts `--max-clients` values up to 100,000, so there are configurations where\:

```text
needed > 65536
```

In those cases, this helper returns 65,536 even though the calculated requirement is higher.

The current kernel's maximum CQ entry count is 65,536. \[[linux/io\_uring/io\_uring.h at master · torvalds/linux](https://github.com/torvalds/linux/blob/master/io_uring/io_uring.h?utm_source=chatgpt.com); [linux/io\_uring/io\_uring.c at master · torvalds/linux](https://github.com/torvalds/linux/blob/master/io_uring/io_uring.c?utm_source=chatgpt.com)\]

So the function cannot guarantee its one-slot-per-potential-operation sizing for every theoretically allowed client configuration. That doesn't mean those configurations necessarily fail, but CQ overflow becomes possible if the actual number of uncollected completions exceeds the available capacity.

For your default 10,000-client configuration, the calculation is comfortably within the limit.

This is a useful limitation to be aware of rather than blindly claiming that the function always guarantees sufficient capacity.

---

# 4\. Understanding ring\_setup()

Now we reach the function that actually initializes the ring.

Your code\:

```c
static int ring_setup(server_t *s, struct io_uring *ring)
{
    struct io_uring_params params = {0};

    params.flags = IORING_SETUP_CQSIZE;
    params.cq_entries = completion_entries(s);

    if (s->config->sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = SQPOLL_IDLE_MS;

        if (s->config->sqpoll_cpu >= 0) {
            params.flags |= IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu =
                (unsigned)s->config->sqpoll_cpu + (unsigned)s->index;
        }
    }

    return io_uring_queue_init_params(RING_DEPTH, ring, &params);
}
```

fileciteturn0file0L111-L125

This function configures how your io\_uring instance should be created.

Let's go line by line.

## 4.1 Create the parameters structure

```c
struct io_uring_params params = {0};
```

What is `io_uring_params`?

It is a structure provided by the io\_uring interface.

It contains configuration fields such as\:

- Setup flags.
- Desired CQ size.
- SQPOLL idle timeout.
- SQPOLL CPU affinity.
- Other optional setup parameters.

Why `{0}`?

It initializes every field to zero.

This is important because we only want to specify the options we actually need. Other fields retain their default/zero values rather than containing garbage data.

For example\:

```text
params.flags          = 0
params.cq_entries     = 0
params.sq_thread_idle = 0
params.sq_thread_cpu  = 0
```

We subsequently configure the relevant fields.

## 4.2 Enable custom CQ sizing

```c
params.flags = IORING_SETUP_CQSIZE;
```

This tells the kernel\:

> I want to configure the Completion Queue size myself rather than use the default.

Then\:

```c
params.cq_entries = completion_entries(s);
```

This supplies the CQ size calculated by our earlier function.

For default settings\:

```text
params.cq_entries = 16384
```

Notice that this does not change the SQ size.

The SQ size is specified separately through the first argument of `io_uring_queue_init_params()`\.

The official liburing documentation describes `IORING_SETUP_CQSIZE` as the flag for overriding the default CQ size. \[[io\_uring\_setup\_flags(7)](https://www.man7.org/linux/man-pages/man7/io_uring_setup_flags.7.html?utm_source=chatgpt.com); [io\_uring\_setup\_flags(7)](https://www.man7.org/linux/man-pages/man7/io_uring_setup_flags.7.html?utm_source=chatgpt.com)\]

## 4.3 Conditionally enable SQPOLL

```c
if (s->config->sqpoll) {
    params.flags |= IORING_SETUP_SQPOLL;
```

Remember\:

```c
params.flags = IORING_SETUP_CQSIZE;
```

We already have the CQSIZE flag.

The `|=` operator adds another flag without removing the previous one.

So now\:

```text
params.flags =
    IORING_SETUP_CQSIZE |
    IORING_SETUP_SQPOLL
```

Why is this conditional?

Because your project supports both plain io\_uring and SQPOLL.

When `--sqpoll 0`\:

```text
SQPOLL is disabled
```

When `--sqpoll 1`\:

```text
SQPOLL is enabled
```

The kernel creates a submission-polling thread when this flag is present. \[[io\_uring\_setup\_flags(7)](https://www.man7.org/linux/man-pages/man7/io_uring_setup_flags.7.html?utm_source=chatgpt.com)\]

## 4.4 Configure the SQPOLL idle timeout

```c
params.sq_thread_idle = SQPOLL_IDLE_MS;
```

We defined\:

```c
#define SQPOLL_IDLE_MS 100
```

So\:

```text
sq_thread_idle = 100 ms
```

This tells the kernel how long the SQPOLL thread should remain idle before it can go to sleep.

Why is this useful?

Without an idle timeout, continuously polling an inactive submission queue could waste a lot of CPU.

This gives your implementation a compromise between latency and CPU consumption.

## 4.5 Configure CPU affinity

Now comes the next condition\:

```c
if (s->config->sqpoll_cpu >= 0) {
```

Remember the configuration field from `framework.h`\:

```c
int sqpoll_cpu;
```

It specifies the CPU intended for the SQPOLL thread, or `-1` when no specific CPU is requested.

fileciteturn0file3L78-L94

If a CPU was specified, your code executes\:

```c
params.flags |= IORING_SETUP_SQ_AFF;
```

This requests CPU affinity for the SQPOLL thread.

Then\:

```c
params.sq_thread_cpu =
    (unsigned)s->config->sqpoll_cpu + (unsigned)s->index;
```

Let's understand why `s->index` is added.

Suppose we have\:

```text
--threads 2
--sqpoll 1
--sqpoll-cpu 4
```

The framework creates two server threads\:

```text
Server thread 0 → s->index = 0
Server thread 1 → s->index = 1
```

Your calculation gives\:

| Server thread | Configured base CPU | Actual SQPOLL CPU |
|--------|--------|--------|
| Thread 0 | 4 | 4 |
| Thread 1 | 4 | 5 |

This allows different worker threads to use different SQPOLL CPUs.

Why is that useful?

If both polling threads run on the same CPU, they compete for CPU time.

Separating them can reduce interference.

One requirement: `IORING_SETUP_SQ_AFF` is meaningful only with `IORING_SETUP_SQPOLL`\. Your code correctly enables the affinity flag only inside the SQPOLL condition. \[[io\_uring\_setup\_flags(7)](https://www.man7.org/linux/man-pages/man7/io_uring_setup_flags.7.html?utm_source=chatgpt.com)\]

Also, the chosen CPUs must be valid and permitted by the system's CPU affinity restrictions.

---

# 5\. The most important line: io\_uring\_queue\_init\_params()

Finally\:

```c
return io_uring_queue_init_params(
    RING_DEPTH,
    ring,
    &params
);
```

This is the actual ring initialization call.

Let's understand the arguments.

| Argument | Value | Meaning |
|--------|--------|--------|
| First | `RING_DEPTH` | Requested SQ capacity: 256 |
| Second | `ring` | The structure liburing will initialize |
| Third | `&params` | Configuration options |

What happens internally?

Conceptually\:

1. Liburing invokes the kernel's ring setup mechanism.
2. The kernel creates the io\_uring instance.
3. The kernel establishes the SQ and CQ structures with the requested sizes and flags.
4. Liburing maps the shared ring memory into the application.
5. Liburing initializes the pointers, indices and internal ring-management state in `struct io_uring`\.

After successful initialization, your application can start using\:

```c
io_uring_get_sqe()
io_uring_submit()
io_uring_peek_cqe()
io_uring_wait_cqe_timeout()
```

You don't manually allocate the shared rings or call `mmap()` in your engine.

Liburing handles that setup for you. \[[liburing/src/setup.c at master · axboe/liburing](https://github.com/axboe/liburing/blob/master/src/setup.c?utm_source=chatgpt.com); [io\_uring\_setup\_flags(7)](https://www.man7.org/linux/man-pages/man7/io_uring_setup_flags.7.html?utm_source=chatgpt.com)\]

### What does it return?

Your function directly returns the result of\:

```c
io_uring_queue_init_params()
```

Typically\:

```text
0          → Success
negative   → Error code
```

For example, setup can fail because of unsupported flags, invalid ring configuration, resource exhaustion or permission restrictions.

This is why we must check the result before using the ring.

---

# 6\. Now understand engine\_run()

We have understood the ring setup. Let's look at the function that connects everything.

Your actual code\:

```c
int engine_run(server_t *s)
{
    struct io_uring ring;

    int rc = ring_setup(s, &ring);

    if (rc < 0) {
        return rc;
    }

    rc = s->config->transport == TRANSPORT_UDP
         ? run_udp(s, &ring)
         : run_tcp(s, &ring);

    io_uring_queue_exit(&ring);

    return rc;
}
```

fileciteturn0file0L413-L423

Let's understand the execution sequence.

### Step 1: Declare the ring

```c
struct io_uring ring;
```

This declares the liburing structure representing the ring instance.

It stores information needed by liburing to access and manage the SQ and CQ.

The actual shared ring memory is established during initialization.

### Step 2: Initialize the ring

```c
int rc = ring_setup(s, &ring);
```

This creates the ring using the configuration we just studied.

### Step 3: Handle initialization failure

```c
if (rc < 0) {
    return rc;
}
```

Why is this necessary?

Because if ring creation fails, we cannot safely execute the event loop.

There would be no successfully initialized ring to submit requests to.

The error is propagated to the framework.

### Step 4: Select TCP or UDP

```c
rc = s->config->transport == TRANSPORT_UDP
     ? run_udp(s, &ring)
     : run_tcp(s, &ring);
```

This is the conditional (ternary) operator.

It means\:

```text
If transport is UDP:
    run_udp()

Otherwise:
    run_tcp()
```

Notice something important\:

**Both TCP and UDP use the same initialized io\_uring instance.**

Their event loops differ, but their ring infrastructure is common.

### Step 5: Destroy the ring

```c
io_uring_queue_exit(&ring);
```

This is extremely important.

When the event loop exits, we must release the resources associated with the ring.

The cleanup includes tearing down the shared memory mappings and releasing the ring's kernel resources.

Your TCP and UDP loops also drain their outstanding operations before returning, which is essential because the kernel must no longer own application buffers when they are freed.

We'll examine that shutdown process separately.

### Step 6: Return the result

```c
return rc;
```

The result from the TCP or UDP event loop is passed back to the common framework.

---

# 7\. Complete initialization flow

Let's put everything together.

\**Framework calls engine\_run(server\_t *s)**

**Declare struct io\_uring ring**

**ring\_setup()**

Configure SQ, CQ and optional SQPOLL

**completion\_entries()**

Calculate custom CQ size

**io\_uring\_queue\_init\_params()**

Kernel setup and shared-memory initialization

**run\_tcp()**

TCP event loop

**run\_udp()**

UDP event loop

**io\_uring\_queue\_exit()**

Release ring resources

**Return to framework**

---

# 8\. What should you be able to explain in your viva?

Here are some likely questions based on these exact functions.

| Viva question | Main point |
|--------|--------|
| Why is SQ depth 256? | Fixed submission capacity; entries are reusable after consumption |
| Why is CQ larger than SQ? | Many operations can be in flight and complete before their CQEs are collected |
| Why use `IORING_SETUP_CQSIZE`? | To request a custom CQ size |
| Why use `{0}` for `io_uring_params`? | Initialize unspecified fields safely |
| Why configure `SQPOLL` conditionally? | Support both plain and polling modes |
| What does `SQ_AFF` do? | Pins the SQPOLL thread to a CPU |
| Why add `s->index` to the CPU number? | Assign different polling CPUs to different server threads |
| What happens if ring setup fails? | Event loop is not started; error is returned |
| Why call `io_uring_queue_exit()`? | Release ring resources after the event loop finishes |

---

# 9\. Check your understanding

Let's test your understanding of your actual code. These are not generic io\_uring questions.

**Lesson 7 - Code understanding** (/5 answered)

## Next: Lesson 8 - Your TCP engine (`run_tcp()`\)

Now that the ring is initialized, we can finally understand how your server uses it to handle real TCP clients.

We'll start with these variables\:

```c
conn_t *head = NULL, *tail = NULL;
static __thread int accept_tag;
int accepting = 0;
unsigned outstanding = 0;
int result = 0;
```

We'll understand the application-side ready queue, why only one accept is kept in flight, how `accept_tag` works, and how `outstanding` tracks kernel-owned operations.

Then we'll follow one client from accepting its connection through receiving a request and sending the response.

**This is where we'll spend most of our implementation walkthrough, because `run_tcp()` is the heart of your assigned engine.**

## Prompt:
08/11/2026, 2:46:00 am

ok, explain this

## Response:
08/11/2026, 2:46:14 am · gpt-6-t-mini-mini

We'll start with just the state variables at the top of `run_tcp()`, because they define how the whole event loop is organised. The key distinction is that your `head`/`tail` queue is an application-side queue of clients waiting for their next operation—not the kernel's Submission Queue.

> **Explaining event-loop variables**
>
> I’m focusing on the variables first: the ready queue’s head and tail, the accept-in-flight flag, outstanding work count, results, and server readiness. I’ll connect them without dumping the entire loop.
>
> **Explained event-loop variables**
>
> 
>
> **Clarifying completion handling**
>
> I’m distinguishing accept completions from client I/O: accept results are new file descriptors, while client results are byte counts or errors. I’ll also explain `accepting`, outstanding operations, and the loop’s separate fatal-status variable.
>
> **Clarified completion handling**
>
> 
>
> **Explained run_tcp variables, flow, and queue helpers**
>
> 
>
> Worked for 1m 53s

# Lesson 8: Understanding the state variables in `run_tcp()`

Let's begin with the first part of your actual function.

```c
static int run_tcp(server_t *s, struct io_uring *ring)
{
    conn_t *head = NULL, *tail = NULL;
    static __thread int accept_tag;
    int accepting = 0;
    unsigned outstanding = 0;
    int result = 0;

    server_ready(s);

    while (!stop_requested) {
        // Event loop
    }
}
```

These variables control how your TCP event loop manages clients, submissions and completions.

Let's understand them individually.

## 1\. `head` and `tail`\: Your application-side ready queue

```c
conn_t *head = NULL, *tail = NULL;
```

This is a very important part of your implementation.

First, remember that your SQ has a capacity of 256.

But your server can have thousands of connected clients.

We therefore need some way to keep track of clients that need their next I/O operation submitted, especially when the SQ doesn't have enough free entries.

That's what this queue does.

**`head` and `tail` represent a FIFO queue of clients waiting for their next operation to be submitted.**

Notice that this is an application-side queue, not the kernel's Submission Queue.

### 1.1 What is actually stored in this queue?

Your `framework.h` defines\:

```c
struct conn_t {
    int fd;
    stage_t stage;
    size_t done, want;

    // Other connection state ...

    conn_t *ready_next;
    int pending;

    unsigned char buffer[];
};
```

The important field here is\:

```c
conn_t *ready_next;
```

It allows your connection structures to be linked together.

fileciteturn0file3L61-L75

For example, imagine three clients\:

```text
Client A → conn_t A
Client B → conn_t B
Client C → conn_t C
```

Your ready queue might look like\:

**Application ready queue**

(head)

**Client A**

**Client B**

**Client C**

(NULL)

(tail → Client C)

Each client is waiting for the engine to submit its next operation.

For example\:

| Client | Current stage | Next operation |
|--------|--------|--------|
| A | HEADER | RECV |
| B | SEND | SEND |
| C | RECEIVE | RECV |

The queue doesn't store actual SQEs. It stores pointers to connection structures.

### 1.2 How are clients added to this queue?

Your code has a separate helper function\:

```c
static void enqueue(conn_t **head, conn_t **tail, conn_t *c)
{
    c->ready_next = NULL;

    if (*tail) {
        (*tail)->ready_next = c;
    } else {
        *head = c;
    }

    *tail = c;
}
```

fileciteturn0file0L85-L95

Let's understand this with an example.

Initially\:

```text
head = NULL
tail = NULL
```

We enqueue Client A\:

```c
enqueue(&head, &tail, A);
```

Since the queue is empty\:

```text
head → A → NULL
tail → A
```

Now enqueue Client B\:

```c
enqueue(&head, &tail, B);
```

Since `tail` already points to A\:

```c
tail->ready_next = B;
```

Then\:

```c
tail = B;
```

The queue becomes\:

```text
head → A → B → NULL
             ↑
            tail
```

Enqueue Client C\:

```text
head → A → B → C → NULL
                 ↑
                tail
```

This is a FIFO queue: first client added, first client removed.

### 1.3 When are clients added to this queue?

There are two important situations in your implementation.

**Situation A: A new client is accepted.**

After successfully accepting a connection\:

```c
conn_t *c = conn_add(s, n);

if (c) {
    enqueue(&head, &tail, c);
}
```

The new connection is added to the ready queue.

Why?

Because it needs its first receive operation.

The framework initializes a newly created connection in the `HEADER` stage, expecting an 8-byte request header. fileciteturn0file2L42-L47 fileciteturn0file2L50-L84

**Situation B: An existing client's operation completes.**

Your completion handler eventually does\:

```c
if (status) {
    conn_close(c, status);
} else {
    enqueue(&head, &tail, c);
}
```

If the connection is still valid, it is added back to the ready queue.

For example\:

```text
Client A completes RECV
        |
        v
Framework updates its state
        |
        v
Stage becomes SEND
        |
        v
Client A enters ready queue
        |
        v
Next iteration prepares SEND SQE
```

### 1.4 Why not directly submit the next SQE inside the completion handler?

You could imagine submitting an operation immediately after handling a completion.

But your implementation separates two responsibilities\:

- Completion handling updates connection state and schedules the client.
- The main loop prepares operations for queued clients and submits them in batches.

This separation makes batching easier.

For example, suppose ten clients complete around the same time.

Instead of immediately entering the kernel for each client's next operation, your engine can enqueue all ten and prepare their next SQEs together.

That's an important design choice.

**Remember\:** Your application ready queue tracks which clients need an operation next. The kernel SQ tracks the actual operation descriptions waiting to be consumed.

---

## 2\. `accept_tag`\: Identifying accept completions

```c
static __thread int accept_tag;
```

This is a small variable, but its purpose is important.

Remember how `user_data` works?

For client operations, you use\:

```c
io_uring_sqe_set_data(sqe, c);
```

Here, `c` is a `conn_t *`\.

But what happens when you submit an accept operation?

At that point, the newly accepted client doesn't have a `conn_t` yet.

So what should you put in `user_data`?

You use a special identifier called `accept_tag`\.

### 2.1 Why do we need it?

Your CQ can contain different types of completions\:

```text
CQE 1 → Client A's receive completed
CQE 2 → Accept operation completed
CQE 3 → Client B's send completed
CQE 4 → Client C's receive completed
```

When you retrieve `user_data`, you need to identify whether the completion belongs to\:

- An accept operation.
- An existing client's operation.

Otherwise, you might incorrectly interpret an accepted socket FD as a client pointer.

### 2.2 How does accept\_tag solve this?

For an accept request, you store\:

```c
io_uring_sqe_set_data(sqe, &accept_tag);
```

For an ordinary client operation\:

```c
io_uring_sqe_set_data(sqe, c);
```

When the completion arrives, you retrieve\:

```c
void *data = io_uring_cqe_get_data(cqe);
```

Then compare\:

```c
if (data == &accept_tag) {
    // Accept completion
} else {
    // Client completion
}
```

This is effectively a special marker.

The actual integer value inside `accept_tag` is irrelevant. You identify it using its address.

### 2.3 Why `static __thread`?

Let's understand both keywords.

**`static`**

The variable has static storage duration. It exists throughout the lifetime of the program's thread, rather than being created and destroyed on each function call.

Its address therefore remains stable.

**`__thread`**

This is a compiler-supported thread-local storage specifier.

Each thread gets its own instance of the variable.

Remember, your framework creates a separate `server_t` and io\_uring instance for each worker thread.

So the arrangement is\:

```text
Worker Thread 0
    |
    |-- Ring 0
    |
    |-- accept_tag 0

Worker Thread 1
    |
    |-- Ring 1
    |
    |-- accept_tag 1
```

This gives each thread a stable, thread-local marker.

The pointer is used as a sentinel, distinct from the heap-allocated `conn_t` pointers.

**Important viva point\:** `accept_tag` is not the accepted socket FD. It is only an identifier for the accept request. The actual accepted FD is returned later in `cqe->res`\.

fileciteturn0file0L313-L327

---

## 3\. `accepting`\: Do we already have an accept operation pending?

```c
int accepting = 0;
```

This variable is a Boolean-like flag.

It tells us whether an accept operation has already been submitted or queued for submission and has not yet had its completion handled.

Why do we need this?

Remember, our server needs to continuously accept new connections.

We could submit an accept operation at the beginning of every loop iteration.

But what if the previous accept operation hasn't completed yet?

We might end up submitting multiple accept requests unnecessarily.

Your implementation avoids this by maintaining at most one outstanding accept request.

### 3.1 How does it work?

Initially\:

```text
accepting = 0
```

This means there is no accept request pending.

Your event loop checks\:

```c
if (!accepting && (sqe = io_uring_get_sqe(ring))) {
```

Two conditions must hold\:

1. `!accepting`\: No accept is already pending.
2. `io_uring_get_sqe(ring)` succeeds: An SQE is available.

Then it prepares the accept operation.

After preparation\:

```c
accepting = 1;
```

Now the next loop iteration will not submit another accept request while this one is outstanding.

### 3.2 When does it become zero again?

When an accept completion is collected\:

```c
if (data == &accept_tag) {
    accepting = 0;
```

This happens regardless of whether the accept succeeded or returned a transient error.

If successful, the accepted client is registered and enqueued.

The next loop iteration can submit another accept request.

The lifecycle is\:

**accepting = 0**

No accept operation pending

**Prepare accept SQE**

**accepting = 1**

One accept operation pending

Kernel completes accept

**Process accept CQE**

**accepting = 0**

Ready to submit another accept

### Why only one accept at a time?

It is a simple design choice.

It avoids managing multiple simultaneous accept operations and multiple accept completions.

The server can keep one accept request pending while it handles existing clients.

One clarification: this is not a fundamental io\_uring limitation. Multiple accept operations could be submitted in other designs. Your implementation chooses one outstanding accept.

---

## 4\. `outstanding`\: How many operations are awaiting completion handling?

```c
unsigned outstanding = 0;
```

This is another very important variable, especially for understanding your shutdown logic later.

It tracks the number of operations for which your engine has not yet processed a completion.

Let's take an example.

Initially\:

```text
outstanding = 0
```

Now your server prepares an accept request\:

```c
outstanding++;
```

Now\:

```text
outstanding = 1
```

It also prepares three client operations\:

```text
Client A → RECV
Client B → SEND
Client C → RECV
```

Each one increments `outstanding`\.

Now\:

```text
outstanding = 4
```

Conceptually\:

```text
1 accept operation
3 client operations

Total = 4
```

### 4.1 When is outstanding decreased?

When you retrieve and consume a CQE\:

```c
io_uring_cqe_seen(ring, cqe);
outstanding--;
```

Suppose Client B's send completes.

Then\:

```text
outstanding = 3
```

The completion has been handled, so one operation is no longer outstanding.

### 4.2 Why do we need this counter?

One particularly important reason is shutdown.

Suppose you receive a SIGTERM signal.

Your event loop stops accepting new work and starts cleaning up.

But there may still be operations pending in the kernel.

You cannot simply free the client buffers because the kernel might still access them.

Your `drain()` function uses `outstanding` to know how many completions must still be collected before the relevant resources can safely be released.

We'll study `drain()` in detail later.

fileciteturn0file0L127-L188

### Important distinction

There is a subtle point worth remembering.

Your code increments `outstanding` when it prepares an SQE, before the submission call.

So the counter precisely represents operations tracked by the event loop that have not yet had their CQEs processed-not necessarily operations that the kernel has already started executing at that exact instant.

This distinction matters when discussing queued submissions and in-flight operations.

Also, a client sitting in the application ready queue does not count as outstanding.

It has not yet received a new operation SQE.

---

## 5\. `result`\: What result should the event loop return?

```c
int result = 0;
```

This is the error/result variable for the whole TCP event loop.

It begins at zero\:

```text
result = 0
```

Meaning that no fatal error has occurred.

During the event loop, if a serious submission or waiting error occurs, the code sets\:

```c
result = rc;
```

For example\:

```c
if (rc < 0 && rc != -EINTR) {
    if (rc == -EAGAIN || rc == -EBUSY) {
        // Recoverable condition
    } else {
        result = rc;
        break;
    }
}
```

Here, `EAGAIN` and `EBUSY` are treated as conditions to handle rather than immediately shutting down the server.

But an unexpected fatal error is stored in `result`\.

At the end\:

```c
drain(s, ring, outstanding, &accept_tag);
return result;
```

So\:

- `0` means the event loop exited without recording a fatal error.
- A negative value indicates an error.

It is not the result of an individual network operation. Individual operation results are stored in `cqe->res`\.

---

## 6\. Why is `server_ready(s)` called here?

Your function also contains\:

```c
server_ready(s);
```

This comes from the common framework, not from io\_uring itself.

Its implementation is\:

```c
void server_ready(server_t *s)
{
    pthread_barrier_wait(&ready_barrier);

    if (s->index == 0) {
        printf("READY %d\n", s->port);
        fflush(stdout);
    }
}
```

fileciteturn0file2L357-L365

Why is this necessary?

Suppose the application runs with\:

```text
--threads 4
```

All four engine threads need to finish their startup and reach the ready state before the benchmark begins.

The barrier makes them wait for one another.

Once all four arrive\:

```text
Thread 0 ─┐
Thread 1 ─┤
Thread 2 ─┼── Barrier releases
Thread 3 ─┘
```

Thread 0 prints\:

```text
READY 9090
```

The benchmarking scripts wait for this message before starting the load generator.

So this is benchmark coordination, not an io\_uring operation.

---

# 7\. Let's put all the variables together

Suppose your server has three connected clients.

Initially\:

```text
head = NULL
tail = NULL

accepting = 0
outstanding = 0
result = 0
```

Now imagine the following sequence.

**Event 1: Server prepares an accept request.**

```text
accepting = 1
outstanding = 1
```

**Event 2: Client A connects and the accept completes.**

The server handles the accept CQE, sets\:

```text
accepting = 0
outstanding = 0
```

It creates Client A's `conn_t` and enqueues it\:

```text
head → A
tail → A
```

**Event 3: The server prepares Client A's first receive.**

Client A is removed from the ready queue and its RECV operation is submitted.

```text
head = NULL
tail = NULL

outstanding = 1
A.pending = 1
```

**Event 4: Client A's receive completes.**

The framework processes the completion.

Suppose the next stage is SEND.

The server clears `pending` and enqueues A again\:

```text
head → A
tail → A

outstanding = 0
A.pending = 0
```

**Event 5: The server prepares the send operation.**

Client A is removed from the queue.

```text
head = NULL
tail = NULL

outstanding = 1
A.pending = 1
```

This is the basic cycle controlled by these variables.

---

# 8\. Quick reference

| Variable | Purpose | Updated when |
|--------|--------|--------|
| `head` | First client waiting for its next operation | Enqueue/dequeue |
| `tail` | Last client waiting for its next operation | Enqueue/dequeue |
| `accept_tag` | Identifies accept completions | Stable throughout thread lifetime |
| `accepting` | Tracks whether an accept is pending | Set on accept preparation, cleared when its CQE is handled |
| `outstanding` | Tracks operations awaiting completion handling | Incremented on preparation, decremented when CQEs are consumed |
| `result` | Overall event-loop result | Set when an unrecoverable error occurs |

---

# 9\. Check your understanding

Let's test the actual logic.

**Q1.** Suppose your ready queue contains\:

```text
A → B → C
```

The SQ has space for only two more SQEs.

What happens to Client C? Does it get lost, or does it remain in the application ready queue?

**Q2.** Why does your implementation use a special `accept_tag` rather than storing the listening socket FD in `user_data`?

**Q3.** Suppose\:

```text
accepting = 1
outstanding = 5
```

An accept completion arrives. What should the two variables become after that completion is handled? Assume no new operations are submitted during this processing.

**Q4. (Viva level)** Suppose Client A has an outstanding receive operation, and your server receives a shutdown signal.

Why can't you simply set `outstanding = 0` and free Client A?

**Q5. (Viva level)** What is the difference between\:

- A client being in the application ready queue.
- A client having an operation outstanding in io\_uring.

---

## Next section: The first actual operations in `run_tcp()`

After these questions, we'll move to this block\:

```c
if (!accepting && (sqe = io_uring_get_sqe(ring))) {
    io_uring_prep_accept(
        sqe,
        s->listener,
        NULL,
        NULL,
        SOCK_NONBLOCK | SOCK_CLOEXEC
    );

    io_uring_sqe_set_data(sqe, &accept_tag);
    accepting = 1;
    outstanding++;
}
```

We'll understand `io_uring_get_sqe()`, asynchronous `accept`, why the listening socket is passed, the meaning of `SOCK_NONBLOCK | SOCK_CLOEXEC`, and what happens inside the kernel when a new client connects.

This is the beginning of the actual TCP connection lifecycle in your implementation.

## Prompt:
08/11/2026, 2:52:18 am

explain whatever is remaining

## Response:
08/11/2026, 2:52:32 am · gpt-6-t-mini-mini

> **Completing `run_tcp`**
>
> I’m covering the remaining lifecycle: arming accepts, preparing client operations, submitting batches, processing completions, handling errors, and draining connections during shutdown.
> 
> The accept loop maintains several pending accept requests. Each request prepares a new connection, records its user data, and lets the kernel notify the server when a client arrives.
> 
> Ready clients then receive their own operations. The server submits those alongside accepts, so accepting new clients and serving existing ones can progress together.
> 
> Completion handling distinguishes accepts from client events using stored metadata. Accepted sockets are initialized, configured, and added to the connection table; client completions update or remove their state.
> 
> The loop chooses between waiting for a completion and briefly checking for already-available completions. This keeps the server responsive without constantly blocking or spinning.
> 
> Finally, shutdown stops new accepts, drains outstanding operations, closes remaining sockets, and releases resources.
>
> **Explaining submission flow**
>
> I’m examining how ready connections are removed from the queue, converted into receive or send requests, and counted as outstanding before submission. A subtle concern is whether partial submission results are handled completely.
>
> **Tracing submission and completions**
>
> I’m noting that clients leave the ready queue before requests are prepared, while outstanding counts track them through completion. Negative submission results trigger handling; queued work can be retried, and completion processing is capped per loop for fairness.
>
> **Handling completion outcomes**
>
> I’m distinguishing accepted sockets from client operations. Successful accepts are configured and registered; transient accept errors are ignored, fatal ones stop the loop. Client completions either requeue usable connections or close them when progress fails definitively.
>
> **Completed TCP submission flow and handled completion outcomes**
>
> 
>
> Worked for 1m 25s

# Lesson 9: Completing `run_tcp()` - Accepting clients, submitting operations and processing completions

We have already understood the variables at the beginning of `run_tcp()`\.

Now let's continue from the first actual operation\:

```c
while (!stop_requested) {
    struct io_uring_sqe *sqe;

    // Remaining implementation
}
```

We'll follow the code in its actual execution order.

There are five major stages\:

1. Submit an accept operation.
2. Prepare operations for ready clients.
3. Submit the prepared operations.
4. Decide whether to wait or just check for completions.
5. Process CQEs and schedule the next operations.

---

# 1\. Submitting an accept operation

Your code\:

```c
if (!accepting && (sqe = io_uring_get_sqe(ring))) {
    io_uring_prep_accept(
        sqe,
        s->listener,
        NULL,
        NULL,
        SOCK_NONBLOCK | SOCK_CLOEXEC
    );

    io_uring_sqe_set_data(sqe, &accept_tag);

    accepting = 1;
    outstanding++;
}
```

fileciteturn0file0L321-L328

Let's break it down.

## 1.1 Why do we need an accept operation?

Remember, your server has a listening socket\:

```c
s->listener
```

It is already bound to an IP address and port and listening for incoming TCP connections.

Whenever a client connects, the server needs to accept that connection.

In a traditional server, we would call\:

```c
int client_fd = accept(server_fd, ...);
```

But your io\_uring engine doesn't call `accept()` directly.

Instead, it submits an asynchronous accept request.

The kernel handles the operation and eventually reports the result through a CQE.

## 1.2 Getting an SQE

```c
sqe = io_uring_get_sqe(ring);
```

This asks liburing for an available Submission Queue Entry.

There are two possibilities\:

| Result | Meaning |
|--------|--------|
| Non-NULL | An SQE slot is available |
| NULL | The SQ is currently full |

Notice that your code checks both conditions\:

```c
!accepting && (sqe = io_uring_get_sqe(ring))
```

It only prepares a new accept if\:

- No previous accept operation is pending.
- An SQE is available.

If the SQ is full, this block is skipped. The loop can still collect completions and try again later.

## 1.3 Preparing the accept operation

```c
io_uring_prep_accept(
    sqe,
    s->listener,
    NULL,
    NULL,
    SOCK_NONBLOCK | SOCK_CLOEXEC
);
```

Let's understand every argument.

| Argument | Meaning |
|--------|--------|
| `sqe` | SQE where the accept request is prepared |
| `s->listener` | Listening socket FD |
| First `NULL` | Don't request the peer's address |
| Second `NULL` | Don't request the peer address length |
| Flags | Properties of the newly accepted socket |

Two important flags\:

**`SOCK_NONBLOCK`**

The newly accepted socket should be non-blocking.

This is useful because the server must never get stuck waiting inside a client socket operation.

For example, if the server later tries to receive data but none is available, the socket should not cause an ordinary blocking receive.

**`SOCK_CLOEXEC`**

This sets close-on-exec for the accepted socket.

If the process subsequently executes another program using `exec()`, the socket descriptor is automatically closed in the new program image.

This prevents unintended file descriptor leakage.

An important distinction: these flags apply to the newly accepted socket, not the listening socket.

Your framework also creates the original listening socket using `SOCK_NONBLOCK | SOCK_CLOEXEC`\. fileciteturn0file2L423-L447

## 1.4 Why are both address arguments NULL?

The normal `accept()` API can return the connecting client's IP address and port.

But your application doesn't need that information.

Your framework handles requests using the accepted socket FD and its associated `conn_t`\.

Therefore, you pass\:

```c
NULL, NULL
```

This avoids requesting peer-address information that your engine doesn't use.

## 1.5 Associate the accept request with accept\_tag

```c
io_uring_sqe_set_data(sqe, &accept_tag);
```

We already discussed this.

When the accept completes, the kernel returns the same `user_data` in the corresponding CQE.

Your completion handler can identify\:

```c
if (data == &accept_tag)
```

Therefore, it knows that the completion belongs to the accept operation.

## 1.6 Update the tracking variables

```c
accepting = 1;
outstanding++;
```

This means\:

```text
One accept operation is now being tracked.
```

Remember that `outstanding` is incremented when the operation is prepared, not only when the kernel starts executing it.

### What happens inside the kernel?

Once the SQE is submitted, the kernel attempts the accept operation.

If a connection is already pending, it can accept it and generate a completion.

Otherwise, the operation can remain pending until a new connection arrives.

For example\:

```text
Application submits ACCEPT
          |
          v
Kernel checks listening socket
          |
          +------ Connection available?
          |                 |
          |               Yes
          |                 |
          |                 v
          |          Accept connection
          |                 |
          |                 v
          |          CQE: res = new FD
          |
          No
          |
          v
Internal polling
          |
          v
New connection arrives
          |
          v
Retry accept and generate CQE
```

Conceptual accept lifecycle; the actual kernel scheduling and polling details depend on the operation and kernel implementation.

---

# 2\. Preparing operations for ready clients

This is one of the most important parts of your implementation.

Your code\:

```c
while (head && (sqe = io_uring_get_sqe(ring))) {
    conn_t *c = head;

    head = c->ready_next;

    if (!head) {
        tail = NULL;
    }

    if (conn_is_reading(c)) {
        io_uring_prep_recv(
            sqe,
            c->fd,
            conn_buffer(c),
            conn_length(c),
            0
        );
    } else {
        io_uring_prep_send(
            sqe,
            c->fd,
            conn_buffer(c),
            conn_length(c),
            MSG_NOSIGNAL
        );
    }

    io_uring_sqe_set_data(sqe, c);
    c->pending = 1;
    outstanding++;
}
```

fileciteturn0file0L329-L342

There are several important design choices here.

## 2.1 Why is this a while loop?

```c
while (head && (sqe = io_uring_get_sqe(ring)))
```

This means\:

Continue processing while\:

1. At least one client is waiting in the application ready queue.
2. The SQ has an available entry.

Suppose your application-side ready queue is\:

```text
head → A → B → C → D
```

And suppose the SQ has room for only two more requests.

Your loop can prepare operations for A and B.

It cannot prepare operations for C and D because no more SQE slots are available.

The situation becomes\:

```text
Application ready queue:

head → C → D

Submission Queue:

SQE 1 → Client A
SQE 2 → Client B
```

**C and D are not lost.** They remain in the application ready queue.

This is why you maintain a separate queue in the first place.

## 2.2 Why do you remove the client from the ready queue before submission?

```c
conn_t *c = head;
head = c->ready_next;
```

You're taking the first client out of the application-side queue.

Why?

Because you're now preparing its next operation.

Once it has been scheduled for submission, it doesn't need to remain in the ready queue.

The flow becomes\:

```text
Client A in ready queue
         |
         v
Remove Client A from queue
         |
         v
Prepare its SQE
         |
         v
Submit operation
         |
         v
Wait for completion
         |
         v
Re-enqueue if connection is still valid
```

This prevents the same client from being scheduled repeatedly while its previous operation is still pending.

## 2.3 Why set tail to NULL?

```c
if (!head) {
    tail = NULL;
}
```

Suppose the queue initially contains only Client A\:

```text
head → A → NULL
       ↑
      tail
```

After removing A\:

```text
head = NULL
```

We must also update\:

```text
tail = NULL
```

Otherwise, `tail` would still point to A, even though the queue is empty.

That would break subsequent enqueue operations.

This is ordinary linked-list queue management, but it's essential for correctness.

---

# 3\. How does the engine decide between RECV and SEND?

Now we reach the key decision\:

```c
if (conn_is_reading(c))
```

This function is implemented in `framework.c`\:

```c
int conn_is_reading(const conn_t *c)
{
    return c->stage == HEADER || c->stage == RECEIVE;
}
```

fileciteturn0file2L154-L157

Your framework defines three stages\:

```c
typedef enum {
    HEADER,
    RECEIVE,
    SEND
} stage_t;
```

fileciteturn0file3L37-L39

Therefore\:

| Client stage | Operation |
|--------|--------|
| HEADER | RECV |
| RECEIVE | RECV |
| SEND | SEND |

The important point is that the io\_uring engine doesn't independently decide what the application should do.

The common framework maintains the client's protocol state.

Your engine simply asks\:

> Does this client currently need to receive or send?

This is part of the project's separation between the engine and the framework.

## 3.1 Case A: Prepare a receive

```c
io_uring_prep_recv(
    sqe,
    c->fd,
    conn_buffer(c),
    conn_length(c),
    0
);
```

We've already discussed these arguments, but let's use a concrete example.

Suppose\:

```text
Client FD = 10

want = 4096
done = 1000
```

Your framework functions give\:

```c
conn_buffer(c) = c->buffer + 1000;
conn_length(c) = 3096;
```

Therefore, the SQE requests\:

```text
Operation: RECV
FD:        10
Buffer:    c->buffer + 1000
Length:    3096
Flags:     0
```

Why `done`?

Because the client might have received only a portion of the current header or body.

We shouldn't overwrite the bytes already received.

The framework maintains this progress information, and your engine passes the remaining buffer region to the kernel.

fileciteturn0file2L154-L167

## 3.2 Case B: Prepare a send

```c
io_uring_prep_send(
    sqe,
    c->fd,
    conn_buffer(c),
    conn_length(c),
    MSG_NOSIGNAL
);
```

Now the client is in the SEND stage.

The engine submits a request to send the remaining response bytes.

Why `MSG_NOSIGNAL`?

Suppose the client closes its connection just before the server sends its response.

A send to that connection could otherwise generate `SIGPIPE`, whose default action is to terminate the process.

`MSG_NOSIGNAL` prevents that signal from being generated by this send.

The error is instead reported through the completion result.

There is an important edge case\:

A send operation can complete partially.

For example\:

```text
Requested = 4096 bytes
Sent      = 2048 bytes
```

The CQE contains\:

```text
res = 2048
```

Your framework updates `done` and the next send is prepared for the remaining bytes.

It doesn't assume that every send transfers the entire requested length.

---

# 4\. Why are user\_data, pending and outstanding updated together?

After preparing the operation, your code executes\:

```c
io_uring_sqe_set_data(sqe, c);
c->pending = 1;
outstanding++;
```

These three lines are closely related, but they serve different purposes.

| Statement | Responsibility |
|--------|--------|
| `io_uring_sqe_set_data(sqe, c)` | Remember which client this operation belongs to |
| `c->pending = 1` | Mark that the operation may be using this client's resources |
| `outstanding++` | Increase the count of operations awaiting completion handling |

Let's understand the buffer ownership problem.

Suppose the engine submits a receive operation using\:

```text
Buffer = Client A's c->buffer
```

The kernel may write into that buffer later.

What happens if we immediately prepare another operation on the same client using the same buffer?

We could overwrite or otherwise interfere with the memory the kernel is using.

Your design avoids that by maintaining one outstanding I/O operation per TCP client.

Until its completion is processed\:

```text
Client A:

pending = 1
```

Only when the completion is retrieved does your engine do\:

```c
c->pending = 0;
```

This helps ensure that the same client is not scheduled for another operation prematurely.

Your `framework.h` explicitly describes `pending` as indicating that io\_uring owns the buffer while the operation is pending. fileciteturn0file3L68-L75

---

# 5\. Submitting all prepared operations

Now the engine has\:

- An accept operation, if needed.
- Receive/send operations for as many queued clients as SQ space permits.

Next\:

```c
int rc = counted_submit(s, ring);
```

We already studied `counted_submit()` in detail.

Let's understand its role here.

Suppose\:

```text
Accept operation       = 1
Client A receive       = 1
Client B send          = 1
Client C receive       = 1
Client D receive       = 1
```

Five SQEs have been prepared.

Your engine calls `counted_submit()` once.

In ordinary io\_uring mode, they can be submitted together through a single kernel entry.

In SQPOLL mode, the polling thread can consume them without an ordinary submission syscall when it is awake.

fileciteturn0file0L344-L352

## 5.1 Understanding the submission error handling

Your code\:

```c
if (rc < 0 && rc != -EINTR) {
    if (rc == -EAGAIN || rc == -EBUSY) {
        /* Completion queue is full: reap below, then submit again. */
    } else {
        result = rc;
        break;
    }
}
```

Let's examine the conditions.

| Return value | What your code does |
|--------|--------|
| Non-negative | Continues to completion processing |
| `-EINTR` | Doesn't treat it as a fatal error |
| `-EAGAIN` | Treats it as a temporary resource condition |
| `-EBUSY` | Treats it as a temporary resource condition |
| Other negative error | Stores the error and exits the main loop |

Why don't you immediately terminate when receiving `EAGAIN` or `EBUSY`?

Because the ring might be temporarily unable to make progress, for example due to completion queue pressure.

Your code tries to collect completions below, freeing CQ space and allowing the loop to make progress.

One distinction to remember: these are negative error results from the liburing submission helper, not the results of individual receive/send operations. Those operation results come through CQEs.

---

# 6\. The interesting part: Should we wait or just peek?

Now comes one of the most important design decisions in your implementation.

Your code\:

```c
struct io_uring_cqe *cqe;
s->loop_calls++;

if (head) {
    rc = io_uring_peek_cqe(ring, &cqe) == 0 ? 0 : -ETIME;
} else {
    rc = counted_wait(s, ring, &cqe);
}
```

fileciteturn0file0L353-L364

There are two possibilities.

### Case A: `head` is not NULL

This means some clients are still waiting in your application-side ready queue.

Why could this happen?

Because the submission queue became full before your engine could prepare an operation for every ready client.

For example\:

```text
Ready queue:

head → Client 500 → Client 501 → Client 502

SQ:

256 operations already prepared
```

There are still clients waiting for their SQEs.

Should the application block waiting for a completion?

Your implementation says no.

Instead\:

```c
io_uring_peek_cqe(ring, &cqe)
```

It checks whether a completion is already available.

If yes, it can process that completion.

If no, it returns\:

```c
-ETIME
```

And the outer loop continues.

### Why is this important?

Consider this situation\:

```text
SQ capacity = 256

256 clients have operations submitted.

More clients are waiting in the application ready queue.
```

Suppose all the submitted operations are receives.

But those clients are waiting for their replies, and their replies are still queued in your application.

The server blocks waiting for receive completions.

But those clients won't send the next data until they receive replies.

And the replies cannot be sent because their send operations haven't been submitted yet.

We have created a progress problem.

Your README describes exactly this stall, which caused throughput to fall to approximately 5,000 requests per second before the fix. fileciteturn0file1L585-L599

Your fix is\:

**While there are still clients waiting in the application ready queue, do not block waiting for a completion. Only peek for already available completions, then return to the top of the loop to submit the remaining clients.**

This ensures that your application keeps submitting work rather than sleeping while unscheduled work remains.

**Your submission-queue stall fix**

SQ becomes full

Clients remain in application ready queue

**Peek at CQ only**

Don't block

Return to loop and submit more clients

### Case B: `head` is NULL

This means your application ready queue is empty.

All currently queued clients have had their next operations prepared.

Now it makes sense to wait for a completion.

Your code calls\:

```c
rc = counted_wait(s, ring, &cqe);
```

This function\:

1. Checks whether a CQE already exists.
2. Uses busy polling if configured.
3. Otherwise waits for a CQE for at most 100 ms.

This prevents the application from unnecessarily consuming CPU when there is no completion to process and no unscheduled client operation waiting.

**Remember this distinction\:**

- Ready queue nonempty → Don't block; there is still work to submit.
- Ready queue empty → Waiting for an operation to complete is reasonable.

This is one of the most important pieces of your TCP event-loop logic.

---

# 7\. Handling the wait result

Your code\:

```c
if (rc == -ETIME || rc == -EINTR) {
    continue;
}

if (rc < 0) {
    result = rc;
    break;
}
```

Let's understand it.

### `-ETIME`

The waiting operation timed out, or the preceding peek found no available completion.

This is not necessarily an error.

For example\:

```text
No CQE available
Wait 100 ms
Timeout
```

The server simply continues its event loop.

### `-EINTR`

The wait was interrupted by a signal.

Remember the framework's signal handler\:

```c
stop_requested = 1;
```

The application should check that flag and exit cleanly.

Your loop goes through the normal control flow and checks `stop_requested`\.

### Other negative results

These indicate an error that your engine doesn't treat as a normal timeout or interruption.

It stores the error and exits the main loop.

---

# 8\. Processing CQEs - the heart of your engine

Now we reach the completion-processing section.

Your code\:

```c
for (int i = 0;
     i < RING_DEPTH && io_uring_peek_cqe(ring, &cqe) == 0;
     i++) {

    void *data = io_uring_cqe_get_data(cqe);
    int n = cqe->res;

    io_uring_cqe_seen(ring, cqe);
    outstanding--;

    // Handle accept or client completion
}
```

fileciteturn0file0L372-L377

Let's understand every part.

## 8.1 Why use a for loop?

You want to process multiple completions in one iteration.

But you also limit the number processed\:

```c
i < RING_DEPTH
```

Since\:

```c
#define RING_DEPTH 256
```

You process at most 256 CQEs per loop iteration.

Why not process every CQE in the CQ?

Because you want the event loop to return to submission and scheduling work after a bounded batch.

This avoids letting completion processing consume an unbounded amount of one iteration when the CQ contains many events.

It also allows newly completed clients to be scheduled for their next operations in subsequent iterations.

Remember that your CQ may contain far more than 256 entries. This loop's limit is a processing-batch limit, not the CQ capacity.

## 8.2 Check whether a completion exists

```c
io_uring_peek_cqe(ring, &cqe) == 0
```

This checks whether a CQE is available.

If there are no more CQEs, the loop stops.

No additional syscall is needed just to inspect an already populated CQ.

## 8.3 Retrieve user\_data

```c
void *data = io_uring_cqe_get_data(cqe);
```

This tells us which operation the completion belongs to.

Remember\:

- Accept operation → `&accept_tag`
- TCP client operation → `conn_t *`

## 8.4 Retrieve the result

```c
int n = cqe->res;
```

This could mean\:

| Completion | `n` |
|--------|--------|
| Receive | Bytes received |
| Send | Bytes sent |
| Accept | Newly accepted FD |
| Failure | Negative error number |
| EOF | 0 |

We will use this value to update the appropriate state.

## 8.5 Mark the CQE as consumed

```c
io_uring_cqe_seen(ring, cqe);
```

We have finished reading the completion.

The CQ head is advanced through liburing's queue-management mechanism, making the slot reusable.

## 8.6 Decrement outstanding

```c
outstanding--;
```

One completion has now been collected and processed by the event loop.

We decrease our outstanding operation count.

This is important for shutdown, as we will see when studying `drain()`\.

---

# 9\. Handling accept completions

Now your code checks\:

```c
if (data == &accept_tag) {
    accepting = 0;
```

This means the completion belongs to the outstanding accept operation.

First, set\:

```c
accepting = 0;
```

Why?

Because the accept request is no longer pending.

The next iteration can submit another accept operation.

Now check the result\:

```c
if (n >= 0) {
    // Successful accept
}
```

If `n` is non-negative, it represents the newly accepted socket FD.

Let's see what happens next.

## 9.1 Enable TCP\_NODELAY

```c
int enabled = 1;

if (setsockopt(
        n,
        IPPROTO_TCP,
        TCP_NODELAY,
        &enabled,
        sizeof(enabled)
    )) {
    close(n);
    s->rejected++;
    continue;
}
```

Why do you set `TCP_NODELAY`?

TCP normally uses Nagle's algorithm to combine small outgoing data segments in certain circumstances.

For a latency-sensitive request-response workload involving small messages, this can introduce additional delay.

`TCP_NODELAY` disables Nagle's algorithm for this connection.

Why might that matter for your project?

Your benchmark includes small echo requests and responses.

You want to measure the I/O mechanism rather than have TCP's small-packet buffering behavior introduce an additional source of latency.

If `setsockopt()` fails\:

1. Close the newly accepted socket.
2. Increment `rejected`\.
3. Skip the rest of this completion handler.

Why `continue`?

Because the connection was not successfully configured and should not be added to the server's client list.

## 9.2 Create the connection structure

Next\:

```c
conn_t *c = conn_add(s, n);
```

This function comes from `framework.c`\.

It does several things\:

1. Checks the maximum-client limit.
2. Allocates a `conn_t` and its associated buffer.
3. Calls the application's `on_open` hook, if present.
4. Initializes the connection's request state.
5. Adds it to the linked list used for shutdown management.
6. Updates connection statistics.

The new connection begins in the `HEADER` stage.

fileciteturn0file2L42-L84

If allocation fails or the client limit is reached, `conn_add()` rejects and closes the socket.

If successful\:

```c
if (c) {
    enqueue(&head, &tail, c);
}
```

We add the new client to the application ready queue.

Why?

Because the newly accepted client needs its first receive operation.

It will be submitted during the next event-loop iteration.

### Complete accept flow

```text
Accept SQE submitted
       |
       v
Accept CQE arrives
       |
       v
Clear accepting
       |
       v
Is cqe->res >= 0?
       |
       v
Set TCP_NODELAY
       |
       v
conn_add()
       |
       v
Initialize conn_t
       |
       v
Enqueue client
       |
       v
Next iteration prepares RECV
```

---

# 10\. What if accept fails?

Your code\:

```c
else if (
    n != -EAGAIN &&
    n != -EINTR &&
    n != -ECONNABORTED
) {
    result = n;
    goto shutdown;
}
```

This is an important error-handling condition.

Let's understand these errors.

| Error | Meaning | Your response |
|--------|--------|--------|
| `-EAGAIN` | No connection currently available to accept | Ignore and retry |
| `-EINTR` | Operation interrupted | Ignore and retry |
| `-ECONNABORTED` | A pending connection was aborted | Ignore and retry |
| Other negative error | Unexpected accept failure | Start shutdown |

Why ignore these three?

Because they are conditions that don't necessarily indicate that the server is broken.

For example, an incoming connection can be aborted before it is accepted.

The server should not terminate just because one connection failed.

For an unexpected error, you store the error and jump to\:

```c
shutdown:
    drain(s, ring, outstanding, &accept_tag);
```

We'll study `drain()` in a separate lesson.

---

# 11\. Handling ordinary client completions

Now look at the `else` branch\:

```c
else {
    conn_t *c = data;

    c->pending = 0;

    int status = conn_complete(c, n);

    if (status) {
        conn_close(c, status);
    } else {
        enqueue(&head, &tail, c);
    }
}
```

This is where your engine interacts with the common TCP framework.

Let's break it down.

## 11.1 Recover the client pointer

```c
conn_t *c = data;
```

We already know that this completion isn't the accept operation.

Therefore, `user_data` contains the pointer to the client whose operation has completed.

## 11.2 Clear pending

```c
c->pending = 0;
```

The particular operation has completed.

The engine can now safely manage that client's buffer again, provided there isn't another outstanding request referencing it.

Remember, your design permits only one outstanding receive or send operation per TCP client.

## 11.3 Pass the result to the framework

```c
int status = conn_complete(c, n);
```

This is a very important architectural decision.

Your io\_uring engine does not implement a separate request-processing state machine.

It passes the completion result to the framework.

Let's understand what the framework does.

Suppose\:

```text
Client A is in HEADER stage
want = 8 bytes
done = 0
```

Its receive completes\:

```text
n = 8
```

The framework updates\:

```text
done = 8
```

Then it decodes the 8-byte request length and determines how much body data should be received and how large the response should be.

Depending on the request, it changes the connection stage to `RECEIVE` or `SEND`\.

Similarly, during SEND, it tracks how many response bytes remain.

fileciteturn0file2L190-L255

## 11.4 Interpret the framework's return value

`conn_complete()` can return\:

| Return | Meaning |
|--------|--------|
| `0` | Connection remains valid |
| `1` | Clean client closure between requests |
| Negative | Connection error |

What does your engine do?

```c
if (status) {
    conn_close(c, status);
}
```

Any nonzero status causes the client to be closed.

Otherwise\:

```c
enqueue(&head, &tail, c);
```

The client is scheduled for its next operation.

For example\:

```text
Client A

RECV completes
      |
      v
conn_complete()
      |
      v
Stage changes to SEND
      |
      v
Enqueue Client A
      |
      v
Next iteration
      |
      v
Prepare SEND SQE
```

This is how the client moves between receive and send operations without the engine having to duplicate the framework's protocol logic.

---

# 12\. Complete TCP event-loop logic

Now let's put the entire function together conceptually.

**Start event-loop iteration**

**1\. Ensure an accept is pending**

Prepare ACCEPT SQE if not already accepting and an SQE is available.

**2\. Schedule ready clients**

Prepare RECV or SEND SQEs until the ready queue is empty or the SQ fills.

**3\. Submit operations**

Call counted\_submit() to publish the batch.

**4\. Wait or peek**

If ready clients remain, only peek. Otherwise use counted\_wait().

**5\. Process up to 256 CQEs**

Identify accept/client operations, update state, close invalid clients or re-enqueue valid ones.

**Next iteration**

When the loop ends\:

```c
shutdown:
    drain(s, ring, outstanding, &accept_tag);
    return result;
```

The engine doesn't simply abandon pending operations.

It calls `drain()` to cancel or collect outstanding work before returning.

We'll study the cancellation logic separately because it involves some important memory-safety and completion-accounting issues.

---

# 13\. Let's simulate a complete request

This example ties together everything we have learned.

Suppose Client A connects and sends an echo request.

Its request contains\:

```text
8-byte length header
4-byte body: "TEST"
```

Your server is configured with the echo application.

### Stage 1: Accept

The server submits\:

```text
ACCEPT SQE
user_data = &accept_tag
```

The CQE returns\:

```text
res = 12
```

Suppose FD 12 is the new connection.

The engine calls\:

```c
conn_add(s, 12);
```

The connection starts in HEADER stage and is enqueued.

### Stage 2: Receive header

The engine prepares\:

```text
RECV SQE
FD = 12
Length = 8
user_data = conn_t pointer
```

The receive completes with\:

```text
res = 8
```

The framework decodes the header.

It determines\:

```text
body_left = 4
response_left = 4
stage = RECEIVE
```

The client is re-enqueued.

### Stage 3: Receive body

Another receive operation is submitted\:

```text
RECV SQE
FD = 12
Length = 4
```

It completes\:

```text
res = 4
```

The framework now changes its stage\:

```text
RECEIVE → SEND
```

For echo, the received body is already in the buffer, so no additional response-generation function is needed.

The client is re-enqueued again.

### Stage 4: Send response

The engine prepares\:

```text
SEND SQE
FD = 12
Buffer = "TEST"
Length = 4
Flags = MSG_NOSIGNAL
```

The send completes\:

```text
res = 4
```

The framework\:

- Records the written bytes.
- Decreases the response bytes remaining.
- Increments the completed message count.
- Resets the client to HEADER stage.

The client is re-enqueued for its next request.

**One complete echo request**

1. **ACCEPT**
   
   New connection accepted; `conn_t` created.
2. **RECV header**
   
   Read 8-byte request length.
3. **RECV body**
   
   Read 4-byte payload.
4. **SEND response**
   
   Return the same 4-byte payload.
5. **Reset to HEADER**
   
   Increment message count and prepare for the next request.

Notice that the engine performs no direct `recv()` or `send()` calls for these TCP operations. It prepares SQEs and processes their CQE results.

---

# 14\. Important implementation observations

Before moving on, there are some details you should be prepared to explain.

| Design choice | Reason |
|--------|--------|
| One accept in flight | Simpler management of incoming connections |
| FIFO ready queue | Schedule clients waiting for their next operation |
| One operation per TCP client | Prevent concurrent use of the same connection buffer |
| `user_data = c` | Identify the client when its CQE arrives |
| `pending = 1` | Track ownership of the client buffer |
| Batching | Submit operations for multiple clients together |
| Wait only when `head == NULL` | Avoid stalling while clients remain unscheduled |
| Reap at most 256 CQEs per iteration | Bound the completion-processing batch |
| Shared `conn_complete()` | Keep protocol state-machine behavior identical across engines |

One subtle point about the last two\:

The 256-CQE processing limit is a scheduling decision. It doesn't mean the completion queue can only contain 256 entries.

Your CQ can be 16,384 entries under the default configuration.

---

# 15\. Check your understanding

These are based directly on your implementation. Try explaining them without referring to the code.

**TCP event-loop quiz** (/6 answered)

## What remains in your implementation walkthrough?

We've now covered the complete main `run_tcp()` event loop.

The remaining major sections of `io_uring_server.c` are\:

1. **`drain()`** - cancellation, outstanding operations, CQE collection and safe shutdown.
2. **UDP implementation** - `udp_slot_t`, `post_udp_recv()`, `post_udp_send()` and `run_udp()`\.
3. **Syscall accounting** - deeper examination of `counted_submit()` and `counted_wait()` against your benchmark results.
4. **Performance and edge cases** - CQ overflow, SQ stalls, partial TCP sends/receives, and the design trade-offs.

I recommend studying `drain()` next. It is arguably the most important correctness-related part of your implementation because it protects against freeing memory while the kernel still has pending access to it.

# Lesson 9: Completing `run_tcp()` - Accepting clients, submitting operations and processing completions

We have already understood the variables at the beginning of `run_tcp()`\.

Now let's continue from the first actual operation\:

```c
while (!stop_requested) {
    struct io_uring_sqe *sqe;

    // Remaining implementation
}
```

We'll follow the code in its actual execution order.

There are five major stages\:

1. Submit an accept operation.
2. Prepare operations for ready clients.
3. Submit the prepared operations.
4. Decide whether to wait or just check for completions.
5. Process CQEs and schedule the next operations.

---

# 1\. Submitting an accept operation

Your code\:

```c
if (!accepting && (sqe = io_uring_get_sqe(ring))) {
    io_uring_prep_accept(
        sqe,
        s->listener,
        NULL,
        NULL,
        SOCK_NONBLOCK | SOCK_CLOEXEC
    );

    io_uring_sqe_set_data(sqe, &accept_tag);

    accepting = 1;
    outstanding++;
}
```

fileciteturn0file0L321-L328

Let's break it down.

## 1.1 Why do we need an accept operation?

Remember, your server has a listening socket\:

```c
s->listener
```

It is already bound to an IP address and port and listening for incoming TCP connections.

Whenever a client connects, the server needs to accept that connection.

In a traditional server, we would call\:

```c
int client_fd = accept(server_fd, ...);
```

But your io\_uring engine doesn't call `accept()` directly.

Instead, it submits an asynchronous accept request.

The kernel handles the operation and eventually reports the result through a CQE.

## 1.2 Getting an SQE

```c
sqe = io_uring_get_sqe(ring);
```

This asks liburing for an available Submission Queue Entry.

There are two possibilities\:

| Result | Meaning |
|--------|--------|
| Non-NULL | An SQE slot is available |
| NULL | The SQ is currently full |

Notice that your code checks both conditions\:

```c
!accepting && (sqe = io_uring_get_sqe(ring))
```

It only prepares a new accept if\:

- No previous accept operation is pending.
- An SQE is available.

If the SQ is full, this block is skipped. The loop can still collect completions and try again later.

## 1.3 Preparing the accept operation

```c
io_uring_prep_accept(
    sqe,
    s->listener,
    NULL,
    NULL,
    SOCK_NONBLOCK | SOCK_CLOEXEC
);
```

Let's understand every argument.

| Argument | Meaning |
|--------|--------|
| `sqe` | SQE where the accept request is prepared |
| `s->listener` | Listening socket FD |
| First `NULL` | Don't request the peer's address |
| Second `NULL` | Don't request the peer address length |
| Flags | Properties of the newly accepted socket |

Two important flags\:

**`SOCK_NONBLOCK`**

The newly accepted socket should be non-blocking.

This is useful because the server must never get stuck waiting inside a client socket operation.

For example, if the server later tries to receive data but none is available, the socket should not cause an ordinary blocking receive.

**`SOCK_CLOEXEC`**

This sets close-on-exec for the accepted socket.

If the process subsequently executes another program using `exec()`, the socket descriptor is automatically closed in the new program image.

This prevents unintended file descriptor leakage.

An important distinction: these flags apply to the newly accepted socket, not the listening socket.

Your framework also creates the original listening socket using `SOCK_NONBLOCK | SOCK_CLOEXEC`\. fileciteturn0file2L423-L447

## 1.4 Why are both address arguments NULL?

The normal `accept()` API can return the connecting client's IP address and port.

But your application doesn't need that information.

Your framework handles requests using the accepted socket FD and its associated `conn_t`\.

Therefore, you pass\:

```c
NULL, NULL
```

This avoids requesting peer-address information that your engine doesn't use.

## 1.5 Associate the accept request with accept\_tag

```c
io_uring_sqe_set_data(sqe, &accept_tag);
```

We already discussed this.

When the accept completes, the kernel returns the same `user_data` in the corresponding CQE.

Your completion handler can identify\:

```c
if (data == &accept_tag)
```

Therefore, it knows that the completion belongs to the accept operation.

## 1.6 Update the tracking variables

```c
accepting = 1;
outstanding++;
```

This means\:

```text
One accept operation is now being tracked.
```

Remember that `outstanding` is incremented when the operation is prepared, not only when the kernel starts executing it.

### What happens inside the kernel?

Once the SQE is submitted, the kernel attempts the accept operation.

If a connection is already pending, it can accept it and generate a completion.

Otherwise, the operation can remain pending until a new connection arrives.

For example\:

```text
Application submits ACCEPT
          |
          v
Kernel checks listening socket
          |
          +------ Connection available?
          |                 |
          |               Yes
          |                 |
          |                 v
          |          Accept connection
          |                 |
          |                 v
          |          CQE: res = new FD
          |
          No
          |
          v
Internal polling
          |
          v
New connection arrives
          |
          v
Retry accept and generate CQE
```

Conceptual accept lifecycle; the actual kernel scheduling and polling details depend on the operation and kernel implementation.

---

# 2\. Preparing operations for ready clients

This is one of the most important parts of your implementation.

Your code\:

```c
while (head && (sqe = io_uring_get_sqe(ring))) {
    conn_t *c = head;

    head = c->ready_next;

    if (!head) {
        tail = NULL;
    }

    if (conn_is_reading(c)) {
        io_uring_prep_recv(
            sqe,
            c->fd,
            conn_buffer(c),
            conn_length(c),
            0
        );
    } else {
        io_uring_prep_send(
            sqe,
            c->fd,
            conn_buffer(c),
            conn_length(c),
            MSG_NOSIGNAL
        );
    }

    io_uring_sqe_set_data(sqe, c);
    c->pending = 1;
    outstanding++;
}
```

fileciteturn0file0L329-L342

There are several important design choices here.

## 2.1 Why is this a while loop?

```c
while (head && (sqe = io_uring_get_sqe(ring)))
```

This means\:

Continue processing while\:

1. At least one client is waiting in the application ready queue.
2. The SQ has an available entry.

Suppose your application-side ready queue is\:

```text
head → A → B → C → D
```

And suppose the SQ has room for only two more requests.

Your loop can prepare operations for A and B.

It cannot prepare operations for C and D because no more SQE slots are available.

The situation becomes\:

```text
Application ready queue:

head → C → D

Submission Queue:

SQE 1 → Client A
SQE 2 → Client B
```

**C and D are not lost.** They remain in the application ready queue.

This is why you maintain a separate queue in the first place.

## 2.2 Why do you remove the client from the ready queue before submission?

```c
conn_t *c = head;
head = c->ready_next;
```

You're taking the first client out of the application-side queue.

Why?

Because you're now preparing its next operation.

Once it has been scheduled for submission, it doesn't need to remain in the ready queue.

The flow becomes\:

```text
Client A in ready queue
         |
         v
Remove Client A from queue
         |
         v
Prepare its SQE
         |
         v
Submit operation
         |
         v
Wait for completion
         |
         v
Re-enqueue if connection is still valid
```

This prevents the same client from being scheduled repeatedly while its previous operation is still pending.

## 2.3 Why set tail to NULL?

```c
if (!head) {
    tail = NULL;
}
```

Suppose the queue initially contains only Client A\:

```text
head → A → NULL
       ↑
      tail
```

After removing A\:

```text
head = NULL
```

We must also update\:

```text
tail = NULL
```

Otherwise, `tail` would still point to A, even though the queue is empty.

That would break subsequent enqueue operations.

This is ordinary linked-list queue management, but it's essential for correctness.

---

# 3\. How does the engine decide between RECV and SEND?

Now we reach the key decision\:

```c
if (conn_is_reading(c))
```

This function is implemented in `framework.c`\:

```c
int conn_is_reading(const conn_t *c)
{
    return c->stage == HEADER || c->stage == RECEIVE;
}
```

fileciteturn0file2L154-L157

Your framework defines three stages\:

```c
typedef enum {
    HEADER,
    RECEIVE,
    SEND
} stage_t;
```

fileciteturn0file3L37-L39

Therefore\:

| Client stage | Operation |
|--------|--------|
| HEADER | RECV |
| RECEIVE | RECV |
| SEND | SEND |

The important point is that the io\_uring engine doesn't independently decide what the application should do.

The common framework maintains the client's protocol state.

Your engine simply asks\:

> Does this client currently need to receive or send?

This is part of the project's separation between the engine and the framework.

## 3.1 Case A: Prepare a receive

```c
io_uring_prep_recv(
    sqe,
    c->fd,
    conn_buffer(c),
    conn_length(c),
    0
);
```

We've already discussed these arguments, but let's use a concrete example.

Suppose\:

```text
Client FD = 10

want = 4096
done = 1000
```

Your framework functions give\:

```c
conn_buffer(c) = c->buffer + 1000;
conn_length(c) = 3096;
```

Therefore, the SQE requests\:

```text
Operation: RECV
FD:        10
Buffer:    c->buffer + 1000
Length:    3096
Flags:     0
```

Why `done`?

Because the client might have received only a portion of the current header or body.

We shouldn't overwrite the bytes already received.

The framework maintains this progress information, and your engine passes the remaining buffer region to the kernel.

fileciteturn0file2L154-L167

## 3.2 Case B: Prepare a send

```c
io_uring_prep_send(
    sqe,
    c->fd,
    conn_buffer(c),
    conn_length(c),
    MSG_NOSIGNAL
);
```

Now the client is in the SEND stage.

The engine submits a request to send the remaining response bytes.

Why `MSG_NOSIGNAL`?

Suppose the client closes its connection just before the server sends its response.

A send to that connection could otherwise generate `SIGPIPE`, whose default action is to terminate the process.

`MSG_NOSIGNAL` prevents that signal from being generated by this send.

The error is instead reported through the completion result.

There is an important edge case\:

A send operation can complete partially.

For example\:

```text
Requested = 4096 bytes
Sent      = 2048 bytes
```

The CQE contains\:

```text
res = 2048
```

Your framework updates `done` and the next send is prepared for the remaining bytes.

It doesn't assume that every send transfers the entire requested length.

---

# 4\. Why are user\_data, pending and outstanding updated together?

After preparing the operation, your code executes\:

```c
io_uring_sqe_set_data(sqe, c);
c->pending = 1;
outstanding++;
```

These three lines are closely related, but they serve different purposes.

| Statement | Responsibility |
|--------|--------|
| `io_uring_sqe_set_data(sqe, c)` | Remember which client this operation belongs to |
| `c->pending = 1` | Mark that the operation may be using this client's resources |
| `outstanding++` | Increase the count of operations awaiting completion handling |

Let's understand the buffer ownership problem.

Suppose the engine submits a receive operation using\:

```text
Buffer = Client A's c->buffer
```

The kernel may write into that buffer later.

What happens if we immediately prepare another operation on the same client using the same buffer?

We could overwrite or otherwise interfere with the memory the kernel is using.

Your design avoids that by maintaining one outstanding I/O operation per TCP client.

Until its completion is processed\:

```text
Client A:

pending = 1
```

Only when the completion is retrieved does your engine do\:

```c
c->pending = 0;
```

This helps ensure that the same client is not scheduled for another operation prematurely.

Your `framework.h` explicitly describes `pending` as indicating that io\_uring owns the buffer while the operation is pending. fileciteturn0file3L68-L75

---

# 5\. Submitting all prepared operations

Now the engine has\:

- An accept operation, if needed.
- Receive/send operations for as many queued clients as SQ space permits.

Next\:

```c
int rc = counted_submit(s, ring);
```

We already studied `counted_submit()` in detail.

Let's understand its role here.

Suppose\:

```text
Accept operation       = 1
Client A receive       = 1
Client B send          = 1
Client C receive       = 1
Client D receive       = 1
```

Five SQEs have been prepared.

Your engine calls `counted_submit()` once.

In ordinary io\_uring mode, they can be submitted together through a single kernel entry.

In SQPOLL mode, the polling thread can consume them without an ordinary submission syscall when it is awake.

fileciteturn0file0L344-L352

## 5.1 Understanding the submission error handling

Your code\:

```c
if (rc < 0 && rc != -EINTR) {
    if (rc == -EAGAIN || rc == -EBUSY) {
        /* Completion queue is full: reap below, then submit again. */
    } else {
        result = rc;
        break;
    }
}
```

Let's examine the conditions.

| Return value | What your code does |
|--------|--------|
| Non-negative | Continues to completion processing |
| `-EINTR` | Doesn't treat it as a fatal error |
| `-EAGAIN` | Treats it as a temporary resource condition |
| `-EBUSY` | Treats it as a temporary resource condition |
| Other negative error | Stores the error and exits the main loop |

Why don't you immediately terminate when receiving `EAGAIN` or `EBUSY`?

Because the ring might be temporarily unable to make progress, for example due to completion queue pressure.

Your code tries to collect completions below, freeing CQ space and allowing the loop to make progress.

One distinction to remember: these are negative error results from the liburing submission helper, not the results of individual receive/send operations. Those operation results come through CQEs.

---

# 6\. The interesting part: Should we wait or just peek?

Now comes one of the most important design decisions in your implementation.

Your code\:

```c
struct io_uring_cqe *cqe;
s->loop_calls++;

if (head) {
    rc = io_uring_peek_cqe(ring, &cqe) == 0 ? 0 : -ETIME;
} else {
    rc = counted_wait(s, ring, &cqe);
}
```

fileciteturn0file0L353-L364

There are two possibilities.

### Case A: `head` is not NULL

This means some clients are still waiting in your application-side ready queue.

Why could this happen?

Because the submission queue became full before your engine could prepare an operation for every ready client.

For example\:

```text
Ready queue:

head → Client 500 → Client 501 → Client 502

SQ:

256 operations already prepared
```

There are still clients waiting for their SQEs.

Should the application block waiting for a completion?

Your implementation says no.

Instead\:

```c
io_uring_peek_cqe(ring, &cqe)
```

It checks whether a completion is already available.

If yes, it can process that completion.

If no, it returns\:

```c
-ETIME
```

And the outer loop continues.

### Why is this important?

Consider this situation\:

```text
SQ capacity = 256

256 clients have operations submitted.

More clients are waiting in the application ready queue.
```

Suppose all the submitted operations are receives.

But those clients are waiting for their replies, and their replies are still queued in your application.

The server blocks waiting for receive completions.

But those clients won't send the next data until they receive replies.

And the replies cannot be sent because their send operations haven't been submitted yet.

We have created a progress problem.

Your README describes exactly this stall, which caused throughput to fall to approximately 5,000 requests per second before the fix. fileciteturn0file1L585-L599

Your fix is\:

**While there are still clients waiting in the application ready queue, do not block waiting for a completion. Only peek for already available completions, then return to the top of the loop to submit the remaining clients.**

This ensures that your application keeps submitting work rather than sleeping while unscheduled work remains.

**Your submission-queue stall fix**

SQ becomes full

Clients remain in application ready queue

**Peek at CQ only**

Don't block

Return to loop and submit more clients

### Case B: `head` is NULL

This means your application ready queue is empty.

All currently queued clients have had their next operations prepared.

Now it makes sense to wait for a completion.

Your code calls\:

```c
rc = counted_wait(s, ring, &cqe);
```

This function\:

1. Checks whether a CQE already exists.
2. Uses busy polling if configured.
3. Otherwise waits for a CQE for at most 100 ms.

This prevents the application from unnecessarily consuming CPU when there is no completion to process and no unscheduled client operation waiting.

**Remember this distinction\:**

- Ready queue nonempty → Don't block; there is still work to submit.
- Ready queue empty → Waiting for an operation to complete is reasonable.

This is one of the most important pieces of your TCP event-loop logic.

---

# 7\. Handling the wait result

Your code\:

```c
if (rc == -ETIME || rc == -EINTR) {
    continue;
}

if (rc < 0) {
    result = rc;
    break;
}
```

Let's understand it.

### `-ETIME`

The waiting operation timed out, or the preceding peek found no available completion.

This is not necessarily an error.

For example\:

```text
No CQE available
Wait 100 ms
Timeout
```

The server simply continues its event loop.

### `-EINTR`

The wait was interrupted by a signal.

Remember the framework's signal handler\:

```c
stop_requested = 1;
```

The application should check that flag and exit cleanly.

Your loop goes through the normal control flow and checks `stop_requested`\.

### Other negative results

These indicate an error that your engine doesn't treat as a normal timeout or interruption.

It stores the error and exits the main loop.

---

# 8\. Processing CQEs - the heart of your engine

Now we reach the completion-processing section.

Your code\:

```c
for (int i = 0;
     i < RING_DEPTH && io_uring_peek_cqe(ring, &cqe) == 0;
     i++) {

    void *data = io_uring_cqe_get_data(cqe);
    int n = cqe->res;

    io_uring_cqe_seen(ring, cqe);
    outstanding--;

    // Handle accept or client completion
}
```

fileciteturn0file0L372-L377

Let's understand every part.

## 8.1 Why use a for loop?

You want to process multiple completions in one iteration.

But you also limit the number processed\:

```c
i < RING_DEPTH
```

Since\:

```c
#define RING_DEPTH 256
```

You process at most 256 CQEs per loop iteration.

Why not process every CQE in the CQ?

Because you want the event loop to return to submission and scheduling work after a bounded batch.

This avoids letting completion processing consume an unbounded amount of one iteration when the CQ contains many events.

It also allows newly completed clients to be scheduled for their next operations in subsequent iterations.

Remember that your CQ may contain far more than 256 entries. This loop's limit is a processing-batch limit, not the CQ capacity.

## 8.2 Check whether a completion exists

```c
io_uring_peek_cqe(ring, &cqe) == 0
```

This checks whether a CQE is available.

If there are no more CQEs, the loop stops.

No additional syscall is needed just to inspect an already populated CQ.

## 8.3 Retrieve user\_data

```c
void *data = io_uring_cqe_get_data(cqe);
```

This tells us which operation the completion belongs to.

Remember\:

- Accept operation → `&accept_tag`
- TCP client operation → `conn_t *`

## 8.4 Retrieve the result

```c
int n = cqe->res;
```

This could mean\:

| Completion | `n` |
|--------|--------|
| Receive | Bytes received |
| Send | Bytes sent |
| Accept | Newly accepted FD |
| Failure | Negative error number |
| EOF | 0 |

We will use this value to update the appropriate state.

## 8.5 Mark the CQE as consumed

```c
io_uring_cqe_seen(ring, cqe);
```

We have finished reading the completion.

The CQ head is advanced through liburing's queue-management mechanism, making the slot reusable.

## 8.6 Decrement outstanding

```c
outstanding--;
```

One completion has now been collected and processed by the event loop.

We decrease our outstanding operation count.

This is important for shutdown, as we will see when studying `drain()`\.

---

# 9\. Handling accept completions

Now your code checks\:

```c
if (data == &accept_tag) {
    accepting = 0;
```

This means the completion belongs to the outstanding accept operation.

First, set\:

```c
accepting = 0;
```

Why?

Because the accept request is no longer pending.

The next iteration can submit another accept operation.

Now check the result\:

```c
if (n >= 0) {
    // Successful accept
}
```

If `n` is non-negative, it represents the newly accepted socket FD.

Let's see what happens next.

## 9.1 Enable TCP\_NODELAY

```c
int enabled = 1;

if (setsockopt(
        n,
        IPPROTO_TCP,
        TCP_NODELAY,
        &enabled,
        sizeof(enabled)
    )) {
    close(n);
    s->rejected++;
    continue;
}
```

Why do you set `TCP_NODELAY`?

TCP normally uses Nagle's algorithm to combine small outgoing data segments in certain circumstances.

For a latency-sensitive request-response workload involving small messages, this can introduce additional delay.

`TCP_NODELAY` disables Nagle's algorithm for this connection.

Why might that matter for your project?

Your benchmark includes small echo requests and responses.

You want to measure the I/O mechanism rather than have TCP's small-packet buffering behavior introduce an additional source of latency.

If `setsockopt()` fails\:

1. Close the newly accepted socket.
2. Increment `rejected`\.
3. Skip the rest of this completion handler.

Why `continue`?

Because the connection was not successfully configured and should not be added to the server's client list.

## 9.2 Create the connection structure

Next\:

```c
conn_t *c = conn_add(s, n);
```

This function comes from `framework.c`\.

It does several things\:

1. Checks the maximum-client limit.
2. Allocates a `conn_t` and its associated buffer.
3. Calls the application's `on_open` hook, if present.
4. Initializes the connection's request state.
5. Adds it to the linked list used for shutdown management.
6. Updates connection statistics.

The new connection begins in the `HEADER` stage.

fileciteturn0file2L42-L84

If allocation fails or the client limit is reached, `conn_add()` rejects and closes the socket.

If successful\:

```c
if (c) {
    enqueue(&head, &tail, c);
}
```

We add the new client to the application ready queue.

Why?

Because the newly accepted client needs its first receive operation.

It will be submitted during the next event-loop iteration.

### Complete accept flow

```text
Accept SQE submitted
       |
       v
Accept CQE arrives
       |
       v
Clear accepting
       |
       v
Is cqe->res >= 0?
       |
       v
Set TCP_NODELAY
       |
       v
conn_add()
       |
       v
Initialize conn_t
       |
       v
Enqueue client
       |
       v
Next iteration prepares RECV
```

---

# 10\. What if accept fails?

Your code\:

```c
else if (
    n != -EAGAIN &&
    n != -EINTR &&
    n != -ECONNABORTED
) {
    result = n;
    goto shutdown;
}
```

This is an important error-handling condition.

Let's understand these errors.

| Error | Meaning | Your response |
|--------|--------|--------|
| `-EAGAIN` | No connection currently available to accept | Ignore and retry |
| `-EINTR` | Operation interrupted | Ignore and retry |
| `-ECONNABORTED` | A pending connection was aborted | Ignore and retry |
| Other negative error | Unexpected accept failure | Start shutdown |

Why ignore these three?

Because they are conditions that don't necessarily indicate that the server is broken.

For example, an incoming connection can be aborted before it is accepted.

The server should not terminate just because one connection failed.

For an unexpected error, you store the error and jump to\:

```c
shutdown:
    drain(s, ring, outstanding, &accept_tag);
```

We'll study `drain()` in a separate lesson.

---

# 11\. Handling ordinary client completions

Now look at the `else` branch\:

```c
else {
    conn_t *c = data;

    c->pending = 0;

    int status = conn_complete(c, n);

    if (status) {
        conn_close(c, status);
    } else {
        enqueue(&head, &tail, c);
    }
}
```

This is where your engine interacts with the common TCP framework.

Let's break it down.

## 11.1 Recover the client pointer

```c
conn_t *c = data;
```

We already know that this completion isn't the accept operation.

Therefore, `user_data` contains the pointer to the client whose operation has completed.

## 11.2 Clear pending

```c
c->pending = 0;
```

The particular operation has completed.

The engine can now safely manage that client's buffer again, provided there isn't another outstanding request referencing it.

Remember, your design permits only one outstanding receive or send operation per TCP client.

## 11.3 Pass the result to the framework

```c
int status = conn_complete(c, n);
```

This is a very important architectural decision.

Your io\_uring engine does not implement a separate request-processing state machine.

It passes the completion result to the framework.

Let's understand what the framework does.

Suppose\:

```text
Client A is in HEADER stage
want = 8 bytes
done = 0
```

Its receive completes\:

```text
n = 8
```

The framework updates\:

```text
done = 8
```

Then it decodes the 8-byte request length and determines how much body data should be received and how large the response should be.

Depending on the request, it changes the connection stage to `RECEIVE` or `SEND`\.

Similarly, during SEND, it tracks how many response bytes remain.

fileciteturn0file2L190-L255

## 11.4 Interpret the framework's return value

`conn_complete()` can return\:

| Return | Meaning |
|--------|--------|
| `0` | Connection remains valid |
| `1` | Clean client closure between requests |
| Negative | Connection error |

What does your engine do?

```c
if (status) {
    conn_close(c, status);
}
```

Any nonzero status causes the client to be closed.

Otherwise\:

```c
enqueue(&head, &tail, c);
```

The client is scheduled for its next operation.

For example\:

```text
Client A

RECV completes
      |
      v
conn_complete()
      |
      v
Stage changes to SEND
      |
      v
Enqueue Client A
      |
      v
Next iteration
      |
      v
Prepare SEND SQE
```

This is how the client moves between receive and send operations without the engine having to duplicate the framework's protocol logic.

---

# 12\. Complete TCP event-loop logic

Now let's put the entire function together conceptually.

**Start event-loop iteration**

**1\. Ensure an accept is pending**

Prepare ACCEPT SQE if not already accepting and an SQE is available.

**2\. Schedule ready clients**

Prepare RECV or SEND SQEs until the ready queue is empty or the SQ fills.

**3\. Submit operations**

Call counted\_submit() to publish the batch.

**4\. Wait or peek**

If ready clients remain, only peek. Otherwise use counted\_wait().

**5\. Process up to 256 CQEs**

Identify accept/client operations, update state, close invalid clients or re-enqueue valid ones.

**Next iteration**

When the loop ends\:

```c
shutdown:
    drain(s, ring, outstanding, &accept_tag);
    return result;
```

The engine doesn't simply abandon pending operations.

It calls `drain()` to cancel or collect outstanding work before returning.

We'll study the cancellation logic separately because it involves some important memory-safety and completion-accounting issues.

---

# 13\. Let's simulate a complete request

This example ties together everything we have learned.

Suppose Client A connects and sends an echo request.

Its request contains\:

```text
8-byte length header
4-byte body: "TEST"
```

Your server is configured with the echo application.

### Stage 1: Accept

The server submits\:

```text
ACCEPT SQE
user_data = &accept_tag
```

The CQE returns\:

```text
res = 12
```

Suppose FD 12 is the new connection.

The engine calls\:

```c
conn_add(s, 12);
```

The connection starts in HEADER stage and is enqueued.

### Stage 2: Receive header

The engine prepares\:

```text
RECV SQE
FD = 12
Length = 8
user_data = conn_t pointer
```

The receive completes with\:

```text
res = 8
```

The framework decodes the header.

It determines\:

```text
body_left = 4
response_left = 4
stage = RECEIVE
```

The client is re-enqueued.

### Stage 3: Receive body

Another receive operation is submitted\:

```text
RECV SQE
FD = 12
Length = 4
```

It completes\:

```text
res = 4
```

The framework now changes its stage\:

```text
RECEIVE → SEND
```

For echo, the received body is already in the buffer, so no additional response-generation function is needed.

The client is re-enqueued again.

### Stage 4: Send response

The engine prepares\:

```text
SEND SQE
FD = 12
Buffer = "TEST"
Length = 4
Flags = MSG_NOSIGNAL
```

The send completes\:

```text
res = 4
```

The framework\:

- Records the written bytes.
- Decreases the response bytes remaining.
- Increments the completed message count.
- Resets the client to HEADER stage.

The client is re-enqueued for its next request.

**One complete echo request**

1. **ACCEPT**
   
   New connection accepted; `conn_t` created.
2. **RECV header**
   
   Read 8-byte request length.
3. **RECV body**
   
   Read 4-byte payload.
4. **SEND response**
   
   Return the same 4-byte payload.
5. **Reset to HEADER**
   
   Increment message count and prepare for the next request.

Notice that the engine performs no direct `recv()` or `send()` calls for these TCP operations. It prepares SQEs and processes their CQE results.

---

# 14\. Important implementation observations

Before moving on, there are some details you should be prepared to explain.

| Design choice | Reason |
|--------|--------|
| One accept in flight | Simpler management of incoming connections |
| FIFO ready queue | Schedule clients waiting for their next operation |
| One operation per TCP client | Prevent concurrent use of the same connection buffer |
| `user_data = c` | Identify the client when its CQE arrives |
| `pending = 1` | Track ownership of the client buffer |
| Batching | Submit operations for multiple clients together |
| Wait only when `head == NULL` | Avoid stalling while clients remain unscheduled |
| Reap at most 256 CQEs per iteration | Bound the completion-processing batch |
| Shared `conn_complete()` | Keep protocol state-machine behavior identical across engines |

One subtle point about the last two\:

The 256-CQE processing limit is a scheduling decision. It doesn't mean the completion queue can only contain 256 entries.

Your CQ can be 16,384 entries under the default configuration.

---

# 15\. Check your understanding

These are based directly on your implementation. Try explaining them without referring to the code.

**TCP event-loop quiz** (/6 answered)

## What remains in your implementation walkthrough?

We've now covered the complete main `run_tcp()` event loop.

The remaining major sections of `io_uring_server.c` are\:

1. **`drain()`** - cancellation, outstanding operations, CQE collection and safe shutdown.
2. **UDP implementation** - `udp_slot_t`, `post_udp_recv()`, `post_udp_send()` and `run_udp()`\.
3. **Syscall accounting** - deeper examination of `counted_submit()` and `counted_wait()` against your benchmark results.
4. **Performance and edge cases** - CQ overflow, SQ stalls, partial TCP sends/receives, and the design trade-offs.

I recommend studying `drain()` next. It is arguably the most important correctness-related part of your implementation because it protects against freeing memory while the kernel still has pending access to it.

## Prompt:
08/11/2026, 2:59:16 am

ok, explain the remaining parts

## Response:
08/11/2026, 2:59:21 am · gpt-6-mini

> **Explaining drain behavior**
>
> I’m clarifying that cancellation can race with newly completing work, so the loop keeps draining until all outstanding operations—including cancellation requests—reach completion. UDP slots then alternate receive, handle, send, and repost.
>
> **Explained drain behavior**
>
> 
>
> **Retrieved remaining code sections and exact search snippets**
>
> 
>
> Worked for 1m 45s

Let's finish the implementation walkthrough, focusing on the remaining code in your `io_uring_server.c`\.

We'll cover **shutdown and cancellation first**, then the **UDP engine**, and finally connect the implementation to syscall accounting and the performance issues documented in your README.

The shutdown section is especially important: an io\_uring operation can still be using a client's buffer after your application has decided to stop. Your engine must handle that safely.

# 1\. `drain()` - safe shutdown and cancellation

Your implementation\:

```c
static void drain(
    server_t *s,
    struct io_uring *ring,
    unsigned outstanding,
    const void *accept_tag
)
```

The function's purpose is\:

**Cancel outstanding operations and collect their completions before allowing memory used by those operations to be freed.**

Why is this necessary?

Imagine this sequence\:

1. You submit a receive operation for Client A.
2. The kernel has not completed it yet.
3. The user presses Ctrl+C.
4. Your server starts shutting down.
5. You free Client A's buffer.

But the kernel might still be waiting to write received data into that buffer.

If the buffer is freed while the kernel still references it, you have a serious memory-safety problem.

**The unsafe shutdown scenario**

Kernel has pending RECV using Client A's buffer

Application frees Client A's buffer

Kernel may still access that memory

**Potential use-after-free / memory corruption**

Your `drain()` prevents this by waiting until all tracked operations have been accounted for.

## 1.1 First, get an SQE for cancellation

```c
struct io_uring_sqe *sqe;
static __thread int cancel_tag;

while ((sqe = io_uring_get_sqe(ring)) == NULL) {
    if (counted_submit(s, ring) < 0) {
        goto broken;
    }
}
```

You need an SQE because cancellation itself is an io\_uring operation.

If the SQ is full, your code attempts to submit already prepared entries to make room.

Once an SQE is available\:

```c
io_uring_prep_cancel(sqe, NULL, CANCEL_EVERYTHING);
io_uring_sqe_set_data(sqe, &cancel_tag);
outstanding++;
```

Let's understand the arguments.

**`io_uring_prep_cancel()`**

Prepares an asynchronous cancellation request.

**Second argument: `NULL`**

This is the cancellation target's `user_data` address.

Here, `NULL` combined with the cancellation flags means the request is intended to cancel all matching operations, rather than just one operation identified by a particular `user_data`\.

**`CANCEL_EVERYTHING`**

Earlier, your code defines\:

```c
#define CANCEL_EVERYTHING \
    (IORING_ASYNC_CANCEL_ANY | IORING_ASYNC_CANCEL_ALL)
```

These flags request broad cancellation\:

- `IORING_ASYNC_CANCEL_ANY`\: cancel any matching request.
- `IORING_ASYNC_CANCEL_ALL`\: cancel all matching requests rather than stopping after one.

The cancellation operation itself will eventually produce a CQE.

That's why you also do\:

```c
outstanding++;
```

The cancellation request must be included in the count.

**Important\:** Submitting a cancellation request does not mean every outstanding operation has already stopped. We still need to collect and account for completions.

## 1.2 Wait until all operations are accounted for

Your code\:

```c
while (outstanding) {
    int rc = counted_submit(s, ring);

    if (rc < 0 && rc != -EINTR) {
        goto broken;
    }

    struct io_uring_cqe *cqe;

    struct __kernel_timespec timeout = {
        .tv_sec = 1,
        .tv_nsec = 0
    };

    rc = io_uring_wait_cqe_timeout(ring, &cqe, &timeout);
```

The loop continues as long as\:

```c
outstanding != 0
```

Each iteration\:

1. Submits pending cancellation requests or other prepared SQEs.
2. Waits for a CQE, with a one-second timeout.
3. Processes the completion.
4. Decrements `outstanding`\.

Why a one-second timeout?

It prevents the shutdown procedure from waiting indefinitely in one wait call without getting an opportunity to retry cancellation.

## 1.3 What if the wait is interrupted?

```c
if (rc == -EINTR) {
    continue;
}
```

The wait was interrupted by a signal.

The engine simply tries again.

## 1.4 What if the wait times out?

This is particularly important\:

```c
if (rc == -ETIME) {
    while ((sqe = io_uring_get_sqe(ring)) == NULL) {
        if (counted_submit(s, ring) < 0) {
            goto broken;
        }
    }

    io_uring_prep_cancel(sqe, NULL, CANCEL_EVERYTHING);
    io_uring_sqe_set_data(sqe, &cancel_tag);
    outstanding++;

    continue;
}
```

Why submit another cancellation request?

Because an operation may have started or become visible to the kernel after the earlier cancellation request was processed.

The first cancellation request is not a guarantee that every operation that might subsequently be submitted has been cancelled.

So your code retries cancellation after a timeout.

This is a defensive shutdown strategy.

## 1.5 Process each completion during shutdown

Once a CQE arrives\:

```c
void *data = io_uring_cqe_get_data(cqe);
int result = cqe->res;

io_uring_cqe_seen(ring, cqe);
outstanding--;
```

This is familiar\:

- Recover `user_data`\.
- Read the operation result.
- Mark the CQE consumed.
- Decrement the outstanding count.

Now comes an important distinction.

```c
if (!accept_tag || data == &cancel_tag) {
    continue;
}
```

If `accept_tag` is NULL, this is the UDP shutdown path.

UDP doesn't use the TCP connection state machine.

If the completion belongs to the cancellation request, there is also nothing else to do.

The cancellation request's own completion has already been accounted for.

### Case A: TCP accept completed during shutdown

```c
if (data == accept_tag) {
    if (result >= 0) {
        close(result);
    }
}
```

Suppose a client connects at almost exactly the same time the server is shutting down.

The accept operation may complete successfully.

But we don't want to start serving that client because the server is stopping.

So the returned FD is immediately closed.

### Case B: TCP receive/send completed during shutdown

```c
else if (result > 0) {
    conn_t *c = data;
    c->pending = 0;
    conn_complete(c, result);
}
```

Why call `conn_complete()` during shutdown?

This is a subtle but important detail.

Suppose a response was already sent successfully, but its CQE had not yet been collected when shutdown started.

The client may have received the complete response.

If your server simply discards that successful completion, its message counters may disagree with what actually happened.

So your code still feeds successful positive-byte completions into the framework's state machine.

But it doesn't enqueue the client for another operation.

This is accounting and state consistency, not a restart of normal request processing.

For other results\:

```c
else {
    ((conn_t *)data)->pending = 0;
}
```

The operation is finished, so the client is no longer marked as pending.

## 1.6 What if the ring is broken?

```c
broken:
    _exit(1);
```

Your comment explains the reason\:

```c
/* A broken ring cannot prove the buffers are safe to free. */
```

This is a deliberate safety decision.

If the engine cannot establish that the kernel has stopped accessing the buffers, it cannot safely continue ordinary cleanup.

`_exit(1)` terminates the process immediately rather than returning to code that might free memory still referenced by the kernel.

**The complete shutdown principle**

1. Stop scheduling new normal work.
2. Submit cancellation.
3. Collect all outstanding operation completions.
4. Close accepted connections that arrived during shutdown.
5. Account for successful TCP completions.
6. Only after draining can the engine safely return to ordinary cleanup.

One important detail: `drain()` does not itself free every `conn_t`\. It ensures outstanding operations are no longer using their buffers. The framework's later cleanup can then safely close and free the connection objects.

fileciteturn1file1

---

# 2\. UDP implementation

Now let's move to the UDP part of your engine.

TCP and UDP are different here.

With TCP, you have a separate connected socket FD for every client.

With UDP, you generally use one socket to receive datagrams from many different peers.

Your implementation uses a pool of reusable slots.

## 2.1 Understanding `udp_slot_t`

Your code\:

```c
typedef struct udp_slot_t {
    struct sockaddr_in peer;
    struct iovec iov;
    struct msghdr msg;

    unsigned char in[MAX_DATAGRAM];
    unsigned char out[MAX_DATAGRAM];

    int sending;
} udp_slot_t;
```

This structure represents one UDP operation slot.

Let's understand every field.

| Field | Purpose |
|--------|--------|
| `peer` | Stores the sender's IP address and port |
| `iov` | Describes the data buffer for `recvmsg()` or `sendmsg()` |
| `msg` | Holds the message metadata used by `recvmsg()`/`sendmsg()` |
| `in` | Buffer into which an incoming datagram is received |
| `out` | Buffer containing the response datagram |
| `sending` | Indicates whether this slot is currently in the send phase |

The two buffers are important.

```text
Incoming datagram
       |
       v
slot->in
       |
       v
udp_handle()
       |
       v
slot->out
       |
       v
Outgoing datagram
```

Unlike TCP, UDP preserves message boundaries.

If one datagram arrives containing 100 bytes, it is one datagram of 100 bytes. You don't need to assemble a TCP-style byte stream or track a multi-byte request header across multiple receives.

## 2.2 Why do you need multiple slots?

Your configuration has\:

```c
int depth = s->config->udp_depth;
```

Suppose\:

```text
udp_depth = 4
```

You allocate four slots\:

```c
udp_slot_t *slots = calloc(
    (size_t)depth,
    sizeof(*slots)
);
```

Conceptually\:

**Four UDP slots**

**Slot 0**

Receive buffer

Send buffer

(One operation)

**Slot 1**

Receive buffer

Send buffer

(One operation)

**Slot 2**

Receive buffer

Send buffer

(One operation)

**Slot 3**

Receive buffer

Send buffer

(One operation)

Each slot cycles independently through receive and send.

All four receive operations can be outstanding on the same UDP socket.

This gives the kernel multiple receive requests to work with.

But more slots are not always better. More on that shortly.

---

# 3\. `post_udp_recv()` - preparing a UDP receive

Your function\:

```c
static int post_udp_recv(
    struct io_uring *ring,
    server_t *s,
    udp_slot_t *slot,
    unsigned *outstanding
)
```

The first step\:

```c
struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
```

It tries to get a submission entry.

If none is available\:

```c
if (!sqe) {
    counted_submit(s, ring);
    sqe = io_uring_get_sqe(ring);

    if (!sqe) {
        return -EBUSY;
    }
}
```

It tries to submit existing SQEs to make room, then tries again.

If it still cannot get one, it returns `-EBUSY`\.

Now\:

```c
slot->sending = 0;
```

This slot is entering the receive phase.

Next\:

```c
slot->iov = (struct iovec) {
    .iov_base = slot->in,
    .iov_len = MAX_DATAGRAM
};
```

This says\:

- Start receiving into `slot->in`\.
- The buffer can hold up to `MAX_DATAGRAM` bytes.

Then\:

```c
slot->msg = (struct msghdr) {
    .msg_name = &slot->peer,
    .msg_namelen = sizeof(slot->peer),
    .msg_iov = &slot->iov,
    .msg_iovlen = 1
};
```

This is the metadata for the receive operation.

The key part is\:

```c
.msg_name = &slot->peer
```

It tells the kernel where to write the sender's address information.

This is important because the server needs to know where to send the response.

Finally\:

```c
io_uring_prep_recvmsg(
    sqe,
    s->listener,
    &slot->msg,
    0
);
```

Here\:

- `s->listener` is the UDP socket.
- `slot->msg` describes the receive buffers and peer-address storage.
- `0` means no additional receive flags are requested.

Then\:

```c
io_uring_sqe_set_data(sqe, slot);
(*outstanding)++;
```

The slot pointer is stored in `user_data`, so the completion handler can recover the correct slot.

`outstanding` is incremented because a new operation is being tracked.

fileciteturn1file3

---

# 4\. `post_udp_send()` - preparing a UDP response

Your function\:

```c
static int post_udp_send(
    struct io_uring *ring,
    server_t *s,
    udp_slot_t *slot,
    size_t length,
    unsigned *outstanding
)
```

It also obtains an SQE, with the same retry pattern as `post_udp_recv()`\.

Then\:

```c
slot->sending = 1;
```

This slot is now in the send phase.

Next\:

```c
slot->iov = (struct iovec) {
    .iov_base = slot->out,
    .iov_len = length
};
```

This time the data comes from the outgoing buffer.

Then\:

```c
slot->msg.msg_iov = &slot->iov;
slot->msg.msg_iovlen = 1;
slot->msg.msg_namelen = sizeof(slot->peer);
```

The message structure is updated to describe the outgoing data and the peer address.

Finally\:

```c
io_uring_prep_sendmsg(
    sqe,
    s->listener,
    &slot->msg,
    MSG_NOSIGNAL
);
```

Notice something interesting\:

**The same UDP socket is used for receiving and sending.**

The destination address is stored in\:

```c
slot->peer
```

This allows the server to send the response back to the sender of the original datagram.

Then\:

```c
io_uring_sqe_set_data(sqe, slot);
(*outstanding)++;
```

Again, the slot pointer identifies the operation when the CQE arrives.

fileciteturn2file1

---

# 5\. `run_udp()` - the UDP event loop

Now let's connect the two helper functions.

## 5.1 Allocate and initially post receive operations

```c
int depth = s->config->udp_depth;

udp_slot_t *slots = calloc(
    (size_t)depth,
    sizeof(*slots)
);
```

Then\:

```c
unsigned outstanding = 0;
int result = 0;

for (int i = 0; i < depth && !result; i++) {
    result = post_udp_recv(
        ring,
        s,
        &slots[i],
        &outstanding
    );
}
```

Suppose `depth = 4`\.

The loop prepares\:

```text
Slot 0 → RECV
Slot 1 → RECV
Slot 2 → RECV
Slot 3 → RECV
```

So\:

```text
outstanding = 4
```

These requests are prepared, and then the main event loop begins.

## 5.2 Main loop

```c
while (!stop_requested && !result) {
    int rc = counted_submit(s, ring);
```

The prepared receive operations are submitted.

Then\:

```c
struct io_uring_cqe *cqe;
s->loop_calls++;

rc = counted_wait(s, ring, &cqe);
```

Unlike the TCP loop, the UDP loop doesn't have a separate application-side ready queue.

There are no TCP clients waiting for unscheduled follow-up operations.

Each UDP slot already has a clearly defined operation to perform.

Therefore, it can wait for a completion.

Timeout and interruption handling are the same general pattern as TCP.

## 5.3 Process UDP completions

Your code\:

```c
for (
    int i = 0;
    i < RING_DEPTH && io_uring_peek_cqe(ring, &cqe) == 0;
    i++
) {
    udp_slot_t *slot = io_uring_cqe_get_data(cqe);
    int n = cqe->res;

    io_uring_cqe_seen(ring, cqe);
    outstanding--;
```

The important difference from TCP\:

```c
udp_slot_t *slot = io_uring_cqe_get_data(cqe);
```

The `user_data` contains a `udp_slot_t *`, not a `conn_t *`\.

Why?

Because the operation belongs to a reusable UDP slot, not an individual TCP connection.

Now the engine checks\:

```c
if (slot->sending)
```

This tells us whether the completion belongs to a send or receive.

### Case A: Send completion

```c
if (slot->sending) {
    if (n < 0) {
        s->datagrams_dropped++;
    } else {
        s->bytes_written += (uint64_t)n;
    }

    result = post_udp_recv(
        ring,
        s,
        slot,
        &outstanding
    );
}
```

If the send failed, increment the dropped-datagram counter.

Otherwise, update the number of bytes written.

Then the slot is reused for another receive.

### Case B: Receive completion

```c
else if (n >= 0) {
    ssize_t reply = udp_handle(
        s,
        slot->in,
        (size_t)n,
        slot->out
    );
```

The incoming datagram is passed to the framework's UDP handler.

It receives\:

- The server state.
- Incoming data buffer.
- Number of received bytes.
- Outgoing response buffer.

The handler returns the response length.

There are two possibilities.

**If the handler returns a negative value\:**

```c
result = post_udp_recv(...);
```

No response is scheduled; the slot is reused to receive another datagram.

**If the handler returns a non-negative value\:**

```c
result = post_udp_send(
    ring,
    s,
    slot,
    (size_t)reply,
    &outstanding
);
```

The slot is scheduled to send the response.

### Case C: Receive returned a transient error

```c
else if (n == -EINTR || n == -EAGAIN) {
    result = post_udp_recv(
        ring,
        s,
        slot,
        &outstanding
    );
}
```

The receive is retried.

Other negative results are treated as fatal\:

```c
else {
    result = n;
}
```

At the end of shutdown\:

```c
drain(s, ring, outstanding, NULL);
free(slots);
return result;
```

Notice the order\:

1. Drain all outstanding operations.
2. Free the slot array.

That ordering is critical.

The kernel may still be using `slot->in`, `slot->out`, or `slot->msg`\. We cannot free the array before draining.

fileciteturn1file3

---

# 6\. Visualizing the UDP slot lifecycle

This is the central idea of your UDP engine.

**Slot is ready**

**RECVMSG**

Receive datagram and sender address

**udp\_handle()**

Process request and prepare response

**Response exists**

**SENDMSG**

**No response**

**RECVMSG**

**Reuse the slot**

One slot handles one datagram at a time.

It doesn't receive a second datagram into its buffers while the previous send is still using them.

This is the same buffer-ownership principle we discussed for TCP.

---

# 7\. Why does UDP receive depth matter?

Your README includes measurements for different UDP depths.

| UDP depth | 10 flows (req/s) | 100 flows (req/s) |
|--------|--------|--------|
| 1 | 196,649 | 199,200 |
| 4 | 221,694 | 228,194 |
| 32 | 184,225 | 237,249 |

These are results from your project's benchmark runs, not universal performance guarantees. fileciteturn2file0

Let's understand why depth changes performance.

### Depth = 1

Only one receive is waiting.

```text
One socket
    |
    └── One pending RECV
```

This limits how much receive work can be outstanding at once.

It also means little opportunity to pipeline multiple datagrams.

### Depth = 4

Four receive operations can be waiting.

This gives more opportunity to process incoming datagrams without having to wait for the application to prepare a new receive after every datagram.

In your measurements, depth 4 was a good compromise.

### Depth = 32

Now there are 32 outstanding receives on the same socket.

This provides more pipelining, but introduces an interesting cost.

Your code comment explains\:

> Every waiting receive is woken when a datagram arrives.

Think about it.

Suppose 32 receives are waiting on the same socket, and one datagram arrives.

The kernel may wake multiple waiting receive operations to contend for that datagram. Only one can consume it; the others may have to retry or remain waiting.

This creates unnecessary wakeups and processing.

So depth 32 can be useful under heavier traffic, but wasteful when traffic is light.

**The takeaway\:** More outstanding operations do not automatically mean better performance. The right depth depends on traffic intensity and the workload.

---

# 8\. Syscall accounting - how your engine measures kernel entries

Your implementation tracks several counters in `server_t`\.

The important ones include\:

- `sc_enter`
- `sc_wait`
- `sc_accept`
- `sc_recv`
- `sc_send`
- `sc_ctl`

For your io\_uring engine, the key counter is\:

```c
sc_enter
```

Why?

Because `io_uring_enter` is the system call used to communicate with the kernel for submission and waiting in ordinary io\_uring operation.

Your helper\:

```c
counted_submit(s, ring)
```

tracks submissions that require a kernel entry.

Your helper\:

```c
counted_wait(s, ring, &cqe)
```

tracks the waiting path.

## 8.1 Why count syscalls instead of just counting operations?

Suppose a server handles 100 requests.

Imagine two designs\:

**Design A\:**

```text
100 requests
100 receive syscalls
100 send syscalls
100 wait-related syscalls
```

**Design B\:**

```text
100 requests
100 receive operations
100 send operations
10 submission/wait kernel entries
```

Both may perform similar amounts of actual network I/O, but Design B batches operations and reduces application-to-kernel transitions.

This is one of the main motivations for io\_uring.

However, be careful with the interpretation\:

**One `io_uring_enter` call can submit many SQEs.** Therefore, counting `sc_enter` is not the same as counting the number of I/O operations.

Also, SQPOLL can consume SQEs without a normal submission syscall when the polling thread is awake. Your `counted_submit()` accounts for the cases where a kernel entry is actually needed, including the `IORING_SQ_NEED_WAKEUP` condition.

## 8.2 What about `sc_recv`, `sc_send`, and `sc_accept`?

These are useful for comparing the engines in the project.

For the io\_uring engine, you prepare receive, send and accept SQEs rather than directly invoking ordinary `recv()`, `send()` and `accept()` calls in the TCP event loop.

Therefore, those counters should not be interpreted as a count of every logical receive, send or accept operation across all engines.

They are useful as part of the framework's cross-engine syscall accounting, but `sc_enter` is the important kernel-entry counter for your io\_uring path.

---

# 9\. Connect the code to the performance findings in your README

There are three particularly useful observations to understand for your viva.

## 9.1 Completion queue sizing

Your implementation doesn't just use the default completion queue size.

It calculates\:

```c
size_t needed =
    s->config->max_clients +
    (size_t)s->config->udp_depth + 64;
```

Then it selects a power-of-two CQ size, starting at\:

```c
unsigned entries = 2 * RING_DEPTH;
```

Why?

Because a large number of clients can have outstanding operations.

If the CQ is too small, completions can accumulate faster than the application consumes them. That can cause overflow and a slower kernel fallback path.

Your README documents an overflow problem with the smaller default CQ configuration and the improvement from increasing the CQ size. fileciteturn1file4

One limitation worth remembering: your loop caps the CQ size at 65,536 entries. So the calculation is a sizing strategy, not an unlimited guarantee for every possible configuration.

## 9.2 The SQ stall fix

This is the earlier fix we studied in `run_tcp()`\.

Your original behavior could block while clients remained in the application ready queue.

That was dangerous because some of those clients needed their next operations submitted to make progress.

Your fix\:

```c
if (head) {
    rc = io_uring_peek_cqe(ring, &cqe) == 0 ? 0 : -ETIME;
} else {
    rc = counted_wait(s, ring, &cqe);
}
```

The result documented in your README was a dramatic increase in throughput in the particular Docker test\:

**Before the fix**

# \~5k

**After the fix**

# 440-490k

Requests per second, as reported in your README's test.

This is a great example of why event-loop scheduling logic matters as much as the underlying I/O API.

## 9.3 SQPOLL versus SQPOLL busy polling

Your code supports two related modes.

| Mode | Who polls the SQ? | How does the application wait for CQEs? |
|--------|--------|--------|
| Normal io\_uring | Kernel processes submissions when entered | Waits normally |
| SQPOLL | Dedicated kernel thread polls submissions | Usually waits for completions |
| SQPOLL + busy polling | Dedicated kernel thread polls submissions | Application busy-polls completions |

Your README's results show that SQPOLL alone was slower than plain io\_uring in some of your tested workloads, despite eliminating ordinary submission syscalls.

Why?

Because the application still sleeps waiting for completions and can be woken up repeatedly.

With busy polling, those wakeups can be reduced, but the application consumes a CPU core continuously.

The important trade-off is\:

- Lower syscall/wakeup overhead.
- Higher CPU consumption.

Your README reports approximately two CPU cores' worth of usage for SQPOLL plus busy polling in the tested setup. fileciteturn2file0

---

# 10\. Final connection: `engine_run()`

We covered this earlier, but it is useful to connect everything.

Your `engine_run()` function is the entry point for the io\_uring engine.

Its responsibilities are\:

1. Create a local `struct io_uring`\.
2. Initialize the ring using `ring_setup()`\.
3. Check whether initialization succeeded.
4. Choose the appropriate event loop\:
   
   - `run_tcp()` for TCP.
   - `run_udp()` for UDP.
5. Exit the ring using `io_uring_queue_exit()`\.
6. Return the result.

Conceptually\:

```text
engine_run()
    |
    v
ring_setup()
    |
    v
Ring initialized?
    |
    +---- No ---> Return error
    |
    v
Choose transport
    |
    +---- TCP ---> run_tcp()
    |
    +---- UDP ---> run_udp()
                     |
                     v
                 drain()
    |
    v
io_uring_queue_exit()
    |
    v
Return result
```

Each framework worker thread gets its own `server_t` and its own ring. This means the workers don't share one submission/completion queue.

The ring is cleaned up after the corresponding event loop returns.

---

# 11\. Complete implementation summary

You've now covered the important parts of your uploaded `io_uring_server.c`\.

Here's the complete picture of what you implemented\:

| Component | What it does |
|--------|--------|
| `completion_entries()` | Calculates a suitable CQ capacity |
| `ring_setup()` | Initializes the ring and optional SQPOLL configuration |
| `counted_submit()` | Submits prepared operations and tracks kernel entries |
| `counted_wait()` | Waits for completions or busy-polls |
| `enqueue()` | Maintains the TCP application-side ready queue |
| `run_tcp()` | Handles asynchronous TCP accept, receive, send and completions |
| `drain()` | Cancels and accounts for outstanding operations during shutdown |
| `udp_slot_t` | Holds reusable UDP operation state and buffers |
| `post_udp_recv()` | Prepares an asynchronous UDP receive |
| `post_udp_send()` | Prepares an asynchronous UDP response |
| `run_udp()` | Runs the UDP receive/send slot lifecycle |
| `engine_run()` | Initializes, dispatches and cleans up the engine |

## The most important concepts to retain for your viva

1. **SQE vs CQE\:** SQEs describe operations; CQEs report their results.
2. **`user_data`\:** Connects a completion to the operation's client or UDP slot.
3. **Buffer lifetime\:** Never free or reuse a buffer while an outstanding operation may access it.
4. **TCP ready queue\:** Holds clients waiting for their next operation to be submitted.
5. **Batching\:** Multiple operations can be submitted together.
6. **Completion processing\:** The application processes results and schedules subsequent operations.
7. **SQ stall fix\:** Never block while unscheduled clients remain in the ready queue.
8. **CQ sizing\:** Must account for the number of operations that can be outstanding.
9. **Cancellation\:** Shutdown must account for every outstanding operation before freeing its resources.
10. **UDP depth\:** Trades receive pipelining against unnecessary wakeups and CPU work.
11. **SQPOLL\:** Reduces normal submission syscall requirements but has CPU and wakeup trade-offs.

---

## What I recommend doing now

Before moving on to interview preparation, make sure you can explain these three things independently\:

- Draw the complete lifecycle of a TCP request through your engine.
- Explain why your `drain()` function is necessary and what can go wrong without it.
- Explain how one UDP slot cycles through `recvmsg → sendmsg → recvmsg`\.

These three topics cover a large portion of the actual implementation and its most important correctness decisions.

Once you've worked through those, we can do a **code-focused io\_uring viva**, where I'll ask questions about your actual implementation, including tricky edge cases and design trade-offs.

## Prompt:
08/11/2026, 3:03:36 am

can you give me a readme for my implementation like specific to what has been done as this reamde is very extensive and it contains design decisions and questions everything. I need one readme specifically to submit.

## Response:
08/11/2026, 3:03:40 am · gpt-6-mini

> **Wrote implementation README and integrated io_uring engine details**
>
> 
>
> Worked for 29s

Yes. The existing README is more of a complete project report and viva-preparation document. For submission, I'd use a **short, implementation-focused README** that documents what your code actually implements, its architecture, configuration and important implementation details-without the theory lessons, experimental discussions, design debates or viva questions.

Here is a standalone version based on your `io_uring_server.c` and the shared framework.

**document: **

# io_uring Network I/O Engine

## 1. Overview

This component implements the `io_uring`-based network I/O engine for the CS331 project, **Scalable Network I/O: From select to io_uring**.

The implementation uses Linux's `io_uring` interface through **liburing** to perform asynchronous network operations. It supports both TCP and UDP transports and integrates with the project's shared networking framework.

The engine supports three operating modes:

- **Plain io_uring:** Standard submission and completion processing.
- **SQPOLL:** Uses a kernel thread to poll the Submission Queue.
- **SQPOLL with busy polling:** Combines SQPOLL with userspace polling of the Completion Queue.

## 2. Implementation Structure

The implementation is contained in:

```text
io_uring_server.c
```

The engine follows the shared project architecture:

- **Engine:** Handles asynchronous socket operations and completion processing.
- **Framework:** Manages connection state, request processing, application callbacks, statistics and shutdown.
- **Application plugins:** Define the request and response behavior independently of the I/O engine.

The engine implements the framework interface:

```c
int engine_run(server_t *s);
```

Each server worker initializes and manages its own io_uring instance.

## 3. Ring Initialization and Configuration

The engine uses the following configuration constants:

| Parameter | Value | Purpose |
|---|---:|---|
| `RING_DEPTH` | 256 | Submission Queue depth |
| `SQPOLL_IDLE_MS` | 100 ms | SQPOLL thread idle timeout |
| `WAIT_MS` | 100 ms | Completion wait timeout |

Ring initialization is performed by `ring_setup()` using `io_uring_queue_init_params()`.

The implementation configures:

- `IORING_SETUP_CQSIZE` to explicitly configure the Completion Queue size.
- `IORING_SETUP_SQPOLL` when SQPOLL is enabled.
- `IORING_SETUP_SQ_AFF` when CPU affinity for the SQPOLL thread is specified.

The Completion Queue capacity is calculated dynamically by `completion_entries()`, based on the configured maximum number of clients, UDP receive depth and additional capacity for control operations.

The size is selected as a power of two, starting at twice the Submission Queue depth, with a maximum of 65,536 entries.

## 4. Submission and Completion Management

The implementation uses the liburing interface to prepare, submit and process asynchronous operations.

### Submission

`counted_submit()` handles submission of prepared SQEs using `io_uring_submit()`.

It also tracks kernel-entry counts and handles the SQPOLL wake-up condition using `IORING_SQ_NEED_WAKEUP`.

### Completion

`counted_wait()` checks for available completions using `io_uring_peek_cqe()` before waiting.

It supports:

- Immediate completion retrieval without an additional wait syscall.
- Timed completion waiting using `io_uring_wait_cqe_timeout()`.
- Busy polling when SQPOLL busy mode is enabled.

The engine uses `io_uring_cqe_get_data()` to retrieve the operation's associated `user_data` and `io_uring_cqe_seen()` to mark processed completions as consumed.

The `outstanding` counter tracks operations awaiting completion processing.

## 5. TCP Implementation

TCP handling is implemented in `run_tcp()`.

### Connection acceptance

The engine maintains at most one outstanding asynchronous accept operation at a time.

- Accept requests are prepared using `io_uring_prep_accept()`.
- The listening socket is used to accept incoming connections.
- Accepted sockets are configured with `SOCK_NONBLOCK` and `SOCK_CLOEXEC`.
- `TCP_NODELAY` is enabled on successfully accepted connections.
- New connections are initialized through the framework's `conn_add()` function.

### Ready queue

The implementation maintains an application-side FIFO ready queue using `head`, `tail` and the `ready_next` field of `conn_t`.

The queue stores connections that need their next I/O operation submitted.

This queue is separate from the kernel's Submission Queue.

### Receive and send operations

For each queued connection, the engine prepares one operation according to its current framework state:

- `io_uring_prep_recv()` for connections in the HEADER or RECEIVE stage.
- `io_uring_prep_send()` for connections in the SEND stage.

The engine uses the framework's `conn_buffer()`, `conn_length()` and `conn_is_reading()` functions to obtain the appropriate buffer, remaining length and operation type.

`MSG_NOSIGNAL` is used for send operations.

Each SQE stores the corresponding `conn_t` pointer in `user_data`. The connection's `pending` flag is set while its operation is outstanding.

### Completion processing

The engine processes completed operations from the Completion Queue.

- Accept completions create and enqueue new connections.
- Receive and send completions are passed to the framework's `conn_complete()` function.
- Connections that remain valid are re-enqueued for their next operation.
- Connections that complete with a closure or error are handled through `conn_close()`.

The framework maintains the request state machine, including partial reads, partial writes, request headers and response progress.

### Batching and queue-stall handling

The engine prepares operations for multiple ready connections before submitting them.

If the Submission Queue fills while connections remain in the application ready queue, the engine uses `io_uring_peek_cqe()` instead of blocking for a completion.

This allows it to process available completions and return to submitting the remaining queued operations, avoiding a stall caused by waiting while unscheduled work remains.

Completion processing is also bounded to `RING_DEPTH` entries per iteration.

## 6. UDP Implementation

UDP handling is implemented through `run_udp()` and two helper functions:

- `post_udp_recv()`
- `post_udp_send()`

### UDP slots

The engine allocates a configurable number of `udp_slot_t` structures based on `udp_depth`.

Each slot contains:

- Sender address information.
- An `iovec` and `msghdr` for message operations.
- An input buffer.
- An output buffer.
- A state flag indicating whether the slot is sending.

### Datagram processing

At startup, the engine posts `udp_depth` asynchronous receive operations on the UDP socket.

Each slot follows this lifecycle:

```text
RECVMSG
   |
   v
Process datagram using udp_handle()
   |
   v
SENDMSG
   |
   v
Post another RECVMSG
```

If no response is generated, the slot is directly reused for another receive.

The sender's address is stored during reception and used as the destination for the response.

The configurable UDP depth controls the number of receive operations maintained concurrently.

## 7. Shutdown and Resource Management

The engine uses `drain()` to handle outstanding operations during shutdown.

The shutdown procedure:

1. Prepares an asynchronous cancellation request using `io_uring_prep_cancel()`.
2. Submits cancellation requests.
3. Collects outstanding completion events.
4. Handles accepted sockets that complete during shutdown by closing them.
5. Updates TCP connection state for successful operations whose completions had not yet been processed.
6. Repeats cancellation after a timeout when necessary.

The `pending` state of TCP connections is cleared as their operations are accounted for.

The UDP slot array is freed only after draining outstanding operations.

Finally, `engine_run()` releases the ring using `io_uring_queue_exit()`.

## 8. Performance-Related Features

The implementation includes the following features relevant to performance:

- **Submission batching:** Multiple operations can be prepared and submitted together.
- **Completion batching:** Multiple CQEs are processed in one event-loop iteration.
- **Configurable Completion Queue size:** Reduces completion queue overflow under high connection counts.
- **SQPOLL:** Allows a kernel thread to consume submissions without requiring a normal submission syscall while it is active.
- **Busy polling:** Allows userspace to poll the Completion Queue instead of sleeping for completions.
- **UDP receive depth:** Allows the number of outstanding UDP receives to be configured.
- **CPU affinity:** Supports assigning the SQPOLL thread to a specified CPU.

## 9. Statistics and Instrumentation

The engine integrates with the framework's statistics and syscall counters.

It tracks or contributes to measurements including:

- Number of event-loop iterations.
- Bytes read and written.
- Accepted, closed and rejected connections.
- Dropped UDP datagrams.
- io_uring kernel-entry and completion-wait counts.

The `counted_submit()` and `counted_wait()` helpers account for relevant kernel entries in the io_uring execution path.

These counters are used by the shared framework for reporting and performance evaluation.

## 10. Dependencies and Integration

The implementation requires:

- Linux with io_uring support (introduced in Linux 5.1).
- GCC or a compatible C compiler.
- liburing development headers and library.
- The project's shared `framework.h` and `framework.c` components.

The io_uring engine is compiled and linked as part of the project's shared build system.

From the integrated repository, the project build command is:

```bash
cd Chirag/code
make
```

The shared framework provides the runtime configuration, including transport selection, SQPOLL options, UDP receive depth, thread configuration and other server settings.

Relevant io_uring options include:

| Option | Description |
|---|---|
| `--sqpoll 1` | Enable SQPOLL mode |
| `--sqpoll-busy 1` | Enable busy polling with SQPOLL |
| `--udp-depth N` | Configure the number of outstanding UDP receive slots |

The engine is designed to operate within the common framework so that the same application plugins and protocol handling can be used across the different network I/O implementations.

---

**Implementation file:** `io_uring_server.c`  
**Project:** CS331 - Scalable Network I/O: From select to io_uring  
**Team:** T016

**A small submission note\:** I have intentionally left out the benchmark numbers, literature review, viva questions, and detailed explanations of why particular decisions were made. Those belong in the project report, not this implementation README.

One thing to verify before submitting: if this README will sit inside your `code/` directory rather than the repository root, adjust the build-command path to match its location.


---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)