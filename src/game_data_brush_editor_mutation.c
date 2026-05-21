/**
 * @file game_data_brush_editor_mutation.c
 * @brief Brush-world editor mutation helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static bool editor_brush_world_set_source_path(brush_world_runtime *world_runtime, const char *source_path)
{
    if (world_runtime == NULL || source_path == NULL)
        return world_runtime != NULL;

    char *copy = SDL_strdup(source_path);
    if (copy == NULL)
        return false;
    SDL_free(world_runtime->editor_source_path);
    world_runtime->editor_source_path = copy;
    return true;
}

void editor_brush_world_mark_dirty(brush_world_runtime *world_runtime)
{
    if (world_runtime == NULL)
        return;
    if (world_runtime->editor_revision < SDL_MAX_UINT64)
        world_runtime->editor_revision++;
    world_runtime->editor_dirty = true;
}

static bool editor_brush_world_mark_saved(brush_world_runtime *world_runtime, const char *source_path)
{
    if (world_runtime == NULL)
        return false;
    if (!editor_brush_world_set_source_path(world_runtime, source_path))
        return false;
    world_runtime->editor_saved_revision = world_runtime->editor_revision;
    world_runtime->editor_dirty = false;
    return true;
}

static bool editor_brush_world_name_exists(const brush_world_runtime *world_runtime, const char *brush_name)
{
    if (world_runtime == NULL || brush_name == NULL || brush_name[0] == '\0')
        return false;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[i];
        if (brush->name != NULL && SDL_strcmp(brush->name, brush_name) == 0)
            return true;
    }
    return false;
}

static bool editor_metadata_stable_id_matches(const slayer3d_game_data_editor_metadata *metadata, const char *stable_id)
{
    return metadata != NULL && metadata->stable_id != NULL && stable_id != NULL &&
           SDL_strcmp(metadata->stable_id, stable_id) == 0;
}

static bool editor_brush_world_stable_id_exists(const brush_world_runtime *world_runtime, const char *stable_id)
{
    if (world_runtime == NULL || stable_id == NULL || stable_id[0] == '\0')
        return false;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (editor_metadata_stable_id_matches(&world->editor, stable_id))
        return true;
    for (int i = 0; i < world->material_count; ++i)
    {
        if (editor_metadata_stable_id_matches(&world->materials[i].editor, stable_id))
            return true;
    }
    for (int i = 0; i < world->brush_count; ++i)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[i];
        if (editor_metadata_stable_id_matches(&brush->editor, stable_id))
            return true;
        for (int face_index = 0; face_index < brush->face_count; ++face_index)
        {
            if (editor_metadata_stable_id_matches(&brush->faces[face_index].editor, stable_id))
                return true;
        }
    }
    return false;
}

static bool editor_brush_world_box_stable_ids_available(const brush_world_runtime *world_runtime,
                                                        const char *brush_name);

bool editor_brush_world_generate_brush_name(const brush_world_runtime *world_runtime, char *buffer, size_t buffer_size)
{
    if (world_runtime == NULL || buffer == NULL || buffer_size == 0u)
        return false;
    const char *world_name = world_runtime->desc.name != NULL ? world_runtime->desc.name : "brush_world";
    for (int i = 0; i < 1000000; ++i)
    {
        SDL_snprintf(buffer, buffer_size, "%s.box.%d", world_name, i + 1);
        if (buffer[0] == '\0')
            return false;
        if (!editor_brush_world_name_exists(world_runtime, buffer) &&
            editor_brush_world_box_stable_ids_available(world_runtime, buffer))
        {
            return true;
        }
    }
    return false;
}

static bool editor_brush_world_box_stable_ids_available(const brush_world_runtime *world_runtime,
                                                        const char *brush_name)
{
    if (editor_brush_world_stable_id_exists(world_runtime, brush_name))
        return false;
    static const char *const face_suffixes[] = {"px", "nx", "py", "ny", "pz", "nz"};
    for (size_t i = 0; i < SDL_arraysize(face_suffixes); ++i)
    {
        char stable_id[320];
        SDL_snprintf(stable_id, sizeof(stable_id), "%s.face.%s", brush_name, face_suffixes[i]);
        if (editor_brush_world_stable_id_exists(world_runtime, stable_id))
            return false;
    }
    return true;
}

static int find_editor_brush_material_index(const brush_world_runtime *world_runtime, const char *material_name)
{
    if (world_runtime == NULL || material_name == NULL || material_name[0] == '\0')
        return -1;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->material_count; ++i)
    {
        if (world->materials[i].name != NULL && SDL_strcmp(world->materials[i].name, material_name) == 0)
            return i;
    }
    return -1;
}

static slayer3d_game_data_brush *find_editor_mutable_brush(brush_world_runtime *world_runtime, const char *brush_name)
{
    if (world_runtime == NULL || brush_name == NULL || brush_name[0] == '\0')
        return NULL;
    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        slayer3d_game_data_brush *brush = (slayer3d_game_data_brush *)&world->brushes[i];
        if ((brush->name != NULL && SDL_strcmp(brush->name, brush_name) == 0) ||
            editor_metadata_stable_id_matches(&brush->editor, brush_name))
        {
            return brush;
        }
    }
    return NULL;
}

bool slayer3d_game_data_create_box_brush(slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_create_box_brush_desc *desc, char *out_brush_name,
                                         size_t out_brush_name_size, char *error_buffer, int error_buffer_size)
{
    if (out_brush_name != NULL && out_brush_name_size > 0u)
        out_brush_name[0] = '\0';
    if (runtime == NULL || desc == NULL || desc->world_name == NULL || desc->world_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "box brush creation requires a brush world");
        return false;
    }
    if (desc->material_name == NULL || desc->material_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "box brush creation requires a material");
        return false;
    }
    if (!(desc->min.x < desc->max.x && desc->min.y < desc->max.y && desc->min.z < desc->max.z))
    {
        set_error(error_buffer, error_buffer_size, "box brush bounds must have min < max on all axes");
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, desc->world_name);
    if (world_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world not found");
        return false;
    }
    const int material_index = find_editor_brush_material_index(world_runtime, desc->material_name);
    if (material_index < 0)
    {
        set_error(error_buffer, error_buffer_size, "box brush material not found");
        return false;
    }
    if (!world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "box brush creation requires an editor_brush_sources source model");
        return false;
    }

    char generated_name[256];
    const char *brush_name = desc->brush_name;
    if (brush_name == NULL || brush_name[0] == '\0')
    {
        if (!editor_brush_world_generate_brush_name(world_runtime, generated_name, sizeof(generated_name)))
        {
            set_error(error_buffer, error_buffer_size, "failed to generate box brush name");
            return false;
        }
        brush_name = generated_name;
    }
    else if (editor_brush_world_name_exists(world_runtime, brush_name))
    {
        set_error(error_buffer, error_buffer_size, "box brush name already exists");
        return false;
    }
    if (!editor_brush_world_box_stable_ids_available(world_runtime, brush_name))
    {
        set_error(error_buffer, error_buffer_size, "box brush stable id already exists");
        return false;
    }

    editor_brush_source_box_runtime source_box;
    if (!editor_brush_source_box_from_create_desc(world_runtime, desc, brush_name, &source_box))
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate source box brush");
        return false;
    }
    char rebuild_error[256] = {0};
    if (!editor_brush_world_insert_source_box_at_index(world_runtime, world_runtime->editor_source_box_count,
                                                       &source_box, rebuild_error, sizeof(rebuild_error)))
    {
        free_editor_brush_source_box_runtime(&source_box);
        if (rebuild_error[0] != '\0')
            set_error(error_buffer, error_buffer_size, rebuild_error);
        else
            set_error(error_buffer, error_buffer_size,
                      "failed to rebuild source-backed brush world after box creation");
        return false;
    }
    if (out_brush_name != NULL && out_brush_name_size > 0u)
        SDL_strlcpy(out_brush_name, brush_name, out_brush_name_size);
    free_editor_brush_source_box_runtime(&source_box);
    editor_brush_world_mark_dirty(world_runtime);
    return true;
}

bool slayer3d_game_data_get_brush_world_editor_state(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                                     slayer3d_game_data_brush_world_editor_state *out_state)
{
    if (out_state != NULL)
        SDL_zero(*out_state);
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL || out_state == NULL)
        return false;
    out_state->world_name = world_runtime->desc.name;
    out_state->source_path = world_runtime->editor_source_path;
    out_state->dirty = world_runtime->editor_dirty;
    out_state->revision = world_runtime->editor_revision;
    out_state->saved_revision = world_runtime->editor_saved_revision;
    return true;
}

bool slayer3d_game_data_mark_brush_world_saved(slayer3d_game_data_runtime *runtime, const char *world_name,
                                               const char *source_path, char *error_buffer, int error_buffer_size)
{
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush world not found");
        return false;
    }
    if (!editor_brush_world_mark_saved(world_runtime, source_path))
    {
        set_error(error_buffer, error_buffer_size, "failed to update brush world save state");
        return false;
    }
    return true;
}

bool slayer3d_game_data_resize_brush_face(slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_resize_brush_face_desc *desc, char *error_buffer,
                                          int error_buffer_size)
{
    if (runtime == NULL || desc == NULL || desc->world_name == NULL || desc->world_name[0] == '\0' ||
        desc->brush_name == NULL || desc->brush_name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "invalid brush face resize request");
        return false;
    }
    if (SDL_fabsf(desc->distance) <= 0.0000001f)
        return true;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, desc->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, desc->brush_name);
    if (brush == NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush not found");
        return false;
    }
    if (desc->face_index < 0 || desc->face_index >= brush->face_count)
    {
        set_error(error_buffer, error_buffer_size, "brush face index out of range");
        return false;
    }

    if (world_runtime->editor_has_source_model)
    {
        const slayer3d_vec3 face_normal = slayer3d_vec3_normalize(brush->faces[desc->face_index].normal);
        if (!editor_brush_world_resize_source_box_face(world_runtime, desc->brush_name, face_normal, desc->distance,
                                                       error_buffer, error_buffer_size))
        {
            return false;
        }
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }
    set_error(error_buffer, error_buffer_size, "brush face resize requires an editor_brush_sources source model");
    return false;
}
