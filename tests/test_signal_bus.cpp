/**
 * @file test_signal_bus.cpp
 * @brief Unit tests for sdl3d_signal_bus — decoupled event dispatch.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
}

/* ================================================================== */
/* Test helpers                                                       */
/* ================================================================== */

static int g_call_count;
static int g_last_signal;
static const sdl3d_properties *g_last_payload;

static void reset_globals()
{
    g_call_count = 0;
    g_last_signal = -1;
    g_last_payload = NULL;
}

static void counting_handler(void *ud, int signal_id, const sdl3d_properties *payload)
{
    (void)ud;
    g_call_count++;
    g_last_signal = signal_id;
    g_last_payload = payload;
}

static void increment_handler(void *ud, int signal_id, const sdl3d_properties *payload)
{
    (void)signal_id;
    (void)payload;
    int *counter = (int *)ud;
    (*counter)++;
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

TEST(SignalBus, CreateAndDestroy)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 0);
    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, DestroyNullIsSafe)
{
    sdl3d_signal_bus_destroy(nullptr);
}

/* ================================================================== */
/* Connect and emit                                                   */
/* ================================================================== */

TEST(SignalBus, ConnectReturnsPositiveId)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    int id = sdl3d_signal_connect(bus, 1, counting_handler, NULL);
    EXPECT_GT(id, 0);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 1);
    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, ConnectNullBusReturnsZero)
{
    EXPECT_EQ(sdl3d_signal_connect(nullptr, 1, counting_handler, NULL), 0);
}

TEST(SignalBus, ConnectNullHandlerReturnsZero)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    EXPECT_EQ(sdl3d_signal_connect(bus, 1, NULL, NULL), 0);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 0);
    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, EmitFiresHandler)
{
    reset_globals();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 42, counting_handler, NULL);

    sdl3d_signal_emit(bus, 42, NULL);
    EXPECT_EQ(g_call_count, 1);
    EXPECT_EQ(g_last_signal, 42);
    EXPECT_EQ(g_last_payload, nullptr);

    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, EmitOnlyMatchingSignal)
{
    reset_globals();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, counting_handler, NULL);

    sdl3d_signal_emit(bus, 2, NULL);
    EXPECT_EQ(g_call_count, 0);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(g_call_count, 1);

    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, MultipleHandlersSameSignal)
{
    int a = 0, b = 0;
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, increment_handler, &a);
    sdl3d_signal_connect(bus, 1, increment_handler, &b);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);

    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, SameHandlerConnectedTwiceFiresTwice)
{
    int counter = 0;
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, increment_handler, &counter);
    sdl3d_signal_connect(bus, 1, increment_handler, &counter);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(counter, 2);

    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, EmitPassesPayload)
{
    reset_globals();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, counting_handler, NULL);

    sdl3d_properties *payload = sdl3d_properties_create();
    sdl3d_properties_set_int(payload, "damage", 50);
    sdl3d_signal_emit(bus, 1, payload);

    EXPECT_EQ(g_call_count, 1);
    ASSERT_NE(g_last_payload, nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(g_last_payload, "damage", 0), 50);

    sdl3d_properties_destroy(payload);
    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, EmitPassesUserdata)
{
    int counter = 0;
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, increment_handler, &counter);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(counter, 1);

    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Disconnect                                                         */
/* ================================================================== */

TEST(SignalBus, DisconnectStopsHandler)
{
    reset_globals();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    int conn = sdl3d_signal_connect(bus, 1, counting_handler, NULL);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(g_call_count, 1);

    sdl3d_signal_disconnect(bus, conn);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 0);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(g_call_count, 1); /* No additional call. */

    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, DisconnectInvalidIdIsNoOp)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, counting_handler, NULL);
    sdl3d_signal_disconnect(bus, 9999);
    sdl3d_signal_disconnect(bus, 0);
    sdl3d_signal_disconnect(bus, -1);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 1);
    sdl3d_signal_bus_destroy(bus);
}

TEST(SignalBus, DisconnectAllBySignal)
{
    int a = 0, b = 0, c = 0;
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, increment_handler, &a);
    sdl3d_signal_connect(bus, 1, increment_handler, &b);
    sdl3d_signal_connect(bus, 2, increment_handler, &c);

    sdl3d_signal_disconnect_all(bus, 1);
    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 1);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 0);

    sdl3d_signal_emit(bus, 2, NULL);
    EXPECT_EQ(c, 1);

    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Disconnect during emission (reentrant safety)                      */
/* ================================================================== */

struct disconnect_ctx
{
    sdl3d_signal_bus *bus;
    int conn_id;
    int call_count;
};

static void self_disconnect_handler(void *ud, int signal_id, const sdl3d_properties *payload)
{
    (void)signal_id;
    (void)payload;
    struct disconnect_ctx *ctx = (struct disconnect_ctx *)ud;
    ctx->call_count++;
    sdl3d_signal_disconnect(ctx->bus, ctx->conn_id);
}

TEST(SignalBus, DisconnectSelfDuringEmission)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    struct disconnect_ctx ctx = {bus, 0, 0};
    ctx.conn_id = sdl3d_signal_connect(bus, 1, self_disconnect_handler, &ctx);

    int other_count = 0;
    sdl3d_signal_connect(bus, 1, increment_handler, &other_count);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(ctx.call_count, 1);
    EXPECT_EQ(other_count, 1);

    /* Second emit: self-disconnected handler should not fire. */
    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(ctx.call_count, 1);
    EXPECT_EQ(other_count, 2);

    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Reentrant emission (handler emits another signal)                  */
/* ================================================================== */

struct reentrant_ctx
{
    sdl3d_signal_bus *bus;
    int secondary_signal;
    int primary_count;
};

static void reentrant_handler(void *ud, int signal_id, const sdl3d_properties *payload)
{
    (void)signal_id;
    (void)payload;
    struct reentrant_ctx *ctx = (struct reentrant_ctx *)ud;
    ctx->primary_count++;
    sdl3d_signal_emit(ctx->bus, ctx->secondary_signal, NULL);
}

TEST(SignalBus, ReentrantEmission)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    int secondary_count = 0;

    struct reentrant_ctx ctx = {bus, 2, 0};
    sdl3d_signal_connect(bus, 1, reentrant_handler, &ctx);
    sdl3d_signal_connect(bus, 2, increment_handler, &secondary_count);

    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(ctx.primary_count, 1);
    EXPECT_EQ(secondary_count, 1);

    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Connect during emission                                            */
/* ================================================================== */

struct connect_ctx
{
    sdl3d_signal_bus *bus;
    int *counter;
};

static void connect_during_emit_handler(void *ud, int signal_id, const sdl3d_properties *payload)
{
    (void)signal_id;
    (void)payload;
    struct connect_ctx *ctx = (struct connect_ctx *)ud;
    /* Connect a new handler during emission. It should NOT fire in
     * this emission pass (snapshot semantics). */
    sdl3d_signal_connect(ctx->bus, signal_id, increment_handler, ctx->counter);
}

TEST(SignalBus, ConnectDuringEmissionDoesNotFireInCurrentPass)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    int counter = 0;
    struct connect_ctx ctx = {bus, &counter};
    sdl3d_signal_connect(bus, 1, connect_during_emit_handler, &ctx);

    sdl3d_signal_emit(bus, 1, NULL);
    /* The newly connected handler should NOT have fired. */
    EXPECT_EQ(counter, 0);

    /* But it fires on the next emission. */
    sdl3d_signal_emit(bus, 1, NULL);
    EXPECT_EQ(counter, 1);

    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Stress: many connections                                           */
/* ================================================================== */

TEST(SignalBus, ManyConnections)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    int counters[100] = {0};

    for (int i = 0; i < 100; i++)
        sdl3d_signal_connect(bus, 1, increment_handler, &counters[i]);

    EXPECT_EQ(sdl3d_signal_bus_connection_count(bus), 100);

    sdl3d_signal_emit(bus, 1, NULL);
    for (int i = 0; i < 100; i++)
        EXPECT_EQ(counters[i], 1) << "counter[" << i << "]";

    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* NULL safety                                                        */
/* ================================================================== */

TEST(SignalBus, NullBusOperationsAreSafe)
{
    EXPECT_EQ(sdl3d_signal_bus_connection_count(nullptr), 0);
    sdl3d_signal_emit(nullptr, 1, NULL);
    sdl3d_signal_disconnect(nullptr, 1);
    sdl3d_signal_disconnect_all(nullptr, 1);
}

/* ================================================================== */
/* Emit with no handlers is a no-op                                   */
/* ================================================================== */

TEST(SignalBus, EmitWithNoHandlersIsNoOp)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_emit(bus, 999, NULL); /* Should not crash. */
    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Connection IDs are unique and monotonic                            */
/* ================================================================== */

TEST(SignalBus, ConnectionIdsAreMonotonic)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    int id1 = sdl3d_signal_connect(bus, 1, counting_handler, NULL);
    int id2 = sdl3d_signal_connect(bus, 1, counting_handler, NULL);
    int id3 = sdl3d_signal_connect(bus, 2, counting_handler, NULL);
    EXPECT_GT(id1, 0);
    EXPECT_GT(id2, id1);
    EXPECT_GT(id3, id2);
    sdl3d_signal_bus_destroy(bus);
}
