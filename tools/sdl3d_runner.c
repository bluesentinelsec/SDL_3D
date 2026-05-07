/**
 * @file sdl3d_runner.c
 * @brief Generic SDL3D data-game runner.
 */

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdio.h>

#include "sdl3d/sdl3d.h"

#include "sdl3d_runner_cli.h"
#include "sdl3d_runner_state.h"

#if defined(SDL3D_RUNNER_EMBEDDED_ASSETS)
#if !defined(SDL3D_RUNNER_EMBEDDED_ASSETS_DATA)
#define SDL3D_RUNNER_EMBEDDED_ASSETS_DATA sdl3d_runner_embedded_assets
#endif
#if !defined(SDL3D_RUNNER_EMBEDDED_ASSETS_SIZE)
#define SDL3D_RUNNER_EMBEDDED_ASSETS_SIZE sdl3d_runner_embedded_assets_size
#endif
extern const unsigned char SDL3D_RUNNER_EMBEDDED_ASSETS_DATA[];
extern const size_t SDL3D_RUNNER_EMBEDDED_ASSETS_SIZE;
#endif

typedef struct runner_state
{
    sdl3d_runner_args args;
    sdl3d_game_config config;
    char title[128];
    sdl3d_data_game_runtime *runtime;
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

static bool runner_mount_assets(sdl3d_asset_resolver *assets, void *userdata, char *error_buffer, int error_buffer_size)
{
    const runner_state *state = (const runner_state *)userdata;
    if (assets == NULL || state == NULL)
    {
        runner_set_error(error_buffer, error_buffer_size, "invalid runner asset mount arguments");
        return false;
    }

    switch (state->args.mount_kind)
    {
    case SDL3D_RUNNER_MOUNT_DIRECTORY:
        return sdl3d_asset_resolver_mount_directory(assets, state->args.mount_path, error_buffer, error_buffer_size);
    case SDL3D_RUNNER_MOUNT_PACK:
        return sdl3d_asset_resolver_mount_pack_file(assets, state->args.mount_path, error_buffer, error_buffer_size);
    case SDL3D_RUNNER_MOUNT_EMBEDDED:
#if defined(SDL3D_RUNNER_EMBEDDED_ASSETS)
        return sdl3d_asset_resolver_mount_memory_pack(assets, SDL3D_RUNNER_EMBEDDED_ASSETS_DATA,
                                                      SDL3D_RUNNER_EMBEDDED_ASSETS_SIZE, "sdl3d_runner.embedded",
                                                      error_buffer, error_buffer_size);
#else
        runner_set_error(error_buffer, error_buffer_size, "runner was not built with embedded assets");
        return false;
#endif
    case SDL3D_RUNNER_MOUNT_NONE:
    default:
        runner_set_error(error_buffer, error_buffer_size, "no asset mount was configured");
        return false;
    }
}

static bool runner_set_errorf(char *error_buffer, int error_buffer_size, const char *fmt, const char *value)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, fmt, value != NULL ? value : "");
    return false;
}

static bool runner_read_asset_state_file(runner_state *state, const char *path, char **out_text, size_t *out_size,
                                         char *error_buffer, int error_buffer_size)
{
    *out_text = NULL;
    *out_size = 0u;
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    if (assets == NULL)
        return runner_set_errorf(error_buffer, error_buffer_size, "failed to create asset resolver for '%s'", path);

    bool ok = runner_mount_assets(assets, state, error_buffer, error_buffer_size);
    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);
    if (ok)
        ok = sdl3d_asset_resolver_read_file(assets, path, &buffer, error_buffer, error_buffer_size);
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
    sdl3d_asset_buffer_free(&buffer);
    sdl3d_asset_resolver_destroy(assets);
    return ok;
}

static bool runner_build_initial_state(runner_state *state, sdl3d_properties **out_state, char *error_buffer,
                                       int error_buffer_size)
{
    *out_state = NULL;
    if (state->args.state_file_count <= 0 && state->args.state_json_count <= 0 &&
        state->args.state_assignment_count <= 0)
    {
        return true;
    }

    sdl3d_properties *props = sdl3d_properties_create();
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
                !sdl3d_runner_apply_state_json_object(props, text, size, state->args.state_files[i], error_buffer,
                                                      error_buffer_size))
            {
                SDL_free(text);
                sdl3d_properties_destroy(props);
                return false;
            }
            SDL_free(text);
            continue;
        }
        if (!sdl3d_runner_apply_state_json_file(props, state->args.state_files[i], error_buffer, error_buffer_size))
        {
            sdl3d_properties_destroy(props);
            return false;
        }
    }
    for (int i = 0; i < state->args.state_json_count; ++i)
    {
        const char *json = state->args.state_json_values[i];
        if (json == NULL || !sdl3d_runner_apply_state_json_object(props, json, SDL_strlen(json), "--state-json",
                                                                  error_buffer, error_buffer_size))
        {
            sdl3d_properties_destroy(props);
            return false;
        }
    }
    for (int i = 0; i < state->args.state_assignment_count; ++i)
    {
        if (!sdl3d_runner_apply_state_assignment(props, state->args.state_assignments[i], error_buffer,
                                                 error_buffer_size))
        {
            sdl3d_properties_destroy(props);
            return false;
        }
    }

    *out_state = props;
    return true;
}

static bool load_runner_config(runner_state *state, char *error_buffer, int error_buffer_size)
{
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    if (assets == NULL)
    {
        runner_set_error(error_buffer, error_buffer_size, "failed to create asset resolver");
        return false;
    }

    bool ok = runner_mount_assets(assets, state, error_buffer, error_buffer_size);
    if (ok)
    {
        SDL_zero(state->config);
        SDL_snprintf(state->title, sizeof(state->title), "%s", "SDL3D");
        ok = sdl3d_game_data_load_app_config_asset(assets, state->args.data_asset_path, &state->config, state->title,
                                                   (int)sizeof(state->title), error_buffer, error_buffer_size);
    }

    sdl3d_asset_resolver_destroy(assets);
    return ok;
}

static bool runner_init(sdl3d_game_context *ctx, void *userdata)
{
    runner_state *state = (runner_state *)userdata;
    sdl3d_data_game_runtime_desc desc;
    sdl3d_properties *initial_state = NULL;
    char error[512] = "";

    if (ctx == NULL || state == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL3D runner received invalid init arguments");
        return false;
    }

    if (!runner_build_initial_state(state, &initial_state, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL3D runner direct-start state failed: %s",
                     error[0] != '\0' ? error : "unknown error");
        return false;
    }

    sdl3d_data_game_runtime_desc_init(&desc);
    desc.session = ctx->session;
    desc.data_asset_path = state->args.data_asset_path;
    desc.media_dir = state->args.media_dir;
    desc.mount_assets = runner_mount_assets;
    desc.mount_userdata = state;
    desc.enable_managed_network = true;
    desc.initial_scene_override = state->args.scene;
    desc.initial_scene_state = initial_state;
    desc.initial_scene_payload = initial_state;
    desc.skip_app_flow_startup = state->args.scene != NULL && state->args.scene[0] != '\0';

    if (!sdl3d_data_game_runtime_create(&desc, &state->runtime, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL3D runner data load failed: %s", error);
        sdl3d_properties_destroy(initial_state);
        return false;
    }
    sdl3d_properties_destroy(initial_state);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D runner loaded data asset: %s", state->args.data_asset_path);
    return true;
}

static void runner_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    runner_state *state = (runner_state *)userdata;
    if (state != NULL)
        (void)sdl3d_data_game_runtime_update_frame(state->runtime, ctx, dt);
}

static void runner_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    runner_state *state = (runner_state *)userdata;
    if (state != NULL)
        (void)sdl3d_data_game_runtime_update_frame(state->runtime, ctx, real_dt);
}

static void runner_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    runner_state *state = (runner_state *)userdata;
    (void)alpha;
    if (state != NULL)
        sdl3d_data_game_runtime_render(state->runtime, ctx);
}

static void runner_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    runner_state *state = (runner_state *)userdata;
    (void)ctx;
    if (state != NULL)
    {
        sdl3d_data_game_runtime_destroy(state->runtime);
        state->runtime = NULL;
    }
}

int main(int argc, char **argv)
{
    runner_state state;
    SDL_zero(state);
    const sdl3d_tool_cli_result cli_result = sdl3d_runner_args_parse(argc, argv, &state.args, stderr);
    if (cli_result != SDL3D_TOOL_CLI_OK)
    {
        return cli_result == SDL3D_TOOL_CLI_HELP ? 0 : 2;
    }

    char error[512] = "";
    if (!load_runner_config(&state, error, (int)sizeof(error)))
    {
        fprintf(stderr, "sdl3d_runner: %s\n", error[0] != '\0' ? error : "failed to load app config");
        sdl3d_runner_args_destroy(&state.args);
        return 1;
    }

    sdl3d_game_callbacks callbacks;
    SDL_zero(callbacks);
    callbacks.init = runner_init;
    callbacks.tick = runner_tick;
    callbacks.pause_tick = runner_pause_tick;
    callbacks.render = runner_render;
    callbacks.shutdown = runner_shutdown;

    const int result = sdl3d_run_game(&state.config, &callbacks, &state);
    sdl3d_runner_args_destroy(&state.args);
    return result;
}
