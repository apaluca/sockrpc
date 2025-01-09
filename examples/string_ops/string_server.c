#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include "sockrpc/sockrpc.h"

static volatile int running = 1;

// Input validation helper
static int validate_string_input(cJSON *params)
{
    cJSON *text = cJSON_GetObjectItem(params, "text");
    return (text && cJSON_IsString(text) && text->valuestring);
}

// Convert string to uppercase
static cJSON *str_uppercase(cJSON *params)
{
    if (!validate_string_input(params))
    {
        return cJSON_CreateString("Invalid input: expected 'text' field with string value");
    }

    const char *input = cJSON_GetObjectItem(params, "text")->valuestring;
    char *result = strdup(input);

    for (size_t i = 0; i < strlen(result); i++)
    {
        result[i] = toupper(result[i]);
    }

    cJSON *response = cJSON_CreateString(result);
    free(result);
    return response;
}

// Count words in string
static cJSON *count_words(cJSON *params)
{
    if (!validate_string_input(params))
    {
        return cJSON_CreateNumber(-1);
    }

    const char *input = cJSON_GetObjectItem(params, "text")->valuestring;
    int count = 0;
    int in_word = 0;

    for (size_t i = 0; i < strlen(input); i++)
    {
        if (isspace(input[i]))
        {
            in_word = 0;
        }
        else if (!in_word)
        {
            in_word = 1;
            count++;
        }
    }

    return cJSON_CreateNumber(count);
}

// Reverse string
static cJSON *str_reverse(cJSON *params)
{
    if (!validate_string_input(params))
    {
        return cJSON_CreateString("Invalid input: expected 'text' field with string value");
    }

    const char *input = cJSON_GetObjectItem(params, "text")->valuestring;
    size_t len = strlen(input);
    char *result = malloc(len + 1);

    for (size_t i = 0; i < len; i++)
    {
        result[i] = input[len - 1 - i];
    }
    result[len] = '\0';

    cJSON *response = cJSON_CreateString(result);
    free(result);
    return response;
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

    // Start server
    sockrpc_server *server = sockrpc_server_create("/tmp/string_rpc.sock");
    if (!server)
    {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    // Register methods
    sockrpc_server_register(server, "uppercase", str_uppercase);
    sockrpc_server_register(server, "wordcount", count_words);
    sockrpc_server_register(server, "reverse", str_reverse);

    sockrpc_server_start(server);
    printf("String operations server started. Press Ctrl+C to exit.\n");
    printf("Available operations:\n");
    printf("  - uppercase: Converts text to uppercase\n");
    printf("  - wordcount: Counts words in text\n");
    printf("  - reverse: Reverses the text\n");

    while (running)
    {
        sleep(1);
    }

    printf("\nShutting down server...\n");
    sockrpc_server_destroy(server);
    return 0;
}