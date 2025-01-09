# SockRPC Library

A lightweight RPC (Remote Procedure Call) library for Linux that enables inter-process communication using Unix domain sockets, with support for both synchronous and asynchronous operations.

## Features

- Unix domain socket-based IPC
- JSON message format using cJSON
- Thread-safe client implementation
- Multi-threaded server with worker pool
- Event-driven architecture using epoll
- Support for both synchronous and asynchronous calls
- Simple method registration system
- Persistent storage support (in database example)
- Comprehensive example applications

## Dependencies

- Linux OS
- GCC compiler
- libcjson-dev
- pthread

## Building

```bash
# Build the library
make

# Build the examples
make examples

# Run tests (with Valgrind memory checks)
make test

# Run tests without memory checks (faster)
make test-fast
```

## Examples

The library comes with several example applications demonstrating different use cases:

### 1. Basic Example
Simple demonstration of sync/async RPC calls:

```bash
# Terminal 1
./examples/basic/basic_server

# Terminal 2
./examples/basic/basic_client
```

### 2. String Operations
Text processing operations (uppercase, word count, reverse):

```bash
# Terminal 1
./examples/string_ops/string_server

# Terminal 2 - Interactive mode
./examples/string_ops/string_client

# Or command line mode
./examples/string_ops/string_client uppercase "hello world"
./examples/string_ops/string_client wordcount "count these words"
./examples/string_ops/string_client reverse "reverse this"
```

### 3. Calculator
Mathematical operations and statistical calculations:

```bash
# Terminal 1
./examples/calculator/calc_server

# Terminal 2 - Interactive mode
./examples/calculator/calc_client

# Or command line mode
./examples/calculator/calc_client calculate add 5 3
./examples/calculator/calc_client calculate multiply 4 6
./examples/calculator/calc_client stats 1 2 3 4 5
```

### 4. Database
Key-value store with persistent storage:

```bash
# Terminal 1
./examples/database/db_server

# Terminal 2 - Interactive mode
./examples/database/db_client

# Or command line mode
./examples/database/db_client set mykey "my value"
./examples/database/db_client get mykey
./examples/database/db_client list
./examples/database/db_client delete mykey
```

## API Reference

### Server API

```c
// Create a new server instance
sockrpc_server* sockrpc_server_create(const char* socket_path);

// Register an RPC method
void sockrpc_server_register(sockrpc_server* server, 
                           const char* name, 
                           rpc_handler handler);

// Start the server
void sockrpc_server_start(sockrpc_server* server);

// Destroy server instance
void sockrpc_server_destroy(sockrpc_server* server);
```

### Client API

```c
// Create a new client instance
sockrpc_client* sockrpc_client_create(const char* socket_path);

// Make synchronous RPC call
cJSON* sockrpc_client_call_sync(sockrpc_client* client,
                               const char* method,
                               cJSON* params);

// Make asynchronous RPC call
void sockrpc_client_call_async(sockrpc_client* client,
                              const char* method,
                              cJSON* params,
                              void (*callback)(cJSON* result));

// Destroy client instance
void sockrpc_client_destroy(sockrpc_client* client);
```

## Project Structure

```
.
├── examples/       # Example applications
│   ├── basic/      # Basic usage example
│   ├── calculator/ # Calculator service
│   ├── database/   # Key-value store
│   └── string_ops/ # String operations
├── include/        # Public headers
│   └── sockrpc/
├── src/            # Library source
├── tests/          # Test suites
├── lib/            # Built library
├── docs/           # Documentation
└── build/          # Build artifacts directory
```

## Best Practices

1. Error Handling
   - Always check return values from API calls
   - Clean up resources properly
   - Handle connection failures gracefully

2. Thread Safety
   - Don't share client instances between threads
   - Make server handlers thread-safe
   - Use proper synchronization for shared resources

3. Performance
   - Keep messages reasonably sized
   - Use async calls for non-blocking operations
   - Implement timeout handling
   - Consider connection pooling for high-load scenarios

## Testing

The library includes two test suites:

1. Unit Tests (`tests/test_suite.c`)
   - Tests basic functionality
   - Verifies API behavior
   - Checks error handling

2. Stress Tests (`tests/stress_test.c`)
   - Tests under load
   - Verifies thread safety
   - Checks memory management
   - Tests concurrent operations

Run tests with memory checks:
```bash
make test
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.