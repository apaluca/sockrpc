#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sockrpc/sockrpc.h"

#define MAX_INPUT 1024

static void print_result(cJSON *result)
{
    if (!result)
    {
        printf("Error: Operation failed\n");
        return;
    }

    char *str = cJSON_Print(result);
    if (cJSON_IsArray(result))
    {
        // Format list output
        cJSON *item;
        int count = 0;
        printf("\nDatabase entries:\n");
        printf("%-32s %s\n", "Key", "Value");
        printf("-------------------------------- --------------------------------\n");
        cJSON_ArrayForEach(item, result)
        {
            const char *key = cJSON_GetObjectItem(item, "key")->valuestring;
            const char *value = cJSON_GetObjectItem(item, "value")->valuestring;
            printf("%-32s %s\n", key, value);
            count++;
        }
        printf("\nTotal entries: %d\n", count);
    }
    else
    {
        // Single operation result
        printf("Result: %s\n", str);
    }

    free(str);
    cJSON_Delete(result);
}

static void db_operation(sockrpc_client *client, const char *operation,
                         const char *key, const char *value)
{
    cJSON *params = cJSON_CreateObject();
    if (key)
    {
        cJSON_AddStringToObject(params, "key", key);
    }
    if (value)
    {
        cJSON_AddStringToObject(params, "value", value);
    }

    printf("\nExecuting operation '%s'", operation);
    if (key)
        printf(" on key '%s'", key);
    if (value)
        printf(" with value '%s'", value);
    printf(":\n");

    cJSON *result = sockrpc_client_call_sync(client, operation, params);
    print_result(result);
}

static void interactive_mode(sockrpc_client *client)
{
    char input[MAX_INPUT];
    char key[256];
    char value[1024];

    while (1)
    {
        printf("\nAvailable operations:\n");
        printf("1. Set key-value pair\n");
        printf("2. Get value by key\n");
        printf("3. Delete key-value pair\n");
        printf("4. List all entries\n");
        printf("5. Quit\n");
        printf("\nEnter choice: ");

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        int choice = atoi(input);

        switch (choice)
        {
        case 1:
        {
            printf("Enter key: ");
            if (fgets(key, sizeof(key), stdin) == NULL)
                break;
            key[strcspn(key, "\n")] = 0;

            printf("Enter value: ");
            if (fgets(value, sizeof(value), stdin) == NULL)
                break;
            value[strcspn(value, "\n")] = 0;

            db_operation(client, "set", key, value);
            break;
        }

        case 2:
        {
            printf("Enter key: ");
            if (fgets(key, sizeof(key), stdin) == NULL)
                break;
            key[strcspn(key, "\n")] = 0;

            db_operation(client, "get", key, NULL);
            break;
        }

        case 3:
        {
            printf("Enter key to delete: ");
            if (fgets(key, sizeof(key), stdin) == NULL)
                break;
            key[strcspn(key, "\n")] = 0;

            db_operation(client, "delete", key, NULL);
            break;
        }

        case 4:
            db_operation(client, "list", NULL, NULL);
            break;

        case 5:
            return;

        default:
            printf("Invalid choice\n");
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    sockrpc_client *client = sockrpc_client_create("/tmp/db_rpc.sock");
    if (!client)
    {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    if (argc > 1)
    {
        // Command line mode
        const char *operation = argv[1];

        if (!strcmp(operation, "help") || !strcmp(operation, "--help"))
        {
            printf("Usage:\n");
            printf("  %s set <key> <value>\n", argv[0]);
            printf("  %s get <key>\n", argv[0]);
            printf("  %s delete <key>\n", argv[0]);
            printf("  %s list\n", argv[0]);
            sockrpc_client_destroy(client);
            return 1;
        }

        if (!strcmp(operation, "set") && argc == 4)
        {
            db_operation(client, "set", argv[2], argv[3]);
        }
        else if (!strcmp(operation, "get") && argc == 3)
        {
            db_operation(client, "get", argv[2], NULL);
        }
        else if (!strcmp(operation, "delete") && argc == 3)
        {
            db_operation(client, "delete", argv[2], NULL);
        }
        else if (!strcmp(operation, "list") && argc == 2)
        {
            db_operation(client, "list", NULL, NULL);
        }
        else
        {
            fprintf(stderr, "Invalid command line arguments\n");
            fprintf(stderr, "Use --help for usage information\n");
            sockrpc_client_destroy(client);
            return 1;
        }
    }
    else
    {
        // Interactive mode
        printf("Database Client\n");
        interactive_mode(client);
    }

    sockrpc_client_destroy(client);
    return 0;
}