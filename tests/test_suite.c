#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <ctype.h>
#include "sockrpc/sockrpc.h"

// Test handlers
static cJSON *echo_handler(cJSON *params)
{
    return cJSON_Duplicate(params, 1);
}

static cJSON *add_handler(cJSON *params)
{
    int a = cJSON_GetArrayItem(params, 0)->valueint;
    int b = cJSON_GetArrayItem(params, 1)->valueint;
    return cJSON_CreateNumber(a + b);
}

static cJSON *string_handler(cJSON *params)
{
    const char *str = cJSON_GetObjectItem(params, "text")->valuestring;
    char *upper = strdup(str);
    for (int i = 0; upper[i]; i++)
    {
        upper[i] = toupper(upper[i]);
    }
    cJSON *result = cJSON_CreateString(upper);
    free(upper);
    return result;
}

// Test callback for async calls
static void async_callback(cJSON *result)
{
    assert(result != NULL);
    char *str = cJSON_Print(result);
    printf("Async result: %s\n", str);
    free(str);
    cJSON_Delete(result);
}

// Helper function to test if socket file exists and is accessible
static int test_socket_exists(const char *path)
{
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX};
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
        return 0;

    int result = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);

    return result == 0;
}

// Test basic server creation and destruction
static void test_server_lifecycle()
{
    printf("Testing server lifecycle...\n");

    // Ensure socket doesn't exist
    unlink("/tmp/test1.sock");

    sockrpc_server *server = sockrpc_server_create("/tmp/test1.sock");
    assert(server != NULL);

    sockrpc_server_start(server);

    // Wait for server to start (up to 1 second)
    int retries = 10;
    while (retries-- > 0)
    {
        if (test_socket_exists("/tmp/test1.sock"))
            break;
        usleep(100000); // 100ms
    }
    assert(test_socket_exists("/tmp/test1.sock"));

    sockrpc_server_destroy(server);

    // Verify cleanup
    assert(!test_socket_exists("/tmp/test1.sock"));

    printf("Server lifecycle test passed\n");
}

// Test basic client creation and destruction
static void test_client_lifecycle()
{
    printf("Testing client lifecycle...\n");

    sockrpc_server *server = sockrpc_server_create("/tmp/test2.sock");
    sockrpc_server_start(server);
    usleep(100000); // Give server time to start

    sockrpc_client *client = sockrpc_client_create("/tmp/test2.sock");
    assert(client != NULL);

    sockrpc_client_destroy(client);
    sockrpc_server_destroy(server);
    printf("Client lifecycle test passed\n");
}

// Test synchronous RPC calls
static void test_sync_calls()
{
    printf("Testing synchronous calls...\n");

    sockrpc_server *server = sockrpc_server_create("/tmp/test3.sock");
    sockrpc_server_register(server, "echo", echo_handler);
    sockrpc_server_register(server, "add", add_handler);
    sockrpc_server_start(server);
    usleep(100000); // Give server time to start

    sockrpc_client *client = sockrpc_client_create("/tmp/test3.sock");

    // Test echo
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "message", "hello");
    cJSON *result = sockrpc_client_call_sync(client, "echo", cJSON_Duplicate(params, 1));
    assert(result != NULL);
    assert(strcmp(cJSON_GetObjectItem(result, "message")->valuestring, "hello") == 0);
    cJSON_Delete(result);
    cJSON_Delete(params);

    // Test add
    params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateNumber(5));
    cJSON_AddItemToArray(params, cJSON_CreateNumber(3));
    result = sockrpc_client_call_sync(client, "add", cJSON_Duplicate(params, 1));
    assert(result != NULL);
    assert(result->valuedouble == 8);
    cJSON_Delete(result);
    cJSON_Delete(params);

    sockrpc_client_destroy(client);
    sockrpc_server_destroy(server);
    printf("Synchronous calls test passed\n");
}

// Test asynchronous RPC calls
static void test_async_calls()
{
    printf("Testing asynchronous calls...\n");

    sockrpc_server *server = sockrpc_server_create("/tmp/test4.sock");
    sockrpc_server_register(server, "string", string_handler);
    sockrpc_server_start(server);
    usleep(100000); // Give server time to start

    sockrpc_client *client = sockrpc_client_create("/tmp/test4.sock");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "text", "hello world");
    // Note: async call will take ownership of params
    sockrpc_client_call_async(client, "string", params, async_callback);
    usleep(100000); // Give time for async call to complete

    sockrpc_client_destroy(client);
    sockrpc_server_destroy(server);
    printf("Asynchronous calls test passed\n");
}

// Test multiple methods
static void test_multiple_methods()
{
    printf("Testing multiple methods...\n");

    sockrpc_server *server = sockrpc_server_create("/tmp/test5.sock");
    sockrpc_server_register(server, "echo", echo_handler);
    sockrpc_server_register(server, "add", add_handler);
    sockrpc_server_register(server, "string", string_handler);
    sockrpc_server_start(server);
    usleep(100000); // Give server time to start

    sockrpc_client *client = sockrpc_client_create("/tmp/test5.sock");

    // Echo test
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "test", "multiple");
    cJSON *result = sockrpc_client_call_sync(client, "echo", cJSON_Duplicate(params, 1));
    assert(result != NULL);
    cJSON_Delete(result);
    cJSON_Delete(params);

    // Add test
    params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateNumber(10));
    cJSON_AddItemToArray(params, cJSON_CreateNumber(20));
    result = sockrpc_client_call_sync(client, "add", cJSON_Duplicate(params, 1));
    assert(result != NULL);
    assert(result->valuedouble == 30);
    cJSON_Delete(result);
    cJSON_Delete(params);

    // String test
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "text", "multiple test");
    // Note: async call will take ownership of params
    sockrpc_client_call_async(client, "string", params, async_callback);
    usleep(100000); // Give time for async call

    sockrpc_client_destroy(client);
    sockrpc_server_destroy(server);
    printf("Multiple methods test passed\n");
}

int main()
{
    // Ignore SIGPIPE to prevent crashes when writing to closed sockets
    signal(SIGPIPE, SIG_IGN);

    test_server_lifecycle();
    test_client_lifecycle();
    test_sync_calls();
    test_async_calls();
    test_multiple_methods();

    printf("\nAll tests passed successfully!\n");
    return 0;
}