//
// Created by Fleming on 2024-08-09.
//

#ifndef CTWISTED_REACTOR_H
#define CTWISTED_REACTOR_H


#include "event_loop.h"


typedef struct Reactor {
    // Function pointers
    void (*run)(struct Reactor *reactor);

    // High-level API for sock
    int (*listen_tcp)(struct Reactor *reactor, int port, ProtocolFactory *factory);
    int (*connect_tcp)(struct Reactor *reactor, const char *host, int port, ProtocolFactory *factory);

    EventLoop *event_loop;
} Reactor;

Reactor *create_reactor();
void destroy_reactor(Reactor *reactor);

#endif //CTWISTED_REACTOR_H
