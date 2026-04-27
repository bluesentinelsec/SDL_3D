/*
 * Doom-style level demo using the SDL3D managed game loop.
 *
 * Gameplay stays in subsystem modules. The managed loop owns SDL init,
 * event polling, fixed-timestep ticking, presentation, and engine cleanup.
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/sdl3d.h"
#include "sdl3d/ui.h"

#include "backend.h"
#include "entities.h"
#include "hazard_effects.h"
#include "level_data.h"
#include "player.h"
#include "renderer.h"
#include "surveillance.h"

#define SIG_FADE_OUT_DONE 2001
#define SIG_SURVEILLANCE_ENTER 2002
#define SIG_SURVEILLANCE_EXIT 2003
#define SIG_AMBIENT_FEEDBACK 2004
#define SIG_AMBIENT_FEEDBACK_SKIP 2005
#define LIFT_FLOOR_MIN_Y 0.0f
#define LIFT_FLOOR_MAX_Y 2.5f
#define LIFT_CEIL_Y 12.0f
#define LIFT_CYCLE_SECONDS 6.0f
#define LIFT_REBUILD_MIN_DELTA 0.02f
#define AMBIENT_FADE_SECONDS 1.0f
#define AMBIENT_FEEDBACK_SECONDS 5.0f
#define TELEPORT_FEEDBACK_SECONDS 2.0f
#define DRAGON_TELEPORTER_ID 1
#define DAMAGE_FEEDBACK_REFERENCE_DPS 30.0f
#define DAMAGE_FEEDBACK_MIN_STRENGTH 0.35f
#define DAMAGE_FEEDBACK_ATTACK_RATE 10.0f
#define DAMAGE_FEEDBACK_DECAY_RATE 4.0f
#define DAMAGE_FEEDBACK_PULSE_HZ 2.8f
#define SURVEILLANCE_BUTTON_X 43.0f
#define SURVEILLANCE_BUTTON_Z 89.0f
#define FEEDBACK_AMBIENT_ZONE "ambient_zone"

typedef struct doom_state
{
    level_data level;
    entities ent;
    doom_hazard_particles hazards;
    player_state player;
    render_state render;
    sdl3d_transition transition;
    sdl3d_font debug_font;
    sdl3d_ui_context *ui;
    sdl3d_demo_player *demo_player;
    sdl3d_audio_engine *audio;
    sdl3d_logic_world *logic;
    sdl3d_logic_branch ambient_feedback_branch;
    sdl3d_sector_watcher sector_watcher;
    sdl3d_teleporter dragon_teleporter;
    doom_surveillance_camera surveillance;
    sdl3d_backend current_backend;
    doom_render_profile render_profile;
    float ambient_feedback_timer;
    float teleport_feedback_timer;
    float damage_feedback_strength;
    float damage_pulse_timer;
    float lift_timer;
    float lift_last_floor_y;
    int action_pause;
    int action_menu;
    int action_toggle_lighting;
    int action_toggle_lightmaps;
    int action_toggle_debug_stats;
    int action_toggle_portal_culling;
    int action_switch_backend;
    int action_render_profile[DOOM_RENDER_PROFILE_COUNT];
    float pause_flash_timer;
    bool has_font;
    bool level_ready;
    bool entities_ready;
    bool quit_pending;
} doom_state;

static void doom_state_cleanup(doom_state *state)
{
    sdl3d_logic_world_destroy(state->logic);
    state->logic = NULL;
    render_state_free(&state->render);
    doom_hazard_particles_free(&state->hazards);
    if (state->entities_ready)
    {
        entities_free(&state->ent);
        state->entities_ready = false;
    }
    if (state->level_ready)
    {
        level_data_free(&state->level);
        state->level_ready = false;
    }
    sdl3d_ui_destroy(state->ui);
    state->ui = NULL;
    sdl3d_demo_playback_free(state->demo_player);
    state->demo_player = NULL;
    if (state->has_font)
    {
        sdl3d_free_font(&state->debug_font);
        state->has_font = false;
    }
}

static void stop_demo_playback(sdl3d_game_context *ctx, doom_state *state)
{
    if (state->demo_player == NULL)
    {
        return;
    }

    if (ctx != NULL && ctx->input != NULL)
    {
        sdl3d_demo_playback_stop(ctx->input);
    }
    sdl3d_demo_playback_free(state->demo_player);
    state->demo_player = NULL;
}

static void apply_window_defaults(sdl3d_game_context *ctx, doom_state *state)
{
    backend_apply_defaults(ctx->renderer, state->render_profile);
    SDL_SetWindowRelativeMouseMode(ctx->window, true);
}

static void on_fade_out_done(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    (void)signal_id;
    (void)payload;
    sdl3d_game_context *ctx = (sdl3d_game_context *)userdata;
    ctx->quit_requested = true;
}

static bool doom_logic_set_ambient(void *userdata, int ambient_id, float fade_seconds, const sdl3d_properties *payload)
{
    (void)payload;
    doom_state *state = (doom_state *)userdata;
    static const sdl3d_audio_ambient ambient_zones[] = {
        {0, NULL, 0.0f, false},
        {DOOM_AMBIENT_DEMO_SOUND_ID, SDL3D_MEDIA_DIR "/audio/ambient_zone.wav", 0.45f, true},
    };

    if (state == NULL)
    {
        return false;
    }

    if (state->audio == NULL)
    {
        return true;
    }

    if (!sdl3d_audio_set_ambient(state->audio, ambient_zones, (int)SDL_arraysize(ambient_zones), ambient_id,
                                 fade_seconds))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ambient transition failed: %s", SDL_GetError());
        SDL_ClearError();
        return false;
    }

    return true;
}

static bool doom_logic_trigger_feedback(void *userdata, const char *feedback_name, float duration_seconds,
                                        const sdl3d_properties *payload)
{
    (void)payload;
    doom_state *state = (doom_state *)userdata;
    if (state == NULL || feedback_name == NULL)
    {
        return false;
    }

    if (SDL_strcmp(feedback_name, FEEDBACK_AMBIENT_ZONE) == 0)
    {
        state->ambient_feedback_timer = duration_seconds;
        return true;
    }

    return false;
}

static bool doom_logic_teleport_player(void *userdata, const sdl3d_teleport_destination *destination,
                                       const sdl3d_properties *payload)
{
    doom_state *state = (doom_state *)userdata;

    if (state == NULL || destination == NULL)
    {
        return false;
    }

    sdl3d_fps_mover_teleport(&state->player.mover, destination->position, destination->use_yaw, destination->yaw,
                             destination->use_pitch, destination->pitch);
    state->player.proj_active = false;
    state->teleport_feedback_timer = TELEPORT_FEEDBACK_SECONDS;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Teleporter %d moved player to %.1f %.1f %.1f",
                sdl3d_properties_get_int(payload, "teleporter_id", -1), destination->position.x,
                destination->position.y, destination->position.z);
    return true;
}

static bool doom_logic_set_active_camera(void *userdata, const char *camera_name, const sdl3d_camera3d *camera,
                                         const sdl3d_properties *payload)
{
    (void)payload;
    doom_state *state = (doom_state *)userdata;
    if (state == NULL || camera == NULL)
    {
        return false;
    }

    state->surveillance.camera = *camera;
    state->surveillance.active = true;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Activated surveillance camera '%s'",
                camera_name != NULL ? camera_name : "unnamed");
    return true;
}

static bool doom_logic_restore_camera(void *userdata, const sdl3d_properties *payload)
{
    (void)payload;
    doom_state *state = (doom_state *)userdata;
    if (state == NULL)
    {
        return false;
    }

    state->surveillance.active = false;
    return true;
}

static void init_dragon_teleporter(doom_state *state)
{
    sdl3d_bounding_box source = {
        sdl3d_vec3_make(22.5f, 0.0f, 86.5f),
        sdl3d_vec3_make(25.5f, 2.4f, 89.5f),
    };
    sdl3d_teleport_destination destination;
    SDL_zero(destination);
    destination.position = sdl3d_vec3_make(72.0f, 2.5f + PLAYER_HEIGHT, 63.0f);
    destination.yaw = -SDL_PI_F * 0.5f;
    destination.pitch = 0.0f;
    destination.use_yaw = true;
    destination.use_pitch = true;

    sdl3d_teleporter_init(&state->dragon_teleporter, DRAGON_TELEPORTER_ID, source, destination);
}

static void init_surveillance_camera(doom_state *state)
{
    sdl3d_bounding_box button_bounds = {
        sdl3d_vec3_make(SURVEILLANCE_BUTTON_X - 1.2f, -0.1f, SURVEILLANCE_BUTTON_Z - 1.2f),
        sdl3d_vec3_make(SURVEILLANCE_BUTTON_X + 1.2f, 2.0f, SURVEILLANCE_BUTTON_Z + 1.2f),
    };
    sdl3d_camera3d camera;
    SDL_zero(camera);
    camera.position = sdl3d_vec3_make(4.0f, 3.25f, 25.2f);
    camera.target = sdl3d_vec3_make(4.0f, 0.15f, 18.4f);
    camera.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = 70.0f;
    camera.projection = SDL3D_CAMERA_PERSPECTIVE;

    doom_surveillance_init(&state->surveillance, button_bounds, camera, SIG_SURVEILLANCE_ENTER, SIG_SURVEILLANCE_EXIT);
}

static bool bind_doom_logic(sdl3d_game_context *ctx, doom_state *state)
{
    state->logic = sdl3d_logic_world_create(ctx->bus, ctx->timers);
    if (state->logic == NULL)
    {
        return false;
    }

    sdl3d_logic_game_adapters adapters;
    SDL_zero(adapters);
    adapters.userdata = state;
    adapters.teleport_player = doom_logic_teleport_player;
    adapters.set_active_camera = doom_logic_set_active_camera;
    adapters.restore_camera = doom_logic_restore_camera;
    adapters.set_ambient = doom_logic_set_ambient;
    adapters.trigger_feedback = doom_logic_trigger_feedback;
    sdl3d_logic_world_set_game_adapters(state->logic, &adapters);

    sdl3d_logic_action ambient_action =
        sdl3d_logic_action_make_set_ambient_from_payload("ambient_sound_id", AMBIENT_FADE_SECONDS);
    if (sdl3d_logic_world_bind_logic_action(state->logic, SDL3D_SIGNAL_ENTERED_SECTOR, &ambient_action) == 0)
    {
        return false;
    }

    sdl3d_value ambient_expected;
    SDL_zero(ambient_expected);
    ambient_expected.type = SDL3D_VALUE_INT;
    ambient_expected.as_int = DOOM_AMBIENT_DEMO_SOUND_ID;
    sdl3d_logic_branch_init_payload(&state->ambient_feedback_branch, 1, "ambient_sound_id", ambient_expected,
                                    SIG_AMBIENT_FEEDBACK, SIG_AMBIENT_FEEDBACK_SKIP);
    if (sdl3d_logic_world_bind_branch(state->logic, SDL3D_SIGNAL_ENTERED_SECTOR, &state->ambient_feedback_branch) == 0)
    {
        return false;
    }

    sdl3d_logic_action ambient_feedback_action =
        sdl3d_logic_action_make_trigger_feedback(FEEDBACK_AMBIENT_ZONE, AMBIENT_FEEDBACK_SECONDS);
    if (sdl3d_logic_world_bind_logic_action(state->logic, SIG_AMBIENT_FEEDBACK, &ambient_feedback_action) == 0)
    {
        return false;
    }

    sdl3d_logic_action teleport_action = sdl3d_logic_action_make_teleport_player_from_payload();
    if (sdl3d_logic_world_bind_logic_action(state->logic, SDL3D_SIGNAL_TELEPORT, &teleport_action) == 0)
    {
        return false;
    }

    sdl3d_logic_action camera_action =
        sdl3d_logic_action_make_set_active_camera("nukage_surveillance", &state->surveillance.camera);
    if (sdl3d_logic_world_bind_logic_action(state->logic, SIG_SURVEILLANCE_ENTER, &camera_action) == 0)
    {
        return false;
    }

    sdl3d_logic_action restore_camera_action = sdl3d_logic_action_make_restore_camera();
    if (sdl3d_logic_world_bind_logic_action(state->logic, SIG_SURVEILLANCE_EXIT, &restore_camera_action) == 0)
    {
        return false;
    }

    return true;
}

static void start_quit_fade(sdl3d_game_context *ctx, doom_state *state)
{
    if (state->quit_pending)
    {
        return;
    }

    state->quit_pending = true;
    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_OUT, (sdl3d_color){0, 0, 0, 255},
                           0.5f, SIG_FADE_OUT_DONE);
}

static void toggle_pause(sdl3d_game_context *ctx, doom_state *state)
{
    if (state->quit_pending)
    {
        return;
    }

    ctx->paused = !ctx->paused;
    state->pause_flash_timer = 0.0f;
}

static int bind_doom_key_action(sdl3d_input_manager *input, const char *name, SDL_Scancode key)
{
    int action = sdl3d_input_register_action(input, name);
    sdl3d_input_bind_key(input, action, key);
    return action;
}

static void bind_doom_actions(sdl3d_input_manager *input, doom_state *state)
{
    static const SDL_Scancode profile_keys[DOOM_RENDER_PROFILE_COUNT] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
    };
    sdl3d_input_bind_fps_defaults(input);
    state->action_pause = sdl3d_input_find_action(input, "pause");
    state->action_menu = sdl3d_input_find_action(input, "menu");
    state->action_toggle_lighting = bind_doom_key_action(input, "toggle_lighting", SDL_SCANCODE_L);
    state->action_toggle_lightmaps = bind_doom_key_action(input, "toggle_lightmaps", SDL_SCANCODE_M);
    state->action_toggle_debug_stats = bind_doom_key_action(input, "toggle_debug_stats", SDL_SCANCODE_F1);
    state->action_toggle_portal_culling = bind_doom_key_action(input, "toggle_portal_culling", SDL_SCANCODE_F2);
    state->action_switch_backend = bind_doom_key_action(input, "switch_backend", SDL_SCANCODE_TAB);
    for (int i = 0; i < DOOM_RENDER_PROFILE_COUNT; ++i)
    {
        char action_name[SDL3D_INPUT_ACTION_NAME_MAX];
        SDL_snprintf(action_name, sizeof(action_name), "render_profile_%d", i + 1);
        state->action_render_profile[i] = bind_doom_key_action(input, action_name, profile_keys[i]);
    }
}

static bool game_init(sdl3d_game_context *ctx, void *userdata)
{
    doom_state *state = (doom_state *)userdata;

    state->current_backend = sdl3d_get_render_context_backend(ctx->renderer);
    state->render_profile = DOOM_RENDER_PROFILE_MODERN;
    state->audio = ctx->audio;
    sdl3d_sector_watcher_init(&state->sector_watcher);
    bind_doom_actions(ctx->input, state);
    apply_window_defaults(ctx, state);

    if (sdl3d_signal_connect(ctx->bus, SIG_FADE_OUT_DONE, on_fade_out_done, ctx) == 0)
    {
        return false;
    }
    state->has_font = sdl3d_load_font(SDL3D_MEDIA_DIR "/fonts/Roboto.ttf", 40.0f, &state->debug_font);
    if (!sdl3d_ui_create(state->has_font ? &state->debug_font : NULL, &state->ui))
    {
        doom_state_cleanup(state);
        return false;
    }

    if (!level_data_init(&state->level))
    {
        doom_state_cleanup(state);
        return false;
    }
    state->level_ready = true;
    state->lift_timer = 0.0f;
    state->lift_last_floor_y = g_sectors[DOOM_DYNAMIC_LIFT_SECTOR].floor_y;

    if (!entities_init(&state->ent, &state->level.unlit, ctx->registry, ctx->bus))
    {
        entities_free(&state->ent);
        doom_state_cleanup(state);
        return false;
    }
    state->entities_ready = true;

    if (!doom_hazard_particles_init(&state->hazards, &state->level.unlit, g_sectors))
    {
        doom_state_cleanup(state);
        return false;
    }

    player_init(&state->player, ctx->input);
    init_dragon_teleporter(state);
    init_surveillance_camera(state);
    if (!bind_doom_logic(ctx, state))
    {
        doom_state_cleanup(state);
        return false;
    }
    render_state_init(&state->render);
    sdl3d_transition_start(&state->transition, SDL3D_TRANSITION_FADE, SDL3D_TRANSITION_IN, (sdl3d_color){0, 0, 0, 255},
                           1.0f, -1);

    state->demo_player = sdl3d_demo_playback_load("attract.dem");
    if (state->demo_player != NULL)
    {
        sdl3d_demo_playback_start(ctx->input, state->demo_player);
    }
    return true;
}

static bool switch_demo_backend(sdl3d_game_context *ctx, doom_state *state)
{
    sdl3d_backend next =
        (state->current_backend == SDL3D_BACKEND_SDLGPU) ? SDL3D_BACKEND_SOFTWARE : SDL3D_BACKEND_SDLGPU;

    if (sdl3d_switch_backend(&ctx->window, &ctx->renderer, next))
    {
        state->current_backend = next;
        apply_window_defaults(ctx, state);
        return true;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backend switch failed: %s", SDL_GetError());
    if (!sdl3d_switch_backend(&ctx->window, &ctx->renderer, state->current_backend))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not restore previous backend: %s", SDL_GetError());
        return false;
    }

    apply_window_defaults(ctx, state);
    return true;
}

static bool game_event(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    doom_state *state = (doom_state *)userdata;

    if (state->demo_player != NULL)
    {
        switch (event->type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            stop_demo_playback(ctx, state);
            break;
        default:
            break;
        }
    }

    sdl3d_ui_process_event(state->ui, event);
    return true;
}

static void apply_doom_debug_actions(sdl3d_game_context *ctx, doom_state *state)
{
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_lighting))
    {
        state->level.use_lit = !state->level.use_lit;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_lightmaps))
    {
        state->level.use_lightmaps = !state->level.use_lightmaps;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_debug_stats))
    {
        state->render.show_debug = !state->render.show_debug;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_toggle_portal_culling))
    {
        state->render.portal_culling = !state->render.portal_culling;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_switch_backend) && !switch_demo_backend(ctx, state))
    {
        ctx->quit_requested = true;
    }
}

static void apply_doom_profile_actions(sdl3d_game_context *ctx, doom_state *state)
{
    for (int i = 0; i < DOOM_RENDER_PROFILE_COUNT; ++i)
    {
        if (!sdl3d_input_is_pressed(ctx->input, state->action_render_profile[i]))
        {
            continue;
        }

        doom_render_profile next = (doom_render_profile)i;
        if (backend_apply_profile(ctx->renderer, next))
        {
            state->render_profile = next;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Render profile: %s", backend_profile_name(next));
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render profile switch failed: %s", SDL_GetError());
        }
    }
}

static void update_dynamic_lift(doom_state *state, float dt)
{
    if (state == NULL || !state->level_ready)
    {
        return;
    }

    state->lift_timer += dt;
    while (state->lift_timer >= LIFT_CYCLE_SECONDS)
    {
        state->lift_timer -= LIFT_CYCLE_SECONDS;
    }

    const float phase = state->lift_timer / LIFT_CYCLE_SECONDS;
    const float wave = (SDL_sinf(phase * SDL_PI_F * 2.0f - SDL_PI_F * 0.5f) + 1.0f) * 0.5f;
    const float floor_y = LIFT_FLOOR_MIN_Y + (LIFT_FLOOR_MAX_Y - LIFT_FLOOR_MIN_Y) * wave;
    if (SDL_fabsf(floor_y - state->lift_last_floor_y) < LIFT_REBUILD_MIN_DELTA)
    {
        return;
    }

    sdl3d_sector_geometry geometry;
    SDL_zero(geometry);
    geometry.floor_y = floor_y;
    geometry.ceil_y = LIFT_CEIL_Y;
    geometry.floor_normal[1] = 1.0f;
    geometry.ceil_normal[1] = -1.0f;

    if (level_data_set_sector_geometry(&state->level, DOOM_DYNAMIC_LIFT_SECTOR, &geometry))
    {
        state->lift_last_floor_y = floor_y;
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dynamic lift update failed: %s", SDL_GetError());
    }
}

static float clamp01(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static void update_damage_feedback(doom_state *state, float damage_this_tick, float dt)
{
    float target_strength = 0.0f;
    if (damage_this_tick > 0.0f && state->player.last_damage_per_second > 0.0f)
    {
        target_strength = DAMAGE_FEEDBACK_MIN_STRENGTH +
                          clamp01(state->player.last_damage_per_second / DAMAGE_FEEDBACK_REFERENCE_DPS) *
                              (1.0f - DAMAGE_FEEDBACK_MIN_STRENGTH);
    }

    const float rate =
        target_strength > state->damage_feedback_strength ? DAMAGE_FEEDBACK_ATTACK_RATE : DAMAGE_FEEDBACK_DECAY_RATE;
    const float blend = clamp01(rate * dt);
    state->damage_feedback_strength += (target_strength - state->damage_feedback_strength) * blend;

    if (state->damage_feedback_strength > 0.001f)
    {
        state->damage_pulse_timer += dt * DAMAGE_FEEDBACK_PULSE_HZ;
        while (state->damage_pulse_timer >= 1.0f)
        {
            state->damage_pulse_timer -= 1.0f;
        }
    }
    else
    {
        state->damage_feedback_strength = 0.0f;
        state->damage_pulse_timer = 0.0f;
    }
}

static void game_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    doom_state *state = (doom_state *)userdata;

    apply_doom_profile_actions(ctx, state);

    if (sdl3d_input_is_pressed(ctx->input, state->action_pause))
    {
        toggle_pause(ctx, state);
        return;
    }

    apply_doom_debug_actions(ctx, state);

    if (state->demo_player != NULL && sdl3d_demo_playback_finished(state->demo_player))
    {
        stop_demo_playback(ctx, state);
    }

    sdl3d_transition_update(&state->transition, ctx->bus, dt);
    update_dynamic_lift(state, dt);
    doom_hazard_particles_update(&state->hazards, dt);
    entities_update(&state->ent, &state->level.unlit, dt, state->player.mover.position);
    if (!player_update(&state->player, ctx->input, &state->level.unlit, g_sectors, dt))
    {
        start_quit_fade(ctx, state);
    }
    else
    {
        const float damage_this_tick = player_apply_sector_damage(&state->player, g_sectors, g_sector_count, dt);
        update_damage_feedback(state, damage_this_tick, dt);
    }

    if (state->ambient_feedback_timer > 0.0f)
    {
        state->ambient_feedback_timer -= dt;
        if (state->ambient_feedback_timer < 0.0f)
        {
            state->ambient_feedback_timer = 0.0f;
        }
    }
    if (state->teleport_feedback_timer > 0.0f)
    {
        state->teleport_feedback_timer -= dt;
        if (state->teleport_feedback_timer < 0.0f)
        {
            state->teleport_feedback_timer = 0.0f;
        }
    }

    sdl3d_teleporter_update(&state->dragon_teleporter,
                            sdl3d_vec3_make(state->player.mover.position.x,
                                            state->player.mover.position.y - PLAYER_HEIGHT,
                                            state->player.mover.position.z),
                            dt, ctx->bus);

    doom_surveillance_update(&state->surveillance, state->logic,
                             sdl3d_vec3_make(state->player.mover.position.x,
                                             state->player.mover.position.y - PLAYER_HEIGHT,
                                             state->player.mover.position.z));

    sdl3d_sector_watcher_update(&state->sector_watcher, &state->level.unlit, g_sectors,
                                sdl3d_vec3_make(state->player.mover.position.x,
                                                state->player.mover.position.y - PLAYER_HEIGHT,
                                                state->player.mover.position.z),
                                ctx->bus);
}

static void game_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    doom_state *state = (doom_state *)userdata;

    apply_doom_profile_actions(ctx, state);

    if (sdl3d_input_is_pressed(ctx->input, state->action_pause))
    {
        toggle_pause(ctx, state);
        return;
    }
    if (sdl3d_input_is_pressed(ctx->input, state->action_menu))
    {
        start_quit_fade(ctx, state);
    }

    state->pause_flash_timer += real_dt * 2.0f;
    while (state->pause_flash_timer >= 1.0f)
    {
        state->pause_flash_timer -= 1.0f;
    }

    if (state->quit_pending)
    {
        sdl3d_transition_update(&state->transition, ctx->bus, real_dt);
    }
}

static void draw_damage_overlay(sdl3d_game_context *ctx, const doom_state *state)
{
    const int width = sdl3d_get_render_context_width(ctx->renderer);
    const int height = sdl3d_get_render_context_height(ctx->renderer);

    if (width <= 0 || height <= 0 || state->damage_feedback_strength <= 0.001f)
    {
        return;
    }

    const float pulse = (SDL_sinf(state->damage_pulse_timer * SDL_PI_F * 2.0f) + 1.0f) * 0.5f;
    const float alpha = (58.0f + pulse * 72.0f) * state->damage_feedback_strength;
    const float max_band = SDL_min((float)width, (float)height) * 0.12f;
    const int layers = 5;
    const float band = max_band / (float)layers;

    for (int i = 0; i < layers; ++i)
    {
        const float inset = (float)i * band;
        const float layer_scale = 1.0f - ((float)i / (float)layers);
        float layer_alpha_f = alpha * layer_scale;
        if (layer_alpha_f > 180.0f)
        {
            layer_alpha_f = 180.0f;
        }
        const Uint8 layer_alpha = (Uint8)layer_alpha_f;
        const sdl3d_color red = {220, 20, 20, layer_alpha};

        sdl3d_draw_rect_overlay(ctx->renderer, inset, inset, (float)width - inset * 2.0f, band, red);
        sdl3d_draw_rect_overlay(ctx->renderer, inset, (float)height - inset - band, (float)width - inset * 2.0f, band,
                                red);
        sdl3d_draw_rect_overlay(ctx->renderer, inset, inset + band, band, (float)height - (inset + band) * 2.0f, red);
        sdl3d_draw_rect_overlay(ctx->renderer, (float)width - inset - band, inset + band, band,
                                (float)height - (inset + band) * 2.0f, red);
    }
}

static void draw_pause_overlay(sdl3d_game_context *ctx, doom_state *state)
{
    const int width = sdl3d_get_render_context_width(ctx->renderer);
    const int height = sdl3d_get_render_context_height(ctx->renderer);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    sdl3d_draw_rect_overlay(ctx->renderer, 0.0f, 0.0f, (float)width, (float)height, (sdl3d_color){0, 0, 0, 128});

    if (state->pause_flash_timer >= 0.5f || !state->has_font)
    {
        return;
    }

    float text_width = 0.0f;
    float text_height = 0.0f;
    sdl3d_measure_text(&state->debug_font, "PAUSED", &text_width, &text_height);
    const float text_x = ((float)width - text_width) * 0.5f;
    const float text_y = ((float)height - text_height) * 0.5f;
    sdl3d_draw_text_overlay(ctx->renderer, &state->debug_font, "PAUSED", text_x, text_y,
                            (sdl3d_color){255, 255, 255, 255});
}

static void game_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    (void)alpha;
    doom_state *state = (doom_state *)userdata;
    const float frame_dt = sdl3d_time_get_unscaled_delta_time();

    render_draw_frame(&state->render, ctx->renderer, state->has_font ? &state->debug_font : NULL, state->ui,
                      &state->level, &state->ent, &state->hazards, &state->surveillance, &state->player, WINDOW_W,
                      WINDOW_H, frame_dt, backend_profile_name(state->render_profile),
                      state->ambient_feedback_timer > 0.0f, state->teleport_feedback_timer > 0.0f);
    sdl3d_transition_draw(&state->transition, ctx->renderer);
    draw_damage_overlay(ctx, state);
    if (ctx->paused && !state->quit_pending)
    {
        draw_pause_overlay(ctx, state);
    }
}

static void game_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    doom_state *state = (doom_state *)userdata;

    stop_demo_playback(ctx, state);
    doom_state_cleanup(state);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    doom_state state = {0};
    sdl3d_game_config config = {0};
    config.title = "SDL3D - Doom Level";
    config.width = WINDOW_W;
    config.height = WINDOW_H;
    config.backend = SDL3D_BACKEND_SDLGPU;
    config.tick_rate = 1.0f / 60.0f;
    config.enable_audio = true;

    sdl3d_game_callbacks callbacks = {0};
    callbacks.init = game_init;
    callbacks.event = game_event;
    callbacks.tick = game_tick;
    callbacks.pause_tick = game_pause_tick;
    callbacks.render = game_render;
    callbacks.shutdown = game_shutdown;

    return sdl3d_run_game(&config, &callbacks, &state);
}
