#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sockrpc/sockrpc.h"

// Async callback
static void print_result(cJSON *result)
{
    char *str = cJSON_Print(result);
    printf("Async result: %s\n", str);
    free(str);
    cJSON_Delete(result);
}

int main()
{
    // Create client
    sockrpc_client *client = sockrpc_client_create("/tmp/basic_rpc.sock");
    if (!client)
    {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    // Prepare parameters
    cJSON *params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateNumber(5));
    cJSON_AddItemToArray(params, cJSON_CreateNumber(3));

    // Synchronous call
    printf("Making synchronous RPC call...\n");
    cJSON *result = sockrpc_client_call_sync(client, "add", cJSON_Duplicate(params, 1));
    if (result)
    {
        char *str = cJSON_Print(result);
        printf("Sync result: %s\n", str);
        free(str);
        cJSON_Delete(result);
    }
    else
    {
        fprintf(stderr, "Sync call failed\n");
    }

    // Asynchronous call
    printf("\nMaking asynchronous RPC call...\n");
    sockrpc_client_call_async(client, "add", params, print_result);

    // Wait a bit for async result
    sleep(1);

    // Cleanup
    sockrpc_client_destroy(client);
    return 0;
}