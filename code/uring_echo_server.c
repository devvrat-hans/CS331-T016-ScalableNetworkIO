#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>

#define PORT 9090
#define QUEUE_DEPTH 256
#define BUFFER_SIZE 4096

/*
 * Type of operation associated with a CQE.
 */
typedef enum
{
    OP_ACCEPT,
    OP_RECV,
    OP_SEND
} op_type;

/*
 * State associated with one connected client.
 */
typedef struct client
{

    int fd;

    /*
     * Data received from the client.
     */
    char buffer[BUFFER_SIZE + 1];

    /*
     * Sending state.
     *
     * send_pos  = how many bytes have already been sent
     * send_len  = total number of bytes that need to be sent
     */
    size_t send_pos;
    size_t send_len;

} client_t;

/*
 * Information attached to an SQE.
 *
 * When the operation completes, this pointer
 * comes back through the CQE.
 */
typedef struct
{

    op_type type;
    client_t *client;

} operation_t;

/*
 * --------------------------------------------------
 * Submit ACCEPT
 * --------------------------------------------------
 */
static int submit_accept(
    struct io_uring *ring,
    int server_fd)
{
    struct io_uring_sqe *sqe;

    /*
     * Get an empty SQE.
     */
    sqe = io_uring_get_sqe(ring);

    if (!sqe)
    {
        fprintf(stderr,
                "Failed to get SQE for ACCEPT\n");
        return -1;
    }

    /*
     * Allocate operation context.
     *
     * This must remain valid until the CQE
     * corresponding to this operation is processed.
     */
    operation_t *op =
        malloc(sizeof(operation_t));

    if (!op)
    {
        fprintf(stderr,
                "malloc failed for ACCEPT\n");
        return -1;
    }

    op->type = OP_ACCEPT;
    op->client = NULL;

    /*
     * Prepare ACCEPT operation.
     */
    io_uring_prep_accept(
        sqe,
        server_fd,
        NULL,
        NULL,
        0);

    /*
     * Store our operation pointer
     * inside the SQE.
     */
    io_uring_sqe_set_data(sqe, op);

    return 0;
}

/*
 * --------------------------------------------------
 * Submit RECV
 * --------------------------------------------------
 */
static int submit_recv(
    struct io_uring *ring,
    client_t *client)
{
    struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);

    if (!sqe)
    {
        fprintf(stderr,
                "Failed to get SQE for RECV\n");
        return -1;
    }

    operation_t *op =
        malloc(sizeof(operation_t));

    if (!op)
    {
        fprintf(stderr,
                "malloc failed for RECV\n");
        return -1;
    }

    op->type = OP_RECV;
    op->client = client;

    /*
     * Ask io_uring to receive data from
     * this client's socket.
     */
    io_uring_prep_recv(
        sqe,
        client->fd,
        client->buffer,
        BUFFER_SIZE,
        0);

    /*
     * Remember which client this operation
     * belongs to.
     */
    io_uring_sqe_set_data(sqe, op);

    return 0;
}

/*
 * --------------------------------------------------
 * Submit SEND
 * --------------------------------------------------
 */
static int submit_send(
    struct io_uring *ring,
    client_t *client)
{
    struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);

    if (!sqe)
    {
        fprintf(stderr,
                "Failed to get SQE for SEND\n");
        return -1;
    }

    operation_t *op =
        malloc(sizeof(operation_t));

    if (!op)
    {
        fprintf(stderr,
                "malloc failed for SEND\n");
        return -1;
    }

    op->type = OP_SEND;
    op->client = client;

    /*
     * Send only the part that has not
     * already been sent.
     */
    io_uring_prep_send(
        sqe,
        client->fd,
        client->buffer + client->send_pos,
        client->send_len - client->send_pos,
        0);

    /*
     * Associate this SEND with the client.
     */
    io_uring_sqe_set_data(sqe, op);

    return 0;
}

int main(void)
{
    /*
     * Ignore SIGPIPE.
     *
     * Otherwise sending to a client that has
     * already disconnected can terminate the
     * entire process.
     */
    signal(SIGPIPE, SIG_IGN);

    /*
     * ==================================================
     * CREATE TCP SOCKET
     * ==================================================
     */

    int server_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        return 1;
    }

    /*
     * Allow quick restart after termination.
     */
    int opt = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)) == -1)
    {

        perror("setsockopt");

        close(server_fd);
        return 1;
    }

    /*
     * ==================================================
     * SERVER ADDRESS
     * ==================================================
     */

    struct sockaddr_in server_addr;

    memset(
        &server_addr,
        0,
        sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);

    /*
     * ==================================================
     * BIND
     * ==================================================
     */

    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == -1)
    {

        perror("bind");

        close(server_fd);
        return 1;
    }

    /*
     * ==================================================
     * LISTEN
     * ==================================================
     */

    if (listen(server_fd, 128) == -1)
    {

        perror("listen");

        close(server_fd);
        return 1;
    }

    printf(
        "io_uring server listening on port %d\n",
        PORT);

    /*
     * ==================================================
     * CREATE IO_URING
     * ==================================================
     */

    struct io_uring ring;

    int ret =
        io_uring_queue_init(
            QUEUE_DEPTH,
            &ring,
            0);

    if (ret < 0)
    {

        fprintf(
            stderr,
            "io_uring_queue_init failed: %s\n",
            strerror(-ret));

        close(server_fd);
        return 1;
    }

    printf("io_uring initialized!\n");

    /*
     * ==================================================
     * SUBMIT FIRST ACCEPT
     * ==================================================
     */

    if (submit_accept(
            &ring,
            server_fd) == -1)
    {

        io_uring_queue_exit(&ring);
        close(server_fd);

        return 1;
    }

    ret =
        io_uring_submit(&ring);

    if (ret < 0)
    {

        fprintf(
            stderr,
            "Initial submit failed: %s\n",
            strerror(-ret));

        io_uring_queue_exit(&ring);
        close(server_fd);

        return 1;
    }

    printf("Waiting for clients...\n");

    /*
     * ==================================================
     * MAIN EVENT LOOP
     * ==================================================
     */

    while (1)
    {

        struct io_uring_cqe *cqe;

        /*
         * Wait until at least one operation
         * completes.
         */
        ret =
            io_uring_wait_cqe(
                &ring,
                &cqe);

        if (ret < 0)
        {

            fprintf(
                stderr,
                "io_uring_wait_cqe failed: %s\n",
                strerror(-ret));

            break;
        }

        /*
         * Retrieve the operation context
         * that we stored in the SQE.
         */
        operation_t *op =
            io_uring_cqe_get_data(cqe);

        /*
         * ==================================================
         * ACCEPT COMPLETED
         * ==================================================
         */

        if (op->type == OP_ACCEPT)
        {

            /*
             * ACCEPT result:
             *
             * >= 0 : new client FD
             * <  0 : negative errno
             */
            int client_fd =
                cqe->res;

            /*
             * This CQE has now been processed.
             */
            io_uring_cqe_seen(
                &ring,
                cqe);

            /*
             * The operation context is
             * no longer needed.
             */
            free(op);

            /*
             * Check ACCEPT result.
             */
            if (client_fd < 0)
            {

                fprintf(
                    stderr,
                    "ACCEPT failed: %s\n",
                    strerror(-client_fd));
            }
            else
            {

                printf(
                    "New client connected! FD = %d\n",
                    client_fd);

                /*
                 * Allocate client state.
                 */
                client_t *client =
                    calloc(
                        1,
                        sizeof(client_t));

                if (!client)
                {

                    fprintf(
                        stderr,
                        "calloc failed\n");

                    close(client_fd);
                }
                else
                {

                    client->fd =
                        client_fd;

                    client->send_pos =
                        0;

                    client->send_len =
                        0;

                    /*
                     * Submit RECV for this client.
                     */
                    if (submit_recv(
                            &ring,
                            client) == -1)
                    {

                        close(client->fd);
                        free(client);
                    }
                }
            }

            /*
             * VERY IMPORTANT:
             *
             * Immediately submit another ACCEPT.
             *
             * Therefore the server remains ready
             * for additional clients.
             */
            if (submit_accept(
                    &ring,
                    server_fd) == -1)
            {

                fprintf(
                    stderr,
                    "Failed to submit next ACCEPT\n");

                break;
            }

            /*
             * Submit the newly prepared
             * ACCEPT and possibly RECV.
             */
            ret =
                io_uring_submit(&ring);

            if (ret < 0)
            {

                fprintf(
                    stderr,
                    "submit failed: %s\n",
                    strerror(-ret));

                break;
            }

            continue;
        }

        /*
         * ==================================================
         * RECV COMPLETED
         * ==================================================
         */

        if (op->type == OP_RECV)
        {

            client_t *client =
                op->client;

            int bytes_received =
                cqe->res;

            /*
             * We are finished with this
             * operation context.
             */
            io_uring_cqe_seen(
                &ring,
                cqe);

            free(op);

            /*
             * RECV failed.
             */
            if (bytes_received < 0)
            {

                fprintf(
                    stderr,
                    "RECV failed for FD %d: %s\n",
                    client->fd,
                    strerror(-bytes_received));

                close(client->fd);
                free(client);

                continue;
            }

            /*
             * recv() returning 0 means
             * the client closed its connection.
             */
            if (bytes_received == 0)
            {

                printf(
                    "Client FD %d disconnected\n",
                    client->fd);

                close(client->fd);
                free(client);

                continue;
            }

            /*
             * Save the amount of data that
             * needs to be echoed.
             */
            client->send_pos = 0;

            client->send_len =
                (size_t)bytes_received;

            /*
             * Null terminate for printing.
             */
            client->buffer[bytes_received] =
                '\0';

            printf(
                "FD %d received: %s\n",
                client->fd,
                client->buffer);

            /*
             * Submit SEND.
             */
            if (submit_send(
                    &ring,
                    client) == -1)
            {

                close(client->fd);
                free(client);

                continue;
            }

            /*
             * Send request to kernel.
             */
            ret =
                io_uring_submit(&ring);

            if (ret < 0)
            {

                fprintf(
                    stderr,
                    "SEND submit failed: %s\n",
                    strerror(-ret));

                close(client->fd);
                free(client);

                continue;
            }

            continue;
        }

        /*
         * ==================================================
         * SEND COMPLETED
         * ==================================================
         */

        if (op->type == OP_SEND)
        {

            client_t *client =
                op->client;

            int bytes_sent =
                cqe->res;

            /*
             * SEND operation is complete.
             */
            io_uring_cqe_seen(
                &ring,
                cqe);

            free(op);

            /*
             * SEND failed.
             */
            if (bytes_sent < 0)
            {

                fprintf(
                    stderr,
                    "SEND failed for FD %d: %s\n",
                    client->fd,
                    strerror(-bytes_sent));

                close(client->fd);
                free(client);

                continue;
            }

            /*
             * Advance the send position.
             */
            client->send_pos +=
                (size_t)bytes_sent;

            /*
             * Only part of the message was sent.
             *
             * Send the remaining portion.
             */
            if (client->send_pos <
                client->send_len)
            {

                if (submit_send(
                        &ring,
                        client) == -1)
                {

                    close(client->fd);
                    free(client);

                    continue;
                }

                ret =
                    io_uring_submit(&ring);

                if (ret < 0)
                {

                    fprintf(
                        stderr,
                        "SEND submit failed: %s\n",
                        strerror(-ret));

                    close(client->fd);
                    free(client);

                    continue;
                }

                continue;
            }

            /*
             * Entire message has been echoed.
             */
            printf(
                "FD %d echoed %zu bytes\n",
                client->fd,
                client->send_len);

            /*
             * Reset send state.
             */
            client->send_pos = 0;
            client->send_len = 0;

            /*
             * Wait for another message
             * from this client.
             */
            if (submit_recv(
                    &ring,
                    client) == -1)
            {

                close(client->fd);
                free(client);

                continue;
            }

            /*
             * Submit RECV.
             */
            ret =
                io_uring_submit(&ring);

            if (ret < 0)
            {

                fprintf(
                    stderr,
                    "RECV submit failed: %s\n",
                    strerror(-ret));

                close(client->fd);
                free(client);

                continue;
            }

            continue;
        }
    }

    /*
     * ==================================================
     * CLEANUP
     * ==================================================
     */

    io_uring_queue_exit(&ring);

    close(server_fd);

    return 0;
}