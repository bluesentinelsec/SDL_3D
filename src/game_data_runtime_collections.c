/**
 * @file game_data_runtime_collections.c
 * @brief Runtime collection storage helpers for authored dynamic-list UI and session data.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static runtime_collection *find_runtime_collection(slayer3d_game_data_runtime *runtime, const char *collection_name)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->collection_count; ++i)
    {
        if (SDL_strcmp(runtime->collections[i].name, collection_name) == 0)
            return &runtime->collections[i];
    }
    return NULL;
}

const runtime_collection *find_runtime_collection_const(const slayer3d_game_data_runtime *runtime,
                                                        const char *collection_name)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->collection_count; ++i)
    {
        if (SDL_strcmp(runtime->collections[i].name, collection_name) == 0)
            return &runtime->collections[i];
    }
    return NULL;
}

static runtime_collection *get_or_create_runtime_collection(slayer3d_game_data_runtime *runtime,
                                                            const char *collection_name)
{
    runtime_collection *existing = find_runtime_collection(runtime, collection_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0')
        return NULL;

    if (runtime->collection_count >= runtime->collection_capacity)
    {
        const int next_capacity = runtime->collection_capacity > 0 ? runtime->collection_capacity * 2 : 4;
        runtime_collection *next = (runtime_collection *)SDL_realloc(
            runtime->collections, (size_t)next_capacity * sizeof(*runtime->collections));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->collection_capacity, 0,
                   (size_t)(next_capacity - runtime->collection_capacity) * sizeof(*runtime->collections));
        runtime->collections = next;
        runtime->collection_capacity = next_capacity;
    }

    runtime_collection *collection = &runtime->collections[runtime->collection_count];
    SDL_zero(*collection);
    collection->name = SDL_strdup(collection_name);
    if (collection->name == NULL)
        return NULL;
    ++runtime->collection_count;
    return collection;
}

static slayer3d_properties *runtime_collection_ensure_row(runtime_collection *collection, int row_index)
{
    if (collection == NULL || row_index < 0)
        return NULL;
    if (row_index > collection->row_count)
        return NULL;
    if (row_index >= collection->row_capacity)
    {
        int next_capacity = collection->row_capacity > 0 ? collection->row_capacity : 4;
        while (next_capacity <= row_index)
            next_capacity *= 2;
        slayer3d_properties **next =
            (slayer3d_properties **)SDL_realloc(collection->rows, (size_t)next_capacity * sizeof(*collection->rows));
        if (next == NULL)
            return NULL;
        SDL_memset(next + collection->row_capacity, 0,
                   (size_t)(next_capacity - collection->row_capacity) * sizeof(*collection->rows));
        collection->rows = next;
        collection->row_capacity = next_capacity;
    }
    if (collection->rows[row_index] == NULL)
    {
        collection->rows[row_index] = slayer3d_properties_create();
        if (collection->rows[row_index] == NULL)
            return NULL;
    }
    if (row_index >= collection->row_count)
        collection->row_count = row_index + 1;
    return collection->rows[row_index];
}

bool runtime_collection_field_to_string(const runtime_collection *collection, int row_index, const char *field_name,
                                        char *buffer, size_t buffer_size)
{
    if (collection == NULL || row_index < 0 || row_index >= collection->row_count || field_name == NULL ||
        field_name[0] == '\0' || buffer == NULL || buffer_size == 0U || collection->rows[row_index] == NULL)
        return false;

    const slayer3d_value *value = slayer3d_properties_get_value(collection->rows[row_index], field_name);
    if (value == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        SDL_snprintf(buffer, buffer_size, "%d", value->as_int);
        return true;
    case SLAYER3D_VALUE_FLOAT:
        SDL_snprintf(buffer, buffer_size, "%.3f", (double)value->as_float);
        return true;
    case SLAYER3D_VALUE_BOOL:
        SDL_strlcpy(buffer, value->as_bool ? "true" : "false", buffer_size);
        return true;
    case SLAYER3D_VALUE_STRING:
        SDL_strlcpy(buffer, value->as_string != NULL ? value->as_string : "", buffer_size);
        return true;
    default:
        return false;
    }
}

bool slayer3d_game_data_runtime_collection_clear(slayer3d_game_data_runtime *runtime, const char *collection_name)
{
    runtime_collection *collection = find_runtime_collection(runtime, collection_name);
    if (collection == NULL)
        return runtime != NULL && collection_name != NULL && collection_name[0] != '\0';

    for (int i = 0; i < collection->row_count; ++i)
    {
        slayer3d_properties_destroy(collection->rows[i]);
        collection->rows[i] = NULL;
    }
    collection->row_count = 0;
    return true;
}

int slayer3d_game_data_runtime_collection_count(const slayer3d_game_data_runtime *runtime, const char *collection_name)
{
    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    return collection != NULL ? collection->row_count : 0;
}

bool slayer3d_game_data_runtime_collection_set_string(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                      int row_index, const char *field_name, const char *value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_string(row, field_name, value != NULL ? value : "");
    return true;
}

bool slayer3d_game_data_runtime_collection_set_int(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                   int row_index, const char *field_name, int value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_int(row, field_name, value);
    return true;
}

bool slayer3d_game_data_runtime_collection_set_float(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                     int row_index, const char *field_name, float value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_float(row, field_name, value);
    return true;
}

bool slayer3d_game_data_runtime_collection_set_bool(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                    int row_index, const char *field_name, bool value)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || row_index < 0 ||
        field_name == NULL || field_name[0] == '\0')
        return false;
    runtime_collection *collection = get_or_create_runtime_collection(runtime, collection_name);
    slayer3d_properties *row = runtime_collection_ensure_row(collection, row_index);
    if (row == NULL)
        return false;
    slayer3d_properties_set_bool(row, field_name, value);
    return true;
}
