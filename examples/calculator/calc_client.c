#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sockrpc/sockrpc.h"

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

#define MAX_INPUT 1024
#define MAX_NUMBERS 100

static void print_result(cJSON *result)
{
    if (!result)
    {
        printf("Error: Operation failed\n");
        return;
    }

    cJSON *error = cJSON_GetObjectItem(result, "error");
    if (error && cJSON_IsString(error))
    {
        printf("Error: %s\n", error->valuestring);
    }
    else
    {
        char *str = cJSON_Print(result);
        printf("Result: %s\n", str);
        free(str);
    }
    cJSON_Delete(result);
}

static void calculate(sockrpc_client *client, const char *operation, double a, double b)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "operation", operation);
    cJSON_AddNumberToObject(params, "a", a);
    cJSON_AddNumberToObject(params, "b", b);

    printf("\nCalculating %g %s %g:\n", a, operation, b);
    cJSON *result = sockrpc_client_call_sync(client, "calculate", params);
    print_result(result);
}

static void calculate_stats(sockrpc_client *client, double *numbers, int count)
{
    cJSON *params = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();

    for (int i = 0; i < count; i++)
    {
        cJSON_AddItemToArray(array, cJSON_CreateNumber(numbers[i]));
    }
    cJSON_AddItemToObject(params, "numbers", array);

    printf("\nCalculating statistics for %d numbers:\n", count);
    cJSON *result = sockrpc_client_call_sync(client, "stats", params);
    print_result(result);
}

static void interactive_mode(sockrpc_client *client)
{
    char input[MAX_INPUT];

    while (1)
    {
        printf("\nAvailable operations:\n");
        printf("1. Basic calculation\n");
        printf("2. Statistical analysis\n");
        printf("3. Quit\n");
        printf("\nEnter choice: ");

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
        int choice = atoi(input);
        switch (choice)
        {
        case 1:
        {
            printf("\nAvailable operations: add, subtract, multiply, divide, power\n");
            printf("Enter operation: ");
            char operation[32];
            if (fgets(operation, sizeof(operation), stdin) == NULL)
                break;
            operation[strcspn(operation, "\n")] = 0;

            double a, b;
            printf("Enter first number: ");
            if (scanf("%lf", &a) != 1)
            {
                printf("Invalid input\n");
                while (getchar() != '\n')
                    ; // Clear input buffer
                break;
            }

            printf("Enter second number: ");
            if (scanf("%lf", &b) != 1)
            {
                printf("Invalid input\n");
                while (getchar() != '\n')
                    ;
                break;
            }
            while (getchar() != '\n')
                ; // Clear trailing newline

            calculate(client, operation, a, b);
            break;
        }

        case 2:
        {
            double numbers[MAX_NUMBERS];
            int count = 0;

            printf("\nEnter numbers (one per line, empty line to finish):\n");
            while (count < MAX_NUMBERS)
            {
                if (fgets(input, sizeof(input), stdin) == NULL)
                    break;
                if (input[0] == '\n')
                    break;

                char *endptr;
                numbers[count] = strtod(input, &endptr);
                if (endptr == input || (*endptr != '\n' && *endptr != 0))
                {
                    printf("Invalid input, skipping\n");
                    continue;
                }
                count++;
            }

            if (count > 0)
            {
                calculate_stats(client, numbers, count);
            }
            break;
        }

        case 3:
            return;

        default:
            printf("Invalid choice\n");
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    sockrpc_client *client = sockrpc_client_create("/tmp/calc_rpc.sock");
    if (!client)
    {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    if (argc > 1)
    {
        // Command line mode
        if (argc < 4 || (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help")))
        {
            printf("Usage:\n");
            printf("  %s calculate <operation> <a> <b>\n", argv[0]);
            printf("  %s stats <number1> [number2 ...]\n", argv[0]);
            printf("\nOperations: add, subtract, multiply, divide, power\n");
            sockrpc_client_destroy(client);
            return 1;
        }

        if (!strcmp(argv[1], "calculate") && argc == 5)
        {
            calculate(client, argv[2], atof(argv[3]), atof(argv[4]));
        }
        else if (!strcmp(argv[1], "stats"))
        {
            double numbers[MAX_NUMBERS];
            int count = MIN(argc - 2, MAX_NUMBERS);
            for (int i = 0; i < count; i++)
            {
                numbers[i] = atof(argv[i + 2]);
            }
            calculate_stats(client, numbers, count);
        }
        else
        {
            fprintf(stderr, "Invalid command line arguments\n");
            sockrpc_client_destroy(client);
            return 1;
        }
    }
    else
    {
        // Interactive mode
        printf("Calculator Client\n");
        interactive_mode(client);
    }

    sockrpc_client_destroy(client);
    return 0;
}