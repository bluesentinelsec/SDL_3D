/**
 * @file game_data_app_runtime.h
 * @brief Runtime app configuration and app-control APIs for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_APP_RUNTIME_H
#define SLAYER3D_GAME_DATA_APP_RUNTIME_H

#include <stdbool.h>

#include "slayer3d/asset.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data_app.h"
#include "slayer3d/game_data_render.h"
#include "slayer3d/game_data_ui.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

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
     * @brief Return the number of authored signals that apply live window settings.
     *
     * Both `app.window.apply_signal` and `app.window.apply_signals` contribute
     * registered, de-duplicated signals to this list.
     */
    int slayer3d_game_data_app_window_apply_signal_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return one authored live-window apply signal id.
     *
     * @param runtime Loaded game-data runtime.
     * @param index Zero-based index below
     * `slayer3d_game_data_app_window_apply_signal_count()`.
     * @return Registered signal id, or -1 when the index is invalid.
     */
    int slayer3d_game_data_app_window_apply_signal_at(const slayer3d_game_data_runtime *runtime, int index);

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

#endif /* SLAYER3D_GAME_DATA_APP_RUNTIME_H */
