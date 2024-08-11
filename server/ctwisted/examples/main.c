#include "../include/reactor.h"
#include "../include/protocol.h"
#include <signal.h>


static Reactor *global_reactor = NULL;  // Global pointer to the reactor


void handle_sigint(int sig) {
    if (global_reactor != NULL) {
        // Clean up resources
        destroy_reactor(global_reactor);
    }
}

// Define a simple Protocol
typedef struct {
    Protocol base;
} EchoProtocol;

void echo_data_received(Protocol *self, const char *data, size_t len) {
    printf("Received: %.*s\n", (int) len, data);
    // write back
    self->transport->handle_send(self->transport, data, len);
}

void echo_connection_lost(Protocol *self) {
    syslog(LOG_INFO, "Connection lost.");
}

Protocol *create_echo_protocol(ProtocolFactory *factory) {
    EchoProtocol *protocol = malloc(sizeof(EchoProtocol));
    if (protocol == NULL) {
        syslog(LOG_ERR, "Failed to allocate EchoProtocol.");
        return NULL;
    }
    protocol->base.factory = factory;
    protocol->base.data_received = echo_data_received;
    protocol->base.connection_lost = echo_connection_lost;
    return (Protocol *) protocol;
}

// Define the ProtocolFactory
typedef struct {
    ProtocolFactory base;
} EchoProtocolFactory;

Protocol *echo_protocol_factory_build_protocol(ProtocolFactory *self) {
    return create_echo_protocol(self);
}

ProtocolFactory *create_echo_protocol_factory() {
    EchoProtocolFactory *factory = malloc(sizeof(EchoProtocolFactory));
    if (factory == NULL) {
        syslog(LOG_ERR, "Failed to allocate EchoProtocolFactory.");
        return NULL;
    }
    factory->base.build_protocol = echo_protocol_factory_build_protocol;
    return (ProtocolFactory *) factory;
}

int main() {
    // Initialize syslog for logging
    openlog("ctwisted", LOG_PID | LOG_CONS, LOG_USER);

    // Create the reactor and event loop
    Reactor *reactor = create_reactor(); // Assuming 4 threads in the thread pool
    global_reactor = reactor;
    if (reactor == NULL) {
        syslog(LOG_ERR, "Failed to create reactor.");
        return EXIT_FAILURE;
    }

    // Set up SIGINT handler
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0; // Restart functions if interrupted by handler
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Failed to set up SIGINT handler.");
        destroy_reactor(reactor);
        return EXIT_FAILURE;
    }

    // Create the protocol factory
    ProtocolFactory *factory = create_echo_protocol_factory();
    if (factory == NULL) {
        syslog(LOG_ERR, "Failed to create protocol factory.");
        destroy_reactor(reactor);
        return EXIT_FAILURE;
    }

    // Start listening on port 8080
    int server_fd = reactor->listen_tcp(reactor, 8080, factory);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Failed to listen on port 8080.");
        destroy_reactor(reactor);
        return EXIT_FAILURE;
    }

    // Run the reactor
    reactor->run(reactor);
    free(factory);

    // Close syslog
    closelog();

    return EXIT_SUCCESS;
}
