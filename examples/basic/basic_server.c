#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "sockrpc/sockrpc.h"

// Example RPC handler
static cJSON *add_numbers(cJSON *params)
{
    int a = cJSON_GetArrayItem(params, 0)->valueint;
    int b = cJSON_GetArrayItem(params, 1)->valueint;
    return cJSON_CreateNumber(a + b);
}

static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main()
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Start server
    sockrpc_server *server = sockrpc_server_create("/tmp/basic_rpc.sock");
    if (!server)
    {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    sockrpc_server_register(server, "add", add_numbers);
    sockrpc_server_start(server);

    printf("Basic RPC server started. Press Ctrl+C to exit.\n");

    // Keep running until signal
    while (running)
    {
        sleep(1);
    }

    printf("\nShutting down server...\n");
    sockrpc_server_destroy(server);
    return 0;
}