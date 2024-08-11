//
// Created by Fleming on 2024-08-09.
//

#ifndef CTWISTED_TRANSPORT_H
#define CTWISTED_TRANSPORT_H

#include "protocol.h"
#include "event_loop.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>


typedef struct EventLoop EventLoop;
typedef struct Transport {
    void (*handle_read)(struct Transport *self);
    void (*handle_write)(struct Transport *self);
    void (*handle_send)(struct Transport *self, const char *data, size_t len);
    void (*handle_accept)(struct Transport *self);
    void (*handle_listen)(struct Transport *self, int port);
    void (*handle_connect)(struct Transport *self, const char *host, int port);
    void (*close)(struct Transport *self);

    int fd;
    int ref_count; // Reference counter
    bool is_listening;
    Protocol *protocol;
    EventLoop *event_loop;
    struct list_head list;  // Embed list_head for linkage
} Transport;

// Transport reference management
static inline void transport_get(Transport *self) {
    int count = __sync_fetch_and_add(&self->ref_count, 1);
    syslog(LOG_DEBUG, "transport_get: fd %d, new ref_count: %d", self->fd, count + 1);
}

static inline void transport_put(Transport *self) {
    int count = __sync_sub_and_fetch(&self->ref_count, 1);
    syslog(LOG_DEBUG, "transport_put: fd %d, new ref_count: %d", self->fd, count);
    if (count == 0) {
        self->close(self);
    }
}

Transport *create_tcp_transport();

#endif //CTWISTED_TRANSPORT_H
