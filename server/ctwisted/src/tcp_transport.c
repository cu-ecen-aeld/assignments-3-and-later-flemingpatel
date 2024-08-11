#include "../include/transport.h"

#define READ_BUFFER_SIZE 4096


typedef struct buffer_node {
    char *data;
    size_t len;
    size_t pos;
    struct list_head list;
} BufferNode;

typedef struct {
    Transport base;
    struct list_head write_queue;
    size_t queued_data_size;
} TCPTransport;

static void tcp_handle_read(Transport *self) {
    char buffer[READ_BUFFER_SIZE];
    ssize_t bytes_read;

    while (1) {
        bytes_read = read(self->fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            syslog(LOG_INFO, "read %zd bytes from fd %d", bytes_read, self->fd);
            self->protocol->data_received(self->protocol, buffer, bytes_read);
        } else if (bytes_read == 0) {
            syslog(LOG_INFO, "connection closed by peer on fd %d", self->fd);
            self->event_loop->remove_event(self->event_loop, self);
            transport_put(self); // Decrement reference and possibly close
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data available to read right now
                break;
            } else {
                syslog(LOG_ERR, "read error on fd %d: %s", self->fd, strerror(errno));
                transport_put(self); // Decrement reference and possibly close
                break;
            }
        }
    }
}

static void tcp_handle_write(Transport *self) {
    TCPTransport *transport = (TCPTransport *) self;

    while (!list_empty(&transport->write_queue)) {
        BufferNode *node = list_entry(transport->write_queue.next, BufferNode, list);
        ssize_t bytes_written = write(self->fd, node->data + node->pos, node->len - node->pos);

        if (bytes_written > 0) {
            syslog(LOG_INFO, "written %zd bytes to fd %d", bytes_written, self->fd);
            node->pos += bytes_written;

            if (node->pos == node->len) {
                // All data in this buffer node has been written, remove it from the queue
                transport->queued_data_size -= node->len;
                list_del(&node->list);
                free(node->data);
                free(node);
                node = NULL;
            }
        } else if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;  // Retry the write operation if interrupted by a signal
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data can be written right now, wait for the next event
                break;
            } else {
                syslog(LOG_ERR, "Write error on fd %d: %s", self->fd, strerror(errno));
                transport_put(self); // Decrement reference and possibly close
                return;
            }
        }
    }

    // If there's no more data to write, switch back to monitoring EPOLLIN
    if (list_empty(&transport->write_queue)) {
        self->event_loop->modify_event(self->event_loop, self, EPOLLIN);
    }
}

static void tcp_handle_send(Transport *self, const char *data, size_t len) {
    TCPTransport *transport = (TCPTransport *) self;

    if (len == 0) {
        syslog(LOG_WARNING, "attempted to send zero-length data");
        return;
    }

    // Allocate a new buffer node
    BufferNode *node = malloc(sizeof(BufferNode));
    if (node == NULL) {
        syslog(LOG_ERR, "failed to allocate buffer node");
        return;
    }

    node->data = malloc(len);
    if (node->data == NULL) {
        syslog(LOG_ERR, "failed to allocate buffer data");
        free(node);
        return;
    }

    memcpy(node->data, data, len);
    node->len = len;
    node->pos = 0;

    // Add the new buffer node to the tail of the list
    list_add_tail(&node->list, &transport->write_queue);
    transport->queued_data_size += len;

    // Notify the event loop to start monitoring EPOLLOUT for this file descriptor
    if (!self->event_loop->modify_event(self->event_loop, self, EPOLLOUT | EPOLLIN)) {
        syslog(LOG_ERR, "failed to modify event loop for write operation");
    }
}

static void tcp_handle_accept(Transport *self) {
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while ((client_fd = accept(self->fd, (struct sockaddr *) &client_addr, &client_len)) >= 0) {
        syslog(LOG_INFO, "accepted new connection: fd %d", client_fd);

        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags == -1 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            syslog(LOG_ERR, "failed to set non-blocking mode for client_fd: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        // Create new Transport and Protocol for this connection
        Transport *client_transport = create_tcp_transport();
        if (!client_transport) {
            syslog(LOG_ERR, "failed to create client transport");
            close(client_fd);
            continue;
        }

        client_transport->fd = client_fd;
        client_transport->event_loop = self->event_loop;

        // Set the protocol's transport to the new client transport
        Protocol *client_protocol = self->protocol->factory->build_protocol(self->protocol->factory);
        if (!client_protocol) {
            syslog(LOG_ERR, "failed to create client protocol");
            transport_put(client_transport);  // Decrement ref count, possibly freeing transport
            close(client_fd);
            continue;
        }

        client_protocol->transport = client_transport;
        client_transport->protocol = client_protocol;
        client_protocol->connection_made(client_protocol);

        if (!self->event_loop->add_event(self->event_loop, client_transport)) {
            syslog(LOG_ERR, "failed to add client_fd to event loop");
            transport_put(client_transport);  // Decrement ref count, possibly freeing transport
            continue;
        }
    }

    if (client_fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        syslog(LOG_ERR, "accept error: %s", strerror(errno));
    }
}

void tcp_handle_listen(Transport *self, int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "socket creation failed: %s", strerror(errno));
        return;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "setsockopt error: %s", strerror(errno));
        close(server_fd);
        return;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "failed to set non-blocking mode for server_fd: %s", strerror(errno));
        close(server_fd);
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "bind error: %s", strerror(errno));
        close(server_fd);
        return;
    }

    if (listen(server_fd, 128) < 0) {
        syslog(LOG_ERR, "listen error: %s", strerror(errno));
        close(server_fd);
        return;
    }

    self->fd = server_fd;
    self->is_listening = true;
    self->handle_accept = tcp_handle_accept;

    // Add the listening socket to the event loop here
    if (!self->event_loop->add_event(self->event_loop, self)) {
        syslog(LOG_ERR, "failed to add listening socket to event loop");
        transport_put(self); // Decrement reference and possibly close
    }
}

void tcp_handle_connect(Transport *self, const char *host, int port) {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        syslog(LOG_ERR, "socket creation failed: %s", strerror(errno));
        return;
    }

    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        syslog(LOG_ERR, "failed to set non-blocking mode for client_fd: %s", strerror(errno));
        close(client_fd);
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        syslog(LOG_ERR, "invalid address: %s", strerror(errno));
        close(client_fd);
        return;
    }

    if (connect(client_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            syslog(LOG_ERR, "connect error: %s", strerror(errno));
            close(client_fd);
            return;
        }
    }

    self->fd = client_fd;
    self->is_listening = false;

    // Add the client socket to the event loop here
    if (!self->event_loop->add_event(self->event_loop, self)) {
        syslog(LOG_ERR, "failed to add client socket to event loop");
        transport_put(self); // Decrement reference and possibly close
    }
}

static void tcp_close(Transport *self) {
    syslog(LOG_INFO, "closing TCP connection for fd %d", self->fd);

    if (self->protocol) {
        // Notify protocol of closure for client only - transport deletes protocol
        if (!self->is_listening) self->protocol->connection_lost(self->protocol);
        // just in-case user don't
        if (self->protocol) {
            free(self->protocol);
        }
    }

    // Clean up and close the socket
    close(self->fd);

    // Free the Transport structure
    free(self);
    self = NULL;
}

Transport *create_tcp_transport() {
    TCPTransport *transport = malloc(sizeof(TCPTransport));
    if (transport == NULL) {
        syslog(LOG_ERR, "failed to allocate TCPTransport");
        return NULL;
    }
    transport->base.fd = -1;  // Initially, the fd is not set
    transport->base.ref_count = 1; // Start with a reference count of 1
    transport->base.is_listening = false;
    transport->base.handle_read = tcp_handle_read;
    transport->base.handle_write = tcp_handle_write;
    transport->base.handle_send = tcp_handle_send;
    transport->base.handle_listen = tcp_handle_listen;
    transport->base.handle_connect = tcp_handle_connect;
    transport->base.close = tcp_close;

    // Initialize the write queue
    INIT_LIST_HEAD(&transport->write_queue);
    transport->queued_data_size = 0;

    return (Transport *) transport;
}
