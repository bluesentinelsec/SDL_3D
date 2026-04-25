/**
 * @file properties.h
 * @brief Generic key-value property bag for game entity state.
 *
 * A property bag stores arbitrary named values of six types: int, float,
 * bool, vec3, string, and color. Any key is accepted — unknown keys are
 * preserved, not rejected. This matches the Quake 3 / Source / Radiant
 * convention where a map file stores whatever the designer typed and the
 * engine interprets known keys while passing through unknown ones.
 *
 * The property bag is the canonical serialization format for entity state:
 * dump all pairs to JSON, load them back, round-trip clean.
 *
 * Usage:
 * @code
 *   sdl3d_properties *props = sdl3d_properties_create();
 *   sdl3d_properties_set_int(props, "health", 100);
 *   sdl3d_properties_set_string(props, "classname", "info_player_start");
 *   sdl3d_properties_set_bool(props, "locked", true);
 *
 *   int hp = sdl3d_properties_get_int(props, "health", 0);
 *   const char *cls = sdl3d_properties_get_string(props, "classname", "");
 *
 *   // Iterate all entries
 *   for (int i = 0; i < sdl3d_properties_count(props); i++) {
 *       const char *key;
 *       sdl3d_value_type type;
 *       sdl3d_properties_get_key_at(props, i, &key, &type);
 *   }
 *
 *   sdl3d_properties_destroy(props);
 * @endcode
 */

#ifndef SDL3D_PROPERTIES_H
#define SDL3D_PROPERTIES_H

#include <stdbool.h>

#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Value types stored in a property bag. */
    typedef enum sdl3d_value_type
    {
        SDL3D_VALUE_INT,
        SDL3D_VALUE_FLOAT,
        SDL3D_VALUE_BOOL,
        SDL3D_VALUE_VEC3,
        SDL3D_VALUE_STRING,
        SDL3D_VALUE_COLOR,
    } sdl3d_value_type;

    /** @brief A tagged value that can hold any supported property type. */
    typedef struct sdl3d_value
    {
        sdl3d_value_type type;
        union {
            int as_int;
            float as_float;
            bool as_bool;
            sdl3d_vec3 as_vec3;
            char *as_string; /**< Owned copy. Freed on overwrite or destroy. */
            sdl3d_color as_color;
        };
    } sdl3d_value;

    /** @brief Opaque property bag handle. */
    typedef struct sdl3d_properties sdl3d_properties;

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an empty property bag.
     * @return A new property bag, or NULL on allocation failure.
     */
    sdl3d_properties *sdl3d_properties_create(void);

    /**
     * @brief Destroy a property bag and free all owned memory.
     *
     * All string values are freed. Safe to call with NULL.
     */
    void sdl3d_properties_destroy(sdl3d_properties *props);

    /* ================================================================== */
    /* Setters                                                            */
    /* ================================================================== */

    /**
     * @brief Set an integer property.
     *
     * If the key already exists, its value is overwritten regardless of
     * the previous type. The key string is copied internally.
     */
    void sdl3d_properties_set_int(sdl3d_properties *props, const char *key, int value);

    /**
     * @brief Set a float property.
     * @see sdl3d_properties_set_int for overwrite semantics.
     */
    void sdl3d_properties_set_float(sdl3d_properties *props, const char *key, float value);

    /**
     * @brief Set a boolean property.
     * @see sdl3d_properties_set_int for overwrite semantics.
     */
    void sdl3d_properties_set_bool(sdl3d_properties *props, const char *key, bool value);

    /**
     * @brief Set a 3D vector property.
     * @see sdl3d_properties_set_int for overwrite semantics.
     */
    void sdl3d_properties_set_vec3(sdl3d_properties *props, const char *key, sdl3d_vec3 value);

    /**
     * @brief Set a string property.
     *
     * The value string is copied internally. The previous value (if any)
     * is freed. Passing NULL for value is equivalent to setting an empty
     * string.
     *
     * @see sdl3d_properties_set_int for overwrite semantics.
     */
    void sdl3d_properties_set_string(sdl3d_properties *props, const char *key, const char *value);

    /**
     * @brief Set a color property.
     * @see sdl3d_properties_set_int for overwrite semantics.
     */
    void sdl3d_properties_set_color(sdl3d_properties *props, const char *key, sdl3d_color value);

    /* ================================================================== */
    /* Getters                                                            */
    /* ================================================================== */

    /**
     * @brief Get an integer property.
     * @param fallback Returned if the key does not exist or has a different type.
     */
    int sdl3d_properties_get_int(const sdl3d_properties *props, const char *key, int fallback);

    /**
     * @brief Get a float property.
     * @param fallback Returned if the key does not exist or has a different type.
     */
    float sdl3d_properties_get_float(const sdl3d_properties *props, const char *key, float fallback);

    /**
     * @brief Get a boolean property.
     * @param fallback Returned if the key does not exist or has a different type.
     */
    bool sdl3d_properties_get_bool(const sdl3d_properties *props, const char *key, bool fallback);

    /**
     * @brief Get a 3D vector property.
     * @param fallback Returned if the key does not exist or has a different type.
     */
    sdl3d_vec3 sdl3d_properties_get_vec3(const sdl3d_properties *props, const char *key, sdl3d_vec3 fallback);

    /**
     * @brief Get a string property.
     *
     * The returned pointer is valid until the key is overwritten, removed,
     * or the property bag is destroyed.
     *
     * @param fallback Returned if the key does not exist or has a different type.
     */
    const char *sdl3d_properties_get_string(const sdl3d_properties *props, const char *key, const char *fallback);

    /**
     * @brief Get a color property.
     * @param fallback Returned if the key does not exist or has a different type.
     */
    sdl3d_color sdl3d_properties_get_color(const sdl3d_properties *props, const char *key, sdl3d_color fallback);

    /* ================================================================== */
    /* Query and mutation                                                 */
    /* ================================================================== */

    /**
     * @brief Test whether a key exists in the property bag.
     * @return true if the key is present (any type), false otherwise.
     */
    bool sdl3d_properties_has(const sdl3d_properties *props, const char *key);

    /**
     * @brief Remove a key from the property bag.
     *
     * If the key holds a string value, the string is freed. No-op if the
     * key does not exist. Safe with NULL props or key.
     */
    void sdl3d_properties_remove(sdl3d_properties *props, const char *key);

    /**
     * @brief Remove all entries from the property bag.
     *
     * All string values are freed. The bag is empty after this call.
     */
    void sdl3d_properties_clear(sdl3d_properties *props);

    /* ================================================================== */
    /* Iteration / introspection                                         */
    /* ================================================================== */

    /**
     * @brief Get the number of key-value pairs in the property bag.
     */
    int sdl3d_properties_count(const sdl3d_properties *props);

    /**
     * @brief Get the key and value type at a given index.
     *
     * Indices are in the range [0, count). The order is arbitrary and may
     * change when entries are added or removed. This is intended for
     * serialization and editor inspection, not indexed access by design.
     *
     * @param index   Zero-based index into the entry array.
     * @param out_key Receives a pointer to the key string (owned by the bag).
     * @param out_type Receives the value type. May be NULL if not needed.
     * @return true if the index was valid, false otherwise.
     */
    bool sdl3d_properties_get_key_at(const sdl3d_properties *props, int index, const char **out_key,
                                     sdl3d_value_type *out_type);

    /**
     * @brief Get the raw tagged value for a key.
     *
     * Returns NULL if the key does not exist. The returned pointer is
     * valid until the key is overwritten, removed, or the bag is destroyed.
     * For string values, the caller must not free `as_string`.
     */
    const sdl3d_value *sdl3d_properties_get_value(const sdl3d_properties *props, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_PROPERTIES_H */
