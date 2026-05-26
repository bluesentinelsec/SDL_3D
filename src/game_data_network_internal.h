#ifndef SLAYER3D_GAME_DATA_NETWORK_INTERNAL_H
#define SLAYER3D_GAME_DATA_NETWORK_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_stdinc.h>

#include "game_data_network_types.h"
#include "game_data_runtime_types.h"
#include "slayer3d/actor_controller.h"
#include "slayer3d/game_data_network.h"
#include "slayer3d/network_replication.h"
#include "yyjson.h"

typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

yyjson_val *game_data_find_replication_channel_by_name(const slayer3d_game_data_runtime *runtime,
                                                       const char *replication_name, int *out_index);
yyjson_val *game_data_find_replication_channel_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index);
bool game_data_replication_channel_is_host_to_client(yyjson_val *channel);
bool game_data_replication_channel_is_client_to_host(yyjson_val *channel);
size_t game_data_replication_channel_field_count(const slayer3d_game_data_runtime *runtime, yyjson_val *channel);
bool game_data_replication_channel_packet_size(const slayer3d_game_data_runtime *runtime, yyjson_val *channel,
                                               size_t *out_size);
size_t game_data_replication_channel_input_count(yyjson_val *channel);
bool game_data_replication_input_packet_size(yyjson_val *channel, size_t *out_size);
const char *game_data_replication_input_action(yyjson_val *input);
int game_data_replication_action_id(const slayer3d_game_data_runtime *runtime, yyjson_val *input);
slayer3d_game_data_network_direction game_data_network_direction_from_string(const char *direction);
const char *game_data_network_direction_name(slayer3d_game_data_network_direction direction);
yyjson_val *game_data_find_network_control_by_name(const slayer3d_game_data_runtime *runtime, const char *control_name,
                                                   int *out_index);
yyjson_val *game_data_find_network_control_by_index(const slayer3d_game_data_runtime *runtime, Uint32 index);
bool game_data_network_control_packet_size(size_t *out_size);
int game_data_network_control_signal_id(const slayer3d_game_data_runtime *runtime, yyjson_val *control);
bool game_data_read_actor_replication_field(const slayer3d_registered_actor *actor,
                                            const slayer3d_replication_field_descriptor *field,
                                            game_data_snapshot_value *out_value);
bool game_data_write_snapshot_value(slayer3d_replication_writer *writer, const game_data_snapshot_value *value);
bool game_data_read_snapshot_value(slayer3d_replication_reader *reader, slayer3d_replication_field_type type,
                                   game_data_snapshot_value *out_value);
bool game_data_apply_actor_replication_field(slayer3d_game_data_runtime *runtime, slayer3d_registered_actor *actor,
                                             const slayer3d_replication_field_descriptor *field,
                                             const game_data_snapshot_value *value);

#endif
