/**
 * @file game_data_brush_editor_import.c
 * @brief Editable-level JSON import helpers for editor tooling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

static void *load_editable_fragment_file(const char *path, size_t *out_size, char *error_buffer, int error_buffer_size)
{
    if (out_size != NULL)
        *out_size = 0u;
    if (path == NULL || path[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "editable level load requires a non-empty file path");
        return NULL;
    }

    SDL_IOStream *stream = SDL_IOFromFile(path, "rb");
    if (stream == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "failed to open editable level '%s': %s", path, SDL_GetError());
        return NULL;
    }

    const Sint64 stream_size = SDL_GetIOSize(stream);
    if (stream_size <= 0)
    {
        set_errorf(error_buffer, error_buffer_size, "failed to read editable level '%s': file is empty", path);
        SDL_CloseIO(stream);
        return NULL;
    }
    if ((Uint64)stream_size > (Uint64)SIZE_MAX)
    {
        set_errorf(error_buffer, error_buffer_size, "failed to read editable level '%s': file is too large", path);
        SDL_CloseIO(stream);
        return NULL;
    }

    const size_t size = (size_t)stream_size;
    void *bytes = SDL_malloc(size);
    if (bytes == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate editable level buffer");
        SDL_CloseIO(stream);
        return NULL;
    }

    size_t offset = 0u;
    while (offset < size)
    {
        const size_t read = SDL_ReadIO(stream, (Uint8 *)bytes + offset, size - offset);
        if (read == 0u)
        {
            set_errorf(error_buffer, error_buffer_size, "failed to read editable level '%s': %s", path, SDL_GetError());
            SDL_free(bytes);
            SDL_CloseIO(stream);
            return NULL;
        }
        offset += read;
    }

    if (!SDL_CloseIO(stream))
    {
        set_errorf(error_buffer, error_buffer_size, "failed to close editable level '%s': %s", path, SDL_GetError());
        SDL_free(bytes);
        return NULL;
    }

    if (out_size != NULL)
        *out_size = size;
    return bytes;
}

static bool editable_fragment_schema_valid(yyjson_val *root)
{
    const char *schema = json_string(root, "schema", NULL);
    return yyjson_is_obj(root) && schema != NULL && SDL_strcmp(schema, "slayer3d.fragment.v0") == 0;
}

static int find_loaded_brush_world_index(const slayer3d_game_data_runtime *runtime, const char *world_name)
{
    if (runtime == NULL || world_name == NULL)
        return -1;
    for (int i = 0; i < runtime->brush_world_count; ++i)
    {
        const char *name = runtime->brush_worlds[i].desc.name;
        if (name != NULL && SDL_strcmp(name, world_name) == 0)
            return i;
    }
    return -1;
}

static void free_staged_import_runtime(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->brush_world_count; ++i)
        free_brush_world_runtime(&runtime->brush_worlds[i]);
    SDL_free(runtime->brush_worlds);
    runtime->brush_worlds = NULL;
    runtime->brush_world_count = 0;
    free_editor_player_starts_runtime(runtime);
    SDL_free(runtime->editor_player_start_source_path);
    runtime->editor_player_start_source_path = NULL;
}

bool slayer3d_game_data_load_editable_level_fragment_json(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                          const void *json, size_t json_size, const char *source_path,
                                                          char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || json == NULL || json_size == 0u)
    {
        set_error(error_buffer, error_buffer_size,
                  "editable level JSON load requires runtime, world name, and non-empty JSON buffer");
        return false;
    }

    yyjson_read_err read_error;
    SDL_zero(read_error);
    yyjson_doc *doc = yyjson_read_opts((char *)json, json_size, 0, NULL, &read_error);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    if (!editable_fragment_schema_valid(root))
    {
        set_error(error_buffer, error_buffer_size,
                  "editable level must be a JSON object with schema 'slayer3d.fragment.v0'");
        yyjson_doc_free(doc);
        return false;
    }
    if (!yyjson_is_arr(obj_get(root, "brush_worlds")))
    {
        set_error(error_buffer, error_buffer_size, "editable level requires a brush_worlds array");
        yyjson_doc_free(doc);
        return false;
    }

    slayer3d_game_data_runtime *staged = (slayer3d_game_data_runtime *)SDL_calloc(1u, sizeof(*staged));
    if (staged == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate editable level import staging runtime");
        yyjson_doc_free(doc);
        return false;
    }
    if (!load_brush_worlds(staged, root, error_buffer, error_buffer_size) ||
        !load_editor_player_starts(staged, root, error_buffer, error_buffer_size))
    {
        free_staged_import_runtime(staged);
        SDL_free(staged);
        yyjson_doc_free(doc);
        return false;
    }

    const int staged_index = find_loaded_brush_world_index(staged, world_name);
    if (staged_index < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "editable level does not contain brush world '%s'", world_name);
        free_staged_import_runtime(staged);
        SDL_free(staged);
        yyjson_doc_free(doc);
        return false;
    }
    brush_world_runtime *target = find_brush_world_runtime_mutable(runtime, world_name);
    if (target == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "runtime brush world '%s' not found", world_name);
        free_staged_import_runtime(staged);
        SDL_free(staged);
        yyjson_doc_free(doc);
        return false;
    }
    if (!staged->brush_worlds[staged_index].editor_has_source_model)
    {
        set_errorf(error_buffer, error_buffer_size,
                   "editable level brush world '%s' requires a matching editor_brush_sources source model", world_name);
        free_staged_import_runtime(staged);
        SDL_free(staged);
        yyjson_doc_free(doc);
        return false;
    }
    slayer3d_game_data_editor_brush_source_diagnostics diagnostics;
    if (!slayer3d_game_data_validate_editor_brush_source_model(staged, world_name, 1, &diagnostics, error_buffer,
                                                               error_buffer_size))
    {
        free_staged_import_runtime(staged);
        SDL_free(staged);
        yyjson_doc_free(doc);
        return false;
    }
    if (!diagnostics.structurally_valid)
    {
        set_errorf(error_buffer, error_buffer_size, "editable level brush world '%s' source model is invalid: %s",
                   world_name,
                   diagnostics.first_issue[0] != '\0' ? diagnostics.first_issue : "source integrity check failed");
        free_staged_import_runtime(staged);
        SDL_free(staged);
        yyjson_doc_free(doc);
        return false;
    }

    brush_world_runtime replacement = staged->brush_worlds[staged_index];
    SDL_zero(staged->brush_worlds[staged_index]);
    free_brush_world_runtime(target);
    *target = replacement;
    target->desc.render_model = &target->artifacts.render_model;

    free_editor_player_starts_runtime(runtime);
    runtime->editor_player_starts = staged->editor_player_starts;
    runtime->editor_player_start_count = staged->editor_player_start_count;
    runtime->editor_player_start_capacity = staged->editor_player_start_capacity;
    staged->editor_player_starts = NULL;
    staged->editor_player_start_count = 0;
    staged->editor_player_start_capacity = 0;

    free_staged_import_runtime(staged);
    SDL_free(staged);
    yyjson_doc_free(doc);

    if (source_path != NULL && source_path[0] != '\0' &&
        (!slayer3d_game_data_mark_brush_world_saved(runtime, world_name, source_path, error_buffer,
                                                    error_buffer_size) ||
         !slayer3d_game_data_mark_player_starts_saved(runtime, source_path, error_buffer, error_buffer_size)))
    {
        return false;
    }
    return true;
}

bool slayer3d_game_data_load_editable_level_fragment_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                          const char *path, char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || path == NULL || path[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size,
                  "editable level load requires runtime, world name, and non-empty file path");
        return false;
    }

    size_t size = 0u;
    void *bytes = load_editable_fragment_file(path, &size, error_buffer, error_buffer_size);
    if (bytes == NULL)
        return false;

    const bool ok = slayer3d_game_data_load_editable_level_fragment_json(runtime, world_name, bytes, size, path,
                                                                         error_buffer, error_buffer_size);
    SDL_free(bytes);
    return ok;
}
