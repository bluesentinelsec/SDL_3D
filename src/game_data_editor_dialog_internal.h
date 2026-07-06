#ifndef SLAYER3D_GAME_DATA_EDITOR_DIALOG_INTERNAL_H
#define SLAYER3D_GAME_DATA_EDITOR_DIALOG_INTERNAL_H

#include <stdbool.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>

typedef struct editor_file_dialog_request
{
    char *path_key;
    char *accept_signal;
    char *status_key;
    char *message_key;
    char *default_location;
    char *selected_path;
    char *message;
    bool canceled;
    bool failed;
} editor_file_dialog_request;

struct slayer3d_game_data_runtime;

Uint32 slayer3d_game_data_editor_file_dialog_event_type(void);
void slayer3d_game_data_editor_file_dialog_request_free(editor_file_dialog_request *request);
void slayer3d_game_data_editor_publish_console_message(struct slayer3d_game_data_runtime *runtime, const char *message);

#endif
