#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sockrpc/sockrpc.h"

#define MAX_INPUT 1024

static void print_result(cJSON *result)
{
    char *str = cJSON_Print(result);
    printf("Result: %s\n", str);
    free(str);
    cJSON_Delete(result);
}

static void process_string(sockrpc_client *client, const char *operation, const char *text)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "text", text);

    printf("\nProcessing '%s' with operation '%s':\n", text, operation);

    cJSON *result = sockrpc_client_call_sync(client, operation, params);
    if (result)
    {
        print_result(result);
    }
    else
    {
        fprintf(stderr, "Operation failed\n");
    }
}

static void interactive_mode(sockrpc_client *client)
{
    char input[MAX_INPUT];

    while (1)
    {
        printf("\nAvailable operations:\n");
        printf("1. uppercase\n");
        printf("2. wordcount\n");
        printf("3. reverse\n");
        printf("4. quit\n");
        printf("\nEnter operation number: ");

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        int choice = atoi(input);
        if (choice == 4)
            break;

        const char *operations[] = {"uppercase", "wordcount", "reverse"};
        if (choice < 1 || choice > 3)
        {
            printf("Invalid choice\n");
            continue;
        }

        printf("Enter text: ");
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        input[strcspn(input, "\n")] = 0; // Remove newline

        process_string(client, operations[choice - 1], input);
    }
}

int main(int argc, char *argv[])
{
    sockrpc_client *client = sockrpc_client_create("/tmp/string_rpc.sock");
    if (!client)
    {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    if (argc > 1)
    {
        // Command line mode
        const char *operation = argv[1];
        const char *text = argv[2];

        if (argc < 3 || (!strcmp(operation, "help") || !strcmp(operation, "--help")))
        {
            printf("Usage: %s <operation> <text>\n", argv[0]);
            printf("Operations: uppercase, wordcount, reverse\n");
            sockrpc_client_destroy(client);
            return 1;
        }

        process_string(client, operation, text);
    }
    else
    {
        // Interactive mode
        printf("String Operations Client\n");
        interactive_mode(client);
    }

    sockrpc_client_destroy(client);
    return 0;
}