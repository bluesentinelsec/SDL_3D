/**
 * @file properties.c
 * @brief Property bag implementation — open-addressing hash map.
 *
 * Keys are copied on insert. String values are copied and owned.
 * The hash map uses linear probing with a load factor threshold of 0.7.
 * Tombstones are used for deletion to preserve probe chains. Entries also
 * carry an insertion-order sequence so editor and serializer iteration is
 * deterministic and human-readable.
 */

#include "slayer3d/properties.h"

#include <SDL3/SDL_stdinc.h>
#include <limits.h>

/* ================================================================== */
/* Internal types                                                     */
/* ================================================================== */

typedef enum entry_state
{
    ENTRY_EMPTY,
    ENTRY_OCCUPIED,
    ENTRY_TOMBSTONE,
} entry_state;

typedef struct entry
{
    entry_state state;
    char *key; /* Owned copy of the key string. */
    slayer3d_value value;
    int order;
} entry;

struct slayer3d_properties
{
    entry *entries;
    int capacity;
    int count;    /* Number of OCCUPIED entries. */
    int occupied; /* OCCUPIED + TOMBSTONE (for load factor). */
    int next_order;
    Uint64 revision;
};

#define INITIAL_CAPACITY 16
#define LOAD_FACTOR_THRESHOLD 70 /* percent */

/* ================================================================== */
/* Hash function (FNV-1a)                                             */
/* ================================================================== */

static unsigned int fnv1a(const char *str)
{
    unsigned int hash = 2166136261u;
    while (*str)
    {
        hash ^= (unsigned char)*str++;
        hash *= 16777619u;
    }
    return hash;
}

/* ================================================================== */
/* Internal helpers                                                   */
/* ================================================================== */

/** Free the owned memory inside a value (strings only). */
static void free_value(slayer3d_value *v)
{
    if (v->type == SLAYER3D_VALUE_STRING)
    {
        SDL_free(v->as_string);
        v->as_string = NULL;
    }
}

/** Free the owned memory inside an entry (key + value). */
static void free_entry(entry *e)
{
    SDL_free(e->key);
    e->key = NULL;
    free_value(&e->value);
    e->state = ENTRY_EMPTY;
    e->order = 0;
}

/**
 * Find the entry for a key, or the first empty/tombstone slot where it
 * would be inserted. Returns NULL only if the table is full (should not
 * happen with proper load factor management).
 */
static entry *find_entry(entry *entries, int capacity, const char *key)
{
    unsigned int hash = fnv1a(key);
    int index = (int)(hash % (unsigned int)capacity);
    entry *first_tombstone = NULL;

    for (int i = 0; i < capacity; ++i)
    {
        entry *e = &entries[index];
        if (e->state == ENTRY_EMPTY)
        {
            return first_tombstone != NULL ? first_tombstone : e;
        }
        if (e->state == ENTRY_TOMBSTONE)
        {
            if (first_tombstone == NULL)
                first_tombstone = e;
        }
        else if (SDL_strcmp(e->key, key) == 0)
        {
            return e;
        }
        index = (index + 1) % capacity;
    }
    return first_tombstone;
}

/** Grow the table and rehash all occupied entries. */
static bool grow(slayer3d_properties *props)
{
    int new_capacity = props->capacity < INITIAL_CAPACITY ? INITIAL_CAPACITY : props->capacity * 2;
    entry *new_entries = (entry *)SDL_calloc((size_t)new_capacity, sizeof(entry));
    if (new_entries == NULL)
        return false;

    /* Rehash occupied entries into the new table. */
    for (int i = 0; i < props->capacity; ++i)
    {
        entry *old = &props->entries[i];
        if (old->state != ENTRY_OCCUPIED)
            continue;
        entry *dest = find_entry(new_entries, new_capacity, old->key);
        *dest = *old;
    }

    SDL_free(props->entries);
    props->entries = new_entries;
    props->capacity = new_capacity;
    props->occupied = props->count; /* Tombstones are gone after rehash. */
    return true;
}

/** Insert or overwrite a key with the given value. */
static bool values_equal(const slayer3d_value *left, const slayer3d_value *right)
{
    if (left == NULL || right == NULL || left->type != right->type)
        return false;
    switch (left->type)
    {
    case SLAYER3D_VALUE_INT:
        return left->as_int == right->as_int;
    case SLAYER3D_VALUE_FLOAT:
        return left->as_float == right->as_float;
    case SLAYER3D_VALUE_BOOL:
        return left->as_bool == right->as_bool;
    case SLAYER3D_VALUE_VEC3:
        return left->as_vec3.x == right->as_vec3.x && left->as_vec3.y == right->as_vec3.y &&
               left->as_vec3.z == right->as_vec3.z;
    case SLAYER3D_VALUE_STRING:
        return SDL_strcmp(left->as_string != NULL ? left->as_string : "",
                          right->as_string != NULL ? right->as_string : "") == 0;
    case SLAYER3D_VALUE_COLOR:
        return left->as_color.r == right->as_color.r && left->as_color.g == right->as_color.g &&
               left->as_color.b == right->as_color.b && left->as_color.a == right->as_color.a;
    }
    return false;
}

static void set_value(slayer3d_properties *props, const char *key, slayer3d_value value)
{
    if (props == NULL || key == NULL)
    {
        free_value(&value);
        return;
    }

    /* Grow if load factor exceeds threshold. */
    if (props->capacity == 0 || (props->occupied + 1) * 100 > props->capacity * LOAD_FACTOR_THRESHOLD)
    {
        if (!grow(props))
        {
            free_value(&value);
            return;
        }
    }

    entry *e = find_entry(props->entries, props->capacity, key);
    if (e == NULL)
    {
        free_value(&value);
        return;
    }

    if (e->state == ENTRY_OCCUPIED)
    {
        if (values_equal(&e->value, &value))
        {
            free_value(&value);
            return;
        }
        /* Overwrite existing entry. Free old value, keep key. */
        free_value(&e->value);
        e->value = value;
    }
    else
    {
        /* New entry. Copy the key. */
        bool was_tombstone = (e->state == ENTRY_TOMBSTONE);
        e->key = SDL_strdup(key);
        if (e->key == NULL)
        {
            free_value(&value);
            return;
        }
        e->value = value;
        e->state = ENTRY_OCCUPIED;
        e->order = props->next_order++;
        props->count++;
        if (!was_tombstone)
            props->occupied++;
    }
    props->revision++;
}

/** Look up a key. Returns NULL if not found. */
static const entry *lookup(const slayer3d_properties *props, const char *key)
{
    if (props == NULL || key == NULL || props->capacity == 0)
        return NULL;
    const entry *e = find_entry(props->entries, props->capacity, key);
    if (e == NULL || e->state != ENTRY_OCCUPIED)
        return NULL;
    return e;
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

slayer3d_properties *slayer3d_properties_create(void)
{
    slayer3d_properties *props = (slayer3d_properties *)SDL_calloc(1, sizeof(slayer3d_properties));
    return props;
}

void slayer3d_properties_destroy(slayer3d_properties *props)
{
    if (props == NULL)
        return;
    slayer3d_properties_clear(props);
    SDL_free(props->entries);
    SDL_free(props);
}

/* ================================================================== */
/* Setters                                                            */
/* ================================================================== */

void slayer3d_properties_set_int(slayer3d_properties *props, const char *key, int value)
{
    slayer3d_value v;
    SDL_zero(v);
    v.type = SLAYER3D_VALUE_INT;
    v.as_int = value;
    set_value(props, key, v);
}

void slayer3d_properties_set_float(slayer3d_properties *props, const char *key, float value)
{
    slayer3d_value v;
    SDL_zero(v);
    v.type = SLAYER3D_VALUE_FLOAT;
    v.as_float = value;
    set_value(props, key, v);
}

void slayer3d_properties_set_bool(slayer3d_properties *props, const char *key, bool value)
{
    slayer3d_value v;
    SDL_zero(v);
    v.type = SLAYER3D_VALUE_BOOL;
    v.as_bool = value;
    set_value(props, key, v);
}

void slayer3d_properties_set_vec3(slayer3d_properties *props, const char *key, slayer3d_vec3 value)
{
    slayer3d_value v;
    SDL_zero(v);
    v.type = SLAYER3D_VALUE_VEC3;
    v.as_vec3 = value;
    set_value(props, key, v);
}

void slayer3d_properties_set_string(slayer3d_properties *props, const char *key, const char *value)
{
    slayer3d_value v;
    SDL_zero(v);
    v.type = SLAYER3D_VALUE_STRING;
    v.as_string = SDL_strdup(value != NULL ? value : "");
    if (v.as_string == NULL)
        return;
    set_value(props, key, v);
}

void slayer3d_properties_set_color(slayer3d_properties *props, const char *key, slayer3d_color value)
{
    slayer3d_value v;
    SDL_zero(v);
    v.type = SLAYER3D_VALUE_COLOR;
    v.as_color = value;
    set_value(props, key, v);
}

/* ================================================================== */
/* Getters                                                            */
/* ================================================================== */

int slayer3d_properties_get_int(const slayer3d_properties *props, const char *key, int fallback)
{
    const entry *e = lookup(props, key);
    if (e == NULL || e->value.type != SLAYER3D_VALUE_INT)
        return fallback;
    return e->value.as_int;
}

float slayer3d_properties_get_float(const slayer3d_properties *props, const char *key, float fallback)
{
    const entry *e = lookup(props, key);
    if (e == NULL || e->value.type != SLAYER3D_VALUE_FLOAT)
        return fallback;
    return e->value.as_float;
}

bool slayer3d_properties_get_bool(const slayer3d_properties *props, const char *key, bool fallback)
{
    const entry *e = lookup(props, key);
    if (e == NULL || e->value.type != SLAYER3D_VALUE_BOOL)
        return fallback;
    return e->value.as_bool;
}

slayer3d_vec3 slayer3d_properties_get_vec3(const slayer3d_properties *props, const char *key, slayer3d_vec3 fallback)
{
    const entry *e = lookup(props, key);
    if (e == NULL || e->value.type != SLAYER3D_VALUE_VEC3)
        return fallback;
    return e->value.as_vec3;
}

const char *slayer3d_properties_get_string(const slayer3d_properties *props, const char *key, const char *fallback)
{
    const entry *e = lookup(props, key);
    if (e == NULL || e->value.type != SLAYER3D_VALUE_STRING)
        return fallback;
    return e->value.as_string;
}

slayer3d_color slayer3d_properties_get_color(const slayer3d_properties *props, const char *key, slayer3d_color fallback)
{
    const entry *e = lookup(props, key);
    if (e == NULL || e->value.type != SLAYER3D_VALUE_COLOR)
        return fallback;
    return e->value.as_color;
}

/* ================================================================== */
/* Query and mutation                                                 */
/* ================================================================== */

bool slayer3d_properties_has(const slayer3d_properties *props, const char *key)
{
    return lookup(props, key) != NULL;
}

bool slayer3d_properties_rename(slayer3d_properties *props, const char *old_key, const char *new_key)
{
    if (props == NULL || old_key == NULL || new_key == NULL || old_key[0] == '\0' || new_key[0] == '\0' ||
        props->capacity == 0)
    {
        return false;
    }
    if (SDL_strcmp(old_key, new_key) == 0)
        return lookup(props, old_key) != NULL;
    if (lookup(props, new_key) != NULL)
        return false;

    entry *old = find_entry(props->entries, props->capacity, old_key);
    if (old == NULL || old->state != ENTRY_OCCUPIED)
        return false;

    char *new_key_copy = SDL_strdup(new_key);
    if (new_key_copy == NULL)
        return false;

    char *old_key_copy = old->key;
    slayer3d_value moved_value = old->value;
    const int moved_order = old->order;
    old->key = NULL;
    SDL_zero(old->value);
    old->state = ENTRY_TOMBSTONE;
    old->order = 0;
    props->count--;

    entry *dest = find_entry(props->entries, props->capacity, new_key);
    if (dest == NULL || dest->state == ENTRY_OCCUPIED)
    {
        old->key = old_key_copy;
        old->value = moved_value;
        old->state = ENTRY_OCCUPIED;
        old->order = moved_order;
        props->count++;
        SDL_free(new_key_copy);
        return false;
    }

    const bool was_empty = (dest->state == ENTRY_EMPTY);
    dest->key = new_key_copy;
    dest->value = moved_value;
    dest->state = ENTRY_OCCUPIED;
    dest->order = moved_order;
    props->count++;
    if (was_empty)
        props->occupied++;
    SDL_free(old_key_copy);
    props->revision++;
    return true;
}

void slayer3d_properties_remove(slayer3d_properties *props, const char *key)
{
    if (props == NULL || key == NULL || props->capacity == 0)
        return;
    entry *e = find_entry(props->entries, props->capacity, key);
    if (e == NULL || e->state != ENTRY_OCCUPIED)
        return;
    free_value(&e->value);
    SDL_free(e->key);
    e->key = NULL;
    e->state = ENTRY_TOMBSTONE;
    e->order = 0;
    props->count--;
    props->revision++;
    /* occupied stays the same — tombstones count toward load factor. */
}

void slayer3d_properties_clear(slayer3d_properties *props)
{
    if (props == NULL)
        return;
    const bool changed = props->count > 0;
    for (int i = 0; i < props->capacity; ++i)
    {
        if (props->entries[i].state == ENTRY_OCCUPIED)
            free_entry(&props->entries[i]);
        else
            props->entries[i].state = ENTRY_EMPTY;
    }
    props->count = 0;
    props->occupied = 0;
    props->next_order = 0;
    if (changed)
        props->revision++;
}

Uint64 slayer3d_properties_revision(const slayer3d_properties *props)
{
    return props != NULL ? props->revision : 0u;
}

/* ================================================================== */
/* Iteration / introspection                                         */
/* ================================================================== */

int slayer3d_properties_count(const slayer3d_properties *props)
{
    if (props == NULL)
        return 0;
    return props->count;
}

bool slayer3d_properties_get_key_at(const slayer3d_properties *props, int index, const char **out_key,
                                    slayer3d_value_type *out_type)
{
    if (props == NULL || index < 0 || index >= props->count || out_key == NULL)
        return false;

    int previous_order = -1;
    for (int seen = 0; seen <= index; ++seen)
    {
        const entry *best = NULL;
        int best_order = INT_MAX;
        for (int i = 0; i < props->capacity; ++i)
        {
            const entry *candidate = &props->entries[i];
            if (candidate->state != ENTRY_OCCUPIED || candidate->order <= previous_order ||
                candidate->order >= best_order)
            {
                continue;
            }
            best = candidate;
            best_order = candidate->order;
        }
        if (best == NULL)
            return false;
        if (seen == index)
        {
            *out_key = best->key;
            if (out_type != NULL)
                *out_type = best->value.type;
            return true;
        }
        previous_order = best_order;
    }
    return false;
}

const slayer3d_value *slayer3d_properties_get_value(const slayer3d_properties *props, const char *key)
{
    const entry *e = lookup(props, key);
    if (e == NULL)
        return NULL;
    return &e->value;
}
