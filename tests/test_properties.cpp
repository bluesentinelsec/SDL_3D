/**
 * @file test_properties.cpp
 * @brief Unit tests for slayer3d_properties — generic key-value property bag.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "slayer3d/math.h"
#include "slayer3d/properties.h"
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

TEST(Properties, CreateAndDestroy)
{
    slayer3d_properties *p = slayer3d_properties_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(slayer3d_properties_count(p), 0);
    slayer3d_properties_destroy(p);
}

TEST(Properties, DestroyNullIsSafe)
{
    slayer3d_properties_destroy(nullptr);
}

/* ================================================================== */
/* Int                                                                */
/* ================================================================== */

TEST(Properties, SetAndGetInt)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "health", 100);
    EXPECT_EQ(slayer3d_properties_get_int(p, "health", 0), 100);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetIntFallbackOnMissing)
{
    slayer3d_properties *p = slayer3d_properties_create();
    EXPECT_EQ(slayer3d_properties_get_int(p, "missing", 42), 42);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetIntFallbackOnTypeMismatch)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_float(p, "speed", 3.5f);
    EXPECT_EQ(slayer3d_properties_get_int(p, "speed", -1), -1);
    slayer3d_properties_destroy(p);
}

TEST(Properties, OverwriteIntChangesValue)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "ammo", 50);
    slayer3d_properties_set_int(p, "ammo", 25);
    EXPECT_EQ(slayer3d_properties_get_int(p, "ammo", 0), 25);
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Float                                                              */
/* ================================================================== */

TEST(Properties, SetAndGetFloat)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_float(p, "speed", 12.5f);
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(p, "speed", 0.0f), 12.5f);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetFloatFallbackOnMissing)
{
    slayer3d_properties *p = slayer3d_properties_create();
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(p, "x", 1.0f), 1.0f);
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Bool                                                               */
/* ================================================================== */

TEST(Properties, SetAndGetBool)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_bool(p, "locked", true);
    EXPECT_TRUE(slayer3d_properties_get_bool(p, "locked", false));
    slayer3d_properties_set_bool(p, "locked", false);
    EXPECT_FALSE(slayer3d_properties_get_bool(p, "locked", true));
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetBoolFallbackOnMissing)
{
    slayer3d_properties *p = slayer3d_properties_create();
    EXPECT_TRUE(slayer3d_properties_get_bool(p, "missing", true));
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Vec3                                                               */
/* ================================================================== */

TEST(Properties, SetAndGetVec3)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_vec3 origin = slayer3d_vec3_make(1.0f, 2.0f, 3.0f);
    slayer3d_properties_set_vec3(p, "origin", origin);
    slayer3d_vec3 result = slayer3d_properties_get_vec3(p, "origin", slayer3d_vec3_make(0, 0, 0));
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetVec3FallbackOnMissing)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_vec3 fb = slayer3d_vec3_make(9, 9, 9);
    slayer3d_vec3 result = slayer3d_properties_get_vec3(p, "nope", fb);
    EXPECT_FLOAT_EQ(result.x, 9.0f);
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* String                                                             */
/* ================================================================== */

TEST(Properties, SetAndGetString)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_string(p, "classname", "info_player_start");
    EXPECT_STREQ(slayer3d_properties_get_string(p, "classname", ""), "info_player_start");
    slayer3d_properties_destroy(p);
}

TEST(Properties, StringIsCopied)
{
    slayer3d_properties *p = slayer3d_properties_create();
    char buf[32];
    SDL_strlcpy(buf, "hello", sizeof(buf));
    slayer3d_properties_set_string(p, "msg", buf);
    /* Mutate the original buffer. */
    buf[0] = 'X';
    EXPECT_STREQ(slayer3d_properties_get_string(p, "msg", ""), "hello");
    slayer3d_properties_destroy(p);
}

TEST(Properties, OverwriteStringFreesOld)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_string(p, "name", "first");
    slayer3d_properties_set_string(p, "name", "second");
    EXPECT_STREQ(slayer3d_properties_get_string(p, "name", ""), "second");
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    slayer3d_properties_destroy(p);
}

TEST(Properties, SetStringNullBecomesEmpty)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_string(p, "msg", NULL);
    EXPECT_STREQ(slayer3d_properties_get_string(p, "msg", "fallback"), "");
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetStringFallbackOnMissing)
{
    slayer3d_properties *p = slayer3d_properties_create();
    EXPECT_STREQ(slayer3d_properties_get_string(p, "missing", "default"), "default");
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetStringFallbackOnTypeMismatch)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "health", 100);
    EXPECT_STREQ(slayer3d_properties_get_string(p, "health", "nope"), "nope");
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Color                                                              */
/* ================================================================== */

TEST(Properties, SetAndGetColor)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_color red = {255, 0, 0, 255};
    slayer3d_properties_set_color(p, "tint", red);
    slayer3d_color result = slayer3d_properties_get_color(p, "tint", (slayer3d_color){0, 0, 0, 0});
    EXPECT_EQ(result.r, 255);
    EXPECT_EQ(result.g, 0);
    EXPECT_EQ(result.b, 0);
    EXPECT_EQ(result.a, 255);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetColorFallbackOnMissing)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_color fb = {1, 2, 3, 4};
    slayer3d_color result = slayer3d_properties_get_color(p, "nope", fb);
    EXPECT_EQ(result.r, 1);
    EXPECT_EQ(result.a, 4);
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Type overwrite (changing type of existing key)                     */
/* ================================================================== */

TEST(Properties, OverwriteChangesType)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "x", 10);
    EXPECT_EQ(slayer3d_properties_get_int(p, "x", 0), 10);

    slayer3d_properties_set_string(p, "x", "hello");
    EXPECT_EQ(slayer3d_properties_get_int(p, "x", -1), -1);
    EXPECT_STREQ(slayer3d_properties_get_string(p, "x", ""), "hello");
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    slayer3d_properties_destroy(p);
}

TEST(Properties, OverwriteStringWithIntFreesString)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_string(p, "val", "some long string value");
    slayer3d_properties_set_int(p, "val", 42);
    EXPECT_EQ(slayer3d_properties_get_int(p, "val", 0), 42);
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Has / Remove / Clear                                               */
/* ================================================================== */

TEST(Properties, HasReturnsTrueForExistingKey)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "a", 1);
    EXPECT_TRUE(slayer3d_properties_has(p, "a"));
    EXPECT_FALSE(slayer3d_properties_has(p, "b"));
    slayer3d_properties_destroy(p);
}

TEST(Properties, RemoveDeletesKey)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "a", 1);
    slayer3d_properties_set_int(p, "b", 2);
    EXPECT_EQ(slayer3d_properties_count(p), 2);

    slayer3d_properties_remove(p, "a");
    EXPECT_FALSE(slayer3d_properties_has(p, "a"));
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    EXPECT_EQ(slayer3d_properties_get_int(p, "b", 0), 2);
    slayer3d_properties_destroy(p);
}

TEST(Properties, RemoveStringFreesMemory)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_string(p, "msg", "hello world");
    slayer3d_properties_remove(p, "msg");
    EXPECT_FALSE(slayer3d_properties_has(p, "msg"));
    slayer3d_properties_destroy(p);
}

TEST(Properties, RemoveNonexistentIsNoOp)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_remove(p, "nope");
    EXPECT_EQ(slayer3d_properties_count(p), 0);
    slayer3d_properties_destroy(p);
}

TEST(Properties, ClearRemovesAll)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "a", 1);
    slayer3d_properties_set_string(p, "b", "hello");
    slayer3d_properties_set_float(p, "c", 3.0f);
    EXPECT_EQ(slayer3d_properties_count(p), 3);

    slayer3d_properties_clear(p);
    EXPECT_EQ(slayer3d_properties_count(p), 0);
    EXPECT_FALSE(slayer3d_properties_has(p, "a"));
    EXPECT_FALSE(slayer3d_properties_has(p, "b"));
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Iteration                                                          */
/* ================================================================== */

TEST(Properties, CountReflectsEntries)
{
    slayer3d_properties *p = slayer3d_properties_create();
    EXPECT_EQ(slayer3d_properties_count(p), 0);
    slayer3d_properties_set_int(p, "a", 1);
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    slayer3d_properties_set_float(p, "b", 2.0f);
    EXPECT_EQ(slayer3d_properties_count(p), 2);
    slayer3d_properties_remove(p, "a");
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetKeyAtEnumeratesAll)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "health", 100);
    slayer3d_properties_set_string(p, "name", "player");
    slayer3d_properties_set_bool(p, "alive", true);

    bool found_health = false, found_name = false, found_alive = false;
    for (int i = 0; i < slayer3d_properties_count(p); i++)
    {
        const char *key = NULL;
        slayer3d_value_type type;
        ASSERT_TRUE(slayer3d_properties_get_key_at(p, i, &key, &type));
        ASSERT_NE(key, nullptr);
        if (SDL_strcmp(key, "health") == 0)
        {
            EXPECT_EQ(type, SLAYER3D_VALUE_INT);
            found_health = true;
        }
        else if (SDL_strcmp(key, "name") == 0)
        {
            EXPECT_EQ(type, SLAYER3D_VALUE_STRING);
            found_name = true;
        }
        else if (SDL_strcmp(key, "alive") == 0)
        {
            EXPECT_EQ(type, SLAYER3D_VALUE_BOOL);
            found_alive = true;
        }
    }
    EXPECT_TRUE(found_health);
    EXPECT_TRUE(found_name);
    EXPECT_TRUE(found_alive);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetKeyAtOutOfRangeReturnsFalse)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "a", 1);
    const char *key = NULL;
    EXPECT_FALSE(slayer3d_properties_get_key_at(p, -1, &key, NULL));
    EXPECT_FALSE(slayer3d_properties_get_key_at(p, 1, &key, NULL));
    EXPECT_FALSE(slayer3d_properties_get_key_at(p, 100, &key, NULL));
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetKeyAtTypeParamIsOptional)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "x", 1);
    const char *key = NULL;
    EXPECT_TRUE(slayer3d_properties_get_key_at(p, 0, &key, NULL));
    EXPECT_STREQ(key, "x");
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Get value (raw tagged union)                                       */
/* ================================================================== */

TEST(Properties, GetValueReturnsTaggedUnion)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "hp", 75);
    const slayer3d_value *v = slayer3d_properties_get_value(p, "hp");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->type, SLAYER3D_VALUE_INT);
    EXPECT_EQ(v->as_int, 75);
    slayer3d_properties_destroy(p);
}

TEST(Properties, GetValueReturnsNullOnMissing)
{
    slayer3d_properties *p = slayer3d_properties_create();
    EXPECT_EQ(slayer3d_properties_get_value(p, "nope"), nullptr);
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Stress: many keys trigger growth                                   */
/* ================================================================== */

TEST(Properties, GrowthHandlesManyKeys)
{
    slayer3d_properties *p = slayer3d_properties_create();
    char key[32];
    for (int i = 0; i < 200; i++)
    {
        SDL_snprintf(key, sizeof(key), "key_%d", i);
        slayer3d_properties_set_int(p, key, i);
    }
    EXPECT_EQ(slayer3d_properties_count(p), 200);

    for (int i = 0; i < 200; i++)
    {
        SDL_snprintf(key, sizeof(key), "key_%d", i);
        EXPECT_EQ(slayer3d_properties_get_int(p, key, -1), i) << "key=" << key;
    }
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* Tombstone correctness: remove then re-insert                       */
/* ================================================================== */

TEST(Properties, RemoveThenReinsertSameKey)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, "x", 1);
    slayer3d_properties_remove(p, "x");
    EXPECT_FALSE(slayer3d_properties_has(p, "x"));
    EXPECT_EQ(slayer3d_properties_count(p), 0);

    slayer3d_properties_set_int(p, "x", 2);
    EXPECT_TRUE(slayer3d_properties_has(p, "x"));
    EXPECT_EQ(slayer3d_properties_get_int(p, "x", 0), 2);
    EXPECT_EQ(slayer3d_properties_count(p), 1);
    slayer3d_properties_destroy(p);
}

TEST(Properties, RemoveDoesNotBreakProbeChain)
{
    /* Insert keys that hash to the same bucket, remove the first,
     * verify the second is still findable. */
    slayer3d_properties *p = slayer3d_properties_create();
    /* Insert enough keys that collisions are likely. */
    for (int i = 0; i < 50; i++)
    {
        char key[16];
        SDL_snprintf(key, sizeof(key), "k%d", i);
        slayer3d_properties_set_int(p, key, i);
    }
    /* Remove every other key. */
    for (int i = 0; i < 50; i += 2)
    {
        char key[16];
        SDL_snprintf(key, sizeof(key), "k%d", i);
        slayer3d_properties_remove(p, key);
    }
    /* Remaining keys must still be findable. */
    for (int i = 1; i < 50; i += 2)
    {
        char key[16];
        SDL_snprintf(key, sizeof(key), "k%d", i);
        EXPECT_EQ(slayer3d_properties_get_int(p, key, -1), i) << "key=" << key;
    }
    EXPECT_EQ(slayer3d_properties_count(p), 25);
    slayer3d_properties_destroy(p);
}

/* ================================================================== */
/* NULL safety                                                        */
/* ================================================================== */

TEST(Properties, NullPropsAreSafe)
{
    EXPECT_EQ(slayer3d_properties_count(nullptr), 0);
    EXPECT_FALSE(slayer3d_properties_has(nullptr, "x"));
    EXPECT_EQ(slayer3d_properties_get_int(nullptr, "x", 42), 42);
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(nullptr, "x", 1.0f), 1.0f);
    EXPECT_FALSE(slayer3d_properties_get_bool(nullptr, "x", false));
    EXPECT_STREQ(slayer3d_properties_get_string(nullptr, "x", "fb"), "fb");
    EXPECT_EQ(slayer3d_properties_get_value(nullptr, "x"), nullptr);

    slayer3d_properties_set_int(nullptr, "x", 1);
    slayer3d_properties_remove(nullptr, "x");
    slayer3d_properties_clear(nullptr);

    const char *key = NULL;
    EXPECT_FALSE(slayer3d_properties_get_key_at(nullptr, 0, &key, NULL));
}

TEST(Properties, NullKeyAreSafe)
{
    slayer3d_properties *p = slayer3d_properties_create();
    slayer3d_properties_set_int(p, NULL, 1);
    EXPECT_EQ(slayer3d_properties_count(p), 0);
    EXPECT_FALSE(slayer3d_properties_has(p, NULL));
    EXPECT_EQ(slayer3d_properties_get_int(p, NULL, 42), 42);
    slayer3d_properties_remove(p, NULL);
    slayer3d_properties_destroy(p);
}
