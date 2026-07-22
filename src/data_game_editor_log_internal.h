#ifndef SLAYER3D_DATA_GAME_EDITOR_LOG_INTERNAL_H
#define SLAYER3D_DATA_GAME_EDITOR_LOG_INTERNAL_H

#include <stdbool.h>

typedef struct data_game_editor_log_mirror data_game_editor_log_mirror;
typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

bool data_game_editor_log_mirror_create(data_game_editor_log_mirror **out_mirror, char *error_buffer,
                                        int error_buffer_size);
void data_game_editor_log_mirror_destroy(data_game_editor_log_mirror *mirror);
void data_game_editor_log_mirror_drain(data_game_editor_log_mirror *mirror, slayer3d_game_data_runtime *runtime);

#endif
