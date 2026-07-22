/**
 * @file data_game_editor_log.c
 * @brief Thread-safe SDL log mirroring for the data-authored editor console.
 */

#include "data_game_editor_log_internal.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_editor_dialog_internal.h"

#define DATA_GAME_EDITOR_LOG_QUEUE_CAPACITY 256
#define DATA_GAME_EDITOR_LOG_MESSAGE_CAPACITY 512

typedef struct data_game_editor_log_entry
{
    SDL_LogPriority priority;
    char message[DATA_GAME_EDITOR_LOG_MESSAGE_CAPACITY];
} data_game_editor_log_entry;

struct data_game_editor_log_mirror
{
    data_game_editor_log_entry queue[DATA_GAME_EDITOR_LOG_QUEUE_CAPACITY];
    int queue_head;
    int queue_count;
    unsigned int dropped_count;
};

typedef struct data_game_editor_log_bridge
{
    SDL_Mutex *mutex;
    data_game_editor_log_mirror *mirror;
    SDL_LogOutputFunction previous_callback;
    void *previous_userdata;
} data_game_editor_log_bridge;

static data_game_editor_log_bridge data_game_log_bridge;

static void data_game_editor_log_set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "");
}

static void SDLCALL data_game_editor_log_output(void *userdata, int category, SDL_LogPriority priority,
                                                const char *message)
{
    data_game_editor_log_bridge *bridge = (data_game_editor_log_bridge *)userdata;
    if (bridge == NULL || bridge->mutex == NULL)
        return;

    SDL_LockMutex(bridge->mutex);
    if (bridge->previous_callback != NULL)
        bridge->previous_callback(bridge->previous_userdata, category, priority, message);

    data_game_editor_log_mirror *mirror = bridge->mirror;
    if (mirror != NULL && category != SLAYER3D_EDITOR_LOG_CATEGORY && message != NULL && message[0] != '\0')
    {
        if (mirror->queue_count == DATA_GAME_EDITOR_LOG_QUEUE_CAPACITY)
        {
            mirror->queue_head = (mirror->queue_head + 1) % DATA_GAME_EDITOR_LOG_QUEUE_CAPACITY;
            mirror->queue_count--;
            mirror->dropped_count++;
        }
        const int tail = (mirror->queue_head + mirror->queue_count) % DATA_GAME_EDITOR_LOG_QUEUE_CAPACITY;
        mirror->queue[tail].priority = priority;
        SDL_strlcpy(mirror->queue[tail].message, message, sizeof(mirror->queue[tail].message));
        mirror->queue_count++;
    }
    SDL_UnlockMutex(bridge->mutex);
}

bool data_game_editor_log_mirror_create(data_game_editor_log_mirror **out_mirror, char *error_buffer,
                                        int error_buffer_size)
{
    if (out_mirror == NULL)
    {
        data_game_editor_log_set_error(error_buffer, error_buffer_size, "editor log mirror requires out_mirror");
        return false;
    }
    *out_mirror = NULL;

    data_game_editor_log_mirror *mirror = (data_game_editor_log_mirror *)SDL_calloc(1, sizeof(*mirror));
    if (mirror == NULL)
    {
        data_game_editor_log_set_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }
    if (data_game_log_bridge.mutex == NULL)
        data_game_log_bridge.mutex = SDL_CreateMutex();
    if (data_game_log_bridge.mutex == NULL)
    {
        data_game_editor_log_set_error(error_buffer, error_buffer_size, SDL_GetError());
        SDL_free(mirror);
        return false;
    }

    SDL_LockMutex(data_game_log_bridge.mutex);
    if (data_game_log_bridge.mirror != NULL)
    {
        SDL_UnlockMutex(data_game_log_bridge.mutex);
        data_game_editor_log_set_error(error_buffer, error_buffer_size,
                                       "an editor console log mirror is already active");
        SDL_free(mirror);
        return false;
    }
    SDL_GetLogOutputFunction(&data_game_log_bridge.previous_callback, &data_game_log_bridge.previous_userdata);
    data_game_log_bridge.mirror = mirror;
    SDL_SetLogOutputFunction(data_game_editor_log_output, &data_game_log_bridge);
    SDL_UnlockMutex(data_game_log_bridge.mutex);

    *out_mirror = mirror;
    return true;
}

void data_game_editor_log_mirror_destroy(data_game_editor_log_mirror *mirror)
{
    if (mirror == NULL)
        return;

    SDL_LockMutex(data_game_log_bridge.mutex);
    if (data_game_log_bridge.mirror == mirror)
    {
        SDL_LogOutputFunction current_callback = NULL;
        void *current_userdata = NULL;
        SDL_GetLogOutputFunction(&current_callback, &current_userdata);
        if (current_callback == data_game_editor_log_output && current_userdata == &data_game_log_bridge)
        {
            SDL_SetLogOutputFunction(data_game_log_bridge.previous_callback, data_game_log_bridge.previous_userdata);
        }
        data_game_log_bridge.mirror = NULL;
        data_game_log_bridge.previous_callback = NULL;
        data_game_log_bridge.previous_userdata = NULL;
    }
    SDL_UnlockMutex(data_game_log_bridge.mutex);
    SDL_free(mirror);
}

void data_game_editor_log_mirror_drain(data_game_editor_log_mirror *mirror, slayer3d_game_data_runtime *runtime)
{
    if (mirror == NULL || runtime == NULL)
        return;

    for (;;)
    {
        data_game_editor_log_entry entry;
        bool have_entry = false;
        unsigned int dropped_count = 0;

        SDL_LockMutex(data_game_log_bridge.mutex);
        if (mirror->dropped_count > 0)
        {
            dropped_count = mirror->dropped_count;
            mirror->dropped_count = 0;
        }
        else if (mirror->queue_count > 0)
        {
            entry = mirror->queue[mirror->queue_head];
            mirror->queue_head = (mirror->queue_head + 1) % DATA_GAME_EDITOR_LOG_QUEUE_CAPACITY;
            mirror->queue_count--;
            have_entry = true;
        }
        SDL_UnlockMutex(data_game_log_bridge.mutex);

        if (dropped_count > 0)
        {
            char message[160];
            SDL_snprintf(message, sizeof(message), "Editor console log mirror dropped %u message%s", dropped_count,
                         dropped_count == 1 ? "" : "s");
            slayer3d_game_data_editor_publish_console_log(runtime, SDL_LOG_PRIORITY_WARN, message);
        }
        else if (have_entry)
        {
            slayer3d_game_data_editor_append_console_message(runtime, entry.priority, entry.message);
        }
        else
        {
            break;
        }
    }
}
