//
// Created by Fleming on 2024-08-09.
//

#include "../include/reactor.h"


static void reactor_run(Reactor *reactor) {
    reactor->event_loop->run(reactor->event_loop);  // Start the event loop
}

static int reactor_listen_tcp(Reactor *reactor, int port, ProtocolFactory *factory) {
    if (reactor == NULL || reactor->event_loop == NULL || factory == NULL) {
        syslog(LOG_ERR, "invalid reactor event loop, or factory provided");
        return -1;
    }

    Transport *transport = create_tcp_transport();
    if (transport == NULL) {
        syslog(LOG_ERR, "failed to create TCP transport");
        return -1;
    }

    transport->event_loop = reactor->event_loop;
    transport->protocol = factory->build_protocol(factory);
    transport->handle_listen(transport, port);

    syslog(LOG_INFO, "listening on port %d", port);
    return 0;
}

static int reactor_connect_tcp(Reactor *reactor, const char *host, int port, ProtocolFactory *factory) {
    if (reactor == NULL || reactor->event_loop == NULL || factory == NULL) {
        syslog(LOG_ERR, "invalid reactor event loop, or factory provided");
        return -1;
    }

    Transport *transport = create_tcp_transport();
    if (transport == NULL) {
        syslog(LOG_ERR, "failed to create TCP transport");
        return -1;
    }

    transport->event_loop = reactor->event_loop;
    transport->protocol = factory->build_protocol(factory);
    transport->handle_connect(transport, host, port);

    syslog(LOG_INFO, "connected to %s on port %d", host, port);
    return 0;
}

// Constructor
Reactor *create_reactor() {
    Reactor *reactor = malloc(sizeof(Reactor));
    if (reactor == NULL) {
        syslog(LOG_ERR, "failed to allocate memory for reactor");
        return NULL;
    }

    syslog(LOG_INFO, "Installing event loop...");
    reactor->event_loop = create_event_loop();

    syslog(LOG_INFO, "Initializing reactor...");
    if (reactor->event_loop == NULL) {

        syslog(LOG_ERR, "failed to initialize reactor components");

        if (reactor->event_loop) destroy_event_loop(reactor->event_loop);

        free(reactor);

        return NULL;
    }

    reactor->run = reactor_run;
    reactor->listen_tcp = reactor_listen_tcp;
    reactor->connect_tcp = reactor_connect_tcp;

    return reactor;
}

// Destructor
void destroy_reactor(Reactor *reactor) {
    if (reactor == NULL) return;

    syslog(LOG_INFO, "Shutting down reactor...");
    // Signal to shut down the reactor event loop
    reactor->event_loop->running = false;

    free(reactor);
    reactor = NULL;
}
