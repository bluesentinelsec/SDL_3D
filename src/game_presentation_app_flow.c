/**
 * @file game_presentation_app_flow.c
 * @brief Data-authored application flow helpers.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_internal.h"

static slayer3d_window_mode parse_window_mode_setting(const char *value, slayer3d_window_mode fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;
    if (SDL_strcasecmp(value, "windowed") == 0 || SDL_strcasecmp(value, "window") == 0)
        return SLAYER3D_WINDOW_MODE_WINDOWED;
    if (SDL_strcasecmp(value, "fullscreen_exclusive") == 0 || SDL_strcasecmp(value, "exclusive") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE;
    if (SDL_strcasecmp(value, "fullscreen_borderless") == 0 || SDL_strcasecmp(value, "borderless") == 0 ||
        SDL_strcasecmp(value, "desktop_fullscreen") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS;
    return fallback;
}

static const char *window_mode_setting_name(slayer3d_window_mode mode)
{
    switch (mode)
    {
    case SLAYER3D_WINDOW_MODE_WINDOWED:
        return "windowed";
    case SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE:
        return "fullscreen_exclusive";
    case SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS:
        return "fullscreen_borderless";
    case SLAYER3D_WINDOW_MODE_DEFAULT:
    default:
        return "default";
    }
}

static slayer3d_backend parse_backend_setting(const char *value, slayer3d_backend fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;
    if (SDL_strcasecmp(value, "software") == 0 || SDL_strcasecmp(value, "sw") == 0)
        return SLAYER3D_BACKEND_SOFTWARE;
    if (SDL_strcasecmp(value, "opengl") == 0 || SDL_strcasecmp(value, "gl") == 0)
        return SLAYER3D_BACKEND_OPENGL;
    if (SDL_strcasecmp(value, "auto") == 0)
        return SLAYER3D_BACKEND_AUTO;
    return fallback;
}

static void app_flow_request_quit(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                  slayer3d_game_data_runtime *runtime)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || flow->quit_pending)
        return;

    flow->quit_pending = true;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D app quit requested");
    slayer3d_game_data_transition_desc transition;
    if (flow->app.quit_transition == NULL ||
        !slayer3d_game_data_get_transition(runtime, flow->app.quit_transition, &transition))
    {
        ctx->quit_requested = true;
        return;
    }

    slayer3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                              transition.duration, transition.done_signal_id);
}

static void app_flow_on_quit_request(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    slayer3d_game_data_app_flow *flow = (slayer3d_game_data_app_flow *)userdata;
    (void)signal_id;
    (void)payload;
    if (flow != NULL)
        flow->quit_request_received = true;
}

static bool app_flow_request_scene(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                   const char *scene_name)
{
    if (slayer3d_game_data_scene_flow_request(&flow->scene_flow, runtime, scene_name))
    {
        slayer3d_game_data_scene_transition_policy policy;
        (void)slayer3d_game_data_get_scene_transition_policy(runtime, &policy);
        if (policy.reset_menu_input_on_request)
            flow->scene_input_armed = false;
        return true;
    }
    return false;
}

static void app_flow_apply_pause_command(slayer3d_game_context *ctx, slayer3d_game_data_menu_pause_command command)
{
    if (ctx == NULL)
        return;

    if (command == SLAYER3D_GAME_DATA_MENU_PAUSE_PAUSE)
        ctx->paused = true;
    else if (command == SLAYER3D_GAME_DATA_MENU_PAUSE_RESUME)
        ctx->paused = false;
    else if (command == SLAYER3D_GAME_DATA_MENU_PAUSE_TOGGLE)
        ctx->paused = !ctx->paused;
}

static bool app_flow_apply_window_settings(const slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                           slayer3d_game_data_runtime *runtime)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || ctx->window == NULL || ctx->renderer == NULL)
        return false;

    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, flow->app.window_settings_target);
    if (settings == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D window settings skipped: settings actor '%s' was not found",
                    flow->app.window_settings_target != NULL ? flow->app.window_settings_target : "");
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(ctx->window, &width, &height);

    slayer3d_window_config config;
    slayer3d_init_window_config(&config);
    config.width = width;
    config.height = height;
    if (!slayer3d_get_render_context_authored_size(ctx->renderer, &config.logical_width, &config.logical_height))
        return false;
    config.logical_size_policy = slayer3d_get_render_context_logical_size_policy(ctx->renderer);
    config.title = SDL_GetWindowTitle(ctx->window);
    config.display_mode = parse_window_mode_setting(
        slayer3d_properties_get_string(settings->props, flow->app.window_display_mode_key, "windowed"),
        SLAYER3D_WINDOW_MODE_WINDOWED);
    config.backend =
        parse_backend_setting(slayer3d_properties_get_string(settings->props, flow->app.window_renderer_key, "opengl"),
                              slayer3d_get_render_context_backend(ctx->renderer));
    config.vsync = slayer3d_properties_get_bool(settings->props, flow->app.window_vsync_key, true);
    config.maximized = true;
    config.resizable = true;
    config.allow_backend_fallback = false;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D authored window apply requested: mode=%s renderer=%s vsync=%s",
                window_mode_setting_name(config.display_mode), slayer3d_get_backend_name(config.backend),
                config.vsync ? "on" : "off");
    return slayer3d_apply_window_config(&ctx->window, &ctx->renderer, &config);
}

static bool app_flow_consume_menu(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                  slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                  slayer3d_signal_bus *bus)
{
    if (flow->quit_pending || slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        return false;

    slayer3d_game_data_ui_metrics metrics;
    SDL_zero(metrics);
    metrics.paused = ctx != NULL && ctx->paused;

    slayer3d_game_data_menu_update_result result;
    if (!slayer3d_game_data_update_menus_for_metrics(runtime, input, &flow->scene_input_armed, &metrics, &result) ||
        !result.handled_input)
        return false;

    if (result.move_signal_id >= 0)
        slayer3d_signal_emit(bus, result.move_signal_id, NULL);
    if (result.select_signal_id >= 0)
        slayer3d_signal_emit(bus, result.select_signal_id, NULL);
    if (!result.selected)
        return true;

    if (result.signal_id >= 0)
        slayer3d_signal_emit(bus, result.signal_id, NULL);
    if (slayer3d_game_data_app_signal_applies_window_settings(runtime, result.signal_id))
        (void)app_flow_apply_window_settings(flow, ctx, runtime);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    if (result.return_to != NULL && scene_state != NULL)
        slayer3d_properties_set_string(scene_state, "return_scene", result.return_to);
    if (result.scene_state_key != NULL && result.scene_state_value != NULL && scene_state != NULL)
        slayer3d_properties_set_string(scene_state, result.scene_state_key, result.scene_state_value);
    if (result.has_return_paused && scene_state != NULL)
        slayer3d_properties_set_bool(scene_state, "return_paused", result.return_paused);

    const char *scene = result.scene;
    if (result.return_scene && scene_state != NULL)
    {
        scene = slayer3d_properties_get_string(scene_state, "return_scene", scene);
        if (ctx != NULL)
            ctx->paused = slayer3d_properties_get_bool(scene_state, "return_paused", ctx->paused);
    }
    app_flow_apply_pause_command(ctx, result.pause_command);

    if (result.quit)
        app_flow_request_quit(flow, ctx, runtime);
    else if (scene != NULL)
        (void)app_flow_request_scene(flow, runtime, scene);

    return true;
}

static void app_flow_consume_scene_shortcuts(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                             slayer3d_input_manager *input)
{
    if (flow->quit_pending || slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        return;

    const int count = slayer3d_game_data_scene_shortcut_count(runtime);
    for (int i = 0; i < count; ++i)
    {
        slayer3d_game_data_scene_shortcut shortcut;
        if (slayer3d_game_data_scene_shortcut_at(runtime, i, &shortcut) && shortcut.action_id >= 0 &&
            slayer3d_game_data_active_scene_allows_action(runtime, shortcut.action_id) &&
            slayer3d_input_is_pressed(input, shortcut.action_id))
        {
            (void)app_flow_request_scene(flow, runtime, shortcut.scene);
            return;
        }
    }
}

static bool app_flow_set_scene_without_transition(slayer3d_game_data_app_flow *flow,
                                                  slayer3d_game_data_runtime *runtime, const char *scene_name)
{
    if (flow == NULL || runtime == NULL || scene_name == NULL)
        return false;

    if (!slayer3d_game_data_set_active_scene(runtime, scene_name))
        return false;

    slayer3d_game_data_scene_flow_init(&flow->scene_flow);
    flow->scene_input_armed = false;
    slayer3d_game_data_timeline_state_init(&flow->timeline);
    return true;
}

static bool app_flow_apply_skip_policy(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                       const slayer3d_input_manager *input, bool capture_input, bool *out_applied,
                                       bool *out_block_menus, bool *out_block_scene_shortcuts)
{
    if (out_applied != NULL)
        *out_applied = false;
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;
    if (flow == NULL || runtime == NULL)
        return false;

    slayer3d_game_data_skip_policy policy;
    if (!slayer3d_game_data_get_active_skip_policy(runtime, &policy))
    {
        flow->skip_scene = NULL;
        flow->skip_requested = false;
        return false;
    }

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (flow->skip_scene != active_scene)
    {
        flow->skip_scene = active_scene;
        flow->skip_requested = false;
    }

    bool consumed = false;
    if (capture_input && input != NULL)
    {
        bool matched = false;
        if (policy.input == SLAYER3D_GAME_DATA_SKIP_INPUT_ANY)
        {
            matched = slayer3d_input_any_pressed(input);
        }
        else if (policy.input == SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION)
        {
            matched = policy.action_id >= 0 &&
                      slayer3d_game_data_active_scene_allows_action(runtime, policy.action_id) &&
                      slayer3d_input_is_pressed(input, policy.action_id);
        }

        if (matched)
        {
            flow->skip_requested = true;
            consumed = policy.consume_input;
            if (out_block_menus != NULL)
                *out_block_menus = policy.block_menus || policy.consume_input;
            if (out_block_scene_shortcuts != NULL)
                *out_block_scene_shortcuts = policy.block_scene_shortcuts || policy.consume_input;
        }
    }

    if (flow->skip_requested && !flow->quit_pending && !flow->transition.active &&
        !slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
    {
        const bool requested = policy.preserve_exit_transition
                                   ? app_flow_request_scene(flow, runtime, policy.scene)
                                   : app_flow_set_scene_without_transition(flow, runtime, policy.scene);
        if (requested)
        {
            flow->skip_requested = false;
            if (out_applied != NULL)
                *out_applied = true;
        }
    }

    return consumed;
}

static bool app_flow_timeline_is_pending(const slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime)
{
    slayer3d_game_data_timeline_policy policy;
    if (flow == NULL || runtime == NULL || flow->timeline.complete ||
        !slayer3d_game_data_get_active_timeline_policy(runtime, &policy))
    {
        return false;
    }
    return true;
}

static void app_flow_timeline_blocks(const slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                     bool *out_block_menus, bool *out_block_scene_shortcuts)
{
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;

    if (!app_flow_timeline_is_pending(flow, runtime))
        return;

    slayer3d_game_data_timeline_policy policy;
    if (!slayer3d_game_data_get_active_timeline_policy(runtime, &policy))
        return;

    if (out_block_menus != NULL)
        *out_block_menus = policy.block_menus;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = policy.block_scene_shortcuts;
}

static bool app_flow_update_timeline(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime, float dt)
{
    if (flow == NULL || runtime == NULL || flow->quit_pending || flow->transition.active ||
        slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
    {
        return true;
    }

    slayer3d_game_data_timeline_update_result result;
    if (!slayer3d_game_data_update_timeline(runtime, &flow->timeline, dt, &result))
        return false;

    if (result.scene_request != NULL)
        (void)app_flow_request_scene(flow, runtime, result.scene_request);
    return true;
}

static void app_flow_update_transition(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                       slayer3d_signal_bus *bus, float dt)
{
    if (flow->transition.active)
        slayer3d_transition_update(&flow->transition, bus, dt);
    if (flow->quit_pending && flow->transition.finished && !flow->transition.active)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D app quit transition finished");
        ctx->quit_requested = true;
    }
}

void slayer3d_game_data_app_flow_init(slayer3d_game_data_app_flow *flow)
{
    if (flow == NULL)
        return;
    SDL_zero(*flow);
    slayer3d_game_data_scene_flow_init(&flow->scene_flow);
    slayer3d_transition_reset(&flow->transition);
    flow->app.start_signal_id = -1;
    flow->app.quit_action_id = -1;
    flow->app.quit_request_signal_id = -1;
    flow->app.pause_action_id = -1;
    flow->app.quit_signal_id = -1;
}

void slayer3d_game_data_app_flow_stop(slayer3d_game_data_app_flow *flow)
{
    if (flow == NULL)
        return;
    if (flow->signal_bus != NULL && flow->quit_request_connection_id > 0)
        slayer3d_signal_disconnect(flow->signal_bus, flow->quit_request_connection_id);
    flow->signal_bus = NULL;
    flow->quit_request_connection_id = 0;
    flow->quit_request_received = false;
}

bool slayer3d_game_data_app_flow_start(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime)
{
    if (flow == NULL || runtime == NULL)
        return false;

    slayer3d_game_data_app_flow_stop(flow);
    slayer3d_game_data_scene_flow_init(&flow->scene_flow);
    slayer3d_transition_reset(&flow->transition);
    flow->quit_pending = false;
    flow->scene_input_armed = false;
    flow->skip_scene = NULL;
    flow->skip_requested = false;
    slayer3d_game_data_timeline_state_init(&flow->timeline);
    if (!slayer3d_game_data_get_app_control(runtime, &flow->app))
        return false;

    if (flow->app.quit_request_signal_id >= 0)
    {
        flow->signal_bus = runtime->session != NULL ? slayer3d_game_session_get_signal_bus(runtime->session) : NULL;
        flow->quit_request_connection_id =
            slayer3d_signal_connect(flow->signal_bus, flow->app.quit_request_signal_id, app_flow_on_quit_request, flow);
        if (flow->quit_request_connection_id <= 0)
        {
            slayer3d_game_data_app_flow_stop(flow);
            return SDL_SetError("Unable to connect app quit request signal");
        }
    }

    slayer3d_game_data_transition_desc transition;
    if (flow->app.startup_transition != NULL &&
        slayer3d_game_data_get_transition(runtime, flow->app.startup_transition, &transition))
    {
        slayer3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                                  transition.duration, transition.done_signal_id);
    }
    return true;
}

bool slayer3d_game_data_app_flow_quit_pending(const slayer3d_game_data_app_flow *flow)
{
    return flow != NULL && flow->quit_pending;
}

bool slayer3d_game_data_app_flow_is_transitioning(const slayer3d_game_data_app_flow *flow)
{
    return flow != NULL &&
           (flow->transition.active || slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow));
}

bool slayer3d_game_data_app_flow_update(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                        slayer3d_game_data_runtime *runtime, float dt)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || ctx->session == NULL)
        return false;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(ctx->session);
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(ctx->session);
    yyjson_val *quit = obj_get(obj_get(runtime_root(runtime), "app"), "quit");
    const bool quit_enabled = eval_data_condition(runtime, obj_get(quit, "enabled_if"), NULL);
    if (flow->quit_request_received)
    {
        flow->quit_request_received = false;
        if (quit_enabled)
            app_flow_request_quit(flow, ctx, runtime);
    }
    bool skip_applied = false;
    bool skip_blocks_menus = false;
    bool skip_blocks_scene_shortcuts = false;
    const bool skip_consumed = app_flow_apply_skip_policy(flow, runtime, input, true, &skip_applied, &skip_blocks_menus,
                                                          &skip_blocks_scene_shortcuts);
    bool timeline_blocks_menus = false;
    bool timeline_blocks_scene_shortcuts = false;
    app_flow_timeline_blocks(flow, runtime, &timeline_blocks_menus, &timeline_blocks_scene_shortcuts);

    bool activity_blocks_menus = false;
    bool activity_blocks_scene_shortcuts = false;
    const bool activity_wake_consumed = slayer3d_game_data_scene_activity_consumes_wake_input(
        runtime, input, &activity_blocks_menus, &activity_blocks_scene_shortcuts);

    if (!skip_consumed && !activity_wake_consumed)
    {
        if (quit_enabled && flow->app.quit_action_id >= 0 &&
            slayer3d_game_data_active_scene_allows_action(runtime, flow->app.quit_action_id) &&
            slayer3d_input_is_pressed(input, flow->app.quit_action_id))
            app_flow_request_quit(flow, ctx, runtime);

        if (!skip_blocks_scene_shortcuts && !timeline_blocks_scene_shortcuts && !activity_blocks_scene_shortcuts)
            app_flow_consume_scene_shortcuts(flow, runtime, input);
        bool menu_consumed = false;
        if (!skip_blocks_menus && !timeline_blocks_menus && !activity_blocks_menus)
            menu_consumed = app_flow_consume_menu(flow, ctx, runtime, input, bus);

        if (!menu_consumed && flow->app.pause_action_id >= 0 &&
            slayer3d_input_is_pressed(input, flow->app.pause_action_id) &&
            slayer3d_game_data_active_scene_allows_action(runtime, flow->app.pause_action_id) && !flow->quit_pending &&
            !slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        {
            if (ctx->paused)
                ctx->paused = false;
            else
            {
                slayer3d_game_data_ui_metrics metrics;
                SDL_zero(metrics);
                metrics.paused = ctx->paused;
                if (slayer3d_game_data_app_pause_allowed(runtime, &metrics) &&
                    slayer3d_game_data_active_scene_updates_game(runtime))
                    ctx->paused = true;
            }
        }
    }

    const bool scene_transitioning_before = slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow);
    app_flow_update_transition(flow, ctx, bus, dt);
    slayer3d_game_data_scene_flow_update(&flow->scene_flow, runtime, bus, dt);
    bool deferred_skip_applied = false;
    (void)app_flow_apply_skip_policy(flow, runtime, input, false, &deferred_skip_applied, NULL, NULL);
    skip_applied = skip_applied || deferred_skip_applied;
    if (!scene_transitioning_before && !skip_applied && !app_flow_update_timeline(flow, runtime, dt))
        return false;
    return true;
}

void slayer3d_game_data_app_flow_draw(const slayer3d_game_data_app_flow *flow, slayer3d_render_context *renderer)
{
    if (flow == NULL)
        return;
    slayer3d_game_data_scene_flow_draw(&flow->scene_flow, renderer);
    slayer3d_transition_draw(&flow->transition, renderer);
}
