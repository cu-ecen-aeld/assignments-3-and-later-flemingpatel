//
// Created by Fleming on 2024-08-09.
//


#include "../include/epoll_loop.h"


EventLoop *create_event_loop() {
#ifdef __linux__
    return create_epoll_event_loop();
#elif defined(__APPLE__) || defined(__FreeBSD__)
    //return create_kqueue_event_loop();
    return NULL;
#else
    //return create_select_event_loop();
    return NULL;
#endif
}

void destroy_event_loop(EventLoop* event_loop) {
    event_loop->destroy(event_loop);
}
