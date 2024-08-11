#include "../include/epoll_loop.h"


typedef struct {
    EventLoop base;
    int epoll_fd;
    struct epoll_event events[MAX_EVENTS];
    struct list_head active_transports;  // Linked list of active transports
} EpollLoop;

static void epoll_run(EventLoop *self) {
    syslog(LOG_INFO, "starting reactor event loop");

    EpollLoop *loop = (EpollLoop *) self;
    self->running = true;

    // TODO re-design transport(references) and event loop
    while (loop->base.running) {
        int n = epoll_wait(loop->epoll_fd, loop->events, MAX_EVENTS, EPOLL_TIMEOUT);
        if (n < 0) {
            int err = errno;
            if (err == EINTR) {
                if (!loop->base.running) break;
                continue;
            }
            syslog(LOG_ERR, "epoll_wait failed: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            Transport *transport = (Transport *) loop->events[i].data.ptr;
            transport_get(transport); // Increase ref count before handling event

            if (loop->events[i].events & (EPOLLERR | EPOLLHUP)) {
                syslog(LOG_ERR, "EPOLLERR or EPOLLHUP on fd %d", transport->fd);
                self->remove_event(self, transport);
                transport_put(transport); // Decrement reference, possibly closing
                continue;
            }
            if (loop->events[i].events & EPOLLIN) {
                if (transport->is_listening) {
                    transport->handle_accept(transport);
                } else {
                    transport->handle_read(transport);
                }
            }
            if (loop->events[i].events & EPOLLOUT) {
                transport->handle_write(transport);
            }

            transport_put(transport); // Decrement reference after handling event
        }
    }
    loop->base.destroy(self);
    syslog(LOG_INFO, "exited reactor event loop");
}

static bool epoll_add_event(EventLoop *self, Transport *transport) {
    EpollLoop *loop = (EpollLoop *) self;
    struct epoll_event event;

    event.events = EPOLLIN | EPOLLOUT;
    event.data.ptr = transport;

    syslog(LOG_DEBUG, "Adding event for fd %d with EPOLLIN | EPOLLOUT", transport->fd);

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, transport->fd, &event) == -1) {
        syslog(LOG_ERR, "failed to add event to epoll: %s", strerror(errno));
        return false;
    }

    list_add_tail(&transport->list, &loop->active_transports);
    return true;
}

static bool epoll_modify_event(EventLoop *self, Transport *transport, u_int32_t events) {
    EpollLoop *loop = (EpollLoop *) self;
    struct epoll_event event;

    event.events = events; // Can be EPOLLIN, EPOLLOUT, or both
    event.data.ptr = transport;

    syslog(LOG_DEBUG, "Modifying event for fd %d with EPOLLIN | EPOLLOUT", transport->fd);

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, transport->fd, &event) == -1) {
        syslog(LOG_ERR, "failed to modify event in epoll: %s", strerror(errno));
        return false;
    }

    return true;
}

static bool epoll_remove_event(EventLoop *self, Transport *transport) {
    EpollLoop *loop = (EpollLoop *) self;

    syslog(LOG_DEBUG, "Removing event for fd %d", transport->fd);

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, transport->fd, NULL) == -1) {
        syslog(LOG_ERR, "failed to remove event from epoll: %s", strerror(errno));
        return false;
    }

    list_del(&transport->list);
    return true;
}

static void epoll_destroy(EventLoop *self) {
    syslog(LOG_INFO, "shutting down reactor event loop. Freeing resources...");

    EpollLoop *loop = (EpollLoop *) self;

    // Cleanup all active transports
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &loop->active_transports) {
        Transport *transport = list_entry(pos, Transport, list);
        self->remove_event(self, transport);
        transport_put(transport); // Decrement reference, possibly freeing transport
    }

    close(loop->epoll_fd);
    free(loop);
    loop = NULL;
}

EventLoop *create_epoll_event_loop() {
    EpollLoop *loop = malloc(sizeof(EpollLoop));
    if (loop == NULL) {
        syslog(LOG_ERR, "Memory allocation failed for EpollLoop");
        return NULL;
    }

    loop->epoll_fd = epoll_create1(0);
    if (loop->epoll_fd == -1) {
        syslog(LOG_ERR, "epoll_create1 failed: %s", strerror(errno));
        free(loop);
        return NULL;
    }

    INIT_LIST_HEAD(&loop->active_transports);

    loop->base.running = false;
    loop->base.run = epoll_run;
    loop->base.add_event = epoll_add_event;
    loop->base.modify_event = epoll_modify_event;
    loop->base.remove_event = epoll_remove_event;
    loop->base.destroy = epoll_destroy;

    return (EventLoop *) loop;
}
