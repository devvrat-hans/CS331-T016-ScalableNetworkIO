#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>

#define PORT 9090
#define QUEUE_DEPTH 1024
#define BUFFER_SIZE 4096

// Graceful Shutdown
static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

// Operation type
typedef enum {
    OP_ACCEPT,
    OP_RECV,
    OP_SEND
} op_type;

// Client State
typedef struct client {
    int fd;

    // Data received from the client
    char buffer[BUFFER_SIZE];

    /*
     * Sending state
     *
     * send_pos = how many bytes have already been sent
     * send_len = total number of bytes that need to be sent
     */
    size_t send_pos;
    size_t send_len;

    // Operation currently associated with this client
    op_type operation;

    // Linked list pointer used for cleanup
    struct client *next;

} client_t;

// Statistics
typedef struct {
    unsigned long connections;
    unsigned long recv_ops;
    unsigned long send_ops;

    unsigned long long bytes_received;
    unsigned long long bytes_sent;

    unsigned long enter_calls;
    unsigned long total_cqes;

} stats_t;

/*
 * Special marker used to identify ACCEPT completions.
 *
 * ACCEPT is different from RECV/SEND because there is
 * no client_t yet when ACCEPT is submitted.
 */
static int accept_marker;

// Client List Helpers
static void add_client(client_t **clients, client_t *client) {
    client->next = *clients;
    *clients = client;
}

static void remove_client(client_t **clients, client_t *client) {
    client_t **current = clients;

    while (*current) {
        if (*current == client) {
            *current = client->next;
            return;
        }

        current = &(*current)->next;
    }
}

// Submit ACCEPT
static int submit_accept(struct io_uring *ring, int server_fd) {
    struct io_uring_sqe *sqe;

    // Get an empty SQE.
    sqe = io_uring_get_sqe(ring);

    if (!sqe) {
        fprintf(stderr, "Failed to get SQE for ACCEPT\n");
        return -1;
    }

    /*
     * Prepare MULTISHOT ACCEPT operation.
     */
    io_uring_prep_multishot_accept(
        sqe,
        server_fd,
        NULL,
        NULL,
        0
    );

    /*
     * ACCEPT does not have a client_t yet
     *
     * Therefore we use a special marker to identify
     * ACCEPT completions later. The same marker is reused
     * for every completion from the multishot request.
     */
    io_uring_sqe_set_data(sqe, &accept_marker);

    return 0;
}

// Submit RECV
static int submit_recv(struct io_uring *ring, client_t *client) {
    struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);

    if (!sqe) {
        if (io_uring_submit(ring) < 0) {
            fprintf(stderr, "Failed to submit pending SQEs for RECV\n");
            return -1;
        }

        sqe = io_uring_get_sqe(ring);

        if (!sqe) {
            fprintf(stderr, "Failed to get SQE for RECV after retry\n");
            return -1;
        }
    }

    // Remember which operation is currently outstanding.
    client->operation = OP_RECV;

    // Prepare RECV operation.
    io_uring_prep_recv(
        sqe,
        client->fd,
        client->buffer,
        BUFFER_SIZE,
        0
    );

    // Store the client directly in user_data.
    io_uring_sqe_set_data(sqe, client);

    return 0;
}

// Submit SEND
static int submit_send(struct io_uring *ring, client_t *client) {
    struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);

    if (!sqe) {
        if (io_uring_submit(ring) < 0) {
            fprintf(stderr, "Failed to submit pending SQEs for SEND\n");
            return -1;
        }

        sqe = io_uring_get_sqe(ring);

        if (!sqe) {
            fprintf(stderr, "Failed to get SQE for SEND after retry\n");
            return -1;
        }
    }

    // Remember which operation is currently outstanding.
    client->operation = OP_SEND;

    // Send only the part of the buffer that has not already been sent.
    io_uring_prep_send(
        sqe,
        client->fd,
        client->buffer + client->send_pos,
        client->send_len - client->send_pos,
        0
    );

    // Store the client directly in user_data.
    io_uring_sqe_set_data(sqe, client);

    return 0;
}


int main(void) {
    // SIGNAL HANDLING
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    /*
     * Do not use SA_RESTART.
     *
     * This allows io_uring's wait to return when Ctrl+C
     * interrupts it.
     */
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        return 1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction SIGTERM");
        return 1;
    }

    /*
     * Ignore SIGPIPE.
     *
     * Otherwise sending to a client that has already
     * disconnected could terminate the entire process.
     */
    struct sigaction ignore_pipe;

    memset(&ignore_pipe, 0, sizeof(ignore_pipe));

    ignore_pipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_pipe.sa_mask);

    if (sigaction(SIGPIPE, &ignore_pipe, NULL) == -1) {
        perror("sigaction SIGPIPE");
        return 1;
    }

    // CREATE TCP SOCKET
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    // Allow quick restart after termination.
    int opt = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)) == -1) {

        perror("setsockopt");

        close(server_fd);
        return 1;
    }

    // SERVER ADDRESS
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // BIND
    if (bind(server_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) == -1) {
        perror("bind");

        close(server_fd);
        return 1;
    }

    // LISTEN
    if (listen(server_fd, 128) == -1) {
        perror("listen");

        close(server_fd);
        return 1;
    }

    printf("io_uring server listening on port %d\n", PORT);

    // CREATE IO_URING
    struct io_uring ring;

    int ret = io_uring_queue_init(QUEUE_DEPTH,&ring,0);

    if (ret < 0) {
        fprintf(stderr,"io_uring_queue_init failed: %s\n",strerror(-ret));

        close(server_fd);
        return 1;
    }

    printf("io_uring initialized!\n");

    
    // CLIENT LIST
    client_t *clients = NULL;

    // STATISTICS
    stats_t stats = {0};


    // PREPARE FIRST MULTISHOT ACCEPT
    if (submit_accept(&ring, server_fd) == -1) {
        io_uring_queue_exit(&ring);
        close(server_fd);

        return 1;
    }

    /*
     * Actually submit the initial multishot ACCEPT.
     *
     * One submitted ACCEPT can now produce multiple
     * completions, so we do not need to submit a new
     * ACCEPT after every successful connection.
     */
    ret = io_uring_submit(&ring);

    if (ret < 0) {
        fprintf(stderr,"Initial multishot ACCEPT submit failed: %s\n",strerror(-ret));

        io_uring_queue_exit(&ring);
        close(server_fd);

        return 1;
    }

    printf("Waiting for clients...\n");

    // MAIN EVENT LOOP
    while (running) {
        /*
         * Submit ALL SQEs currently waiting in the
         * submission queue and wait for at least one
         * completion.
         */
        stats.enter_calls++;

        ret = io_uring_submit_and_wait(&ring, 1);

        if (ret < 0) {
            // Ctrl+C can interrupt the wait.
            if (ret == -EINTR && !running) {
                break;
            }

            fprintf(stderr,"io_uring_submit_and_wait failed: %s\n",strerror(-ret));

            break;
        }

        // Process ALL currently available CQEs.
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;

        io_uring_for_each_cqe(&ring,head,cqe) {
            stats.total_cqes++;

            /*
             * Retrieve the user_data that was stored
             * when the SQE was prepared.
             */
            void *data = io_uring_cqe_get_data(cqe);

            // ACCEPT COMPLETED
            if (data == &accept_marker) {
                int client_fd = cqe->res;

                // Check ACCEPT result.
                if (client_fd < 0) {
                    /*
                     * If shutdown is happening, don't report
                     * the interrupted ACCEPT as a normal error.
                     */
                    if (running) {
                        fprintf(stderr,"ACCEPT failed: %s\n",strerror(-client_fd));
                    }
                } else {
                    // Allocate state for this client.
                    client_t *client = calloc(1, sizeof(client_t));

                    if (!client) {
                        fprintf(stderr, "calloc failed\n");

                        close(client_fd);
                    } else {
                        client->fd = client_fd;
                        client->send_pos = 0;
                        client->send_len = 0;

                        // Add client to our active-client list.
                        add_client(&clients, client);

                        // One successful connection.
                        stats.connections++;

                        /*
                         * Prepare RECV.
                         *
                         * IMPORTANT:
                         *
                         * We do NOT call io_uring_submit()
                         * here.
                         *
                         * The RECV will be submitted together
                         * with other pending SQEs during the
                         * next submit_and_wait().
                         */
                        if (submit_recv(&ring, client) == -1) {
                            remove_client(&clients,client);

                            close(client->fd);
                            free(client);
                        }
                    }
                }

                /*
                 * MULTISHOT ACCEPT
                 *
                 * If IORING_CQE_F_MORE is set, the original
                 * ACCEPT request is still active. Therefore
                 * we must NOT submit another ACCEPT here.
                 *
                 * If IORING_CQE_F_MORE is NOT set, the
                 * multishot request has terminated. We then
                 * re-arm it so the server can accept more
                 * connections.
                 */
                if (running && !(cqe->flags & IORING_CQE_F_MORE)) {
                    if (submit_accept(&ring,server_fd) == -1) {
                        fprintf(stderr,"Failed to re-arm multishot ACCEPT\n");

                        // We cannot continue accepting clients
                        running = 0;
                    }
                }
                count++;

                continue;
            }

            // RECV and SEND store client_t* directly in user_data.
            client_t *client = data;

            // RECV COMPLETED
            if (client->operation == OP_RECV) {
                int bytes_received = cqe->res;

                // RECV failed.
                if (bytes_received < 0) {
                    fprintf(
                        stderr,
                        "RECV failed for FD %d: %s\n",
                        client->fd,
                        strerror(-bytes_received)
                    );

                    remove_client(&clients, client);

                    close(client->fd);
                    free(client);

                    count++;
                    continue;
                }

                //recv() returning 0 means that the client closed the connection
                if (bytes_received == 0) {
                    remove_client(&clients,client);

                    close(client->fd);
                    free(client);

                    count++;
                    continue;
                }

                // Update statistics
                stats.recv_ops++;
                stats.bytes_received += (unsigned long long)bytes_received;

                // Save the amount of data that needs
                client->send_pos = 0;
                client->send_len = (size_t)bytes_received;

                /*
                 * Prepare SEND.
                 *
                 * Do NOT submit immediately.
                 */
                if (submit_send(&ring, client) == -1) {
                    remove_client(&clients,client);

                    close(client->fd);
                    free(client);

                    count++;
                    continue;
                }
                count++;
                continue;
            }

            // SEND COMPLETED      
            if (client->operation == OP_SEND) {
                int bytes_sent = cqe->res;

                /*
                 * SEND failed.
                 */
                if (bytes_sent < 0) {
                    fprintf(
                        stderr,
                        "SEND failed for FD %d: %s\n",
                        client->fd,
                        strerror(-bytes_sent)
                    );

                    remove_client(&clients,client);

                    close(client->fd);
                    free(client);

                    count++;
                    continue;
                }

                // Update statistics.
                stats.send_ops++;
                stats.bytes_sent += (unsigned long long)bytes_sent;

                /*
                 * Advance the send position.
                 */
                client->send_pos += (size_t)bytes_sent;

                /*
                 * Only part of the message was sent.
                 *
                 * Prepare another SEND for the
                 * remaining bytes.
                 */
                if (client->send_pos < client->send_len) {
                    if (submit_send(&ring,client) == -1) {
                        remove_client(&clients,client);

                        close(client->fd);
                        free(client);

                        count++;
                        continue;
                    }
                    count++;
                    continue;
                }

                /*
                 * Entire message has been echoed.
                 *
                 * Reset send state
                 */
                client->send_pos = 0;
                client->send_len = 0;

                /*
                 * Prepare another RECV.
                 *
                 * Again, we do NOT submit immediately.
                 */
                if (submit_recv(&ring,client) == -1) {
                    remove_client(&clients,client);

                    close(client->fd);
                    free(client);

                    count++;
                    continue;
                }
                count++;
                continue;
            }
            count++;
        }

        /*
         * Tell io_uring that all processed CQEs have been consumed
         *
         * One call instead of cqe_seen() for every CQE.
         */

        io_uring_cq_advance(&ring,count);
    }

    // SHUTDOWN
    printf("\nServer shutting down...\n");

    // Stop accepting new work and destroy the io_uring instance first.
    io_uring_queue_exit(&ring);

    // Close the listening socket.
    close(server_fd);

    // Clean up all remaining connected clients.
    client_t *client = clients;

    while (client) {
        client_t *next = client->next;

        close(client->fd);
        free(client);

        client = next;
    }

    // PRINT FINAL STATISTICS

    printf("\n========== Statistics ==========\n");

    printf("Connections:                %lu\n",stats.connections);

    printf("RECV operations:            %lu\n",stats.recv_ops);

    printf("SEND operations:            %lu\n",stats.send_ops);

    printf("Bytes received:             %llu\n",stats.bytes_received);

    printf("Bytes sent:                 %llu\n",stats.bytes_sent);

    printf("io_uring enter calls:       %lu\n",stats.enter_calls);

    printf("Total CQEs:                 %lu\n",stats.total_cqes);

    printf("================================\n");

    return 0;
}