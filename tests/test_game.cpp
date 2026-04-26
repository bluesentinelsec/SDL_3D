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
    int input_action = -1;
    int tick_count = 0;
    int render_count = 0;
    float tick_dt = 0.0f;
    float last_alpha = -1.0f;
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

void quit_after_input_tick(sdl3d_game_context *ctx, void *userdata, float dt)
{
    (void)dt;
    auto *state = static_cast<GameTestState *>(userdata);
    state->tick_count++;
    state->saw_input_action_in_tick =
        sdl3d_input_is_pressed(ctx->input, state->input_action) && sdl3d_input_is_held(ctx->input, state->input_action);
    ctx->quit_requested = true;
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
