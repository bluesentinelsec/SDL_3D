/**
 * @file slayer3d_runner.c
 * @brief Generic SLAYER3D data-game runner.
 */

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#if !defined(SLAYER3D_RUNNER_NO_MAIN)
#include <SDL3/SDL_main.h>
#endif
#include <SDL3/SDL_stdinc.h>

#include <stdio.h>

#include "slayer3d/map.h"
#include "slayer3d/slayer3d.h"

#include "slayer3d_fused.h"
#include "slayer3d_runner_cli.h"
#include "slayer3d_runner_manifest.h"
#include "slayer3d_runner_state.h"

#if defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
#if !defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS_DATA)
#define SLAYER3D_RUNNER_EMBEDDED_ASSETS_DATA slayer3d_runner_embedded_assets
#endif
#if !defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS_SIZE)
#define SLAYER3D_RUNNER_EMBEDDED_ASSETS_SIZE slayer3d_runner_embedded_assets_size
#endif
extern const unsigned char SLAYER3D_RUNNER_EMBEDDED_ASSETS_DATA[];
extern const size_t SLAYER3D_RUNNER_EMBEDDED_ASSETS_SIZE;
#endif

typedef struct runner_state
{
    slayer3d_runner_args args;
    const char *executable_path;
    bool fused_available;
    slayer3d_fused_pack fused_pack;
    slayer3d_game_config config;
    char title[128];
    slayer3d_data_game_runtime *runtime;
    char *generated_map_output_dir;
} runner_state;

static bool runner_is_asset_path(const char *path)
{
    return path != NULL && SDL_strncmp(path, "asset://", 8) == 0;
}

static void runner_set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "unknown error");
}

static bool runner_mount_assets(slayer3d_asset_resolver *assets, void *userdata, char *error_buffer,
                                int error_buffer_size)
{
    const runner_state *state = (const runner_state *)userdata;
    if (assets == NULL || state == NULL)
    {
        runner_set_error(error_buffer, error_buffer_size, "invalid runner asset mount arguments");
        return false;
    }

    switch (state->args.mount_kind)
    {
    case SLAYER3D_RUNNER_MOUNT_DIRECTORY:
        if (state->args.mount_path_count > 0)
        {
            for (int i = 0; i < state->args.mount_path_count; ++i)
            {
                if (!slayer3d_asset_resolver_mount_directory(assets, state->args.mount_paths[i], error_buffer,
                                                             error_buffer_size))
                {
                    return false;
                }
            }
            return true;
        }
        return slayer3d_asset_resolver_mount_directory(assets, state->args.mount_path, error_buffer, error_buffer_size);
    case SLAYER3D_RUNNER_MOUNT_PACK:
        return slayer3d_asset_resolver_mount_pack_file(assets, state->args.mount_path, error_buffer, error_buffer_size);
    case SLAYER3D_RUNNER_MOUNT_EMBEDDED:
#if defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
        return slayer3d_asset_resolver_mount_memory_pack(assets, SLAYER3D_RUNNER_EMBEDDED_ASSETS_DATA,
                                                         SLAYER3D_RUNNER_EMBEDDED_ASSETS_SIZE,
                                                         "slayer3d_runner.embedded", error_buffer, error_buffer_size);
#else
        runner_set_error(error_buffer, error_buffer_size, "runner was not built with embedded assets");
        return false;
#endif
    case SLAYER3D_RUNNER_MOUNT_NONE:
        if (state->fused_available)
            return slayer3d_fused_mount_pack(assets, state->executable_path, &state->fused_pack, error_buffer,
                                             error_buffer_size);
        runner_set_error(error_buffer, error_buffer_size,
                         "no asset mount was configured and the runner executable is not fused");
        return false;
    default:
        runner_set_error(error_buffer, error_buffer_size, "no asset mount was configured");
        return false;
    }
}

static bool runner_prepare_fused_defaults(runner_state *state, char *error_buffer, int error_buffer_size)
{
    if (state == NULL || state->args.mount_kind != SLAYER3D_RUNNER_MOUNT_NONE)
        return true;

    char footer_error[256] = "";
    if (!slayer3d_fused_read_footer(state->executable_path, &state->fused_pack, footer_error,
                                    (int)sizeof(footer_error)))
    {
        if (state->args.data_asset_path != NULL)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s",
                         footer_error[0] != '\0' ? footer_error : "runner executable is not fused");
            return false;
        }
        return true;
    }

    state->fused_available = true;
    if (state->args.data_asset_path == NULL || state->args.data_asset_path[0] == '\0')
        state->args.data_asset_path = state->fused_pack.data_asset_path;
    return true;
}

static bool runner_set_errorf(char *error_buffer, int error_buffer_size, const char *fmt, const char *value)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, fmt, value != NULL ? value : "");
    return false;
}

static char *runner_join_path(const char *directory, const char *leaf)
{
    if (directory == NULL || leaf == NULL)
        return NULL;
    const size_t dir_len = SDL_strlen(directory);
    const size_t leaf_len = SDL_strlen(leaf);
    const bool needs_sep = dir_len > 0u && directory[dir_len - 1u] != '/' && directory[dir_len - 1u] != '\\' &&
                           leaf[0] != '/' && leaf[0] != '\\';
    char *path = (char *)SDL_malloc(dir_len + leaf_len + (needs_sep ? 2u : 1u));
    if (path == NULL)
        return NULL;
    SDL_snprintf(path, dir_len + leaf_len + (needs_sep ? 2u : 1u), "%s%s%s", directory, needs_sep ? "/" : "", leaf);
    return path;
}

static bool runner_prepare_playable_map(runner_state *state, char *error_buffer, int error_buffer_size)
{
    if (state == NULL || state->args.map_path == NULL)
        return true;

    slayer3d_map_document *map = NULL;
    if (!slayer3d_map_load_file(state->args.map_path, NULL, &map, error_buffer, error_buffer_size))
        return false;

    slayer3d_map_playable_scene_desc scene_desc;
    if (!slayer3d_map_build_playable_scene_desc(map, &scene_desc, error_buffer, error_buffer_size))
    {
        slayer3d_map_destroy(map);
        return false;
    }

    char *pref_path = SDL_GetPrefPath("bluesentinelsec", "Slayer3DRunner");
    char *output_dir = pref_path != NULL ? runner_join_path(pref_path, "slayermap-playable") : NULL;
    SDL_free(pref_path);
    if (output_dir == NULL)
    {
        slayer3d_map_destroy(map);
        runner_set_error(error_buffer, error_buffer_size, "failed to allocate playable map output directory");
        return false;
    }

    const bool ok = slayer3d_map_write_playable_game_files(map, output_dir, error_buffer, error_buffer_size);
    /* scene_desc strings are borrowed from the map document, so log before
     * the document is destroyed. */
    if (ok)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "SLAYER3D runner materialized map '%s' to '%s' (%llu playable brushes, %llu actors, player=%s)",
                    state->args.map_path, output_dir, (unsigned long long)scene_desc.playable_brush_count,
                    (unsigned long long)scene_desc.actor_count,
                    scene_desc.player_actor_id != NULL ? scene_desc.player_actor_id : "<none>");
    }
    slayer3d_map_destroy(map);
    if (!ok)
    {
        SDL_free(output_dir);
        return false;
    }

    state->generated_map_output_dir = output_dir;
    state->args.mount_kind = SLAYER3D_RUNNER_MOUNT_DIRECTORY;
    state->args.mount_path = state->generated_map_output_dir;
    state->args.data_asset_path = "asset://playable_map.game.json";
    state->args.scene = NULL;
    state->args.player_start = NULL;
    return true;
}

static bool runner_read_asset_state_file(runner_state *state, const char *path, char **out_text, size_t *out_size,
                                         char *error_buffer, int error_buffer_size)
{
    *out_text = NULL;
    *out_size = 0u;
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    if (assets == NULL)
        return runner_set_errorf(error_buffer, error_buffer_size, "failed to create asset resolver for '%s'", path);

    bool ok = runner_mount_assets(assets, state, error_buffer, error_buffer_size);
    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    if (ok)
        ok = slayer3d_asset_resolver_read_file(assets, path, &buffer, error_buffer, error_buffer_size);
    if (ok)
    {
        char *text = (char *)SDL_malloc(buffer.size + 1u);
        if (text == NULL)
        {
            ok = false;
            runner_set_error(error_buffer, error_buffer_size, "failed to allocate state file buffer");
        }
        else
        {
            SDL_memcpy(text, buffer.data, buffer.size);
            text[buffer.size] = '\0';
            *out_text = text;
            *out_size = buffer.size;
        }
    }
    slayer3d_asset_buffer_free(&buffer);
    slayer3d_asset_resolver_destroy(assets);
    return ok;
}

static bool runner_read_text_file(runner_state *state, const char *path, char **out_text, size_t *out_size,
                                  char *error_buffer, int error_buffer_size)
{
    if (runner_is_asset_path(path))
        return runner_read_asset_state_file(state, path, out_text, out_size, error_buffer, error_buffer_size);

    *out_text = NULL;
    *out_size = 0u;
    size_t size = 0u;
    char *text = (char *)SDL_LoadFile(path, &size);
    if (text == NULL)
        return runner_set_errorf(error_buffer, error_buffer_size, "failed to read file '%s'", path);

    char *terminated = (char *)SDL_realloc(text, size + 1u);
    if (terminated == NULL)
    {
        SDL_free(text);
        runner_set_error(error_buffer, error_buffer_size, "failed to terminate file buffer");
        return false;
    }
    terminated[size] = '\0';
    *out_text = terminated;
    *out_size = size;
    return true;
}

static bool runner_apply_test_run_manifest(runner_state *state, char *error_buffer, int error_buffer_size)
{
    if (state == NULL || state->args.test_run_manifest_path == NULL)
        return true;

    char *text = NULL;
    size_t size = 0u;
    if (!runner_read_text_file(state, state->args.test_run_manifest_path, &text, &size, error_buffer,
                               error_buffer_size))
    {
        return false;
    }

    const bool ok = slayer3d_runner_apply_test_run_manifest_json(
        &state->args, text, size, state->args.test_run_manifest_path, error_buffer, error_buffer_size);
    SDL_free(text);
    return ok;
}

static bool runner_build_initial_state(runner_state *state, slayer3d_properties **out_state, char *error_buffer,
                                       int error_buffer_size)
{
    *out_state = NULL;
    if (state->args.state_file_count <= 0 && state->args.state_json_count <= 0 &&
        state->args.state_assignment_count <= 0)
    {
        return true;
    }

    slayer3d_properties *props = slayer3d_properties_create();
    if (props == NULL)
    {
        runner_set_error(error_buffer, error_buffer_size, "failed to allocate initial scene state");
        return false;
    }

    for (int i = 0; i < state->args.state_file_count; ++i)
    {
        if (runner_is_asset_path(state->args.state_files[i]))
        {
            char *text = NULL;
            size_t size = 0u;
            if (!runner_read_asset_state_file(state, state->args.state_files[i], &text, &size, error_buffer,
                                              error_buffer_size) ||
                !slayer3d_runner_apply_state_json_object(props, text, size, state->args.state_files[i], error_buffer,
                                                         error_buffer_size))
            {
                SDL_free(text);
                slayer3d_properties_destroy(props);
                return false;
            }
            SDL_free(text);
            continue;
        }
        if (!slayer3d_runner_apply_state_json_file(props, state->args.state_files[i], error_buffer, error_buffer_size))
        {
            slayer3d_properties_destroy(props);
            return false;
        }
    }
    for (int i = 0; i < state->args.state_json_count; ++i)
    {
        const char *json = state->args.state_json_values[i];
        if (json == NULL || !slayer3d_runner_apply_state_json_object(props, json, SDL_strlen(json), "--state-json",
                                                                     error_buffer, error_buffer_size))
        {
            slayer3d_properties_destroy(props);
            return false;
        }
    }
    for (int i = 0; i < state->args.state_assignment_count; ++i)
    {
        if (!slayer3d_runner_apply_state_assignment(props, state->args.state_assignments[i], error_buffer,
                                                    error_buffer_size))
        {
            slayer3d_properties_destroy(props);
            return false;
        }
    }

    *out_state = props;
    return true;
}

static bool load_runner_config(runner_state *state, char *error_buffer, int error_buffer_size)
{
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    if (assets == NULL)
    {
        runner_set_error(error_buffer, error_buffer_size, "failed to create asset resolver");
        return false;
    }

    bool ok = runner_mount_assets(assets, state, error_buffer, error_buffer_size);
    if (ok)
    {
        SDL_zero(state->config);
        SDL_snprintf(state->title, sizeof(state->title), "%s", "SLAYER3D");
        ok = slayer3d_game_data_load_app_config_asset(assets, state->args.data_asset_path, &state->config, state->title,
                                                      (int)sizeof(state->title), error_buffer, error_buffer_size);
    }

    slayer3d_asset_resolver_destroy(assets);
    return ok;
}

static bool runner_init(slayer3d_game_context *ctx, void *userdata)
{
    runner_state *state = (runner_state *)userdata;
    slayer3d_data_game_runtime_desc desc;
    slayer3d_properties *initial_state = NULL;
    char error[512] = "";

    if (ctx == NULL || state == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D runner received invalid init arguments");
        return false;
    }

    if (!runner_build_initial_state(state, &initial_state, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D runner direct-start state failed: %s",
                     error[0] != '\0' ? error : "unknown error");
        return false;
    }

    slayer3d_data_game_runtime_desc_init(&desc);
    desc.session = ctx->session;
    desc.data_asset_path = state->args.data_asset_path;
    desc.media_dir = state->args.media_dir;
    desc.mount_assets = runner_mount_assets;
    desc.mount_userdata = state;
    desc.enable_managed_network = true;
    desc.initial_scene_override = state->args.scene;
    desc.initial_player_start = state->args.player_start;
    desc.initial_scene_state = initial_state;
    desc.initial_scene_payload = initial_state;
    desc.skip_app_flow_startup = (state->args.scene != NULL && state->args.scene[0] != '\0') ||
                                 (state->args.player_start != NULL && state->args.player_start[0] != '\0');

    if (!slayer3d_data_game_runtime_create(&desc, &state->runtime, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D runner data load failed: %s", error);
        slayer3d_properties_destroy(initial_state);
        return false;
    }
    slayer3d_properties_destroy(initial_state);

    slayer3d_data_game_runtime_apply_mouse_capture(state->runtime, ctx);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D runner loaded data asset: %s", state->args.data_asset_path);
    return true;
}

static void runner_tick(slayer3d_game_context *ctx, void *userdata, float dt)
{
    runner_state *state = (runner_state *)userdata;
    if (state != NULL)
        (void)slayer3d_data_game_runtime_update_frame(state->runtime, ctx, dt);
}

static void runner_pause_tick(slayer3d_game_context *ctx, void *userdata, float real_dt)
{
    runner_state *state = (runner_state *)userdata;
    if (state != NULL)
        (void)slayer3d_data_game_runtime_update_frame(state->runtime, ctx, real_dt);
}

static bool runner_event(slayer3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    runner_state *state = (runner_state *)userdata;
    if (state != NULL && slayer3d_data_game_runtime_process_event(state->runtime, ctx, event))
        return true;
    return true;
}

static void runner_render(slayer3d_game_context *ctx, void *userdata, float alpha)
{
    runner_state *state = (runner_state *)userdata;
    (void)alpha;
    if (state != NULL)
        slayer3d_data_game_runtime_render(state->runtime, ctx);
}

static void runner_shutdown(slayer3d_game_context *ctx, void *userdata)
{
    runner_state *state = (runner_state *)userdata;
    if (state != NULL)
    {
        slayer3d_data_game_runtime_release_mouse_capture(state->runtime, ctx);
        slayer3d_data_game_runtime_destroy(state->runtime);
        state->runtime = NULL;
    }
}

int slayer3d_runner_main(int argc, char **argv)
{
    runner_state state;
    SDL_zero(state);
    state.executable_path = argc > 0 && argv != NULL && argv[0] != NULL ? argv[0] : NULL;
    const slayer3d_tool_cli_result cli_result = slayer3d_runner_args_parse(argc, argv, &state.args, stderr);
    if (cli_result != SLAYER3D_TOOL_CLI_OK)
    {
        return cli_result == SLAYER3D_TOOL_CLI_HELP ? 0 : 2;
    }

    char error[512] = "";
    if (!runner_prepare_playable_map(&state, error, (int)sizeof(error)))
    {
        fprintf(stderr, "slayer3d_runner: %s\n", error[0] != '\0' ? error : "failed to prepare playable map");
        slayer3d_runner_args_destroy(&state.args);
        SDL_free(state.generated_map_output_dir);
        return 1;
    }
    if (!runner_prepare_fused_defaults(&state, error, (int)sizeof(error)))
    {
        fprintf(stderr, "slayer3d_runner: %s\n", error[0] != '\0' ? error : "failed to inspect fused runner");
        slayer3d_runner_args_destroy(&state.args);
        SDL_free(state.generated_map_output_dir);
        return 1;
    }
    if (!runner_apply_test_run_manifest(&state, error, (int)sizeof(error)))
    {
        fprintf(stderr, "slayer3d_runner: %s\n", error[0] != '\0' ? error : "failed to apply test-run manifest");
        slayer3d_runner_args_destroy(&state.args);
        SDL_free(state.generated_map_output_dir);
        return 1;
    }
    if ((state.args.data_asset_path == NULL || state.args.data_asset_path[0] == '\0') &&
        state.args.mount_kind == SLAYER3D_RUNNER_MOUNT_NONE)
    {
        fprintf(stderr, "slayer3d_runner: an asset mount and --data are required unless the executable is fused\n");
        slayer3d_runner_args_destroy(&state.args);
        SDL_free(state.generated_map_output_dir);
        return 2;
    }
    if (!load_runner_config(&state, error, (int)sizeof(error)))
    {
        fprintf(stderr, "slayer3d_runner: %s\n", error[0] != '\0' ? error : "failed to load app config");
        slayer3d_runner_args_destroy(&state.args);
        SDL_free(state.generated_map_output_dir);
        return 1;
    }

    /* Runner-hosted games are interactive apps: browser builds must yield to
     * the browser between frames instead of blocking the tab. */
    state.config.browser_main_loop = true;

    slayer3d_game_callbacks callbacks;
    SDL_zero(callbacks);
    callbacks.init = runner_init;
    callbacks.event = runner_event;
    callbacks.tick = runner_tick;
    callbacks.pause_tick = runner_pause_tick;
    callbacks.render = runner_render;
    callbacks.shutdown = runner_shutdown;

    const int result = slayer3d_run_game(&state.config, &callbacks, &state);
    slayer3d_runner_args_destroy(&state.args);
    SDL_free(state.generated_map_output_dir);
    return result;
}

#if !defined(SLAYER3D_RUNNER_NO_MAIN)
int main(int argc, char **argv)
{
    return slayer3d_runner_main(argc, argv);
}
#endif
