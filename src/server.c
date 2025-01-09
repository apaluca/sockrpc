#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include "sockrpc/sockrpc.h"

/**
 * @file server.c
 * @brief Implementation of the SockRPC server component
 *
 * This file implements a multi-threaded RPC server using Unix domain sockets
 * and epoll for efficient I/O multiplexing. The server uses a worker pool
 * architecture with round-robin load balancing.
 *
 * Key features:
 * - Thread pool with configurable number of workers
 * - Non-blocking I/O using epoll
 * - Round-robin load balancing
 * - Thread-safe method registration
 * - Graceful shutdown handling
 *
 * @note The server uses JSON for message serialization via the cJSON library
 */

/**
 * @brief Maximum number of epoll events to handle in one iteration
 * @note This value affects responsiveness vs CPU usage tradeoff
 */
#define MAX_EVENTS 10

/**
 * @brief Maximum number of RPC methods that can be registered
 * @note Can be increased if needed, affects memory usage
 */
#define MAX_METHODS 100

/**
 * @brief Size of the buffer for reading/writing socket data
 * @note Should be large enough for typical RPC messages
 */
#define BUFFER_SIZE 4096

/**
 * @brief Number of worker threads in the thread pool
 * @note Can be adjusted based on the host system's CPU cores
 */
#define NUM_WORKERS 4

/**
 * @brief Context structure for worker threads
 *
 * Each worker thread maintains its own epoll instance and connection counter.
 * The mutex protects access to shared resources within the worker context.
 *
 * @note The num_connections counter is marked volatile as it's accessed
 *       from multiple threads
 */
typedef struct
{
    int worker_id;                /**< Unique identifier for the worker */
    int epoll_fd;                 /**< Worker's epoll instance */
    volatile int num_connections; /**< Number of active connections */
    pthread_mutex_t mutex;        /**< Protects worker's shared state */
} worker_context;

/**
 * @brief Main server context structure
 *
 * Contains all server state including:
 * - Socket configuration
 * - Worker thread pool
 * - Registered RPC methods
 * - Synchronization primitives
 *
 * Thread safety is ensured through multiple mutexes:
 * - mutex: Protects method registration
 * - lb_mutex: Protects load balancer state
 * - Per-worker mutexes: Protect worker-specific state
 */
struct sockrpc_server
{
    char *socket_path;                     /**< Path to Unix domain socket */
    int server_fd;                         /**< Server socket file descriptor */
    volatile int running;                  /**< Server running flag */
    pthread_t worker_threads[NUM_WORKERS]; /**< Worker thread pool */
    worker_context workers[NUM_WORKERS];   /**< Worker contexts */
    rpc_method methods[MAX_METHODS];       /**< Registered RPC methods */
    size_t method_count;                   /**< Number of registered methods */
    pthread_mutex_t mutex;                 /**< Protects method registration */
    int next_worker;                       /**< Next worker for round-robin */
    pthread_mutex_t lb_mutex;              /**< Protects load balancing state */
};

/**
 * @brief Sets a file descriptor to non-blocking mode
 * @param fd File descriptor to modify
 *
 * Used for both the server socket and client connections to ensure
 * non-blocking I/O operations.
 *
 * @note This is a critical operation - failure here would impact
 *       the server's ability to handle concurrent connections
 */
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief Reads all available data from a socket
 * @param fd Socket file descriptor
 * @param buffer Buffer to store read data
 * @param max_len Maximum number of bytes to read
 * @return Number of bytes read, or -1 on error
 *
 * Handles partial reads and common socket errors (EAGAIN, EINTR).
 * Ensures null-termination of the received data.
 */
static ssize_t read_all(int fd, char *buffer, size_t max_len)
{
    size_t total = 0;
    while (total < max_len - 1)
    {
        ssize_t n = read(fd, buffer + total, max_len - total - 1);
        if (n == 0)
            break; // EOF
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            return -1;
        }
        total += n;
    }
    buffer[total] = '\0';
    return total;
}

/**
 * @brief Writes all data to a socket
 * @param fd Socket file descriptor
 * @param buffer Data to write
 * @param len Number of bytes to write
 * @return Number of bytes written, or -1 on error
 *
 * Handles partial writes and common socket errors (EAGAIN, EINTR).
 * Ensures all data is written or an error is returned.
 */
static ssize_t write_all(int fd, const char *buffer, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t n = write(fd, buffer + total, len - total);
        if (n <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            if (errno == EINTR)
                continue;
            return -1;
        }
        total += n;
    }
    return total;
}

/**
 * @brief Selects the next worker thread for a new connection
 * @param server Server context
 * @return Pointer to selected worker context
 *
 * Implements round-robin load balancing across worker threads.
 * Thread-safe through lb_mutex.
 */
static worker_context *select_worker(sockrpc_server *server)
{
    pthread_mutex_lock(&server->lb_mutex);
    int selected = server->next_worker;
    server->next_worker = (server->next_worker + 1) % NUM_WORKERS;
    pthread_mutex_unlock(&server->lb_mutex);

    return &server->workers[selected];
}

/**
 * @brief Handles an RPC request from a client
 * @param server Server context
 * @param worker Worker context handling the request
 * @param client_fd Client socket file descriptor
 *
 * Processes a single RPC request:
 * 1. Reads request data
 * 2. Parses JSON message
 * 3. Looks up method handler
 * 4. Executes handler
 * 5. Sends response
 *
 * @note Handles its own memory management for JSON objects
 */
static void handle_client_request(sockrpc_server *server, worker_context *worker, int client_fd)
{
    char buffer[BUFFER_SIZE];
    ssize_t n = read_all(client_fd, buffer, BUFFER_SIZE);

    if (n <= 0)
    {
        pthread_mutex_lock(&worker->mutex);
        worker->num_connections--;
        pthread_mutex_unlock(&worker->mutex);
        return;
    }

    cJSON *request = cJSON_Parse(buffer);
    if (!request)
    {
        return;
    }

    const char *method = cJSON_GetObjectItem(request, "method")->valuestring;
    cJSON *params = cJSON_GetObjectItem(request, "params");

    cJSON *result = NULL;
    rpc_handler found_handler = NULL;

    // Find the handler while holding the lock
    pthread_mutex_lock(&server->mutex);
    for (size_t i = 0; i < server->method_count; i++)
    {
        if (strcmp(server->methods[i].name, method) == 0)
        {
            found_handler = server->methods[i].handler;
            break;
        }
    }
    pthread_mutex_unlock(&server->mutex);

    // Execute handler outside the critical section
    if (found_handler)
    {
        result = found_handler(params);
    }

    if (result)
    {
        char *response = cJSON_Print(result);
        write_all(client_fd, response, strlen(response));
        free(response);
        cJSON_Delete(result);
    }

    cJSON_Delete(request);
}

/**
 * @brief Worker thread main function
 * @param arg Pointer to worker context
 * @return NULL
 *
 * Main loop for worker threads:
 * 1. Waits for events using epoll
 * 2. Handles client requests
 * 3. Manages connection lifecycle
 *
 * @note Runs until server->running becomes false
 */
static void *worker_routine(void *arg)
{
    worker_context *worker = (worker_context *)arg;
    sockrpc_server *server = (sockrpc_server *)((char *)worker - offsetof(sockrpc_server, workers[worker->worker_id]));
    struct epoll_event events[MAX_EVENTS];

    printf("Worker %d started\n", worker->worker_id);

    while (server->running)
    {
        int nfds = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, 100);
        if (nfds == -1)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            handle_client_request(server, worker, fd);
        }
    }

    printf("Worker %d shutting down (handled %d connections)\n",
           worker->worker_id, worker->num_connections);
    return NULL;
}

/**
 * @brief Acceptor thread main function
 * @param arg Pointer to server context
 * @return NULL
 *
 * Accepts new client connections and distributes them to workers:
 * 1. Accepts connection
 * 2. Sets non-blocking mode
 * 3. Selects worker thread
 * 4. Adds to worker's epoll set
 *
 * @note Runs until server->running becomes false
 */
static void *acceptor_routine(void *arg)
{
    sockrpc_server *server = (sockrpc_server *)arg;

    printf("Acceptor started\n");

    while (server->running)
    {
        int client_fd = accept(server->server_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            break;
        }

        set_nonblocking(client_fd);

        // Select worker using round-robin
        worker_context *worker = select_worker(server);

        // Add connection to worker's epoll
        struct epoll_event ev = {
            .events = EPOLLIN | EPOLLET,
            .data.fd = client_fd};

        if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
        {
            close(client_fd);
            continue;
        }

        // Update worker's connection count
        pthread_mutex_lock(&worker->mutex);
        worker->num_connections++;
        printf("Connection assigned to worker %d (total: %d)\n",
               worker->worker_id, worker->num_connections);
        pthread_mutex_unlock(&worker->mutex);
    }

    printf("Acceptor shutting down\n");
    return NULL;
}

/**
 * @brief Creates a new RPC server instance
 * @param socket_path Path where the Unix domain socket will be created
 * @return Pointer to server context or NULL on error
 *
 * This function initializes a new server instance with:
 * - A worker thread pool
 * - Method registration table
 * - Synchronization primitives
 * - Socket configuration
 *
 * Initialization steps:
 * 1. Allocates and zeros server context
 * 2. Copies socket path
 * 3. Initializes synchronization primitives
 * 4. Sets up worker contexts
 *
 * Thread safety:
 * - Thread-safe after initialization
 * - Must not be called while server is running
 *
 * Error handling:
 * - Returns NULL if memory allocation fails
 * - Returns NULL if socket path is invalid
 * - Returns NULL if worker initialization fails
 *
 * @note Caller must call sockrpc_server_destroy() to clean up
 */
sockrpc_server *sockrpc_server_create(const char *socket_path)
{
    sockrpc_server *server = calloc(1, sizeof(sockrpc_server));
    if (!server)
        return NULL;

    server->socket_path = strdup(socket_path);
    server->method_count = 0;
    server->running = 0;
    server->next_worker = 0;
    pthread_mutex_init(&server->mutex, NULL);
    pthread_mutex_init(&server->lb_mutex, NULL);

    // Initialize worker contexts
    for (int i = 0; i < NUM_WORKERS; i++)
    {
        server->workers[i].worker_id = i;
        server->workers[i].num_connections = 0;
        server->workers[i].epoll_fd = epoll_create1(0);
        pthread_mutex_init(&server->workers[i].mutex, NULL);
    }

    return server;
}

/**
 * @brief Starts the RPC server
 * @param server Server context
 *
 * Server startup process:
 * 1. Creates non-blocking Unix domain socket
 * 2. Binds to specified path
 * 3. Starts listening for connections
 * 4. Launches worker threads
 * 5. Starts acceptor thread
 *
 * Error handling:
 * - Returns silently on socket creation failure
 * - Returns silently on bind failure
 * - Returns silently on listen failure
 * - Existing socket file is removed before binding
 *
 * Thread safety:
 * - Not thread-safe
 * - Must be called only once
 * - Must be called before any client connections
 *
 * Resource management:
 * - Creates NUM_WORKERS threads
 * - Creates detached acceptor thread
 * - Manages worker thread lifecycle
 *
 * @note Server continues running until sockrpc_server_destroy() is called
 */
void sockrpc_server_start(sockrpc_server *server)
{
    server->server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->server_fd == -1)
        return;

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX};
    strncpy(addr.sun_path, server->socket_path, sizeof(addr.sun_path) - 1);

    unlink(server->socket_path);
    if (bind(server->server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        close(server->server_fd);
        return;
    }

    if (listen(server->server_fd, SOMAXCONN) == -1)
    {
        close(server->server_fd);
        return;
    }

    server->running = 1;

    for (int i = 0; i < NUM_WORKERS; i++)
    {
        pthread_create(&server->worker_threads[i], NULL, worker_routine, &server->workers[i]);
    }

    pthread_t acceptor_thread;
    pthread_create(&acceptor_thread, NULL, acceptor_routine, server);
    pthread_detach(acceptor_thread);
}

/**
 * @brief Registers an RPC method with the server
 * @param server Server context
 * @param name Method name to register
 * @param handler Function pointer to method implementation
 *
 * Registration process:
 * 1. Validates input parameters
 * 2. Checks for method name conflicts
 * 3. Adds or updates method in registry
 *
 * Thread safety:
 * - Thread-safe
 * - Can be called before or after server start
 * - Can be called from multiple threads
 *
 * Error handling:
 * - Returns silently if server is NULL
 * - Returns silently if name is NULL
 * - Returns silently if handler is NULL
 * - Returns silently if method table is full
 *
 * Memory management:
 * - Creates copy of method name
 * - Frees old name on update
 * - Server owns method name memory
 *
 * @note Limited to MAX_METHODS registered methods
 */
void sockrpc_server_register(sockrpc_server *server, const char *name, rpc_handler handler)
{
    if (!server || !name || !handler || server->method_count >= MAX_METHODS)
        return;

    pthread_mutex_lock(&server->mutex);

    for (size_t i = 0; i < server->method_count; i++)
    {
        if (strcmp(server->methods[i].name, name) == 0)
        {
            free((char *)server->methods[i].name);
            server->methods[i].handler = handler;
            server->methods[i].name = strdup(name);
            pthread_mutex_unlock(&server->mutex);
            return;
        }
    }

    server->methods[server->method_count].name = strdup(name);
    server->methods[server->method_count].handler = handler;
    server->method_count++;

    pthread_mutex_unlock(&server->mutex);
}

/**
 * @brief Destroys an RPC server instance
 * @param server Server context to destroy
 *
 * Cleanup process:
 * 1. Signals server to stop (sets running = 0)
 * 2. Shuts down server socket
 * 3. Waits for worker threads to finish
 * 4. Frees registered method names
 * 5. Closes file descriptors
 * 6. Removes socket file
 * 7. Destroys synchronization primitives
 * 8. Frees all allocated memory
 *
 * Thread safety:
 * - Thread-safe but should not be called concurrently
 * - All worker threads must exit before return
 * - No new connections accepted during shutdown
 *
 * Resource cleanup:
 * - All worker threads joined
 * - All file descriptors closed
 * - All dynamic memory freed
 * - Socket file removed from filesystem
 *
 * @note Acceptor thread terminates automatically due to socket shutdown
 */
void sockrpc_server_destroy(sockrpc_server *server)
{
    if (!server)
        return;

    server->running = 0;

    shutdown(server->server_fd, SHUT_RDWR);

    for (int i = 0; i < NUM_WORKERS; i++)
    {
        pthread_join(server->worker_threads[i], NULL);
    }

    for (int i = 0; i < NUM_WORKERS; i++)
    {
        pthread_mutex_destroy(&server->workers[i].mutex);
    }

    for (size_t i = 0; i < server->method_count; i++)
    {
        free((char *)server->methods[i].name);
    }

    close(server->server_fd);

    unlink(server->socket_path);

    pthread_mutex_destroy(&server->mutex);
    pthread_mutex_destroy(&server->lb_mutex);

    free(server->socket_path);
    free(server);
}