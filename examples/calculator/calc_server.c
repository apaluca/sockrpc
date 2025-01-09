#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include "sockrpc/sockrpc.h"

static volatile int running = 1;

// Validate numeric input
static int validate_numeric_params(cJSON *params, const char *op)
{
    cJSON *a = cJSON_GetObjectItem(params, "a");
    cJSON *b = cJSON_GetObjectItem(params, "b");

    if (!a || !b || !cJSON_IsNumber(a) || !cJSON_IsNumber(b))
    {
        return 0;
    }

    // Additional validation for division
    if (strcmp(op, "divide") == 0 && fabs(b->valuedouble) < 1e-10)
    {
        return 0;
    }

    return 1;
}

// Basic arithmetic operation
static cJSON *calculate(cJSON *params)
{
    const char *op = cJSON_GetObjectItem(params, "operation")->valuestring;

    if (!validate_numeric_params(params, op))
    {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Invalid parameters or division by zero");
        return error;
    }

    double a = cJSON_GetObjectItem(params, "a")->valuedouble;
    double b = cJSON_GetObjectItem(params, "b")->valuedouble;
    double result = 0;
    char *error = NULL;

    if (strcmp(op, "add") == 0)
        result = a + b;
    else if (strcmp(op, "subtract") == 0)
        result = a - b;
    else if (strcmp(op, "multiply") == 0)
        result = a * b;
    else if (strcmp(op, "divide") == 0)
        result = a / b;
    else if (strcmp(op, "power") == 0)
    {
        if (a == 0 && b < 0)
        {
            error = "Division by zero in power operation";
        }
        else
        {
            result = pow(a, b);
        }
    }
    else
    {
        error = "Unknown operation";
    }

    if (error)
    {
        cJSON *error_response = cJSON_CreateObject();
        cJSON_AddStringToObject(error_response, "error", error);
        return error_response;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "result", result);
    return response;
}

// Statistical operations on array
static cJSON *array_stats(cJSON *params)
{
    cJSON *array = cJSON_GetObjectItem(params, "numbers");
    if (!array || !cJSON_IsArray(array) || cJSON_GetArraySize(array) == 0)
    {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Invalid or empty array");
        return error;
    }

    int size = cJSON_GetArraySize(array);
    double sum = 0, mean = 0, variance = 0;
    double min = INFINITY, max = -INFINITY;

    // Calculate sum, min, max
    for (int i = 0; i < size; i++)
    {
        double val = cJSON_GetArrayItem(array, i)->valuedouble;
        sum += val;
        min = fmin(min, val);
        max = fmax(max, val);
    }
    mean = sum / size;

    // Calculate variance
    for (int i = 0; i < size; i++)
    {
        double diff = cJSON_GetArrayItem(array, i)->valuedouble - mean;
        variance += diff * diff;
    }
    variance /= size;

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "count", size);
    cJSON_AddNumberToObject(result, "sum", sum);
    cJSON_AddNumberToObject(result, "mean", mean);
    cJSON_AddNumberToObject(result, "variance", variance);
    cJSON_AddNumberToObject(result, "stddev", sqrt(variance));
    cJSON_AddNumberToObject(result, "min", min);
    cJSON_AddNumberToObject(result, "max", max);

    return result;
}

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main()
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    sockrpc_server *server = sockrpc_server_create("/tmp/calc_rpc.sock");
    if (!server)
    {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    sockrpc_server_register(server, "calculate", calculate);
    sockrpc_server_register(server, "stats", array_stats);

    sockrpc_server_start(server);
    printf("Calculator server started. Press Ctrl+C to exit.\n");
    printf("Available operations:\n");
    printf("  - calculate: Basic arithmetic (add, subtract, multiply, divide, power)\n");
    printf("  - stats: Statistical operations on arrays\n");

    while (running)
    {
        sleep(1);
    }

    printf("\nShutting down server...\n");
    sockrpc_server_destroy(server);
    return 0;
}