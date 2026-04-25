/**
 * @file test_actor_registry.cpp
 * @brief Unit tests for sdl3d_actor_registry — unified game object table.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/actor_registry.h"
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/trigger.h"
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

TEST(ActorRegistry, CreateAndDestroy)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(sdl3d_actor_registry_count(reg), 0);
    sdl3d_actor_registry_destroy(reg);
}

TEST(ActorRegistry, DestroyNullIsSafe)
{
    sdl3d_actor_registry_destroy(nullptr);
}

/* ================================================================== */
/* Add                                                                */
/* ================================================================== */

TEST(ActorRegistry, AddReturnsValidActor)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_registered_actor *a = sdl3d_actor_registry_add(reg, "door_1");

    ASSERT_NE(a, nullptr);
    EXPECT_GT(a->id, 0);
    EXPECT_STREQ(a->name, "door_1");
    EXPECT_NE(a->props, nullptr);
    EXPECT_TRUE(a->active);
    EXPECT_EQ(a->sector_id, -1);
    EXPECT_EQ(a->trigger_count, 0);
    EXPECT_EQ(sdl3d_actor_registry_count(reg), 1);

    sdl3d_actor_registry_destroy(reg);
}

TEST(ActorRegistry, AddNullArgsReturnNull)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    EXPECT_EQ(sdl3d_actor_registry_add(nullptr, "x"), nullptr);
    EXPECT_EQ(sdl3d_actor_registry_add(reg, nullptr), nullptr);
    sdl3d_actor_registry_destroy(reg);
}

TEST(ActorRegistry, AddMultipleActors)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_registered_actor *a = sdl3d_actor_registry_add(reg, "a");
    sdl3d_registered_actor *b = sdl3d_actor_registry_add(reg, "b");
    sdl3d_registered_actor *c = sdl3d_actor_registry_add(reg, "c");

    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);
    EXPECT_NE(c, nullptr);
    EXPECT_NE(a->id, b->id);
    EXPECT_NE(b->id, c->id);
    EXPECT_EQ(sdl3d_actor_registry_count(reg), 3);

    sdl3d_actor_registry_destroy(reg);
}

TEST(ActorRegistry, NameIsCopied)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    char buf[32];
    SDL_strlcpy(buf, "sensor", sizeof(buf));
    sdl3d_registered_actor *a = sdl3d_actor_registry_add(reg, buf);
    buf[0] = 'X';
    EXPECT_STREQ(a->name, "sensor");
    sdl3d_actor_registry_destroy(reg);
}

/* ================================================================== */
/* Properties are per-actor                                           */
/* ================================================================== */

TEST(ActorRegistry, EachActorHasOwnProperties)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_registered_actor *a = sdl3d_actor_registry_add(reg, "a");
    sdl3d_registered_actor *b = sdl3d_actor_registry_add(reg, "b");

    sdl3d_properties_set_int(a->props, "health", 100);
    sdl3d_properties_set_int(b->props, "health", 50);

    EXPECT_EQ(sdl3d_properties_get_int(a->props, "health", 0), 100);
    EXPECT_EQ(sdl3d_properties_get_int(b->props, "health", 0), 50);

    sdl3d_actor_registry_destroy(reg);
}

/* ================================================================== */
/* Find and get                                                       */
/* ================================================================== */

TEST(ActorRegistry, FindByName)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_actor_registry_add(reg, "alpha");
    sdl3d_registered_actor *b = sdl3d_actor_registry_add(reg, "beta");
    sdl3d_actor_registry_add(reg, "gamma");

    sdl3d_registered_actor *found = sdl3d_actor_registry_find(reg, "beta");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, b->id);

    EXPECT_EQ(sdl3d_actor_registry_find(reg, "missing"), nullptr);
    EXPECT_EQ(sdl3d_actor_registry_find(reg, nullptr), nullptr);
    EXPECT_EQ(sdl3d_actor_registry_find(nullptr, "x"), nullptr);

    sdl3d_actor_registry_destroy(reg);
}

TEST(ActorRegistry, GetById)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_registered_actor *a = sdl3d_actor_registry_add(reg, "test");

    sdl3d_registered_actor *got = sdl3d_actor_registry_get(reg, a->id);
    ASSERT_NE(got, nullptr);
    EXPECT_STREQ(got->name, "test");

    EXPECT_EQ(sdl3d_actor_registry_get(reg, 9999), nullptr);
    EXPECT_EQ(sdl3d_actor_registry_get(reg, 0), nullptr);
    EXPECT_EQ(sdl3d_actor_registry_get(nullptr, 1), nullptr);

    sdl3d_actor_registry_destroy(reg);
}

/* ================================================================== */
/* Remove                                                             */
/* ================================================================== */

TEST(ActorRegistry, RemoveDeletesActor)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_registered_actor *a = sdl3d_actor_registry_add(reg, "a");
    sdl3d_actor_registry_add(reg, "b");
    int id = a->id;

    sdl3d_actor_registry_remove(reg, id);
    EXPECT_EQ(sdl3d_actor_registry_count(reg), 1);
    EXPECT_EQ(sdl3d_actor_registry_get(reg, id), nullptr);
    EXPECT_NE(sdl3d_actor_registry_find(reg, "b"), nullptr);

    sdl3d_actor_registry_destroy(reg);
}

TEST(ActorRegistry, RemoveInvalidIdIsNoOp)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_actor_registry_add(reg, "a");
    sdl3d_actor_registry_remove(reg, 9999);
    sdl3d_actor_registry_remove(reg, 0);
    sdl3d_actor_registry_remove(nullptr, 1);
    EXPECT_EQ(sdl3d_actor_registry_count(reg), 1);
    sdl3d_actor_registry_destroy(reg);
}

/* ================================================================== */
/* Update: spatial trigger evaluation                                 */
/* ================================================================== */

TEST(ActorRegistry, UpdateEvaluatesSpatialTriggers)
{
    reset_globals();
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, fire_counter, NULL);

    sdl3d_registered_actor *sensor = sdl3d_actor_registry_add(reg, "sensor");
    sensor->triggers[0].type = SDL3D_TRIGGER_SPATIAL;
    sensor->triggers[0].edge = SDL3D_TRIGGER_EDGE_ENTER;
    sensor->triggers[0].emit_signal_id = 1;
    sensor->triggers[0].enabled = true;
    sensor->triggers[0].spatial.zone = (sdl3d_bounding_box){{0, 0, 0}, {10, 10, 10}};
    sensor->trigger_count = 1;

    /* Outside → no fire. */
    sdl3d_actor_registry_update(reg, bus, sdl3d_vec3_make(-5, 5, 5));
    EXPECT_EQ(g_fire_count, 0);

    /* Enter → fire. */
    sdl3d_actor_registry_update(reg, bus, sdl3d_vec3_make(5, 5, 5));
    EXPECT_EQ(g_fire_count, 1);

    /* Stay inside → no re-fire (edge enter). */
    sdl3d_actor_registry_update(reg, bus, sdl3d_vec3_make(6, 5, 5));
    EXPECT_EQ(g_fire_count, 1);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_actor_registry_destroy(reg);
}

/* ================================================================== */
/* Update: property trigger evaluation                                */
/* ================================================================== */

TEST(ActorRegistry, UpdateEvaluatesPropertyTriggers)
{
    reset_globals();
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 10, fire_counter, NULL);

    sdl3d_registered_actor *player = sdl3d_actor_registry_add(reg, "player");
    sdl3d_properties_set_float(player->props, "health", 100.0f);

    player->triggers[0].type = SDL3D_TRIGGER_PROPERTY;
    player->triggers[0].edge = SDL3D_TRIGGER_EDGE_ENTER;
    player->triggers[0].emit_signal_id = 10;
    player->triggers[0].enabled = true;
    player->triggers[0].property.source = player->props;
    player->triggers[0].property.key = "health";
    player->triggers[0].property.op = SDL3D_CMP_LE;
    player->triggers[0].property.threshold = 0.0f;
    player->trigger_count = 1;

    /* health=100 → no fire. */
    sdl3d_actor_registry_update(reg, bus, sdl3d_vec3_make(0, 0, 0));
    EXPECT_EQ(g_fire_count, 0);

    /* health=0 → fire. */
    sdl3d_properties_set_float(player->props, "health", 0.0f);
    sdl3d_actor_registry_update(reg, bus, sdl3d_vec3_make(0, 0, 0));
    EXPECT_EQ(g_fire_count, 1);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_actor_registry_destroy(reg);
}

/* ================================================================== */
/* Update: inactive actors are skipped                                */
/* ================================================================== */

TEST(ActorRegistry, InactiveActorsSkipped)
{
    reset_globals();
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    sdl3d_signal_connect(bus, 1, fire_counter, NULL);

    sdl3d_registered_actor *sensor = sdl3d_actor_registry_add(reg, "sensor");
    sensor->triggers[0].type = SDL3D_TRIGGER_SPATIAL;
    sensor->triggers[0].edge = SDL3D_TRIGGER_EDGE_ENTER;
    sensor->triggers[0].emit_signal_id = 1;
    sensor->triggers[0].enabled = true;
    sensor->triggers[0].spatial.zone = (sdl3d_bounding_box){{0, 0, 0}, {10, 10, 10}};
    sensor->trigger_count = 1;
    sensor->active = false;

    sdl3d_actor_registry_update(reg, bus, sdl3d_vec3_make(5, 5, 5));
    EXPECT_EQ(g_fire_count, 0);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_actor_registry_destroy(reg);
}

/* ================================================================== */
/* NULL safety                                                        */
/* ================================================================== */

TEST(ActorRegistry, NullArgsAreSafe)
{
    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    EXPECT_EQ(sdl3d_actor_registry_count(nullptr), 0);
    sdl3d_actor_registry_update(nullptr, bus, sdl3d_vec3_make(0, 0, 0));
    sdl3d_actor_registry_update(nullptr, nullptr, sdl3d_vec3_make(0, 0, 0));
    sdl3d_signal_bus_destroy(bus);
}

/* ================================================================== */
/* Stress: many actors                                                */
/* ================================================================== */

TEST(ActorRegistry, ManyActors)
{
    sdl3d_actor_registry *reg = sdl3d_actor_registry_create();
    char name[32];
    for (int i = 0; i < 100; i++)
    {
        SDL_snprintf(name, sizeof(name), "actor_%d", i);
        sdl3d_registered_actor *a = sdl3d_actor_registry_add(reg, name);
        ASSERT_NE(a, nullptr) << "Failed at i=" << i;
        sdl3d_properties_set_int(a->props, "index", i);
    }
    EXPECT_EQ(sdl3d_actor_registry_count(reg), 100);

    for (int i = 0; i < 100; i++)
    {
        SDL_snprintf(name, sizeof(name), "actor_%d", i);
        sdl3d_registered_actor *a = sdl3d_actor_registry_find(reg, name);
        ASSERT_NE(a, nullptr) << "Not found: " << name;
        EXPECT_EQ(sdl3d_properties_get_int(a->props, "index", -1), i);
    }

    sdl3d_actor_registry_destroy(reg);
}
