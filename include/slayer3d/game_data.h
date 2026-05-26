/**
 * @file game_data.h
 * @brief JSON-authored game data runtime.
 *
 * The game data runtime loads an SLAYER3D game JSON file and instantiates its
 * generic composition primitives into an existing game session: actors, input
 * actions, signals, timers, sensors, and signal-to-action bindings.
 *
 * Game-specific behavior stays behind named adapters. JSON chooses where an
 * adapter is invoked and can bind it to a Lua function loaded from a script
 * next to the data file. Game code may also register native C callbacks for
 * adapters that need host integration or optimized native code.
 */

#ifndef SLAYER3D_GAME_DATA_H
#define SLAYER3D_GAME_DATA_H

#include <stdbool.h>

#include "slayer3d/actor_registry.h"
#include "slayer3d/asset.h"
#include "slayer3d/camera.h"
#include "slayer3d/effects.h"
#include "slayer3d/font.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data_app.h"
#include "slayer3d/game_data_asset_runtime.h"
#include "slayer3d/game_data_assets.h"
#include "slayer3d/game_data_brush.h"
#include "slayer3d/game_data_defaults.h"
#include "slayer3d/game_data_editor.h"
#include "slayer3d/game_data_editor_metadata.h"
#include "slayer3d/game_data_editor_runtime.h"
#include "slayer3d/game_data_input_runtime.h"
#include "slayer3d/game_data_menu_runtime.h"
#include "slayer3d/game_data_network.h"
#include "slayer3d/game_data_network_runtime.h"
#include "slayer3d/game_data_render.h"
#include "slayer3d/game_data_render_runtime.h"
#include "slayer3d/game_data_runtime.h"
#include "slayer3d/game_data_runtime_collections.h"
#include "slayer3d/game_data_scene.h"
#include "slayer3d/game_data_scene_runtime.h"
#include "slayer3d/game_data_ui.h"
#include "slayer3d/game_data_ui_runtime.h"
#include "slayer3d/game_data_validation.h"
#include "slayer3d/game_data_world.h"
#include "slayer3d/game_data_world_model.h"
#include "slayer3d/game_data_world_runtime.h"
#include "slayer3d/level.h"
#include "slayer3d/lighting.h"
#include "slayer3d/model.h"
#include "slayer3d/network.h"
#include "slayer3d/network_replication.h"
#include "slayer3d/properties.h"
#include "slayer3d/render_context.h"
#include "slayer3d/sprite_asset.h"
#include "slayer3d/storage.h"
#include "slayer3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Read the managed-loop config authored in a JSON game data asset.
     *
     * This lightweight reader is intended for startup, before a managed loop
     * creates a window or game session. Missing fields keep the values already
     * present in @p out_config, so callers can initialize defaults first. When
     * an authored title is present, it is copied into @p title_buffer and
     * out_config->title points at that buffer.
     */
    bool slayer3d_game_data_load_app_config_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                                  slayer3d_game_config *out_config, char *title_buffer,
                                                  int title_buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data file.
     *
     * Missing fields keep the values already present in @p out_config, so
     * callers can initialize defaults first. When an authored title is present,
     * it is copied into @p title_buffer and out_config->title points at that
     * buffer.
     */
    bool slayer3d_game_data_load_app_config_file(const char *path, slayer3d_game_config *out_config, char *title_buffer,
                                                 int title_buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Read data-authored application lifecycle hooks.
     *
     * Missing fields return neutral values: signal/action ids are -1 and
     * transition names are NULL.
     */
    bool slayer3d_game_data_get_app_control(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_app_control *out_control);

    /**
     * @brief Return whether an authored signal should apply live window settings.
     *
     * Games declare these signals under `app.window.apply_signal` or
     * `app.window.apply_signals`. This lets reusable menu controls apply display
     * mode, renderer, and V-sync changes immediately without hard-coding menu
     * names in the host.
     */
    bool slayer3d_game_data_app_signal_applies_window_settings(const slayer3d_game_data_runtime *runtime,
                                                               int signal_id);

    /**
     * @brief Evaluate the data-authored app pause condition.
     *
     * Returns true when `app.pause.allowed_if` is absent. When present, the
     * condition uses the same generic condition language as UI visibility:
     * actor property comparisons, app pause checks, camera checks, and
     * all/any/not composition. @p metrics may be NULL when the condition does
     * not refer to app metrics.
     */
    bool slayer3d_game_data_app_pause_allowed(const slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_ui_metrics *metrics);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_H */
