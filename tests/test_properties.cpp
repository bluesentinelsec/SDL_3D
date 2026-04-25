/**
 * @file test_properties.cpp
 * @brief Unit tests for sdl3d_properties — generic key-value property bag.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

TEST(Properties, CreateAndDestroy)
{
    sdl3d_properties *p = sdl3d_properties_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(sdl3d_properties_count(p), 0);
    sdl3d_properties_destroy(p);
}

TEST(Properties, DestroyNullIsSafe)
{
    sdl3d_properties_destroy(nullptr);
}

/* ================================================================== */
/* Int                                                                */
/* ================================================================== */

TEST(Properties, SetAndGetInt)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "health", 100);
    EXPECT_EQ(sdl3d_properties_get_int(p, "health", 0), 100);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetIntFallbackOnMissing)
{
    sdl3d_properties *p = sdl3d_properties_create();
    EXPECT_EQ(sdl3d_properties_get_int(p, "missing", 42), 42);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetIntFallbackOnTypeMismatch)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_float(p, "speed", 3.5f);
    EXPECT_EQ(sdl3d_properties_get_int(p, "speed", -1), -1);
    sdl3d_properties_destroy(p);
}

TEST(Properties, OverwriteIntChangesValue)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "ammo", 50);
    sdl3d_properties_set_int(p, "ammo", 25);
    EXPECT_EQ(sdl3d_properties_get_int(p, "ammo", 0), 25);
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Float                                                              */
/* ================================================================== */

TEST(Properties, SetAndGetFloat)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_float(p, "speed", 12.5f);
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(p, "speed", 0.0f), 12.5f);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetFloatFallbackOnMissing)
{
    sdl3d_properties *p = sdl3d_properties_create();
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(p, "x", 1.0f), 1.0f);
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Bool                                                               */
/* ================================================================== */

TEST(Properties, SetAndGetBool)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_bool(p, "locked", true);
    EXPECT_TRUE(sdl3d_properties_get_bool(p, "locked", false));
    sdl3d_properties_set_bool(p, "locked", false);
    EXPECT_FALSE(sdl3d_properties_get_bool(p, "locked", true));
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetBoolFallbackOnMissing)
{
    sdl3d_properties *p = sdl3d_properties_create();
    EXPECT_TRUE(sdl3d_properties_get_bool(p, "missing", true));
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Vec3                                                               */
/* ================================================================== */

TEST(Properties, SetAndGetVec3)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_vec3 origin = sdl3d_vec3_make(1.0f, 2.0f, 3.0f);
    sdl3d_properties_set_vec3(p, "origin", origin);
    sdl3d_vec3 result = sdl3d_properties_get_vec3(p, "origin", sdl3d_vec3_make(0, 0, 0));
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetVec3FallbackOnMissing)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_vec3 fb = sdl3d_vec3_make(9, 9, 9);
    sdl3d_vec3 result = sdl3d_properties_get_vec3(p, "nope", fb);
    EXPECT_FLOAT_EQ(result.x, 9.0f);
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* String                                                             */
/* ================================================================== */

TEST(Properties, SetAndGetString)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_string(p, "classname", "info_player_start");
    EXPECT_STREQ(sdl3d_properties_get_string(p, "classname", ""), "info_player_start");
    sdl3d_properties_destroy(p);
}

TEST(Properties, StringIsCopied)
{
    sdl3d_properties *p = sdl3d_properties_create();
    char buf[32];
    SDL_strlcpy(buf, "hello", sizeof(buf));
    sdl3d_properties_set_string(p, "msg", buf);
    /* Mutate the original buffer. */
    buf[0] = 'X';
    EXPECT_STREQ(sdl3d_properties_get_string(p, "msg", ""), "hello");
    sdl3d_properties_destroy(p);
}

TEST(Properties, OverwriteStringFreesOld)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_string(p, "name", "first");
    sdl3d_properties_set_string(p, "name", "second");
    EXPECT_STREQ(sdl3d_properties_get_string(p, "name", ""), "second");
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    sdl3d_properties_destroy(p);
}

TEST(Properties, SetStringNullBecomesEmpty)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_string(p, "msg", NULL);
    EXPECT_STREQ(sdl3d_properties_get_string(p, "msg", "fallback"), "");
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetStringFallbackOnMissing)
{
    sdl3d_properties *p = sdl3d_properties_create();
    EXPECT_STREQ(sdl3d_properties_get_string(p, "missing", "default"), "default");
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetStringFallbackOnTypeMismatch)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "health", 100);
    EXPECT_STREQ(sdl3d_properties_get_string(p, "health", "nope"), "nope");
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Color                                                              */
/* ================================================================== */

TEST(Properties, SetAndGetColor)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_color red = {255, 0, 0, 255};
    sdl3d_properties_set_color(p, "tint", red);
    sdl3d_color result = sdl3d_properties_get_color(p, "tint", (sdl3d_color){0, 0, 0, 0});
    EXPECT_EQ(result.r, 255);
    EXPECT_EQ(result.g, 0);
    EXPECT_EQ(result.b, 0);
    EXPECT_EQ(result.a, 255);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetColorFallbackOnMissing)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_color fb = {1, 2, 3, 4};
    sdl3d_color result = sdl3d_properties_get_color(p, "nope", fb);
    EXPECT_EQ(result.r, 1);
    EXPECT_EQ(result.a, 4);
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Type overwrite (changing type of existing key)                     */
/* ================================================================== */

TEST(Properties, OverwriteChangesType)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "x", 10);
    EXPECT_EQ(sdl3d_properties_get_int(p, "x", 0), 10);

    sdl3d_properties_set_string(p, "x", "hello");
    EXPECT_EQ(sdl3d_properties_get_int(p, "x", -1), -1);
    EXPECT_STREQ(sdl3d_properties_get_string(p, "x", ""), "hello");
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    sdl3d_properties_destroy(p);
}

TEST(Properties, OverwriteStringWithIntFreesString)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_string(p, "val", "some long string value");
    sdl3d_properties_set_int(p, "val", 42);
    EXPECT_EQ(sdl3d_properties_get_int(p, "val", 0), 42);
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Has / Remove / Clear                                               */
/* ================================================================== */

TEST(Properties, HasReturnsTrueForExistingKey)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "a", 1);
    EXPECT_TRUE(sdl3d_properties_has(p, "a"));
    EXPECT_FALSE(sdl3d_properties_has(p, "b"));
    sdl3d_properties_destroy(p);
}

TEST(Properties, RemoveDeletesKey)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "a", 1);
    sdl3d_properties_set_int(p, "b", 2);
    EXPECT_EQ(sdl3d_properties_count(p), 2);

    sdl3d_properties_remove(p, "a");
    EXPECT_FALSE(sdl3d_properties_has(p, "a"));
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    EXPECT_EQ(sdl3d_properties_get_int(p, "b", 0), 2);
    sdl3d_properties_destroy(p);
}

TEST(Properties, RemoveStringFreesMemory)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_string(p, "msg", "hello world");
    sdl3d_properties_remove(p, "msg");
    EXPECT_FALSE(sdl3d_properties_has(p, "msg"));
    sdl3d_properties_destroy(p);
}

TEST(Properties, RemoveNonexistentIsNoOp)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_remove(p, "nope");
    EXPECT_EQ(sdl3d_properties_count(p), 0);
    sdl3d_properties_destroy(p);
}

TEST(Properties, ClearRemovesAll)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "a", 1);
    sdl3d_properties_set_string(p, "b", "hello");
    sdl3d_properties_set_float(p, "c", 3.0f);
    EXPECT_EQ(sdl3d_properties_count(p), 3);

    sdl3d_properties_clear(p);
    EXPECT_EQ(sdl3d_properties_count(p), 0);
    EXPECT_FALSE(sdl3d_properties_has(p, "a"));
    EXPECT_FALSE(sdl3d_properties_has(p, "b"));
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Iteration                                                          */
/* ================================================================== */

TEST(Properties, CountReflectsEntries)
{
    sdl3d_properties *p = sdl3d_properties_create();
    EXPECT_EQ(sdl3d_properties_count(p), 0);
    sdl3d_properties_set_int(p, "a", 1);
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    sdl3d_properties_set_float(p, "b", 2.0f);
    EXPECT_EQ(sdl3d_properties_count(p), 2);
    sdl3d_properties_remove(p, "a");
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetKeyAtEnumeratesAll)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "health", 100);
    sdl3d_properties_set_string(p, "name", "player");
    sdl3d_properties_set_bool(p, "alive", true);

    bool found_health = false, found_name = false, found_alive = false;
    for (int i = 0; i < sdl3d_properties_count(p); i++)
    {
        const char *key = NULL;
        sdl3d_value_type type;
        ASSERT_TRUE(sdl3d_properties_get_key_at(p, i, &key, &type));
        ASSERT_NE(key, nullptr);
        if (SDL_strcmp(key, "health") == 0)
        {
            EXPECT_EQ(type, SDL3D_VALUE_INT);
            found_health = true;
        }
        else if (SDL_strcmp(key, "name") == 0)
        {
            EXPECT_EQ(type, SDL3D_VALUE_STRING);
            found_name = true;
        }
        else if (SDL_strcmp(key, "alive") == 0)
        {
            EXPECT_EQ(type, SDL3D_VALUE_BOOL);
            found_alive = true;
        }
    }
    EXPECT_TRUE(found_health);
    EXPECT_TRUE(found_name);
    EXPECT_TRUE(found_alive);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetKeyAtOutOfRangeReturnsFalse)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "a", 1);
    const char *key = NULL;
    EXPECT_FALSE(sdl3d_properties_get_key_at(p, -1, &key, NULL));
    EXPECT_FALSE(sdl3d_properties_get_key_at(p, 1, &key, NULL));
    EXPECT_FALSE(sdl3d_properties_get_key_at(p, 100, &key, NULL));
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetKeyAtTypeParamIsOptional)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "x", 1);
    const char *key = NULL;
    EXPECT_TRUE(sdl3d_properties_get_key_at(p, 0, &key, NULL));
    EXPECT_STREQ(key, "x");
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Get value (raw tagged union)                                       */
/* ================================================================== */

TEST(Properties, GetValueReturnsTaggedUnion)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "hp", 75);
    const sdl3d_value *v = sdl3d_properties_get_value(p, "hp");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->type, SDL3D_VALUE_INT);
    EXPECT_EQ(v->as_int, 75);
    sdl3d_properties_destroy(p);
}

TEST(Properties, GetValueReturnsNullOnMissing)
{
    sdl3d_properties *p = sdl3d_properties_create();
    EXPECT_EQ(sdl3d_properties_get_value(p, "nope"), nullptr);
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Stress: many keys trigger growth                                   */
/* ================================================================== */

TEST(Properties, GrowthHandlesManyKeys)
{
    sdl3d_properties *p = sdl3d_properties_create();
    char key[32];
    for (int i = 0; i < 200; i++)
    {
        SDL_snprintf(key, sizeof(key), "key_%d", i);
        sdl3d_properties_set_int(p, key, i);
    }
    EXPECT_EQ(sdl3d_properties_count(p), 200);

    for (int i = 0; i < 200; i++)
    {
        SDL_snprintf(key, sizeof(key), "key_%d", i);
        EXPECT_EQ(sdl3d_properties_get_int(p, key, -1), i) << "key=" << key;
    }
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* Tombstone correctness: remove then re-insert                       */
/* ================================================================== */

TEST(Properties, RemoveThenReinsertSameKey)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, "x", 1);
    sdl3d_properties_remove(p, "x");
    EXPECT_FALSE(sdl3d_properties_has(p, "x"));
    EXPECT_EQ(sdl3d_properties_count(p), 0);

    sdl3d_properties_set_int(p, "x", 2);
    EXPECT_TRUE(sdl3d_properties_has(p, "x"));
    EXPECT_EQ(sdl3d_properties_get_int(p, "x", 0), 2);
    EXPECT_EQ(sdl3d_properties_count(p), 1);
    sdl3d_properties_destroy(p);
}

TEST(Properties, RemoveDoesNotBreakProbeChain)
{
    /* Insert keys that hash to the same bucket, remove the first,
     * verify the second is still findable. */
    sdl3d_properties *p = sdl3d_properties_create();
    /* Insert enough keys that collisions are likely. */
    for (int i = 0; i < 50; i++)
    {
        char key[16];
        SDL_snprintf(key, sizeof(key), "k%d", i);
        sdl3d_properties_set_int(p, key, i);
    }
    /* Remove every other key. */
    for (int i = 0; i < 50; i += 2)
    {
        char key[16];
        SDL_snprintf(key, sizeof(key), "k%d", i);
        sdl3d_properties_remove(p, key);
    }
    /* Remaining keys must still be findable. */
    for (int i = 1; i < 50; i += 2)
    {
        char key[16];
        SDL_snprintf(key, sizeof(key), "k%d", i);
        EXPECT_EQ(sdl3d_properties_get_int(p, key, -1), i) << "key=" << key;
    }
    EXPECT_EQ(sdl3d_properties_count(p), 25);
    sdl3d_properties_destroy(p);
}

/* ================================================================== */
/* NULL safety                                                        */
/* ================================================================== */

TEST(Properties, NullPropsAreSafe)
{
    EXPECT_EQ(sdl3d_properties_count(nullptr), 0);
    EXPECT_FALSE(sdl3d_properties_has(nullptr, "x"));
    EXPECT_EQ(sdl3d_properties_get_int(nullptr, "x", 42), 42);
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(nullptr, "x", 1.0f), 1.0f);
    EXPECT_FALSE(sdl3d_properties_get_bool(nullptr, "x", false));
    EXPECT_STREQ(sdl3d_properties_get_string(nullptr, "x", "fb"), "fb");
    EXPECT_EQ(sdl3d_properties_get_value(nullptr, "x"), nullptr);

    sdl3d_properties_set_int(nullptr, "x", 1);
    sdl3d_properties_remove(nullptr, "x");
    sdl3d_properties_clear(nullptr);

    const char *key = NULL;
    EXPECT_FALSE(sdl3d_properties_get_key_at(nullptr, 0, &key, NULL));
}

TEST(Properties, NullKeyAreSafe)
{
    sdl3d_properties *p = sdl3d_properties_create();
    sdl3d_properties_set_int(p, NULL, 1);
    EXPECT_EQ(sdl3d_properties_count(p), 0);
    EXPECT_FALSE(sdl3d_properties_has(p, NULL));
    EXPECT_EQ(sdl3d_properties_get_int(p, NULL, 42), 42);
    sdl3d_properties_remove(p, NULL);
    sdl3d_properties_destroy(p);
}
