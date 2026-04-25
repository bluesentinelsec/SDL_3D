/**
 * @file test_timer_pool.cpp
 * @brief Unit tests for sdl3d_timer_pool — deferred and repeating signal emission.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/action.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"
}

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static int g_fire_count;
static int g_last_signal;

static void reset_globals()
{
    g_fire_count = 0;
    g_last_signal = -1;
}

static void fire_counter(void *ud, int signal_id, const sdl3d_properties *payload)
{
    (void)ud;
    (void)payload;
    g_fire_count++;
    g_last_signal = signal_id;
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

TEST(TimerPool, CreateAndDestroy)
{
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(sdl3d_timer_pool_active_count(pool), 0);
    sdl3d_timer_pool_destroy(pool);
}

TEST(TimerPool, DestroyNullIsSafe)
{
    sdl3d_timer_pool_destroy(nullptr);
}

/* ================================================================== */
/* One-shot timer                                                     */
/* ================================================================== */

TEST(TimerPool, OneShotFiresAfterDelay)
{
    reset_globals();
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, fire_counter, NULL);

    int id = sdl3d_timer_start(pool, 1.0f, 1, false, 0);
    EXPECT_GT(id, 0);
    EXPECT_EQ(sdl3d_timer_pool_active_count(pool), 1);

    /* Not yet expired. */
    sdl3d_timer_pool_update(pool, bus, 0.5f);
    EXPECT_EQ(g_fire_count, 0);
    EXPECT_EQ(sdl3d_timer_pool_active_count(pool), 1);

    /* Expires. */
    sdl3d_timer_pool_update(pool, bus, 0.6f);
    EXPECT_EQ(g_fire_count, 1);
    EXPECT_EQ(g_last_signal, 1);
    EXPECT_EQ(sdl3d_timer_pool_active_count(pool), 0);

    /* Does not fire again. */
    sdl3d_timer_pool_update(pool, bus, 1.0f);
    EXPECT_EQ(g_fire_count, 1);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}

TEST(TimerPool, OneShotFiresExactlyAtZero)
{
    reset_globals();
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, fire_counter, NULL);

    sdl3d_timer_start(pool, 1.0f, 1, false, 0);
    sdl3d_timer_pool_update(pool, bus, 1.0f);
    EXPECT_EQ(g_fire_count, 1);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}

/* ================================================================== */
/* Repeating timer                                                    */
/* ================================================================== */

TEST(TimerPool, RepeatingFiresMultipleTimes)
{
    reset_globals();
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 2, fire_counter, NULL);

    sdl3d_timer_start(pool, 0.5f, 2, true, 0.5f);

    /* First firing at 0.5s. */
    sdl3d_timer_pool_update(pool, bus, 0.5f);
    EXPECT_EQ(g_fire_count, 1);
    EXPECT_EQ(sdl3d_timer_pool_active_count(pool), 1);

    /* Second firing at 1.0s. */
    sdl3d_timer_pool_update(pool, bus, 0.5f);
    EXPECT_EQ(g_fire_count, 2);

    /* Third firing at 1.5s. */
    sdl3d_timer_pool_update(pool, bus, 0.5f);
    EXPECT_EQ(g_fire_count, 3);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}

TEST(TimerPool, RepeatingAtMostOncePerUpdate)
{
    reset_globals();
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, fire_counter, NULL);

    /* Interval 0.1s, but we pass dt=10s. Should fire once, not 100 times. */
    sdl3d_timer_start(pool, 0.1f, 1, true, 0.1f);
    sdl3d_timer_pool_update(pool, bus, 10.0f);
    EXPECT_EQ(g_fire_count, 1);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}

/* ================================================================== */
/* Cancel                                                             */
/* ================================================================== */

TEST(TimerPool, CancelPreventsFireing)
{
    reset_globals();
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, fire_counter, NULL);

    int id = sdl3d_timer_start(pool, 1.0f, 1, false, 0);
    sdl3d_timer_cancel(pool, id);
    EXPECT_EQ(sdl3d_timer_pool_active_count(pool), 0);

    sdl3d_timer_pool_update(pool, bus, 2.0f);
    EXPECT_EQ(g_fire_count, 0);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}

TEST(TimerPool, CancelInvalidIdIsNoOp)
{
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_timer_cancel(pool, 999);
    sdl3d_timer_cancel(pool, 0);
    sdl3d_timer_cancel(pool, -1);
    sdl3d_timer_pool_destroy(pool);
}

TEST(TimerPool, CancelNullPoolIsNoOp)
{
    sdl3d_timer_cancel(nullptr, 1);
}

/* ================================================================== */
/* Multiple timers                                                    */
/* ================================================================== */

TEST(TimerPool, MultipleTimersFireIndependently)
{
    int count_a = 0, count_b = 0;
    auto handler_a = [](void *ud, int sig, const sdl3d_properties *p) {
        (void)sig;
        (void)p;
        (*(int *)ud)++;
    };

    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, handler_a, &count_a);
    sdl3d_signal_connect(bus, 2, handler_a, &count_b);

    sdl3d_timer_start(pool, 0.5f, 1, false, 0);
    sdl3d_timer_start(pool, 1.5f, 2, false, 0);

    sdl3d_timer_pool_update(pool, bus, 0.6f);
    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 0);

    sdl3d_timer_pool_update(pool, bus, 1.0f);
    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}

/* ================================================================== */
/* Validation                                                         */
/* ================================================================== */

TEST(TimerPool, StartWithZeroDelayReturnsZero)
{
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    EXPECT_EQ(sdl3d_timer_start(pool, 0.0f, 1, false, 0), 0);
    EXPECT_EQ(sdl3d_timer_start(pool, -1.0f, 1, false, 0), 0);
    sdl3d_timer_pool_destroy(pool);
}

TEST(TimerPool, RepeatingWithZeroIntervalReturnsZero)
{
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    EXPECT_EQ(sdl3d_timer_start(pool, 1.0f, 1, true, 0.0f), 0);
    EXPECT_EQ(sdl3d_timer_start(pool, 1.0f, 1, true, -1.0f), 0);
    sdl3d_timer_pool_destroy(pool);
}

TEST(TimerPool, StartNullPoolReturnsZero)
{
    EXPECT_EQ(sdl3d_timer_start(nullptr, 1.0f, 1, false, 0), 0);
}

/* ================================================================== */
/* NULL safety                                                        */
/* ================================================================== */

TEST(TimerPool, UpdateNullArgsAreSafe)
{
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_timer_pool_update(nullptr, bus, 1.0f);
    sdl3d_timer_pool_update(pool, nullptr, 1.0f);
    EXPECT_EQ(sdl3d_timer_pool_active_count(nullptr), 0);
    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}

/* ================================================================== */
/* Timer IDs are monotonic                                            */
/* ================================================================== */

TEST(TimerPool, IdsAreMonotonic)
{
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    int a = sdl3d_timer_start(pool, 1.0f, 1, false, 0);
    int b = sdl3d_timer_start(pool, 1.0f, 2, false, 0);
    int c = sdl3d_timer_start(pool, 1.0f, 3, false, 0);
    EXPECT_GT(a, 0);
    EXPECT_GT(b, a);
    EXPECT_GT(c, b);
    sdl3d_timer_pool_destroy(pool);
}

/* ================================================================== */
/* Integration: action START_TIMER → timer → signal                   */
/* ================================================================== */

TEST(TimerPool, ActionStartTimerIntegration)
{
    reset_globals();
    sdl3d_timer_pool *pool = sdl3d_timer_pool_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 99, fire_counter, NULL);

    /* Execute a START_TIMER action. */
    sdl3d_action a{};
    a.type = SDL3D_ACTION_START_TIMER;
    a.start_timer.delay = 2.0f;
    a.start_timer.signal_id = 99;
    a.start_timer.repeating = false;
    a.start_timer.interval = 0;

    sdl3d_action_execute(&a, bus, pool);
    EXPECT_EQ(sdl3d_timer_pool_active_count(pool), 1);

    /* Not yet. */
    sdl3d_timer_pool_update(pool, bus, 1.0f);
    EXPECT_EQ(g_fire_count, 0);

    /* Fires. */
    sdl3d_timer_pool_update(pool, bus, 1.5f);
    EXPECT_EQ(g_fire_count, 1);
    EXPECT_EQ(g_last_signal, 99);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_timer_pool_destroy(pool);
}
