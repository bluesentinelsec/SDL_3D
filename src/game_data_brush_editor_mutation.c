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

static bool editor_brush_world_generate_brush_name(const brush_world_runtime *world_runtime, char *buffer,
                                                   size_t buffer_size)
{
    if (world_runtime == NULL || buffer == NULL || buffer_size == 0u)
        return false;
    const char *world_name = world_runtime->desc.name != NULL ? world_runtime->desc.name : "brush_world";
    for (int i = 0; i < 1000000; ++i)
    {
        SDL_snprintf(buffer, buffer_size, "%s.box.%d", world_name, i + 1);
        if (buffer[0] == '\0')
            return false;
        if (!editor_brush_world_name_exists(world_runtime, buffer))
            return true;
    }
    return false;
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
        if (brush->name != NULL && SDL_strcmp(brush->name, brush_name) == 0)
            return brush;
    }
    return NULL;
}

static void free_editor_runtime_brush(slayer3d_game_data_brush *brush)
{
    if (brush == NULL)
        return;
    SDL_free((void *)brush->name);
    for (int tag_index = 0; tag_index < brush->tag_count; ++tag_index)
        SDL_free((void *)brush->tags[tag_index]);
    SDL_free((void *)brush->tags);
    for (int face_index = 0; face_index < brush->face_count; ++face_index)
    {
        slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
        free_editor_metadata(&face->editor);
    }
    SDL_free((void *)brush->faces);
    free_editor_metadata(&brush->editor);
    SDL_zero(*brush);
}

static void init_box_brush_face(slayer3d_game_data_brush_face *face, slayer3d_vec3 normal, float distance,
                                int material_index, const char *material_name)
{
    SDL_zero(*face);
    face->normal = normal;
    face->distance = distance;
    face->material_index = material_index;
    face->material_name = material_name;
    face->uv_scale[0] = 1.0f;
    face->uv_scale[1] = 1.0f;
}

static bool init_editor_box_brush(const brush_world_runtime *world_runtime,
                                  const slayer3d_game_data_create_box_brush_desc *desc, const char *brush_name,
                                  int material_index, slayer3d_game_data_brush *out_brush)
{
    if (world_runtime == NULL || desc == NULL || brush_name == NULL || out_brush == NULL || material_index < 0 ||
        material_index >= world_runtime->desc.material_count)
    {
        return false;
    }

    SDL_zero(*out_brush);
    out_brush->name = SDL_strdup(brush_name);
    out_brush->contents = desc->contents != 0u ? desc->contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    out_brush->face_count = 6;
    out_brush->faces = (slayer3d_game_data_brush_face *)SDL_calloc(6u, sizeof(*out_brush->faces));
    if (out_brush->name == NULL || out_brush->faces == NULL)
    {
        free_editor_runtime_brush(out_brush);
        return false;
    }

    const char *material_name = world_runtime->desc.materials[material_index].name;
    slayer3d_game_data_brush_face *faces = (slayer3d_game_data_brush_face *)out_brush->faces;
    init_box_brush_face(&faces[0], slayer3d_vec3_make(1.0f, 0.0f, 0.0f), desc->max.x, material_index, material_name);
    init_box_brush_face(&faces[1], slayer3d_vec3_make(-1.0f, 0.0f, 0.0f), -desc->min.x, material_index, material_name);
    init_box_brush_face(&faces[2], slayer3d_vec3_make(0.0f, 1.0f, 0.0f), desc->max.y, material_index, material_name);
    init_box_brush_face(&faces[3], slayer3d_vec3_make(0.0f, -1.0f, 0.0f), -desc->min.y, material_index, material_name);
    init_box_brush_face(&faces[4], slayer3d_vec3_make(0.0f, 0.0f, 1.0f), desc->max.z, material_index, material_name);
    init_box_brush_face(&faces[5], slayer3d_vec3_make(0.0f, 0.0f, -1.0f), -desc->min.z, material_index, material_name);
    return true;
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

    slayer3d_game_data_brush new_brush;
    if (!init_editor_box_brush(world_runtime, desc, brush_name, material_index, &new_brush))
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate box brush");
        return false;
    }

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    slayer3d_game_data_brush *old_brushes = (slayer3d_game_data_brush *)world->brushes;
    const int old_count = world->brush_count;
    slayer3d_game_data_brush *brushes =
        (slayer3d_game_data_brush *)SDL_calloc((size_t)old_count + 1u, sizeof(*brushes));
    if (brushes == NULL)
    {
        free_editor_runtime_brush(&new_brush);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush world entries");
        return false;
    }
    if (old_count > 0)
        SDL_memcpy(brushes, old_brushes, sizeof(*brushes) * (size_t)old_count);
    brushes[old_count] = new_brush;
    world->brushes = brushes;
    world->brush_count = old_count + 1;

    char rebuild_error[256] = {0};
    if (!rebuild_brush_world_runtime_artifacts(world_runtime, rebuild_error, sizeof(rebuild_error)))
    {
        world->brushes = old_brushes;
        world->brush_count = old_count;
        free_editor_runtime_brush(&brushes[old_count]);
        SDL_free(brushes);
        (void)rebuild_brush_world_runtime_artifacts(world_runtime, NULL, 0);
        set_errorf(error_buffer, error_buffer_size, "failed to rebuild brush world after box creation%s%s",
                   rebuild_error[0] != '\0' ? ": " : "", rebuild_error[0] != '\0' ? rebuild_error : "");
        return false;
    }

    SDL_free(old_brushes);
    editor_brush_world_mark_dirty(world_runtime);
    if (out_brush_name != NULL && out_brush_name_size > 0u)
        SDL_strlcpy(out_brush_name, brushes[old_count].name != NULL ? brushes[old_count].name : "",
                    out_brush_name_size);
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

static bool editor_brush_bounds_valid(const slayer3d_game_data_brush *brush)
{
    if (brush == NULL || !brush->has_bounds)
        return false;
    const float epsilon = 0.0001f;
    return brush->bounds.max.x - brush->bounds.min.x > epsilon && brush->bounds.max.y - brush->bounds.min.y > epsilon &&
           brush->bounds.max.z - brush->bounds.min.z > epsilon;
}

static bool resize_editor_brush_face_plane(slayer3d_game_data_brush *brush, int face_index, float distance)
{
    if (brush == NULL || face_index < 0 || face_index >= brush->face_count)
        return false;
    slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
    face->distance += distance;
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

    if (!resize_editor_brush_face_plane(brush, desc->face_index, desc->distance))
    {
        set_error(error_buffer, error_buffer_size, "failed to resize brush face");
        return false;
    }

    if (rebuild_brush_world_runtime_artifacts(world_runtime, NULL, 0) && editor_brush_bounds_valid(brush))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    (void)resize_editor_brush_face_plane(brush, desc->face_index, -desc->distance);
    (void)rebuild_brush_world_runtime_artifacts(world_runtime, NULL, 0);
    set_error(error_buffer, error_buffer_size, "brush face resize would create invalid geometry");
    return false;
}
