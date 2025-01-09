#ifndef SOCKRPC_H
#define SOCKRPC_H

#include <stddef.h>
#include <cjson/cJSON.h>

/**
 * @mainpage SockRPC Library
 *
 * SockRPC is a lightweight RPC library for Linux that enables inter-process
 * communication using Unix domain sockets. It provides both synchronous and
 * asynchronous operations with JSON-based message passing.
 *
 * Key features:
 * - Thread-safe client operations
 * - Multi-threaded server with worker pool
 * - JSON message format
 * - Synchronous and asynchronous calls
 * - Automatic resource management
 */

/**
 * @brief Function pointer type for RPC method handlers
 * @param params JSON object containing the method parameters
 * @return JSON object containing the method result or NULL on error
 *
 * The handler function is responsible for:
 * - Parsing and validating input parameters
 * - Performing the requested operation
 * - Creating and returning a JSON response
 * - Handling all memory management for the response
 *
 * Thread safety:
 * - Must be thread-safe as handlers are called from worker threads
 * - Multiple instances may execute concurrently
 *
 * Memory management:
 * - Handler receives ownership of params
 * - Must return newly allocated JSON object
 * - Must free params when done
 *
 * @note If params is NULL, handler should handle it gracefully
 * @note Return NULL to indicate error condition
 *
 * Example:
 * @code
 * cJSON* my_handler(cJSON* params) {
 *     // Always check params
 *     if (!params) return NULL;
 *
 *     // Extract parameters
 *     cJSON* value = cJSON_GetObjectItem(params, "key");
 *     if (!value || !cJSON_IsString(value)) {
 *         cJSON_Delete(params);
 *         return NULL;
 *     }
 *
 *     // Process and create response
 *     cJSON* result = cJSON_CreateObject();
 *     if (!result) {
 *         cJSON_Delete(params);
 *         return NULL;
 *     }
 *
 *     cJSON_AddStringToObject(result, "processed", value->valuestring);
 *     
 *     // Clean up and return
 *     cJSON_Delete(params);
 *     return result;
 * }
 * @endcode
 */
typedef cJSON *(*rpc_handler)(cJSON *params);

/**
 * @brief Structure for registering RPC methods
 *
 * This structure associates a method name with its handler function.
 * The server maintains an array of these structures for method dispatch.
 *
 * Thread safety:
 * - Registration is thread-safe
 * - Methods can be registered before or after server start
 * - Handler functions must be thread-safe
 *
 * Memory management:
 * - Server takes ownership of method names
 * - Handler functions remain caller's responsibility
 */
typedef struct {
    const char *name;    /**< Method name used in RPC calls */
    rpc_handler handler; /**< Function pointer to method handler */
} rpc_method;

/**
 * @brief Opaque server context structure
 *
 * The actual structure is defined in server.c. This provides
 * encapsulation of server implementation details.
 *
 * Features:
 * - Multi-threaded worker pool
 * - Method registration system
 * - Connection management
 * - Resource cleanup
 */
typedef struct sockrpc_server sockrpc_server;

/**
 * @brief Opaque client context structure
 *
 * The actual structure is defined in client.c. This provides
 * encapsulation of client implementation details.
 *
 * Features:
 * - Thread-safe operations
 * - Connection management
 * - Synchronous and asynchronous calls
 */
typedef struct sockrpc_client sockrpc_client;

/* Server API */

/**
 * @brief Create a new RPC server instance
 * @param socket_path Path where the Unix domain socket will be created
 * @return Pointer to server context or NULL on error
 *
 * Creates and initializes a new server instance with:
 * - Worker thread pool
 * - Method registration table
 * - Socket configuration
 * - Synchronization primitives
 *
 * Thread safety:
 * - Not thread-safe
 * - Create server before spawning any threads
 *
 * Error conditions (returns NULL):
 * - Invalid socket_path
 * - Memory allocation failure
 * - System resource limits reached
 *
 * @note The server is not started automatically
 * @note The path length must not exceed sizeof(sun_path) - 1 bytes
 * @note Any existing socket file will be removed on server start
 *
 * Example:
 * @code
 * sockrpc_server* server = sockrpc_server_create("/tmp/my_server.sock");
 * if (!server) {
 *     // Handle error
 *     return -1;
 * }
 * @endcode
 *
 * @see sockrpc_server_start
 * @see sockrpc_server_destroy
 */
sockrpc_server *sockrpc_server_create(const char *socket_path);

/**
 * @brief Register an RPC method with the server
 * @param server Server context
 * @param name Method name
 * @param handler Function pointer to method handler
 *
 * Thread safety:
 * - Thread-safe
 * - Can be called before or after server start
 * - Can be called from multiple threads
 *
 * Memory management:
 * - Server takes ownership of method name (creates copy)
 * - Handler function pointer must remain valid
 * - Previous handler for same name is replaced
 *
 * Error conditions (silently returns):
 * - NULL server
 * - NULL name
 * - NULL handler
 * - Maximum methods exceeded
 *
 * @note Method names are case-sensitive
 * @note Maximum methods limited by MAX_METHODS constant
 *
 * Example:
 * @code
 * // Register a method
 * sockrpc_server_register(server, "echo", handle_echo);
 *
 * // Replace existing method
 * sockrpc_server_register(server, "echo", handle_echo_v2);
 * @endcode
 *
 * @see rpc_handler
 */
void sockrpc_server_register(sockrpc_server *server, const char *name, rpc_handler handler);

/**
 * @brief Start the RPC server
 * @param server Server context
 *
 * Server startup process:
 * - Creates Unix domain socket
 * - Starts worker threads (NUM_WORKERS)
 * - Begins accepting client connections
 * - Returns immediately (server runs in background)
 *
 * Thread safety:
 * - Not thread-safe
 * - Call only once per server instance
 * - Call from main thread
 *
 * Error handling:
 * - Returns silently on socket creation failure
 * - Returns silently on bind failure
 * - Returns silently on thread creation failure
 *
 * @note Register methods before starting server
 * @note Existing socket file is removed before binding
 * @warning Ensure proper permissions for socket path
 *
 * Example:
 * @code
 * // Register methods first
 * sockrpc_server_register(server, "method1", handler1);
 * sockrpc_server_register(server, "method2", handler2);
 *
 * // Then start server
 * sockrpc_server_start(server);
 * @endcode
 *
 * @see sockrpc_server_register
 * @see sockrpc_server_destroy
 */
void sockrpc_server_start(sockrpc_server *server);

/**
 * @brief Destroy an RPC server instance
 * @param server Server context
 *
 * Cleanup process:
 * - Stops accepting new connections
 * - Waits for worker threads to finish
 * - Closes all client connections
 * - Removes socket file
 * - Frees all allocated memory
 *
 * Thread safety:
 * - Not thread-safe
 * - Call only once
 * - Call from main thread
 * - Do not use server after destroy
 *
 * Memory management:
 * - Frees all method names
 * - Frees server context
 * - Caller must ensure no threads are using server
 *
 * @note All pending operations are terminated
 * @note Waits for worker threads to complete
 * @warning Ensure no threads are using server
 *
 * Example:
 * @code
 * // Clean shutdown
 * sockrpc_server_destroy(server);
 * server = NULL;
 * @endcode
 */
void sockrpc_server_destroy(sockrpc_server *server);

/* Client API */

/**
 * @brief Create a new RPC client instance
 * @param socket_path Path to the server's Unix domain socket
 * @return Pointer to client context or NULL on error
 *
 * Initialization process:
 * - Creates socket connection to server
 * - Initializes client context
 * - Sets up synchronization primitives
 *
 * Thread safety:
 * - Thread-safe after creation
 * - Create separate instance for each thread for best performance
 *
 * Error conditions (returns NULL):
 * - Invalid socket path
 * - Server not running
 * - Memory allocation failure
 * - Connection failure
 *
 * @note Connection maintained until client destroyed
 * @note No automatic reconnection on failure
 *
 * Example:
 * @code
 * sockrpc_client* client = sockrpc_client_create("/tmp/server.sock");
 * if (!client) {
 *     // Handle connection failure
 *     return -1;
 * }
 * @endcode
 *
 * @see sockrpc_client_destroy
 */
sockrpc_client *sockrpc_client_create(const char *socket_path);

/**
 * @brief Make a synchronous RPC call
 * @param client Client context
 * @param method Method name to call
 * @param params JSON object containing method parameters (ownership transferred)
 * @return JSON object containing the result or NULL on error
 *
 * Thread safety:
 * - Thread-safe
 * - Multiple threads can share client
 * - Calls are serialized per client
 *
 * Memory management:
 * - Takes ownership of params (freed even on error)
 * - Returns newly allocated result
 * - Caller must free result with cJSON_Delete
 *
 * Error conditions (returns NULL):
 * - Invalid client
 * - Method not found
 * - Connection failure
 * - Memory allocation failure
 * - Invalid response from server
 *
 * @note Blocks until response received
 * @note Timeout depends on system socket timeout
 *
 * Example:
 * @code
 * // Create parameters
 * cJSON* params = cJSON_CreateObject();
 * cJSON_AddStringToObject(params, "key", "value");
 *
 * // Make call (params ownership transferred)
 * cJSON* result = sockrpc_client_call_sync(client, "method", params);
 * if (result) {
 *     // Process result
 *     cJSON_Delete(result);
 * }
 * @endcode
 */
cJSON *sockrpc_client_call_sync(sockrpc_client *client, const char *method, cJSON *params);

/**
 * @brief Make an asynchronous RPC call
 * @param client Client context
 * @param method Method name to call
 * @param params JSON object containing method parameters (ownership transferred)
 * @param callback Function to call with result (may be NULL)
 *
 * Thread safety:
 * - Thread-safe
 * - Multiple async calls can be active
 * - Callback runs in separate thread
 *
 * Memory management:
 * - Takes ownership of params (freed even on error)
 * - Result ownership transferred to callback
 * - Result freed automatically if callback NULL
 *
 * Error handling:
 * - Connection failures reported via callback
 * - Invalid responses reported via callback
 * - System errors reported via callback
 *
 * @note Method name copied internally
 * @note Callback must be thread-safe
 * @warning Callback may run in any thread
 *
 * Example:
 * @code
 * void handle_result(cJSON* result) {
 *     if (!result) {
 *         // Handle error
 *         return;
 *     }
 *     // Process result
 *     cJSON_Delete(result);
 * }
 *
 * // Create parameters
 * cJSON* params = cJSON_CreateObject();
 * cJSON_AddStringToObject(params, "key", "value");
 *
 * // Make async call (params ownership transferred)
 * sockrpc_client_call_async(client, "method", params, handle_result);
 * @endcode
 */
void sockrpc_client_call_async(sockrpc_client *client, const char *method, cJSON *params,
                               void (*callback)(cJSON *result));

/**
 * @brief Destroy an RPC client instance
 * @param client Client context
 *
 * Cleanup process:
 * - Cancels pending async calls
 * - Closes server connection
 * - Frees all resources
 *
 * Thread safety:
 * - Not thread-safe
 * - Ensure no calls in progress
 * - Call only once
 *
 * Memory management:
 * - Frees all internal buffers
 * - Pending async calls terminated
 * - Client context freed
 *
 * @note Callbacks for pending async calls will not be invoked
 * @warning Do not use client after destroying
 *
 * Example:
 * @code
 * sockrpc_client_destroy(client);
 * client = NULL;
 * @endcode
 */
void sockrpc_client_destroy(sockrpc_client *client);

#endif /* SOCKRPC_H */