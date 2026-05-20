/**
 * @file game_data_brush_artifacts.c
 * @brief Brush-world compile artifact manifest, verification, and layout helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"

#include <stdarg.h>
#include <stdlib.h>

static bool artifact_export_add_vec3(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, slayer3d_vec3 value)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value.x) && yyjson_mut_arr_add_real(doc, arr, value.y) &&
           yyjson_mut_arr_add_real(doc, arr, value.z) && yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool artifact_export_add_int3_values(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, int x, int y,
                                            int z)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    return arr != NULL && yyjson_mut_arr_add_int(doc, arr, x) && yyjson_mut_arr_add_int(doc, arr, y) &&
           yyjson_mut_arr_add_int(doc, arr, z) && yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool artifact_export_add_bounds(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                       slayer3d_bounding_box bounds)
{
    yyjson_mut_val *bounds_obj = yyjson_mut_obj(doc);
    return bounds_obj != NULL && yyjson_mut_obj_add_val(doc, obj, key, bounds_obj) &&
           artifact_export_add_vec3(doc, bounds_obj, "min", bounds.min) &&
           artifact_export_add_vec3(doc, bounds_obj, "max", bounds.max);
}

static bool artifact_export_add_optional_string(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key,
                                                const char *value)
{
    return value == NULL || value[0] == '\0' || yyjson_mut_obj_add_strcpy(doc, obj, key, value);
}

static bool artifact_export_add_compile_policy(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                               const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *policy = yyjson_mut_obj(doc);
    return policy != NULL && yyjson_mut_obj_add_val(doc, root, "policy", policy) &&
           yyjson_mut_obj_add_bool(doc, policy, "hidden_face_culling", world->compile_hidden_face_culling) &&
           yyjson_mut_obj_add_real(doc, policy, "chunk_cell_size_hint", world->compile_chunk_cell_size_hint) &&
           yyjson_mut_obj_add_real(doc, policy, "chunk_cell_size", world->compile_chunk_cell_size) &&
           yyjson_mut_obj_add_real(doc, policy, "visibility_cell_size", world->visibility_cell_size);
}

static bool artifact_brush_is_renderable(const slayer3d_game_data_brush *brush)
{
    if (brush == NULL || (brush->contents & SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY) != 0u)
        return false;
    return (brush->contents & (SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER |
                               SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA)) != 0u;
}

static bool artifact_export_add_source_summary(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                               const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *source = yyjson_mut_obj(doc);
    const char *units = world->units != NULL ? world->units : "meters";
    int solid_brush_count = 0;
    int sky_brush_count = 0;
    int renderable_brush_count = 0;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if ((brush->contents & SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID) != 0u)
            ++solid_brush_count;
        if ((brush->contents & SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY) != 0u)
            ++sky_brush_count;
        if (artifact_brush_is_renderable(brush))
            ++renderable_brush_count;
    }
    const int nonrenderable_brush_count = world->brush_count - renderable_brush_count;
    return source != NULL && yyjson_mut_obj_add_val(doc, root, "source", source) &&
           yyjson_mut_obj_add_strcpy(doc, source, "units", units) &&
           yyjson_mut_obj_add_real(doc, source, "meters_per_unit", world->meters_per_unit) &&
           yyjson_mut_obj_add_int(doc, source, "material_count", world->material_count) &&
           yyjson_mut_obj_add_int(doc, source, "brush_count", world->brush_count) &&
           yyjson_mut_obj_add_int(doc, source, "solid_brush_count", solid_brush_count) &&
           yyjson_mut_obj_add_int(doc, source, "sky_brush_count", sky_brush_count) &&
           yyjson_mut_obj_add_int(doc, source, "renderable_brush_count", renderable_brush_count) &&
           yyjson_mut_obj_add_int(doc, source, "nonrenderable_brush_count", nonrenderable_brush_count) &&
           yyjson_mut_obj_add_bool(doc, source, "has_bounds", world->has_bounds) &&
           (!world->has_bounds || artifact_export_add_bounds(doc, source, "bounds", world->bounds));
}

static bool artifact_export_add_render_face_metadata(yyjson_mut_doc *doc, yyjson_mut_val *render,
                                                     const slayer3d_game_data_brush_world *world);

static bool artifact_export_add_render_summary(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                               const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *render = yyjson_mut_obj(doc);
    if (render == NULL || !yyjson_mut_obj_add_val(doc, root, "render", render) ||
        !yyjson_mut_obj_add_int(doc, render, "face_count", world->compile_face_count) ||
        !yyjson_mut_obj_add_int(doc, render, "rendered_face_count", world->compile_rendered_face_count) ||
        !yyjson_mut_obj_add_int(doc, render, "culled_face_count", world->compile_culled_face_count) ||
        !yyjson_mut_obj_add_int(doc, render, "triangle_count", world->compile_triangle_count) ||
        !yyjson_mut_obj_add_int(doc, render, "invalid_brush_count", world->compile_invalid_brush_count) ||
        !yyjson_mut_obj_add_int(doc, render, "degenerate_face_count", world->compile_degenerate_face_count))
    {
        return false;
    }

    const slayer3d_model *model = world->render_model;
    int vertex_count = 0;
    int index_count = 0;
    for (int mesh_index = 0; model != NULL && mesh_index < model->mesh_count; ++mesh_index)
    {
        vertex_count += model->meshes[mesh_index].vertex_count;
        index_count += model->meshes[mesh_index].index_count;
    }

    return yyjson_mut_obj_add_int(doc, render, "material_count", model != NULL ? model->material_count : 0) &&
           yyjson_mut_obj_add_int(doc, render, "mesh_count", model != NULL ? model->mesh_count : 0) &&
           yyjson_mut_obj_add_int(doc, render, "vertex_count", vertex_count) &&
           yyjson_mut_obj_add_int(doc, render, "index_count", index_count) &&
           artifact_export_add_render_face_metadata(doc, render, world);
}

static bool artifact_export_add_render_face_metadata(yyjson_mut_doc *doc, yyjson_mut_val *render,
                                                     const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *faces = yyjson_mut_arr(doc);
    if (faces == NULL ||
        !yyjson_mut_obj_add_int(doc, render, "face_metadata_count", world->compile_rendered_face_metadata_count) ||
        !yyjson_mut_obj_add_val(doc, render, "faces", faces))
    {
        return false;
    }

    for (int face_index = 0; face_index < world->compile_rendered_face_metadata_count; ++face_index)
    {
        const slayer3d_game_data_brush_compiled_face *face = &world->compile_rendered_faces[face_index];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        if (item == NULL || !yyjson_mut_arr_add_val(faces, item) ||
            !yyjson_mut_obj_add_int(doc, item, "index", face_index) ||
            !yyjson_mut_obj_add_int(doc, item, "brush_index", face->brush_index) ||
            !yyjson_mut_obj_add_int(doc, item, "face_index", face->face_index) ||
            !yyjson_mut_obj_add_int(doc, item, "material_index", face->material_index) ||
            !yyjson_mut_obj_add_int(doc, item, "mesh_index", face->mesh_index) ||
            !yyjson_mut_obj_add_int(doc, item, "first_vertex", face->first_vertex) ||
            !yyjson_mut_obj_add_int(doc, item, "vertex_count", face->vertex_count) ||
            !yyjson_mut_obj_add_int(doc, item, "triangle_count", face->triangle_count) ||
            !artifact_export_add_optional_string(doc, item, "brush", face->brush_name) ||
            !artifact_export_add_optional_string(doc, item, "material", face->material_name) ||
            !artifact_export_add_optional_string(doc, item, "source_brush_stable_id", face->source_brush_stable_id) ||
            !artifact_export_add_optional_string(doc, item, "source_face_stable_id", face->source_face_stable_id))
        {
            return false;
        }
    }
    return true;
}

static bool artifact_export_add_chunks(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                       const slayer3d_game_data_brush_world *world)
{
    yyjson_mut_val *chunks = yyjson_mut_obj(doc);
    yyjson_mut_val *items = yyjson_mut_arr(doc);
    if (chunks == NULL || items == NULL || !yyjson_mut_obj_add_val(doc, root, "chunks", chunks) ||
        !yyjson_mut_obj_add_real(doc, chunks, "cell_size", world->compile_chunk_cell_size) ||
        !yyjson_mut_obj_add_int(doc, chunks, "count", world->compile_chunk_count) ||
        !yyjson_mut_obj_add_int(doc, chunks, "brush_index_count", world->compile_chunk_brush_index_count) ||
        !yyjson_mut_obj_add_val(doc, chunks, "items", items))
    {
        return false;
    }

    for (int chunk_index = 0; chunk_index < world->compile_chunk_count; ++chunk_index)
    {
        const slayer3d_game_data_brush_compile_chunk *chunk = &world->compile_chunks[chunk_index];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        if (item == NULL || !yyjson_mut_arr_add_val(items, item) ||
            !yyjson_mut_obj_add_int(doc, item, "index", chunk_index) ||
            !yyjson_mut_obj_add_int(doc, item, "brush_count", chunk->brush_count) ||
            !yyjson_mut_obj_add_uint(doc, item, "contents_mask", chunk->contents_mask) ||
            !yyjson_mut_obj_add_bool(doc, item, "has_bounds", chunk->has_bounds) ||
            (chunk->has_bounds && !artifact_export_add_bounds(doc, item, "bounds", chunk->bounds)))
        {
            return false;
        }
    }
    return true;
}

static bool artifact_export_add_visibility_grid(yyjson_mut_doc *doc, yyjson_mut_val *root,
                                                const brush_world_compile_artifacts *artifacts)
{
    yyjson_mut_val *grid = yyjson_mut_obj(doc);
    const bool has_grid = artifacts != NULL && artifacts->visibility_grid_solid != NULL &&
                          artifacts->visibility_grid_cell_count > 0 && artifacts->visibility_cell_size > 0.0f;
    return grid != NULL && yyjson_mut_obj_add_val(doc, root, "visibility_grid", grid) &&
           yyjson_mut_obj_add_bool(doc, grid, "present", has_grid) &&
           yyjson_mut_obj_add_real(doc, grid, "cell_size", has_grid ? artifacts->visibility_cell_size : 0.0f) &&
           artifact_export_add_int3_values(doc, grid, "dimensions", has_grid ? artifacts->visibility_grid_dim_x : 0,
                                           has_grid ? artifacts->visibility_grid_dim_y : 0,
                                           has_grid ? artifacts->visibility_grid_dim_z : 0) &&
           yyjson_mut_obj_add_int(doc, grid, "cell_count", has_grid ? artifacts->visibility_grid_cell_count : 0) &&
           (!has_grid || artifact_export_add_bounds(doc, grid, "bounds", artifacts->visibility_grid_bounds));
}

static Uint64 brush_artifact_hash_string(const char *value)
{
    Uint64 hash = 1469598103934665603ull;
    if (value == NULL)
        return hash;
    while (*value != '\0')
    {
        hash ^= (Uint8)*value;
        hash *= 1099511628211ull;
        ++value;
    }
    return hash;
}

static bool brush_artifact_safe_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.';
}

static bool brush_artifact_make_world_key(const char *world_name, char *buffer, size_t buffer_size)
{
    if (world_name == NULL || world_name[0] == '\0' || buffer == NULL || buffer_size == 0u)
        return false;

    const Uint64 name_hash = brush_artifact_hash_string(world_name);
    char suffix[24];
    const int suffix_len = SDL_snprintf(suffix, sizeof(suffix), "-%016llx", (unsigned long long)name_hash);
    if (suffix_len < 0 || (size_t)suffix_len >= sizeof(suffix) || (size_t)suffix_len + 1u >= buffer_size)
        return false;

    const size_t max_prefix = buffer_size - (size_t)suffix_len - 1u;
    size_t offset = 0u;
    bool last_was_separator = false;
    for (const char *cursor = world_name; *cursor != '\0' && offset < max_prefix; ++cursor)
    {
        const char out = brush_artifact_safe_char(*cursor) ? *cursor : '_';
        if (out == '_' && last_was_separator)
            continue;
        buffer[offset++] = out;
        last_was_separator = out == '_';
    }
    while (offset > 0u && buffer[offset - 1u] == '_')
        --offset;
    if (offset == 0u)
        buffer[offset++] = 'w';
    if (offset + (size_t)suffix_len >= buffer_size)
        return false;
    SDL_memcpy(buffer + offset, suffix, (size_t)suffix_len + 1u);
    return true;
}

static bool brush_artifact_copy_trimmed_root(const char *artifact_root, char *buffer, size_t buffer_size,
                                             char *error_buffer, int error_buffer_size)
{
    if (artifact_root == NULL || artifact_root[0] == '\0' || SDL_strstr(artifact_root, "://") != NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush compile artifact layout requires a filesystem root path");
        return false;
    }

    size_t len = SDL_strlen(artifact_root);
    while (len > 1u && (artifact_root[len - 1u] == '/' || artifact_root[len - 1u] == '\\'))
        --len;
    if (len + 1u > buffer_size)
    {
        set_error(error_buffer, error_buffer_size, "brush compile artifact root path is too long");
        return false;
    }
    SDL_memcpy(buffer, artifact_root, len);
    buffer[len] = '\0';
    return true;
}

static bool brush_artifact_snprintf_path(char *buffer, size_t buffer_size, char *error_buffer, int error_buffer_size,
                                         const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = SDL_vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= buffer_size)
    {
        set_error(error_buffer, error_buffer_size, "brush compile artifact layout path is too long");
        return false;
    }
    return true;
}

bool slayer3d_game_data_get_brush_world_compile_artifact_layout(
    const slayer3d_game_data_runtime *runtime, const char *world_name, const char *artifact_root,
    slayer3d_game_data_brush_compile_artifact_layout *out_layout, char *error_buffer, int error_buffer_size)
{
    if (out_layout != NULL)
        SDL_zero(*out_layout);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || out_layout == NULL)
    {
        set_error(error_buffer, error_buffer_size,
                  "brush compile artifact layout requires runtime, world name, and output");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "brush world '%s' not found", world_name);
        return false;
    }

    char root[SLAYER3D_GAME_DATA_BRUSH_COMPILE_ARTIFACT_LAYOUT_PATH_MAX];
    if (!brush_artifact_copy_trimmed_root(artifact_root, root, sizeof(root), error_buffer, error_buffer_size) ||
        !brush_artifact_make_world_key(world_name, out_layout->world_key, sizeof(out_layout->world_key)))
    {
        if (error_buffer != NULL && error_buffer_size > 0 && error_buffer[0] == '\0')
            set_error(error_buffer, error_buffer_size, "failed to build brush compile artifact layout key");
        return false;
    }

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    out_layout->source_hash = slayer3d_game_data_brush_world_compute_source_hash(world);
    out_layout->compile_artifact_hash = world->compile_artifact_hash;
    char source_hash[24];
    char artifact_hash[24];
    SDL_snprintf(source_hash, sizeof(source_hash), "%016llx", (unsigned long long)out_layout->source_hash);
    SDL_snprintf(artifact_hash, sizeof(artifact_hash), "%016llx",
                 (unsigned long long)out_layout->compile_artifact_hash);

    return brush_artifact_snprintf_path(out_layout->directory, sizeof(out_layout->directory), error_buffer,
                                        error_buffer_size, "%s/brush/v0/%s/%s/%s", root, out_layout->world_key,
                                        source_hash, artifact_hash) &&
           brush_artifact_snprintf_path(out_layout->manifest_path, sizeof(out_layout->manifest_path), error_buffer,
                                        error_buffer_size, "%s/manifest.json", out_layout->directory) &&
           brush_artifact_snprintf_path(out_layout->render_payload_path, sizeof(out_layout->render_payload_path),
                                        error_buffer, error_buffer_size, "%s/render.payload.bin",
                                        out_layout->directory) &&
           brush_artifact_snprintf_path(out_layout->collision_payload_path, sizeof(out_layout->collision_payload_path),
                                        error_buffer, error_buffer_size, "%s/collision.payload.bin",
                                        out_layout->directory) &&
           brush_artifact_snprintf_path(out_layout->visibility_payload_path,
                                        sizeof(out_layout->visibility_payload_path), error_buffer, error_buffer_size,
                                        "%s/visibility.payload.bin", out_layout->directory);
}

bool slayer3d_game_data_export_brush_world_compile_artifact_json(const slayer3d_game_data_runtime *runtime,
                                                                 const char *world_name, char **out_json,
                                                                 size_t *out_size, char *error_buffer,
                                                                 int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size,
                  "brush compile artifact export requires runtime, world name, and output");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "brush world '%s' not found", world_name);
        return false;
    }

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    if (doc == NULL || root == NULL)
    {
        yyjson_mut_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush compile artifact document");
        return false;
    }

    yyjson_mut_doc_set_root(doc, root);
    const Uint64 source_hash = slayer3d_game_data_brush_world_compute_source_hash(world);
    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.brush_compile_artifact.v0") &&
              yyjson_mut_obj_add_strcpy(doc, root, "world", world->name != NULL ? world->name : "") &&
              yyjson_mut_obj_add_uint(doc, root, "source_hash", source_hash) &&
              yyjson_mut_obj_add_uint(doc, root, "compile_artifact_hash", world->compile_artifact_hash) &&
              artifact_export_add_compile_policy(doc, root, world) &&
              artifact_export_add_source_summary(doc, root, world) &&
              artifact_export_add_render_summary(doc, root, world) && artifact_export_add_chunks(doc, root, world) &&
              artifact_export_add_visibility_grid(doc, root, &world_runtime->artifacts);
    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write brush compile artifact JSON");
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate brush compile artifact JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static bool artifact_json_real_matches(yyjson_val *obj, const char *key, float expected)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    return yyjson_is_num(value) && SDL_fabsf((float)yyjson_get_num(value) - expected) <= 0.0001f;
}

bool slayer3d_game_data_verify_brush_world_compile_artifact_json(
    const slayer3d_game_data_runtime *runtime, const char *world_name, const char *json, size_t json_size,
    slayer3d_game_data_brush_compile_artifact_status *out_status, char *error_buffer, int error_buffer_size)
{
    if (out_status != NULL)
        SDL_zero(*out_status);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || json == NULL || out_status == NULL)
    {
        set_error(error_buffer, error_buffer_size,
                  "brush compile artifact verification requires runtime, world name, JSON, and status output");
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "brush world '%s' not found", world_name);
        return false;
    }

    const size_t parse_size = json_size > 0u ? json_size : SDL_strlen(json);
    yyjson_doc *doc = yyjson_read(json, parse_size, YYJSON_READ_NOFLAG);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    if (doc == NULL || !yyjson_is_obj(root))
    {
        yyjson_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to parse brush compile artifact JSON");
        return false;
    }

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    out_status->expected_source_hash = slayer3d_game_data_brush_world_compute_source_hash(world);
    out_status->expected_compile_artifact_hash = world->compile_artifact_hash;
    out_status->artifact_source_hash = yyjson_get_uint(yyjson_obj_get(root, "source_hash"));
    out_status->artifact_compile_artifact_hash = yyjson_get_uint(yyjson_obj_get(root, "compile_artifact_hash"));
    const char *schema = yyjson_get_str(yyjson_obj_get(root, "schema"));
    const char *artifact_world = yyjson_get_str(yyjson_obj_get(root, "world"));
    out_status->schema_matches = schema != NULL && SDL_strcmp(schema, "slayer3d.brush_compile_artifact.v0") == 0;
    out_status->world_matches = artifact_world != NULL && SDL_strcmp(artifact_world, world_name) == 0;
    out_status->source_hash_matches = out_status->artifact_source_hash == out_status->expected_source_hash;
    out_status->compile_artifact_hash_matches =
        out_status->artifact_compile_artifact_hash == out_status->expected_compile_artifact_hash;

    yyjson_val *policy = yyjson_obj_get(root, "policy");
    yyjson_val *hidden_face_culling = yyjson_is_obj(policy) ? yyjson_obj_get(policy, "hidden_face_culling") : NULL;
    out_status->policy_matches =
        yyjson_is_obj(policy) && yyjson_is_bool(hidden_face_culling) &&
        yyjson_get_bool(hidden_face_culling) == world->compile_hidden_face_culling &&
        artifact_json_real_matches(policy, "chunk_cell_size_hint", world->compile_chunk_cell_size_hint) &&
        artifact_json_real_matches(policy, "chunk_cell_size", world->compile_chunk_cell_size) &&
        artifact_json_real_matches(policy, "visibility_cell_size", world->visibility_cell_size);

    out_status->fresh = out_status->schema_matches && out_status->world_matches && out_status->source_hash_matches &&
                        out_status->policy_matches && out_status->compile_artifact_hash_matches;
    yyjson_doc_free(doc);
    return true;
}

bool slayer3d_game_data_verify_brush_world_compile_artifact_file(
    const slayer3d_game_data_runtime *runtime, const char *world_name, const char *path,
    slayer3d_game_data_brush_compile_artifact_status *out_status, char *error_buffer, int error_buffer_size)
{
    if (out_status != NULL)
        SDL_zero(*out_status);
    if (path == NULL || path[0] == '\0' || SDL_strstr(path, "://") != NULL)
    {
        set_error(error_buffer, error_buffer_size, "brush compile artifact verification requires a filesystem path");
        return false;
    }

    size_t size = 0u;
    char *json = (char *)SDL_LoadFile(path, &size);
    if (json == NULL || size == 0u)
    {
        SDL_free(json);
        set_errorf(error_buffer, error_buffer_size, "failed to read brush compile artifact file '%s'", path);
        return false;
    }

    const bool ok = slayer3d_game_data_verify_brush_world_compile_artifact_json(
        runtime, world_name, json, size, out_status, error_buffer, error_buffer_size);
    SDL_free(json);
    return ok;
}

bool slayer3d_game_data_save_brush_world_compile_artifact_file(const slayer3d_game_data_runtime *runtime,
                                                               const char *world_name, const char *path,
                                                               size_t *out_size, char *error_buffer,
                                                               int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0u;

    char *json = NULL;
    size_t size = 0u;
    if (!slayer3d_game_data_export_brush_world_compile_artifact_json(runtime, world_name, &json, &size, error_buffer,
                                                                     error_buffer_size))
    {
        return false;
    }

    const bool ok =
        editor_save_bytes_atomic(path, json, size, "brush compile artifact manifest", error_buffer, error_buffer_size);
    SDL_free(json);
    if (!ok)
        return false;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

bool slayer3d_game_data_save_brush_world_compile_artifact_layout(
    const slayer3d_game_data_runtime *runtime, const char *world_name, const char *artifact_root,
    slayer3d_game_data_brush_compile_artifact_layout *out_layout, size_t *out_size, char *error_buffer,
    int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0u;

    slayer3d_game_data_brush_compile_artifact_layout layout;
    if (!slayer3d_game_data_get_brush_world_compile_artifact_layout(runtime, world_name, artifact_root, &layout,
                                                                    error_buffer, error_buffer_size))
    {
        if (out_layout != NULL)
            SDL_zero(*out_layout);
        return false;
    }

    if (out_layout != NULL)
        *out_layout = layout;
    return slayer3d_game_data_save_brush_world_compile_artifact_file(runtime, world_name, layout.manifest_path,
                                                                     out_size, error_buffer, error_buffer_size);
}
