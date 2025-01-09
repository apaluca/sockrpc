#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <stdbool.h>
#include "sockrpc/sockrpc.h"

#define NUM_CLIENTS 5
#define OPERATIONS_PER_CLIENT 20
#define ARRAY_SIZE 20
#define STRING_SIZE 128
#define MATRIX_SIZE 3

// Timeout for the entire stress test (in seconds)
#define TEST_TIMEOUT 30

// Thread-safe context
typedef struct
{
    sockrpc_client *client;
    int client_id;
    pthread_mutex_t mutex;
    int success_count;
    int error_count;
    int pending_async;
} client_context;

// Global test variables
static sig_atomic_t running = 1;
static client_context *g_contexts[NUM_CLIENTS];
static pthread_mutex_t g_contexts_mutex = PTHREAD_MUTEX_INITIALIZER;

// Signal handler to stop test gracefully
static void handle_timeout(int sig)
{
    (void)sig;
    printf("\nTest timeout reached, stopping gracefully...\n");
    running = 0;
}

// Complex handlers
static cJSON *array_sort_handler(cJSON *params)
{
    int size = cJSON_GetArraySize(params);
    int *numbers = malloc(size * sizeof(int));

    // Extract numbers
    for (int i = 0; i < size; i++)
    {
        numbers[i] = cJSON_GetArrayItem(params, i)->valueint;
    }

    // Sort
    for (int i = 0; i < size; i++)
    {
        for (int j = i + 1; j < size; j++)
        {
            if (numbers[i] > numbers[j])
            {
                int temp = numbers[i];
                numbers[i] = numbers[j];
                numbers[j] = temp;
            }
        }
    }

    // Create result array
    cJSON *result = cJSON_CreateArray();
    for (int i = 0; i < size; i++)
    {
        cJSON_AddItemToArray(result, cJSON_CreateNumber(numbers[i]));
    }

    free(numbers);
    return result;
}

static cJSON *string_process_handler(cJSON *params)
{
    const char *input = cJSON_GetObjectItem(params, "text")->valuestring;
    char *processed = strdup(input);
    int len = strlen(processed);

    // Reverse
    for (int i = 0; i < len / 2; i++)
    {
        char temp = processed[i];
        processed[i] = processed[len - 1 - i];
        processed[len - 1 - i] = temp;
    }

    // To upper
    for (int i = 0; i < len; i++)
    {
        processed[i] = toupper(processed[i]);
    }

    cJSON *result = cJSON_CreateString(processed);
    free(processed);
    return result;
}

static cJSON *matrix_multiply_handler(cJSON *params)
{
    cJSON *matrix1 = cJSON_GetObjectItem(params, "matrix1");
    cJSON *matrix2 = cJSON_GetObjectItem(params, "matrix2");

    // Validate matrices
    if (!matrix1 || !matrix2 ||
        !cJSON_IsArray(matrix1) || !cJSON_IsArray(matrix2) ||
        cJSON_GetArraySize(matrix1) == 0 || cJSON_GetArraySize(matrix2) == 0)
    {
        // Return empty array for invalid input
        return cJSON_CreateArray();
    }

    int size = cJSON_GetArraySize(matrix1);
    if (size != cJSON_GetArraySize(matrix2))
    {
        return cJSON_CreateArray();
    }

    // Create result matrix
    cJSON *result = cJSON_CreateArray();
    for (int i = 0; i < size; i++)
    {
        cJSON *row1 = cJSON_GetArrayItem(matrix1, i);
        if (!row1 || !cJSON_IsArray(row1) || cJSON_GetArraySize(row1) != size)
        {
            cJSON_Delete(result);
            return cJSON_CreateArray();
        }

        cJSON *row = cJSON_CreateArray();
        for (int j = 0; j < size; j++)
        {
            int sum = 0;
            for (int k = 0; k < size; k++)
            {
                cJSON *row2 = cJSON_GetArrayItem(matrix2, k);
                if (!row2 || !cJSON_IsArray(row2) || cJSON_GetArraySize(row2) != size)
                {
                    cJSON_Delete(result);
                    return cJSON_CreateArray();
                }

                int a = cJSON_GetArrayItem(row1, k)->valueint;
                int b = cJSON_GetArrayItem(row2, j)->valueint;
                sum += a * b;
            }
            cJSON_AddItemToArray(row, cJSON_CreateNumber(sum));
        }
        cJSON_AddItemToArray(result, row);
    }

    return result;
}

// Async callback
static void async_callback(cJSON *result)
{
    client_context *found_ctx = NULL;

    pthread_mutex_lock(&g_contexts_mutex);
    for (int i = 0; i < NUM_CLIENTS; i++)
    {
        if (g_contexts[i])
        {
            found_ctx = g_contexts[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_contexts_mutex);

    if (found_ctx)
    {
        pthread_mutex_lock(&found_ctx->mutex);
        if (result)
            found_ctx->success_count++;
        else
            found_ctx->error_count++;
        found_ctx->pending_async--;
        pthread_mutex_unlock(&found_ctx->mutex);
    }

    if (result)
    {
        cJSON_Delete(result);
    }
}

// Client thread function
static void *client_thread(void *arg)
{
    client_context *ctx = (client_context *)arg;

    for (int i = 0; i < OPERATIONS_PER_CLIENT && running; i++)
    {
        int op = rand() % 3;

        switch (op)
        {
            // In stress_test.c - client_thread function
        case 0:
        { // Array sort
            cJSON *params = cJSON_CreateArray();
            for (int j = 0; j < ARRAY_SIZE; j++)
            {
                cJSON_AddItemToArray(params, cJSON_CreateNumber(rand()));
            }

            if (rand() % 2)
            {
                cJSON *result = sockrpc_client_call_sync(ctx->client, "sort", params);
                // params now owned by call_sync
                if (result)
                {
                    pthread_mutex_lock(&ctx->mutex);
                    ctx->success_count++;
                    pthread_mutex_unlock(&ctx->mutex);
                    cJSON_Delete(result);
                }
                else
                {
                    pthread_mutex_lock(&ctx->mutex);
                    ctx->error_count++;
                    pthread_mutex_unlock(&ctx->mutex);
                }
            }
            else
            {
                pthread_mutex_lock(&ctx->mutex);
                ctx->pending_async++;
                pthread_mutex_unlock(&ctx->mutex);
                sockrpc_client_call_async(ctx->client, "sort", params, async_callback);
                // params now owned by async call
            }
            break;
        }

        case 1:
        { // String processing
            cJSON *params = cJSON_CreateObject();
            char *random_string = malloc(STRING_SIZE);
            for (int j = 0; j < STRING_SIZE - 1; j++)
            {
                random_string[j] = 'a' + (rand() % 26);
            }
            random_string[STRING_SIZE - 1] = '\0';
            cJSON_AddStringToObject(params, "text", random_string);
            free(random_string);

            if (rand() % 2)
            {
                cJSON *result = sockrpc_client_call_sync(ctx->client, "process", params);
                // params now owned by call_sync
                if (result)
                {
                    pthread_mutex_lock(&ctx->mutex);
                    ctx->success_count++;
                    pthread_mutex_unlock(&ctx->mutex);
                    cJSON_Delete(result);
                }
                else
                {
                    pthread_mutex_lock(&ctx->mutex);
                    ctx->error_count++;
                    pthread_mutex_unlock(&ctx->mutex);
                }
            }
            else
            {
                pthread_mutex_lock(&ctx->mutex);
                ctx->pending_async++;
                pthread_mutex_unlock(&ctx->mutex);
                sockrpc_client_call_async(ctx->client, "process", params, async_callback);
                // params now owned by async call
            }
            break;
        }

        case 2:
        { // Matrix multiplication
            cJSON *params = cJSON_CreateObject();
            if (!params)
            {
                pthread_mutex_lock(&ctx->mutex);
                ctx->error_count++;
                pthread_mutex_unlock(&ctx->mutex);
                break;
            }

            cJSON *matrix1 = cJSON_CreateArray();
            cJSON *matrix2 = cJSON_CreateArray();
            if (!matrix1 || !matrix2)
            {
                cJSON_Delete(params);
                cJSON_Delete(matrix1); // Safe to call with NULL
                cJSON_Delete(matrix2); // Safe to call with NULL
                pthread_mutex_lock(&ctx->mutex);
                ctx->error_count++;
                pthread_mutex_unlock(&ctx->mutex);
                break;
            }

            // Create MATRIX_SIZE x MATRIX_SIZE matrices
            bool failed = false;
            for (int j = 0; j < MATRIX_SIZE && !failed; j++)
            {
                cJSON *row1 = cJSON_CreateArray();
                cJSON *row2 = cJSON_CreateArray();
                if (!row1 || !row2)
                {
                    failed = true;
                    cJSON_Delete(row1); // Safe to call with NULL
                    cJSON_Delete(row2); // Safe to call with NULL
                    break;
                }

                for (int k = 0; k < MATRIX_SIZE && !failed; k++)
                {
                    cJSON *num1 = cJSON_CreateNumber(rand() % 10);
                    cJSON *num2 = cJSON_CreateNumber(rand() % 10);
                    if (!num1 || !num2)
                    {
                        failed = true;
                        cJSON_Delete(num1); // Safe to call with NULL
                        cJSON_Delete(num2); // Safe to call with NULL
                        break;
                    }
                    cJSON_AddItemToArray(row1, num1);
                    cJSON_AddItemToArray(row2, num2);
                }

                if (!failed)
                {
                    cJSON_AddItemToArray(matrix1, row1);
                    cJSON_AddItemToArray(matrix2, row2);
                }
                else
                {
                    cJSON_Delete(row1);
                    cJSON_Delete(row2);
                }
            }

            if (failed)
            {
                cJSON_Delete(matrix1);
                cJSON_Delete(matrix2);
                cJSON_Delete(params);
                pthread_mutex_lock(&ctx->mutex);
                ctx->error_count++;
                pthread_mutex_unlock(&ctx->mutex);
                break;
            }

            cJSON_AddItemToObject(params, "matrix1", matrix1);
            cJSON_AddItemToObject(params, "matrix2", matrix2);

            if (rand() % 2)
            {
                cJSON *result = sockrpc_client_call_sync(ctx->client, "multiply", params);
                // params now owned by call_sync
                if (result)
                {
                    pthread_mutex_lock(&ctx->mutex);
                    ctx->success_count++;
                    pthread_mutex_unlock(&ctx->mutex);
                    cJSON_Delete(result);
                }
                else
                {
                    pthread_mutex_lock(&ctx->mutex);
                    ctx->error_count++;
                    pthread_mutex_unlock(&ctx->mutex);
                }
            }
            else
            {
                pthread_mutex_lock(&ctx->mutex);
                ctx->pending_async++;
                pthread_mutex_unlock(&ctx->mutex);
                sockrpc_client_call_async(ctx->client, "multiply", params, async_callback);
                // params ownership transferred to async call
            }
            break;
        }
        }

        usleep(5000 + (rand() % 10000));
    }

    // Wait for pending async operations with timeout
    int timeout_ms = 5000; // 5 second timeout
    int sleep_interval_ms = 10;
    int iterations = timeout_ms / sleep_interval_ms;

    for (int i = 0; i < iterations; i++)
    {
        pthread_mutex_lock(&ctx->mutex);
        int pending = ctx->pending_async;
        pthread_mutex_unlock(&ctx->mutex);

        if (pending == 0)
            break;

        usleep(sleep_interval_ms * 1000);
    }

    return NULL;
}

void test_stress()
{
    printf("Starting stress test (timeout: %d seconds)...\n", TEST_TIMEOUT);

    // Set up timeout handler
    signal(SIGALRM, handle_timeout);
    alarm(TEST_TIMEOUT);

    srand(time(NULL));

    // Create and start server
    sockrpc_server *server = sockrpc_server_create("/tmp/stress.sock");
    if (!server)
    {
        printf("Failed to create server\n");
        return;
    }

    sockrpc_server_register(server, "sort", array_sort_handler);
    sockrpc_server_register(server, "process", string_process_handler);
    sockrpc_server_register(server, "multiply", matrix_multiply_handler);

    sockrpc_server_start(server);
    usleep(100000); // Give server time to start

    // Create clients and their threads
    client_context contexts[NUM_CLIENTS];
    pthread_t threads[NUM_CLIENTS];
    int active_threads = 0;

    for (int i = 0; i < NUM_CLIENTS; i++)
    {
        contexts[i].client = sockrpc_client_create("/tmp/stress.sock");
        if (!contexts[i].client)
        {
            printf("Failed to create client %d\n", i);
            continue;
        }

        contexts[i].client_id = i;
        contexts[i].success_count = 0;
        contexts[i].error_count = 0;
        contexts[i].pending_async = 0;
        pthread_mutex_init(&contexts[i].mutex, NULL);

        pthread_mutex_lock(&g_contexts_mutex);
        g_contexts[i] = &contexts[i];
        pthread_mutex_unlock(&g_contexts_mutex);

        if (pthread_create(&threads[i], NULL, client_thread, &contexts[i]) == 0)
        {
            active_threads++;
        }
        else
        {
            printf("Failed to create thread for client %d\n", i);
            sockrpc_client_destroy(contexts[i].client);
            pthread_mutex_destroy(&contexts[i].mutex);
        }
    }

    if (active_threads == 0)
    {
        printf("No active clients, aborting test\n");
        sockrpc_server_destroy(server);
        return;
    }

    // Wait for all threads to complete
    for (int i = 0; i < NUM_CLIENTS; i++)
    {
        if (g_contexts[i])
        {
            pthread_join(threads[i], NULL);

            pthread_mutex_lock(&g_contexts_mutex);
            g_contexts[i] = NULL;
            pthread_mutex_unlock(&g_contexts_mutex);

            pthread_mutex_destroy(&contexts[i].mutex);
            sockrpc_client_destroy(contexts[i].client);
        }
    }

    // Cancel the alarm
    alarm(0);

    // Print statistics
    int total_success = 0, total_error = 0;
    int expected_ops = running ? OPERATIONS_PER_CLIENT : 0;

    for (int i = 0; i < active_threads; i++)
    {
        printf("Client %d: %d successful, %d failed operations (out of %d max)\n",
               i, contexts[i].success_count, contexts[i].error_count, expected_ops);
        total_success += contexts[i].success_count;
        total_error += contexts[i].error_count;
    }

    printf("\nTotal statistics:\n");
    printf("Successful operations: %d\n", total_success);
    printf("Failed operations: %d\n", total_error);
    printf("Success rate: %.2f%%\n",
           (float)total_success / (total_success + total_error) * 100);

    if (!running)
    {
        printf("Test stopped early due to timeout\n");
    }

    sockrpc_server_destroy(server);
    printf("Stress test completed\n");
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    test_stress();
    return 0;
}