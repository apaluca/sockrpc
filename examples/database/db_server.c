#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include "sockrpc/sockrpc.h"

#define MAX_RECORDS 1000
#define MAX_KEY_LENGTH 64
#define MAX_VALUE_LENGTH 1024
#define DB_FILE "/tmp/sockrpc_db.dat"

typedef struct
{
    char key[MAX_KEY_LENGTH];
    char value[MAX_VALUE_LENGTH];
    int valid;
} Record;

static Record *database = NULL;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int running = 1;

// Database persistence
static void load_database()
{
    FILE *fp = fopen(DB_FILE, "rb");
    if (!fp)
        return;

    pthread_mutex_lock(&db_mutex);
    fread(database, sizeof(Record), MAX_RECORDS, fp);
    pthread_mutex_unlock(&db_mutex);

    fclose(fp);
}

static void save_database()
{
    FILE *fp = fopen(DB_FILE, "wb");
    if (!fp)
    {
        fprintf(stderr, "Failed to save database\n");
        return;
    }

    pthread_mutex_lock(&db_mutex);
    fwrite(database, sizeof(Record), MAX_RECORDS, fp);
    pthread_mutex_unlock(&db_mutex);

    fclose(fp);
}

// Validate key-value parameters
static int validate_params(cJSON *params, const char **key, const char **value)
{
    cJSON *key_obj = cJSON_GetObjectItem(params, "key");
    if (!key_obj || !cJSON_IsString(key_obj) ||
        strlen(key_obj->valuestring) >= MAX_KEY_LENGTH)
    {
        return 0;
    }
    *key = key_obj->valuestring;

    if (value)
    {
        cJSON *value_obj = cJSON_GetObjectItem(params, "value");
        if (!value_obj || !cJSON_IsString(value_obj) ||
            strlen(value_obj->valuestring) >= MAX_VALUE_LENGTH)
        {
            return 0;
        }
        *value = value_obj->valuestring;
    }

    return 1;
}

// Set key-value
static cJSON *db_set(cJSON *params)
{
    const char *key, *value;
    if (!validate_params(params, &key, &value))
    {
        return cJSON_CreateString("Invalid parameters");
    }

    pthread_mutex_lock(&db_mutex);

    // Find empty slot or existing key
    int slot = -1;
    for (int i = 0; i < MAX_RECORDS; i++)
    {
        if (!database[i].valid)
        {
            slot = i;
            break;
        }
        if (strcmp(database[i].key, key) == 0)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        pthread_mutex_unlock(&db_mutex);
        return cJSON_CreateString("Database full");
    }

    strncpy(database[slot].key, key, MAX_KEY_LENGTH - 1);
    strncpy(database[slot].value, value, MAX_VALUE_LENGTH - 1);
    database[slot].valid = 1;

    pthread_mutex_unlock(&db_mutex);
    save_database();

    return cJSON_CreateString("OK");
}

// Get value by key
static cJSON *db_get(cJSON *params)
{
    const char *key;
    if (!validate_params(params, &key, NULL))
    {
        return cJSON_CreateString("Invalid parameters");
    }

    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < MAX_RECORDS; i++)
    {
        if (database[i].valid && strcmp(database[i].key, key) == 0)
        {
            cJSON *result = cJSON_CreateString(database[i].value);
            pthread_mutex_unlock(&db_mutex);
            return result;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    return cJSON_CreateString("Not found");
}

// Delete key
static cJSON *db_delete(cJSON *params)
{
    const char *key;
    if (!validate_params(params, &key, NULL))
    {
        return cJSON_CreateString("Invalid parameters");
    }

    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < MAX_RECORDS; i++)
    {
        if (database[i].valid && strcmp(database[i].key, key) == 0)
        {
            database[i].valid = 0;
            pthread_mutex_unlock(&db_mutex);
            save_database();
            return cJSON_CreateString("OK");
        }
    }

    pthread_mutex_unlock(&db_mutex);
    return cJSON_CreateString("Not found");
}

// List all keys
static cJSON *db_list(cJSON *params)
{
    (void)params;
    cJSON *list = cJSON_CreateArray();

    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < MAX_RECORDS; i++)
    {
        if (database[i].valid)
        {
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "key", database[i].key);
            cJSON_AddStringToObject(entry, "value", database[i].value);
            cJSON_AddItemToArray(list, entry);
        }
    }

    pthread_mutex_unlock(&db_mutex);
    return list;
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

    // Initialize database
    database = calloc(MAX_RECORDS, sizeof(Record));
    if (!database)
    {
        fprintf(stderr, "Failed to allocate database\n");
        return 1;
    }

    load_database();

    // Start server
    sockrpc_server *server = sockrpc_server_create("/tmp/db_rpc.sock");
    if (!server)
    {
        fprintf(stderr, "Failed to create server\n");
        free(database);
        return 1;
    }

    sockrpc_server_register(server, "set", db_set);
    sockrpc_server_register(server, "get", db_get);
    sockrpc_server_register(server, "delete", db_delete);
    sockrpc_server_register(server, "list", db_list);

    sockrpc_server_start(server);
    printf("Database server started. Press Ctrl+C to exit.\n");
    printf("Available operations:\n");
    printf("  - set: Set key-value pair\n");
    printf("  - get: Get value by key\n");
    printf("  - delete: Delete key-value pair\n");
    printf("  - list: List all entries\n");

    while (running)
    {
        sleep(1);
    }

    printf("\nShutting down server...\n");
    save_database();
    sockrpc_server_destroy(server);
    free(database);
    return 0;
}