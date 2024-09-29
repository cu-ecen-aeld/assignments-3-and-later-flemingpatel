//
// Created by Fleming on 2024-08-11.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>


#if USE_AESD_CHAR_DEVICE
#define FILE_PATH "/dev/aesdchar"
#else
#define FILE_PATH "/var/tmp/aesdsocketdata"
#endif

#define PID_FILE "/var/run/aesdsocket.pid"
#define PORT 9000
#define BUF_SIZE 1024
#define INITIAL_BUFFER_SIZE 1024

volatile sig_atomic_t keep_running = 1;
pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER; // mutex for file operations


/**
 * Dynamic buffer
 */
typedef struct dynamic_buffer
{
    char *data;
    size_t size;
    size_t capacity;
} dynamic_buffer_t;

void init_buffer(dynamic_buffer_t *buffer)
{
    buffer->data = malloc(INITIAL_BUFFER_SIZE);
    buffer->size = 0;
    buffer->capacity = INITIAL_BUFFER_SIZE;
}

void free_buffer(dynamic_buffer_t *buffer)
{
    free(buffer->data);
    buffer->size = 0;
    buffer->capacity = 0;
}

void append_to_buffer(dynamic_buffer_t *buffer, const char *data, size_t data_size)
{
    while (buffer->size + data_size > buffer->capacity)
    {
        buffer->capacity *= 2;
        buffer->data = realloc(buffer->data, buffer->capacity);
    }
    memcpy(buffer->data + buffer->size, data, data_size);
    buffer->size += data_size;
}

int message_complete(const dynamic_buffer_t *buffer)
{
    if (buffer->size > 0 && buffer->data[buffer->size - 1] == '\n')
    {
        return 1;
    }
    return 0;
}

/**
 * thread list
 */
typedef struct thread_node
{
    pthread_t thread_id;
    struct thread_node *next;
} thread_node_t;

typedef struct
{
    _Atomic(thread_node_t*) head;
} thread_list_t;

thread_list_t thread_list = {.head = NULL};


void add_thread(pthread_t thread_id)
{
    syslog(LOG_DEBUG, "adding thread %lu", (unsigned long) thread_id);
    thread_node_t *new_node = malloc(sizeof(thread_node_t));
    new_node->thread_id = thread_id;

    thread_node_t *old_head;
    do
    {
        old_head = atomic_load(&thread_list.head);
        new_node->next = old_head;
    } while (!atomic_compare_exchange_weak(&thread_list.head, &old_head, new_node));
}

void remove_thread(pthread_t thread_id)
{
    thread_node_t **current = (thread_node_t **) &thread_list.head;
    thread_node_t *prev = NULL;

    while (*current)
    {
        if ((*current)->thread_id == thread_id)
        {
            thread_node_t *to_free = *current;
            if (prev)
            {
                prev->next = to_free->next;
            } else
            {
                atomic_store(&thread_list.head, to_free->next);
            }
            syslog(LOG_DEBUG, "removing thread %lu", (unsigned long) thread_id);
            free(to_free);
            break;
        }
        prev = *current;
        current = &((*current)->next);
    }
}

void clean_up_threads()
{
    syslog(LOG_DEBUG, "cleaning up threads if any...");
    thread_node_t *current = atomic_load(&thread_list.head);
    while (current)
    {
        pthread_join(current->thread_id, NULL);
        thread_node_t *to_free = current;
        current = current->next;
        syslog(LOG_DEBUG, "removing thread %lu", (unsigned long) to_free->thread_id);
        free(to_free);
    }
}

void handle_signal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        keep_running = 0;
        syslog(LOG_INFO, "received signal %d, shutting down...", signal);
    }
}

void write_pid()
{
    FILE *pid_file = fopen(PID_FILE, "w");
    if (pid_file)
    {
        fprintf(pid_file, "%d\n", getpid());
        fclose(pid_file);
    } else
    {
        syslog(LOG_ERR, "failed to write %s: %m", PID_FILE);
    }
}

void remove_test_file()
{
    if (remove(FILE_PATH) != 0)
    {
        syslog(LOG_ERR, "failed to remove file %s: %m", FILE_PATH);
    }
}

void write_timestamp()
{
    time_t current_time;
    struct tm *time_info;
    char time_string[100];

    time(&current_time);
    time_info = localtime(&current_time);

    // format the time string according to RFC 2822
    strftime(time_string, sizeof(time_string), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", time_info);

    pthread_mutex_lock(&file_lock);

    FILE *file = fopen(FILE_PATH, "a+");
    if (file == NULL)
    {
        syslog(LOG_ERR, "failed to open file %s for timestamp writing: %m", FILE_PATH);
        pthread_mutex_unlock(&file_lock);
        return;
    }

    if (fwrite(time_string, 1, strlen(time_string), file) != strlen(time_string))
    {
        syslog(LOG_ERR, "failed to write timestamp to file %s: %m", FILE_PATH);
    }

    fflush(file); // ensure data is written to disk
    fclose(file);

    pthread_mutex_unlock(&file_lock);
}

pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;

void *timestamp_thread(void *arg)
{
    /**
     * this is hackish function
     */
    syslog(LOG_INFO, "starting timer thread...");
    struct timespec ts;

    while (keep_running)
    {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 10;

        // wait for the condition variable with a timeout of 10 seconds
        pthread_mutex_lock(&timer_mutex);
        int rc = pthread_cond_timedwait(&timer_cond, &timer_mutex, &ts);
        pthread_mutex_unlock(&timer_mutex);

        if (rc == ETIMEDOUT)
        {
            // timeout occurred, write the timestamp
            write_timestamp();
        } else if (rc == 0)
        {
            // condition was signaled, check if we should exit
            if (!keep_running)
            {
                break;
            }
        }
        // Explicitly check for cancellation after each iteration
        pthread_testcancel();
    }

    syslog(LOG_INFO, "exiting timer thread...");
    return NULL;
}

void *handle_client(void *arg)
{
    int client_sock = (intptr_t) arg;
    dynamic_buffer_t buffer;
    init_buffer(&buffer);

    char recv_buffer[BUF_SIZE];
    ssize_t bytes_received;

    while ((bytes_received = recv(client_sock, recv_buffer, BUF_SIZE, 0)) > 0)
    {
        append_to_buffer(&buffer, recv_buffer, bytes_received);

        if (message_complete(&buffer))
        {
            pthread_mutex_lock(&file_lock);

            FILE *file = fopen(FILE_PATH, "a+");
            if (file == NULL)
            {
                syslog(LOG_ERR, "failed to open file %s: %m", FILE_PATH);
                pthread_mutex_unlock(&file_lock);
                free_buffer(&buffer);
                close(client_sock);
                pthread_t self_id = pthread_self();
                remove_thread(self_id);
                return NULL;
            }

            if (fwrite(buffer.data, 1, buffer.size, file) != buffer.size)
            {
                syslog(LOG_ERR, "failed to write data to file %s: %m", FILE_PATH);
                fclose(file);
                pthread_mutex_unlock(&file_lock);
                free_buffer(&buffer);
                close(client_sock);
                pthread_t self_id = pthread_self();
                remove_thread(self_id);
                return NULL;
            }

            fflush(file); // Ensure data is written to disk
            // sent the file content
            char file_buffer[BUF_SIZE];
            ssize_t bytes_read;
            // rewind the file pointer to the beginning of the file
            fseek(file, 0, SEEK_SET);
            while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0)
            {
                ssize_t total_sent = 0;
                while (total_sent < bytes_read)
                {
                    ssize_t bytes_sent = send(client_sock, file_buffer + total_sent, bytes_read - total_sent, 0);
                    if (bytes_sent < 0)
                    {
                        syslog(LOG_ERR, "send failed: %m");
                        break;
                    }
                    total_sent += bytes_sent;
                }
            }
            fclose(file);
            pthread_mutex_unlock(&file_lock);
            buffer.size = 0;
        }
    }

    if (bytes_received == 0)
    {
        syslog(LOG_INFO, "client %d disconnected gracefully", client_sock);
    } else if (bytes_received < 0)
    {
        syslog(LOG_ERR, "recv failed: %m");
    }

    // clean up
    free_buffer(&buffer);
    close(client_sock);
    return NULL;
}


int main(int argc, char *argv[])
{
    int daemonize = 0;

    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1)
    {
        switch (opt)
        {
            case 'd':
                daemonize = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // initialize syslog for logging
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // set up SIGINT handler
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "error: cannot handle SIGINT: %m");
        return EXIT_FAILURE;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "error: cannot handle SIGTERM: %m");
        return EXIT_FAILURE;
    }

    // daemonize if the -d flag was set
    if (daemonize)
    {
        syslog(LOG_INFO, "starting sever in demon mode");
        pid_t pid = fork();
        if (pid < 0)
        {
            syslog(LOG_ERR, "failed to fork process for daemon mode: %m");
            return EXIT_FAILURE;
        }
        if (pid > 0)
        {
            // parent process exits
            exit(EXIT_SUCCESS);
        }

        // child process continues (daemon)
        // create a new session and set the process group
        if (setsid() < 0)
        {
            syslog(LOG_ERR, "failed to create new session: %m");
            return EXIT_FAILURE;
        }

        // change the working directory to the root
        if (chdir("/") < 0)
        {
            syslog(LOG_ERR, "failed to change directory to root: %m");
            return EXIT_FAILURE;
        }

        // redirect standard file descriptors to /dev/null
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        int null_fd = open("/dev/null", O_RDWR);
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);

        syslog(LOG_INFO, "effective UID: %d", geteuid());
        write_pid();
    }

#if !(USE_AESD_CHAR_DEVICE)
    pthread_t timestamp_tid;
    if (pthread_create(&timestamp_tid, NULL, timestamp_thread, NULL) != 0)
    {
        syslog(LOG_ERR, "failed to create timestamp thread: %m");
        return EXIT_FAILURE;
    }
    add_thread(timestamp_tid);
#endif

    int server_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        syslog(LOG_ERR, "socket creation failed: %m");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
    {
        syslog(LOG_ERR, "bind failed: %m");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, 10) < 0)
    {
        syslog(LOG_ERR, "listen failed: %m");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "server listening on port %d", PORT);

    while (keep_running)
    {
        int client_sock = accept(server_sock, (struct sockaddr *) &client_addr, &client_addr_len);
        if (client_sock < 0)
        {
            if (keep_running)
            {
                syslog(LOG_ERR, "accept failed: %m");
            }
            continue;
        }

        syslog(LOG_INFO, "client accepted with fd %d", client_sock);
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *) (intptr_t) client_sock) != 0)
        {
            syslog(LOG_ERR, "thread creation failed: %m");
            close(client_sock);
            continue;
        }

        add_thread(thread_id);
    }

#if !(USE_AESD_CHAR_DEVICE)
    // cancel timer thread
    pthread_cond_signal(&timer_cond);
#endif

    // clean up resources
    clean_up_threads();
    close(server_sock);

#if !(USE_AESD_CHAR_DEVICE)
    // remove out file
    remove_test_file();
#endif

    syslog(LOG_INFO, "server exiting successfully");
    closelog();

    return EXIT_SUCCESS;
}
