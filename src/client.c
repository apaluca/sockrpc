#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sockrpc/sockrpc.h"

/**
 * @file client.c
 * @brief Implementation of the SockRPC client component
 *
 * This file implements a thread-safe RPC client using Unix domain sockets.
 * The client supports both synchronous and asynchronous RPC calls, with
 * proper resource management and error handling.
 *
 * Key features:
 * - Thread-safe operations
 * - Synchronous and asynchronous calls
 * - Automatic resource cleanup
 * - JSON message serialization
 *
 * @note The client uses JSON for message serialization via the cJSON library
 */

/**
 * @brief Maximum size for message buffers
 * @note Should match server's BUFFER_SIZE for optimal operation
 */
#define BUFFER_SIZE 4096

/**
 * @brief Client context structure
 *
 * Contains client state and synchronization primitives. The mutex ensures
 * thread-safe access to the socket connection.
 *
 * Thread safety guarantees:
 * - Multiple threads can safely share a client instance
 * - Each RPC call is atomic
 * - Async calls create their own thread
 */
struct sockrpc_client
{
    int fd;                /**< Socket file descriptor */
    pthread_mutex_t mutex; /**< Mutex for thread safety */
};

/**
 * @brief Context structure for asynchronous calls
 *
 * Contains all data needed to execute an asynchronous RPC call in a
 * separate thread. The structure owns all its memory and is responsible
 * for cleanup.
 *
 * Memory ownership:
 * - method: Owned by the structure (freed after call)
 * - params: Transferred to the RPC call
 * - result: Transferred to the callback
 */
struct async_call_data
{
    sockrpc_client *client;          /**< Reference to client context */
    char *method;                    /**< Method name (owned) */
    cJSON *params;                   /**< Parameters (transferred) */
    void (*callback)(cJSON *result); /**< Result callback */
    pthread_mutex_t mutex;           /**< Thread synchronization */
};

/**
 * @brief Creates a new client instance
 * @param socket_path Path to server's Unix domain socket
 * @return New client context or NULL on error
 *
 * Initialization process:
 * 1. Allocates client context
 * 2. Creates Unix domain socket
 * 3. Connects to server
 * 4. Initializes synchronization primitives
 *
 * Error handling:
 * - Returns NULL if memory allocation fails
 * - Returns NULL if socket creation fails
 * - Returns NULL if connection fails
 *
 * @note The client must be destroyed using sockrpc_client_destroy()
 */
sockrpc_client *sockrpc_client_create(const char *socket_path)
{
    sockrpc_client *client = calloc(1, sizeof(sockrpc_client));

    client->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX};
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    connect(client->fd, (struct sockaddr *)&addr, sizeof(addr));
    pthread_mutex_init(&client->mutex, NULL);

    return client;
}

/**
 * @brief Makes a synchronous RPC call
 * @param client Client context
 * @param method Method name to call
 * @param params JSON parameters (ownership transferred)
 * @return JSON result or NULL on error
 *
 * Call process:
 * 1. Creates JSON request object
 * 2. Sends request to server
 * 3. Waits for response
 * 4. Parses response
 *
 * Thread safety:
 * - Safe to call from multiple threads
 * - Blocks until response received
 * - Only one call active at a time per client
 *
 * Memory management:
 * - Takes ownership of params
 * - Returns newly allocated result
 * - Caller must free result using cJSON_Delete()
 *
 * @note Blocks until server responds or error occurs
 */
cJSON *sockrpc_client_call_sync(sockrpc_client *client, const char *method, cJSON *params)
{
    // JSON operations outside the lock
    cJSON *request = cJSON_CreateObject();
    if (!request)
    {
        cJSON_Delete(params);
        return NULL;
    }

    cJSON_AddStringToObject(request, "method", method);
    cJSON_AddItemToObject(request, "params", params);

    char *request_str = cJSON_Print(request);
    cJSON_Delete(request);
    if (!request_str)
    {
        return NULL;
    }

    // Only lock the socket operations
    pthread_mutex_lock(&client->mutex);

    write(client->fd, request_str, strlen(request_str));
    free(request_str);

    char buffer[BUFFER_SIZE];
    ssize_t n = read(client->fd, buffer, BUFFER_SIZE);

    pthread_mutex_unlock(&client->mutex);

    if (n <= 0)
        return NULL;

    buffer[n] = '\0';
    return cJSON_Parse(buffer);
}

/**
 * @brief Thread routine for asynchronous calls
 * @param arg Pointer to async_call_data
 * @return NULL
 *
 * Implementation:
 * 1. Extracts call data
 * 2. Makes synchronous RPC call
 * 3. Invokes callback with result
 * 4. Cleans up resources
 *
 * Memory management:
 * - Frees async_call_data structure
 * - Transfers result ownership to callback
 * - Handles cleanup if callback is NULL
 */
static void *async_call_routine(void *arg)
{
    struct async_call_data *data = (struct async_call_data *)arg;
    if (!data)
        return NULL;

    cJSON *result = sockrpc_client_call_sync(data->client, data->method, data->params);
    // params now owned and freed by call_sync

    if (data->callback)
    {
        data->callback(result); // Callback takes ownership of result
    }
    else if (result)
    {
        cJSON_Delete(result);
    }

    // Cleanup
    free(data->method);
    pthread_mutex_destroy(&data->mutex);
    free(data);

    return NULL;
}

/**
 * @brief Makes an asynchronous RPC call
 * @param client Client context
 * @param method Method name to call
 * @param params JSON parameters (ownership transferred)
 * @param callback Function to receive result
 *
 * Implementation:
 * 1. Creates async call context
 * 2. Spawns worker thread
 * 3. Returns immediately
 *
 * Thread safety:
 * - Safe to call from multiple threads
 * - Multiple async calls can be active
 * - Callback may be invoked from different thread
 *
 * Memory management:
 * - Takes ownership of params
 * - Callback takes ownership of result
 * - Internal cleanup handled automatically
 *
 * @note Callback may be NULL if result is not needed
 */
void sockrpc_client_call_async(sockrpc_client *client, const char *method, cJSON *params,
                               void (*callback)(cJSON *result))
{
    struct async_call_data *data = malloc(sizeof(struct async_call_data));
    data->client = client;
    data->method = strdup(method);
    data->params = params;
    data->callback = callback;
    pthread_mutex_init(&data->mutex, NULL); // Initialize the mutex

    pthread_t thread;
    pthread_create(&thread, NULL, async_call_routine, data);
    pthread_detach(thread);
}

/**
 * @brief Destroys a client instance
 * @param client Client context to destroy
 *
 * Cleanup process:
 * 1. Closes socket connection
 * 2. Destroys synchronization primitives
 * 3. Frees memory
 *
 * @note Outstanding async calls may be terminated
 */
void sockrpc_client_destroy(sockrpc_client *client)
{
    close(client->fd);
    pthread_mutex_destroy(&client->mutex);
    free(client);
}