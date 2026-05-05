/**
 * @file sdl3d_runner.c
 * @brief Generic SDL3D data-game runner.
 */

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdio.h>

#include "sdl3d/sdl3d.h"

#if defined(SDL3D_RUNNER_EMBEDDED_ASSETS)
extern const unsigned char sdl3d_runner_embedded_assets[];
extern const size_t sdl3d_runner_embedded_assets_size;
#endif

typedef enum runner_mount_kind
{
    RUNNER_MOUNT_NONE = 0,
    RUNNER_MOUNT_DIRECTORY,
    RUNNER_MOUNT_PACK,
    RUNNER_MOUNT_EMBEDDED
} runner_mount_kind;

typedef struct runner_state
{
    runner_mount_kind mount_kind;
    const char *mount_path;
    const char *data_asset_path;
    const char *media_dir;
    bool help_requested;
    sdl3d_game_config config;
    char title[128];
    sdl3d_data_game_runtime *runtime;
} runner_state;

static void runner_set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "unknown error");
}

static void print_usage(const char *argv0)
{
    const char *program = argv0 != NULL ? argv0 : "sdl3d_runner";
    fprintf(stderr,
            "Usage:\n"
            "  %s --root <asset-root> --data <asset://game.json> [--media <media-dir>]\n"
            "  %s --pack <game.sdl3dpak> --data <asset://game.json> [--media <media-dir>]\n",
            program, program);
#if defined(SDL3D_RUNNER_EMBEDDED_ASSETS)
    fprintf(stderr, "  %s --embedded --data <asset://game.json> [--media <media-dir>]\n", program);
#endif
}

static bool parse_args(int argc, char **argv, runner_state *state)
{
    SDL_zero(*state);
    state->data_asset_path = NULL;
#if defined(SDL3D_MEDIA_DIR)
    state->media_dir = SDL3D_MEDIA_DIR;
#endif

    for (int i = 1; i < argc; ++i)
    {
        if (SDL_strcmp(argv[i], "--root") == 0 && i + 1 < argc)
        {
            if (state->mount_kind != RUNNER_MOUNT_NONE)
                return false;
            state->mount_kind = RUNNER_MOUNT_DIRECTORY;
            state->mount_path = argv[++i];
        }
        else if (SDL_strcmp(argv[i], "--pack") == 0 && i + 1 < argc)
        {
            if (state->mount_kind != RUNNER_MOUNT_NONE)
                return false;
            state->mount_kind = RUNNER_MOUNT_PACK;
            state->mount_path = argv[++i];
        }
        else if (SDL_strcmp(argv[i], "--embedded") == 0)
        {
#if defined(SDL3D_RUNNER_EMBEDDED_ASSETS)
            if (state->mount_kind != RUNNER_MOUNT_NONE)
                return false;
            state->mount_kind = RUNNER_MOUNT_EMBEDDED;
#else
            return false;
#endif
        }
        else if (SDL_strcmp(argv[i], "--data") == 0 && i + 1 < argc)
        {
            state->data_asset_path = argv[++i];
        }
        else if (SDL_strcmp(argv[i], "--media") == 0 && i + 1 < argc)
        {
            state->media_dir = argv[++i];
        }
        else if (SDL_strcmp(argv[i], "--help") == 0 || SDL_strcmp(argv[i], "-h") == 0)
        {
            state->help_requested = true;
            return false;
        }
        else
        {
            return false;
        }
    }

    return state->mount_kind != RUNNER_MOUNT_NONE && state->data_asset_path != NULL &&
           state->data_asset_path[0] != '\0';
}

static bool runner_mount_assets(sdl3d_asset_resolver *assets, void *userdata, char *error_buffer, int error_buffer_size)
{
    const runner_state *state = (const runner_state *)userdata;
    if (assets == NULL || state == NULL)
    {
        runner_set_error(error_buffer, error_buffer_size, "invalid runner asset mount arguments");
        return false;
    }

    switch (state->mount_kind)
    {
    case RUNNER_MOUNT_DIRECTORY:
        return sdl3d_asset_resolver_mount_directory(assets, state->mount_path, error_buffer, error_buffer_size);
    case RUNNER_MOUNT_PACK:
        return sdl3d_asset_resolver_mount_pack_file(assets, state->mount_path, error_buffer, error_buffer_size);
    case RUNNER_MOUNT_EMBEDDED:
#if defined(SDL3D_RUNNER_EMBEDDED_ASSETS)
        return sdl3d_asset_resolver_mount_memory_pack(assets, sdl3d_runner_embedded_assets,
                                                      sdl3d_runner_embedded_assets_size, "sdl3d_runner.embedded",
                                                      error_buffer, error_buffer_size);
#else
        runner_set_error(error_buffer, error_buffer_size, "runner was not built with embedded assets");
        return false;
#endif
    case RUNNER_MOUNT_NONE:
    default:
        runner_set_error(error_buffer, error_buffer_size, "no asset mount was configured");
        return false;
    }
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
        ok = sdl3d_game_data_load_app_config_asset(assets, state->data_asset_path, &state->config, state->title,
                                                   (int)sizeof(state->title), error_buffer, error_buffer_size);
    }

    sdl3d_asset_resolver_destroy(assets);
    return ok;
}

static bool runner_init(sdl3d_game_context *ctx, void *userdata)
{
    runner_state *state = (runner_state *)userdata;
    sdl3d_data_game_runtime_desc desc;
    char error[512] = "";

    if (ctx == NULL || state == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL3D runner received invalid init arguments");
        return false;
    }

    sdl3d_data_game_runtime_desc_init(&desc);
    desc.session = ctx->session;
    desc.data_asset_path = state->data_asset_path;
    desc.media_dir = state->media_dir;
    desc.mount_assets = runner_mount_assets;
    desc.mount_userdata = state;

    if (!sdl3d_data_game_runtime_create(&desc, &state->runtime, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL3D runner data load failed: %s", error);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D runner loaded data asset: %s", state->data_asset_path);
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
    if (!parse_args(argc, argv, &state))
    {
        print_usage(argc > 0 ? argv[0] : NULL);
        return state.help_requested ? 0 : 2;
    }

    char error[512] = "";
    if (!load_runner_config(&state, error, (int)sizeof(error)))
    {
        fprintf(stderr, "sdl3d_runner: %s\n", error[0] != '\0' ? error : "failed to load app config");
        return 1;
    }

    sdl3d_game_callbacks callbacks;
    SDL_zero(callbacks);
    callbacks.init = runner_init;
    callbacks.tick = runner_tick;
    callbacks.pause_tick = runner_pause_tick;
    callbacks.render = runner_render;
    callbacks.shutdown = runner_shutdown;

    return sdl3d_run_game(&state.config, &callbacks, &state);
}
