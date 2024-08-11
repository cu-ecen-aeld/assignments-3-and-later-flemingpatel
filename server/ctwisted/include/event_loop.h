//
// Created by Fleming on 2024-08-09.
//

#ifndef CTWISTED_EVENT_LOOP_H
#define CTWISTED_EVENT_LOOP_H

#include "transport.h"
#include <sys/epoll.h>
#include <stddef.h>


#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#define MAX_EVENTS 1024
#define EPOLL_TIMEOUT 10000 // Milliseconds to wait before checking the priority queue


typedef struct EventLoop EventLoop;
struct EventLoop {
    void (*run)(EventLoop *self);
    bool (*add_event)(EventLoop *self, Transport *transport);
    bool (*modify_event)(EventLoop *self, Transport *transport, u_int32_t events);
    bool (*remove_event)(EventLoop *self, Transport *transport);
    void (*destroy)(EventLoop *self);

    volatile bool running;
};

// Function to create the event loop for the platform (e.g., epoll on Linux)
EventLoop *create_event_loop();

// Function to destroy the event loop for the platform (e.g., epoll on Linux)
void destroy_event_loop(EventLoop *event_loop);

#endif //CTWISTED_EVENT_LOOP_H
