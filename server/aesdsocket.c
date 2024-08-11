//
// Created by Fleming on 2024-08-11.
// Note: I am using an experimental framework that I created on weekends.
// Happy hacking...
//

#include "ctwisted/include/reactor.h"
#include "ctwisted/include/protocol.h"
#include <signal.h>
#include <assert.h>


#define FILE_PATH "/var/tmp/aesdsocketdata"


// Global pointer to the reactor and factory
static Reactor *global_reactor = NULL;
static ProtocolFactory *global_factory = NULL;


void clean_up() {
    if (remove(FILE_PATH) != 0) {
        syslog(LOG_ERR, "Failed to remove file: %s", strerror(errno));
    }
    if (global_reactor != NULL) {
        destroy_reactor(global_reactor);
    }
    if (global_factory != NULL) {
        free(global_factory);
        global_factory = NULL;
    }
}


void handle_sigint(int sig) {
    if (sig == SIGINT) {
        syslog(LOG_INFO, "received SIGINT, shutting down.");
    } else if (sig == SIGTERM) {
        syslog(LOG_INFO, "received SIGTERM, shutting down.");
    }
    clean_up();
}

// Define a simple Protocol
typedef struct {
    Protocol base;
} AesdProtocol;

void aesd_data_received(Protocol *self, const char *data, size_t len) {
    if (len == 0) {
        syslog(LOG_WARNING, "attempted to read zero-length data");
        return;
    }

    // Open the file in append mode
    FILE *file = fopen(FILE_PATH, "a+");
    if (!file) {
        syslog(LOG_ERR, "failed to open file: %s", strerror(errno));
        return;
    }

    // Write the received data directly to the file
    fwrite(data, 1, len, file);
    fflush(file); // Ensure data is written to disk

    // Check if the received data contains a newline, indicating a complete packet
    if (strchr(data, '\n') != NULL) {
        // Rewind the file pointer to the beginning of the file
        fseek(file, 0, SEEK_SET);

        // Read the full file content and send it back to the client
        char file_buffer[1024];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
            self->transport->handle_send(self->transport, file_buffer, bytes_read);
        }
    }

    // Close the file
    fclose(file);
}

void aesd_connection_made(Protocol *self) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char ip_str[INET_ADDRSTRLEN];

    int fd = self->transport->fd;
    if (getsockname(fd, (struct sockaddr *) &addr, &addr_len) == -1) {
        syslog(LOG_ERR, "getsockname failed");
        return;
    }

    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, sizeof(ip_str));
    syslog(LOG_INFO, "accepted connection from %s", ip_str);
}

void aesd_connection_lost(Protocol *self) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char ip_str[INET_ADDRSTRLEN];

    int fd = self->transport->fd;
    if (getpeername(fd, (struct sockaddr *) &addr, &addr_len) == -1) {
        syslog(LOG_ERR, "getpeername failed");
        return;
    }

    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, sizeof(ip_str));
    syslog(LOG_INFO, "closed connection from %s", ip_str);
}

Protocol *create_aesd_protocol(ProtocolFactory *factory) {
    AesdProtocol *protocol = malloc(sizeof(AesdProtocol));
    if (protocol == NULL) {
        syslog(LOG_ERR, "failed to allocate AesdProtocol.");
        return NULL;
    }
    protocol->base.factory = factory;
    protocol->base.data_received = aesd_data_received;
    protocol->base.connection_made = aesd_connection_made;
    protocol->base.connection_lost = aesd_connection_lost;
    return (Protocol *) protocol;
}

// Define the ProtocolFactory
typedef struct {
    ProtocolFactory base;
} AesdProtocolFactory;

Protocol *aesd_protocol_factory_build_protocol(ProtocolFactory *self) {
    return create_aesd_protocol(self);
}

ProtocolFactory *create_aesd_protocol_factory() {
    AesdProtocolFactory *factory = malloc(sizeof(AesdProtocolFactory));
    if (factory == NULL) {
        syslog(LOG_ERR, "failed to allocate AesdProtocolFactory.");
        return NULL;
    }
    factory->base.build_protocol = aesd_protocol_factory_build_protocol;
    return (ProtocolFactory *) factory;
}


int main(int argc, char *argv[]) {
    int daemonize = 0;

    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                daemonize = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Initialize syslog for logging
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // Create the reactor and event loop
    Reactor *reactor = create_reactor();
    global_reactor = reactor;
    if (reactor == NULL) {
        syslog(LOG_ERR, "failed to create reactor.");
        return EXIT_FAILURE;
    }

    // Set up SIGINT handler
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "failed to set up SIGINT handler.");
        destroy_reactor(reactor);
        return EXIT_FAILURE;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "failed to set up SIGTERM handler.");
        destroy_reactor(reactor);
        return EXIT_FAILURE;
    }

    // Create the protocol factory
    ProtocolFactory *factory = create_aesd_protocol_factory();
    global_factory = factory;
    if (factory == NULL) {
        syslog(LOG_ERR, "Failed to create protocol factory.");
        destroy_reactor(reactor);
        return EXIT_FAILURE;
    }

    // Daemonize if the -d flag was set
    if (daemonize) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "failed to fork process for daemon mode.");
            clean_up();
            return EXIT_FAILURE;
        }
        if (pid > 0) {
            // Parent process exits
            exit(EXIT_SUCCESS);
        }

        // Child process continues (daemon)
        // Create a new session and set the process group
        if (setsid() < 0) {
            syslog(LOG_ERR, "failed to create new session.");
            clean_up();
            return EXIT_FAILURE;
        }

        // Change the working directory to the root
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "failed to change directory to root.");
            clean_up();
            return EXIT_FAILURE;
        }

        // Redirect standard file descriptors to /dev/null
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        int null_fd = open("/dev/null", O_RDWR);
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
    }

    // Start listening on port 9000
    int server_fd = reactor->listen_tcp(reactor, 9000, factory);
    if (server_fd < 0) {
        syslog(LOG_ERR, "failed to listen on port 8080.");
        clean_up();
        return EXIT_FAILURE;
    }

    // Run the reactor
    reactor->run(reactor);
    closelog();

    return EXIT_SUCCESS;
}
