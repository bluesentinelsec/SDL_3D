#define SDL_MAIN_HANDLED

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/game.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/time.h"
#include "sdl3d/timer_pool.h"
}

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cmath>

namespace
{
constexpr int kTestWidth = 96;
constexpr int kTestHeight = 64;
constexpr float kFastFixedDt = 0.000001f;
constexpr int kTimerSignal = 17;

sdl3d_game_config test_config()
{
    sdl3d_game_config config{};
    config.title = "SDL3D Managed Game Loop Test";
    config.width = kTestWidth;
    config.height = kTestHeight;
    config.backend = SDL3D_BACKEND_SOFTWARE;
    config.tick_rate = kFastFixedDt;
    config.max_ticks_per_frame = 1;
    return config;
}

int run_test_game(const sdl3d_game_callbacks *callbacks, void *userdata)
{
    sdl3d_game_config config = test_config();
    return sdl3d_run_game(&config, callbacks, userdata);
}

int run_test_game_with_config(const sdl3d_game_config *config, const sdl3d_game_callbacks *callbacks, void *userdata)
{
    return sdl3d_run_game(config, callbacks, userdata);
}

struct GameTestState
{
    bool init_called = false;
    bool event_called = false;
    bool render_called = false;
    bool shutdown_called = false;
    bool timer_fired = false;
    bool saw_valid_context = false;
    bool saw_default_config = false;
    bool saw_time_update = false;
    bool saw_quit_event_in_callback = false;
    bool saw_input_action_in_tick = false;
    bool saw_input_action_while_paused = false;
    int input_action = -1;
    int tick_count = 0;
    int context_tick_count = 0;
    int input_pressed_tick_count = 0;
    int pause_tick_count = 0;
    int render_count = 0;
    float tick_dt = 0.0f;
    float pause_dt = 0.0f;
    float last_alpha = -1.0f;
    float first_unpaused_alpha = -1.0f;
};

bool quit_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->init_called = true;
    ctx->quit_requested = true;
    return true;
}

bool fail_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->init_called = true;
    ctx->quit_requested = true;
    return false;
}

void record_shutdown(sdl3d_game_context *ctx, void *userdata)
{
    (void)ctx;
    auto *state = static_cast<GameTestState *>(userdata);
    state->shutdown_called = true;
}

bool validate_context_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->saw_valid_context = ctx->window != nullptr && ctx->renderer != nullptr && ctx->registry != nullptr &&
                               ctx->bus != nullptr && ctx->timers != nullptr && ctx->input != nullptr;
    ctx->quit_requested = true;
    return true;
}

bool validate_default_config_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->saw_default_config =
        sdl3d_get_render_context_width(ctx->renderer) == 1280 && sdl3d_get_render_context_height(ctx->renderer) == 720;
    ctx->quit_requested = true;
    return true;
}

bool push_user_event_in_init(sdl3d_game_context *ctx, void *userdata)
{
    (void)ctx;
    auto *state = static_cast<GameTestState *>(userdata);
    state->init_called = true;

    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    SDL_PushEvent(&event);
    return true;
}

bool push_quit_event_in_init(sdl3d_game_context *ctx, void *userdata)
{
    (void)ctx;
    auto *state = static_cast<GameTestState *>(userdata);
    state->init_called = true;

    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
    return true;
}

bool event_returns_false(sdl3d_game_context *ctx, void *userdata, const SDL_Event *event)
{
    (void)ctx;
    auto *state = static_cast<GameTestState *>(userdata);
    state->saw_quit_event_in_callback = event->type == SDL_EVENT_QUIT;
    if (event->type == SDL_EVENT_USER)
    {
        state->event_called = true;
        return false;
    }
    return true;
}

void quit_in_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->render_called = true;
    state->render_count++;
    state->last_alpha = alpha;
    state->saw_time_update = sdl3d_time_get_real_time() >= 0.0f;
    ctx->quit_requested = true;
}

void quit_after_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->tick_count++;
    state->tick_dt = dt;
    ctx->quit_requested = true;
}

void timer_handler(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    (void)payload;
    auto *state = static_cast<GameTestState *>(userdata);
    if (signal_id == kTimerSignal)
    {
        state->timer_fired = true;
    }
}

bool start_timer_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    sdl3d_signal_connect(ctx->bus, kTimerSignal, timer_handler, state);
    sdl3d_timer_start(ctx->timers, kFastFixedDt, kTimerSignal, false, 0.0f);
    return true;
}

void quit_when_timer_fires(sdl3d_game_context *ctx, void *userdata, float dt)
{
    (void)dt;
    auto *state = static_cast<GameTestState *>(userdata);
    state->tick_count++;
    if (state->timer_fired || state->tick_count > 16)
    {
        ctx->quit_requested = true;
    }
}

bool bind_and_push_input_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->input_action = sdl3d_input_register_action(ctx->input, "jump");
    sdl3d_input_bind_key(ctx->input, state->input_action, SDL_SCANCODE_SPACE);

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;
    SDL_PushEvent(&event);
    return true;
}

bool bind_input_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->input_action = sdl3d_input_register_action(ctx->input, "jump");
    sdl3d_input_bind_key(ctx->input, state->input_action, SDL_SCANCODE_SPACE);
    return true;
}

bool pause_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->init_called = true;
    ctx->paused = true;
    return true;
}

bool pause_and_start_timer_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->init_called = true;
    ctx->paused = true;
    sdl3d_signal_connect(ctx->bus, kTimerSignal, timer_handler, state);
    sdl3d_timer_start(ctx->timers, kFastFixedDt, kTimerSignal, false, 0.0f);
    return true;
}

bool pause_and_push_input_in_init(sdl3d_game_context *ctx, void *userdata)
{
    bind_and_push_input_in_init(ctx, userdata);
    ctx->paused = true;
    return true;
}

bool pause_and_bind_pause_input_in_init(sdl3d_game_context *ctx, void *userdata)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->init_called = true;
    state->input_action = sdl3d_input_register_action(ctx->input, "pause");
    sdl3d_input_bind_key(ctx->input, state->input_action, SDL_SCANCODE_RETURN);
    ctx->paused = true;
    return true;
}

void quit_after_input_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    (void)dt;
    auto *state = static_cast<GameTestState *>(userdata);
    state->tick_count++;
    state->saw_input_action_in_tick =
        sdl3d_input_is_pressed(ctx->input, state->input_action) && sdl3d_input_is_held(ctx->input, state->input_action);
    ctx->quit_requested = true;
}

void quit_after_post_render_input_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    auto *state = static_cast<GameTestState *>(userdata);
    if (state->render_count == 0)
    {
        return;
    }
    quit_after_input_tick(ctx, userdata, dt);
}

void count_input_pressed_ticks(sdl3d_game_context *ctx, void *userdata, float dt)
{
    (void)dt;
    auto *state = static_cast<GameTestState *>(userdata);
    state->tick_count++;
    if (sdl3d_input_is_pressed(ctx->input, state->input_action))
    {
        state->input_pressed_tick_count++;
    }
    if (state->tick_count >= 3)
    {
        ctx->quit_requested = true;
    }
}

void stop_after_many_null_frames(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->render_count++;
    state->last_alpha = alpha;
    if (state->render_count >= 256)
    {
        ctx->quit_requested = true;
    }
}

void push_input_on_first_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    (void)alpha;
    auto *state = static_cast<GameTestState *>(userdata);
    state->render_count++;

    if (state->render_count == 1)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = SDL_SCANCODE_SPACE;
        SDL_PushEvent(&event);
    }
    if (state->render_count >= 4096)
    {
        ctx->quit_requested = true;
    }
}

void count_tick_without_quit(sdl3d_game_context *ctx, void *userdata, float dt)
{
    (void)ctx;
    auto *state = static_cast<GameTestState *>(userdata);
    state->tick_count++;
    state->tick_dt = dt;
}

void count_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    (void)ctx;
    auto *state = static_cast<GameTestState *>(userdata);
    state->pause_tick_count++;
    state->pause_dt = real_dt;
}

void unpause_after_one_pause_tick(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    count_pause_tick(ctx, userdata, real_dt);
    ctx->paused = false;
}

void push_pause_input_then_unpause_from_snapshot(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    count_pause_tick(ctx, userdata, real_dt);
    auto *state = static_cast<GameTestState *>(userdata);

    if (state->pause_tick_count == 1)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = SDL_SCANCODE_RETURN;
        SDL_PushEvent(&event);
        return;
    }

    if (sdl3d_input_is_pressed(ctx->input, state->input_action))
    {
        state->saw_input_action_in_tick = true;
        ctx->paused = false;
    }
}

void delay_then_unpause(sdl3d_game_context *ctx, void *userdata, float real_dt)
{
    count_pause_tick(ctx, userdata, real_dt);
    if (static_cast<GameTestState *>(userdata)->pause_tick_count >= 4)
    {
        ctx->paused = false;
        return;
    }

    SDL_Delay(20);
}

void quit_after_three_paused_renders(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->render_called = true;
    state->render_count++;
    state->last_alpha = alpha;
    state->context_tick_count = ctx->tick_count;
    if (state->render_count >= 3)
    {
        ctx->quit_requested = true;
    }
}

void quit_after_first_unpaused_tick_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->render_called = true;
    state->render_count++;
    state->last_alpha = alpha;
    state->context_tick_count = ctx->tick_count;
    if (!ctx->paused && ctx->tick_count > 0)
    {
        ctx->quit_requested = true;
    }
    if (state->render_count >= 128)
    {
        ctx->quit_requested = true;
    }
}

void quit_after_first_unpaused_render(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    auto *state = static_cast<GameTestState *>(userdata);
    state->render_called = true;
    state->render_count++;
    state->last_alpha = alpha;
    state->context_tick_count = ctx->tick_count;
    if (!ctx->paused)
    {
        state->first_unpaused_alpha = alpha;
        ctx->quit_requested = true;
    }
    if (state->render_count >= 128)
    {
        ctx->quit_requested = true;
    }
}

void record_paused_input_snapshot(sdl3d_game_context *ctx, void *userdata, float alpha)
{
    quit_after_three_paused_renders(ctx, userdata, alpha);
    auto *state = static_cast<GameTestState *>(userdata);
    state->saw_input_action_while_paused =
        sdl3d_input_is_pressed(ctx->input, state->input_action) || sdl3d_input_is_held(ctx->input, state->input_action);
}
} // namespace

TEST(ManagedGameLoop, InitCallbackIsCalled)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = quit_in_init;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.init_called);
}

TEST(ManagedGameLoop, InitFailureReturnsOneAndSkipsShutdown)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = fail_in_init;
    callbacks.shutdown = record_shutdown;

    EXPECT_EQ(1, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.init_called);
    EXPECT_FALSE(state.shutdown_called);
}

TEST(ManagedGameLoop, ShutdownCallbackIsCalledAfterQuit)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = quit_in_init;
    callbacks.shutdown = record_shutdown;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.shutdown_called);
}

TEST(ManagedGameLoop, ContextHasValidSubsystems)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = validate_context_in_init;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.saw_valid_context);
}

TEST(ManagedGameLoop, EventCallbackReceivesUserEvents)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = push_user_event_in_init;
    callbacks.event = event_returns_false;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.event_called);
    EXPECT_FALSE(state.saw_quit_event_in_callback);
}

TEST(ManagedGameLoop, SDLQuitEventIsHandledInternally)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = push_quit_event_in_init;
    callbacks.event = event_returns_false;
    callbacks.shutdown = record_shutdown;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_FALSE(state.saw_quit_event_in_callback);
    EXPECT_TRUE(state.shutdown_called);
}

TEST(ManagedGameLoop, TickCallbackReceivesFixedDt)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.tick = quit_after_tick;
    callbacks.render = stop_after_many_null_frames;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_GE(state.tick_count, 1);
    EXPECT_NEAR(kFastFixedDt, state.tick_dt, kFastFixedDt * 0.01f);
}

TEST(ManagedGameLoop, RenderCallbackIsCalledWithNormalizedAlpha)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.render = quit_in_render;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.render_called);
    EXPECT_GE(state.last_alpha, 0.0f);
    EXPECT_LE(state.last_alpha, 1.0f);
    EXPECT_TRUE(state.saw_time_update);
}

TEST(ManagedGameLoop, TimerPoolIsTickedAutomatically)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = start_timer_in_init;
    callbacks.tick = quit_when_timer_fires;
    callbacks.render = stop_after_many_null_frames;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.timer_fired);
}

TEST(ManagedGameLoop, InputManagerProcessesEventsBeforeTick)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = bind_and_push_input_in_init;
    callbacks.tick = quit_after_input_tick;
    callbacks.render = stop_after_many_null_frames;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_GE(state.tick_count, 1);
    EXPECT_TRUE(state.saw_input_action_in_tick);
}

TEST(ManagedGameLoop, InputPressedEdgeIsNotRepeatedAcrossCatchUpTicks)
{
    GameTestState state;
    sdl3d_game_config config = test_config();
    config.max_ticks_per_frame = 4;

    sdl3d_game_callbacks callbacks{};
    callbacks.init = bind_and_push_input_in_init;
    callbacks.tick = count_input_pressed_ticks;
    callbacks.render = stop_after_many_null_frames;

    EXPECT_EQ(0, run_test_game_with_config(&config, &callbacks, &state));
    EXPECT_GE(state.tick_count, 3);
    EXPECT_EQ(state.input_pressed_tick_count, 1);
}

TEST(ManagedGameLoop, InputPressedEdgeWaitsForNextFixedTick)
{
    GameTestState state;
    sdl3d_game_config config = test_config();
    config.tick_rate = 0.05f;
    config.max_ticks_per_frame = 1;

    sdl3d_game_callbacks callbacks{};
    callbacks.init = bind_input_in_init;
    callbacks.tick = quit_after_post_render_input_tick;
    callbacks.render = push_input_on_first_render;

    EXPECT_EQ(0, run_test_game_with_config(&config, &callbacks, &state));
    EXPECT_GE(state.render_count, 1);
    EXPECT_EQ(state.tick_count, 1);
    EXPECT_TRUE(state.saw_input_action_in_tick);
}

TEST(ManagedGameLoop, QuitRequestedFromTickExitsAndRunsShutdown)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.tick = quit_after_tick;
    callbacks.shutdown = record_shutdown;
    callbacks.render = stop_after_many_null_frames;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_GE(state.tick_count, 1);
    EXPECT_TRUE(state.shutdown_called);
}

TEST(ManagedGameLoop, NullCallbacksCanExitFromQueuedQuitEvent)
{
    SDL_SetMainReady();
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    ASSERT_TRUE(SDL_PushEvent(&event)) << SDL_GetError();

    sdl3d_game_config config = test_config();
    EXPECT_EQ(0, sdl3d_run_game(&config, nullptr, nullptr));
}

TEST(ManagedGameLoop, DefaultConfigValues)
{
    GameTestState state;
    sdl3d_game_config config{};
    sdl3d_game_callbacks callbacks{};
    callbacks.init = validate_default_config_in_init;

    EXPECT_EQ(0, sdl3d_run_game(&config, &callbacks, &state));
    EXPECT_TRUE(state.saw_default_config);
}

TEST(ManagedGameLoop, PauseStopsTickingButStillRenders)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_in_init;
    callbacks.tick = count_tick_without_quit;
    callbacks.render = quit_after_three_paused_renders;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.init_called);
    EXPECT_TRUE(state.render_called);
    EXPECT_GE(state.render_count, 3);
    EXPECT_EQ(0, state.tick_count);
    EXPECT_EQ(0, state.context_tick_count);
    EXPECT_FLOAT_EQ(0.0f, state.last_alpha);
}

TEST(ManagedGameLoop, PauseTickRunsWithWallClockDelta)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_in_init;
    callbacks.pause_tick = count_pause_tick;
    callbacks.render = quit_after_three_paused_renders;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_GE(state.pause_tick_count, 1);
    EXPECT_GE(state.pause_dt, 0.0f);
}

TEST(ManagedGameLoop, UnpauseResumesFixedTicks)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_in_init;
    callbacks.pause_tick = unpause_after_one_pause_tick;
    callbacks.tick = count_tick_without_quit;
    callbacks.render = quit_after_first_unpaused_tick_render;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_GE(state.pause_tick_count, 1);
    EXPECT_GE(state.tick_count, 1);
    EXPECT_GE(state.context_tick_count, 1);
}

TEST(ManagedGameLoop, AccumulatorDoesNotCatchUpAfterPause)
{
    GameTestState state;
    sdl3d_game_config config = test_config();
    config.tick_rate = kFastFixedDt;
    config.max_ticks_per_frame = 8;

    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_in_init;
    callbacks.pause_tick = delay_then_unpause;
    callbacks.tick = count_tick_without_quit;
    callbacks.render = quit_after_first_unpaused_render;

    EXPECT_EQ(0, run_test_game_with_config(&config, &callbacks, &state));
    EXPECT_GE(state.pause_tick_count, 4);
    EXPECT_TRUE(state.render_called);
    EXPECT_EQ(state.tick_count, 0);
    EXPECT_EQ(state.context_tick_count, 0);
    EXPECT_FLOAT_EQ(state.first_unpaused_alpha, 0.0f);
}

TEST(ManagedGameLoop, TimersDoNotAdvanceWhilePaused)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_and_start_timer_in_init;
    callbacks.render = quit_after_three_paused_renders;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_FALSE(state.timer_fired);
}

TEST(ManagedGameLoop, InputSnapshotsUpdateWhilePaused)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_and_push_input_in_init;
    callbacks.render = record_paused_input_snapshot;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_TRUE(state.saw_input_action_while_paused);
}

TEST(ManagedGameLoop, PauseTickCanUnpauseFromActionSnapshot)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_and_bind_pause_input_in_init;
    callbacks.pause_tick = push_pause_input_then_unpause_from_snapshot;
    callbacks.render = quit_after_first_unpaused_render;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_GE(state.pause_tick_count, 2);
    EXPECT_TRUE(state.saw_input_action_in_tick);
    EXPECT_GE(state.last_alpha, 0.0f);
}

TEST(ManagedGameLoop, NullPauseTickIsSafe)
{
    GameTestState state;
    sdl3d_game_callbacks callbacks{};
    callbacks.init = pause_in_init;
    callbacks.render = quit_after_three_paused_renders;

    EXPECT_EQ(0, run_test_game(&callbacks, &state));
    EXPECT_GE(state.render_count, 3);
    EXPECT_EQ(0, state.pause_tick_count);
}
