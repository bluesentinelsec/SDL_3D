/**
 * @file game_data_brush_source_lifecycle.c
 * @brief Runtime-owned structural brush source lifecycle helpers.
 */

#include "game_data_brush_source_model_internal.h"

#include <SDL3/SDL_stdinc.h>

void free_editor_brush_source_box(editor_brush_source_box_runtime *box)
{
    if (box == NULL)
        return;
    SDL_free(box->stable_id);
    SDL_free(box->name);
    SDL_free(box->prefab);
    SDL_free(box->material);
    for (size_t i = 0; i < SDL_arraysize(box->face_materials); ++i)
        SDL_free(box->face_materials[i]);
    slayer3d_properties_destroy(box->properties);
    SDL_zero(*box);
}

void free_editor_brush_source_box_runtime(editor_brush_source_box_runtime *box)
{
    free_editor_brush_source_box(box);
}

void free_editor_brush_source_model(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    for (int i = 0; i < world_runtime->editor_source_box_count; ++i)
        free_editor_brush_source_box(&world_runtime->editor_source_boxes[i]);
    SDL_free(world_runtime->editor_source_boxes);
    world_runtime->editor_source_boxes = NULL;
    world_runtime->editor_source_box_count = 0;
    world_runtime->editor_source_box_capacity = 0;
    world_runtime->editor_source_meters_per_unit = 0.001f;
    world_runtime->editor_source_snap_units = 1;
    world_runtime->editor_has_source_model = false;
}

bool copy_source_string(char **field, const char *value)
{
    if (field == NULL)
        return false;
    *field = NULL;
    if (value == NULL)
        return true;
    *field = SDL_strdup(value);
    return *field != NULL;
}

bool copy_editor_brush_source_box_runtime(const editor_brush_source_box_runtime *source,
                                          editor_brush_source_box_runtime *dest)
{
    if (source == NULL || dest == NULL)
        return false;
    SDL_zero(*dest);
    if (!copy_source_string(&dest->stable_id, source->stable_id) || !copy_source_string(&dest->name, source->name) ||
        !copy_source_string(&dest->prefab, source->prefab) || !copy_source_string(&dest->material, source->material))
    {
        free_editor_brush_source_box(dest);
        return false;
    }
    for (size_t i = 0; i < SDL_arraysize(dest->face_materials); ++i)
    {
        if (!copy_source_string(&dest->face_materials[i], source->face_materials[i]))
        {
            free_editor_brush_source_box(dest);
            return false;
        }
    }
    dest->visual = source->visual;
    for (size_t i = 0; i < SDL_arraysize(dest->face_visuals); ++i)
        dest->face_visuals[i] = source->face_visuals[i];
    for (int i = 0; i < 3; ++i)
    {
        dest->min[i] = source->min[i];
        dest->max[i] = source->max[i];
    }
    dest->vertex_count = source->vertex_count;
    for (int i = 0; i < source->vertex_count && i < SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY; ++i)
    {
        dest->vertices[i][0] = source->vertices[i][0];
        dest->vertices[i][1] = source->vertices[i][1];
        dest->vertices[i][2] = source->vertices[i][2];
    }
    dest->contents = source->contents;
    dest->hidden = source->hidden;
    dest->locked = source->locked;
    dest->properties = slayer3d_properties_create();
    if (dest->properties == NULL)
    {
        free_editor_brush_source_box(dest);
        return false;
    }
    const int property_count = source->properties != NULL ? slayer3d_properties_count(source->properties) : 0;
    for (int i = 0; i < property_count; ++i)
    {
        const char *key = NULL;
        slayer3d_value_type type = SLAYER3D_VALUE_STRING;
        if (!slayer3d_properties_get_key_at(source->properties, i, &key, &type))
            continue;
        const slayer3d_value *value = slayer3d_properties_get_value(source->properties, key);
        if (key != NULL && value != NULL && !set_property_from_value(dest->properties, key, value))
        {
            free_editor_brush_source_box(dest);
            return false;
        }
    }
    return true;
}
