/**
 * @file game_data_validation_imports.c
 * @brief Import graph validation, JSON composition, and source-map diagnostics.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

#define GAME_DATA_IMPORT_MAX_DEPTH 16

typedef struct game_data_source_map_entry
{
    char *composed_path;
    char *source_path;
    char *source_json_path;
} game_data_source_map_entry;

struct slayer3d_game_data_source_map
{
    game_data_source_map_entry *entries;
    int count;
    int capacity;
};

typedef struct import_validation_stack
{
    const char *paths[GAME_DATA_IMPORT_MAX_DEPTH];
    int count;
} import_validation_stack;

static bool validate_imports_with_stack(validation_context *ctx, yyjson_val *root, import_validation_stack *stack);
static bool compose_document_into(validation_context *ctx, yyjson_val *root, yyjson_val *sections,
                                  const char *json_path, import_validation_stack *stack, yyjson_mut_doc *doc,
                                  yyjson_mut_val *target, bool is_root);

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static bool source_path_prefix_matches(const char *entry_path, const char *json_path)
{
    if (entry_path == NULL || json_path == NULL)
        return false;
    const size_t length = SDL_strlen(entry_path);
    if (SDL_strncmp(entry_path, json_path, length) != 0)
        return false;
    return json_path[length] == '\0' || json_path[length] == '.' || json_path[length] == '[';
}

static const game_data_source_map_entry *source_map_find_best(const slayer3d_game_data_source_map *map,
                                                              const char *json_path)
{
    const game_data_source_map_entry *best = NULL;
    size_t best_length = 0U;
    for (int i = 0; map != NULL && i < map->count; ++i)
    {
        const game_data_source_map_entry *entry = &map->entries[i];
        if (!source_path_prefix_matches(entry->composed_path, json_path))
            continue;
        const size_t length = SDL_strlen(entry->composed_path);
        if (length > best_length)
        {
            best = entry;
            best_length = length;
        }
    }
    return best;
}

void validation_resolve_source_location(const validation_context *ctx, const char *json_path, char *source_buffer,
                                        size_t source_buffer_size, char *path_buffer, size_t path_buffer_size)
{
    const char *path = json_path != NULL ? json_path : "$";
    const char *source = ctx != NULL && ctx->source_path != NULL ? ctx->source_path : "<game-data>";
    if (ctx != NULL)
    {
        const game_data_source_map_entry *entry = source_map_find_best(ctx->source_map, path);
        if (entry != NULL)
        {
            source = entry->source_path != NULL ? entry->source_path : source;
            const size_t prefix_length = SDL_strlen(entry->composed_path);
            const char *suffix = path + prefix_length;
            SDL_snprintf(path_buffer, path_buffer_size, "%s%s",
                         entry->source_json_path != NULL ? entry->source_json_path : "$", suffix);
            SDL_snprintf(source_buffer, source_buffer_size, "%s", source);
            return;
        }
    }
    SDL_snprintf(source_buffer, source_buffer_size, "%s", source);
    SDL_snprintf(path_buffer, path_buffer_size, "%s", path);
}

static bool path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return SDL_strlen(path) > 2 && path[1] == ':';
}

char *validation_path_dirname(const char *path)
{
    if (path == NULL)
        return NULL;

    const char *last = NULL;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p;
    }

    if (last == NULL)
        return SDL_strdup(".");

    const size_t length = (size_t)(last - path);
    if (length == 0)
        return SDL_strdup(path[0] == '\\' ? "\\" : "/");

    char *dir = (char *)SDL_malloc(length + 1);
    if (dir == NULL)
        return NULL;
    SDL_memcpy(dir, path, length);
    dir[length] = '\0';
    return dir;
}

char *validation_path_join(const char *base_dir, const char *path)
{
    if (path == NULL)
        return NULL;
    if (path_is_absolute(path) || base_dir == NULL || base_dir[0] == '\0')
        return SDL_strdup(path);

    const size_t base_len = SDL_strlen(base_dir);
    const size_t path_len = SDL_strlen(path);
    const bool needs_sep = base_len > 0 && base_dir[base_len - 1] != '/' && base_dir[base_len - 1] != '\\';
    char *joined = (char *)SDL_malloc(base_len + (needs_sep ? 1u : 0u) + path_len + 1u);
    if (joined == NULL)
        return NULL;

    SDL_memcpy(joined, base_dir, base_len);
    size_t offset = base_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, path, path_len);
    joined[offset + path_len] = '\0';
    return joined;
}

const char *validation_asset_path_without_scheme(const char *path)
{
    return path != NULL && SDL_strncmp(path, "asset://", 8) == 0 ? path + 8 : path;
}

static const char *import_path_compare_start(const char *path)
{
    while (path != NULL && path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
        path += 2;
    return path != NULL ? path : "";
}

static char *import_validation_path_join(const char *base_dir, const char *path)
{
    if (path == NULL)
        return NULL;
    if (base_dir == NULL || base_dir[0] == '\0' || SDL_strcmp(base_dir, ".") == 0)
        return SDL_strdup(path);
    return validation_path_join(base_dir, path);
}

static bool import_stack_contains(const import_validation_stack *stack, const char *path)
{
    const char *target = import_path_compare_start(validation_asset_path_without_scheme(path));
    for (int i = 0; stack != NULL && i < stack->count; ++i)
    {
        const char *existing = import_path_compare_start(validation_asset_path_without_scheme(stack->paths[i]));
        if (SDL_strcmp(existing, target) == 0)
            return true;
    }
    return false;
}

static bool import_path_is_safe_relative(const char *path)
{
    if (path == NULL || path[0] == '\0' || path_is_absolute(path) || SDL_strstr(path, "://") != NULL)
        return false;
    if (SDL_strchr(path, '\\') != NULL || SDL_strchr(path, ':') != NULL)
        return false;

    const char *segment = path;
    while (*segment != '\0')
    {
        const char *end = SDL_strchr(segment, '/');
        const size_t length = end != NULL ? (size_t)(end - segment) : SDL_strlen(segment);
        if (length == 0U)
            return false;
        if ((length == 1U && segment[0] == '.') || (length == 2U && segment[0] == '.' && segment[1] == '.'))
        {
            return false;
        }
        if (end == NULL)
            break;
        segment = end + 1;
    }
    return true;
}

static bool import_section_name_allowed(const char *name)
{
    static const char *const allowed[] = {"storage",
                                          "persistence",
                                          "profiles",
                                          "assets",
                                          "scripts",
                                          "input",
                                          "render",
                                          "transitions",
                                          "ui",
                                          "editor",
                                          "factions",
                                          "entities",
                                          "grid_maps",
                                          "grid_pickup_layers",
                                          "sector_levels",
                                          "sector_level_fragments",
                                          "brush_worlds",
                                          "editor_brush_sources",
                                          "editor_player_starts",
                                          "sector_navigation",
                                          "sector_doors",
                                          "sector_platforms",
                                          "actor_archetypes",
                                          "actor_instances",
                                          "actor_pools",
                                          "signals",
                                          "logic",
                                          "adapters",
                                          "network",
                                          "haptics",
                                          "presentation",
                                          "update_phases"};
    for (size_t i = 0; name != NULL && i < SDL_arraysize(allowed); ++i)
    {
        if (SDL_strcmp(name, allowed[i]) == 0)
            return true;
    }
    return false;
}

static bool import_fragment_key_allowed(const char *name)
{
    return SDL_strcmp(name != NULL ? name : "", "schema") == 0 ||
           SDL_strcmp(name != NULL ? name : "", "imports") == 0 || import_section_name_allowed(name);
}

static bool import_sections_contains(yyjson_val *sections, const char *name)
{
    if (!yyjson_is_arr(sections))
        return true;
    for (size_t i = 0; i < yyjson_arr_size(sections); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(sections, i);
        if (yyjson_is_str(entry) && SDL_strcmp(yyjson_get_str(entry), name) == 0)
            return true;
    }
    return false;
}

static bool validate_import_sections(validation_context *ctx, yyjson_val *sections, yyjson_val *fragment_root,
                                     const char *json_path)
{
    if (sections == NULL)
        return true;
    if (!yyjson_is_arr(sections) || yyjson_arr_size(sections) == 0)
        return validation_error(ctx, json_path, "import sections must be a non-empty array when present");

    for (size_t i = 0; i < yyjson_arr_size(sections); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "%s.sections[%zu]", json_path, i);
        yyjson_val *entry = yyjson_arr_get(sections, i);
        if (!yyjson_is_str(entry) || yyjson_get_str(entry)[0] == '\0')
            return validation_error(ctx, path, "import section must be a non-empty string");
        const char *section = yyjson_get_str(entry);
        if (!import_section_name_allowed(section))
            return validation_error(ctx, path, "import section '%s' is not mergeable", section);
        for (size_t prior = 0; prior < i; ++prior)
        {
            yyjson_val *prior_entry = yyjson_arr_get(sections, prior);
            if (yyjson_is_str(prior_entry) && SDL_strcmp(yyjson_get_str(prior_entry), section) == 0)
                return validation_error(ctx, path, "duplicate import section '%s'", section);
        }
        if (obj_get(fragment_root, section) == NULL)
            return validation_error(ctx, path, "import section '%s' is not present in fragment", section);
    }
    return true;
}

static bool validate_fragment_keys(validation_context *ctx, yyjson_val *fragment_root, yyjson_val *sections)
{
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(fragment_root, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        value = yyjson_obj_iter_get_val(key);
        (void)value;
        const char *name = yyjson_get_str(key);
        if (!import_fragment_key_allowed(name))
            return validation_error(ctx, "$", "fragment contains root-only or unsupported section '%s'", name);
        if (import_section_name_allowed(name) && !import_sections_contains(sections, name))
        {
            char path[PATH_BUFFER_SIZE];
            format_path(path, sizeof(path), "$.%s", name);
            return validation_error(ctx, path, "fragment section is not selected by the import filter");
        }
    }
    return true;
}

static bool validate_import_document(validation_context *parent_ctx, const char *asset_path, yyjson_val *sections,
                                     const char *json_path, import_validation_stack *stack)
{
    if (parent_ctx->assets == NULL)
        return validation_error(parent_ctx, json_path, "imports require an asset resolver");
    if (stack->count >= GAME_DATA_IMPORT_MAX_DEPTH)
        return validation_error(parent_ctx, json_path, "import depth exceeds %d", GAME_DATA_IMPORT_MAX_DEPTH);
    if (import_stack_contains(stack, asset_path))
        return validation_error(parent_ctx, json_path, "import cycle detected for '%s'", asset_path);

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(parent_ctx->assets, asset_path, &buffer, asset_error,
                                           (int)sizeof(asset_error)))
    {
        return validation_error(parent_ctx, json_path, "import fragment '%s' does not exist or cannot be read",
                                asset_path);
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&buffer);
    if (doc == NULL)
    {
        return validation_error(parent_ctx, json_path, "import yyjson error %u at byte %llu: %s", err.code,
                                (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
    }

    yyjson_val *fragment_root = yyjson_doc_get_root(doc);
    char *base_dir = validation_path_dirname(validation_asset_path_without_scheme(asset_path));
    validation_context child_ctx = *parent_ctx;
    child_ctx.source_path = asset_path;
    child_ctx.base_dir = base_dir;

    bool ok = true;
    if (!yyjson_is_obj(fragment_root))
    {
        ok = validation_error(&child_ctx, "$", "fragment root must be an object");
    }
    else if (SDL_strcmp(json_string(fragment_root, "schema") != NULL ? json_string(fragment_root, "schema") : "",
                        "slayer3d.fragment.v0") != 0)
    {
        ok = validation_error(&child_ctx, "$.schema", "import fragment must use schema slayer3d.fragment.v0");
    }
    else
    {
        stack->paths[stack->count++] = asset_path;
        ok = validate_import_sections(parent_ctx, sections, fragment_root, json_path) &&
             validate_fragment_keys(&child_ctx, fragment_root, sections) &&
             validate_imports_with_stack(&child_ctx, fragment_root, stack);
        stack->count--;
    }

    SDL_free(base_dir);
    yyjson_doc_free(doc);
    return ok;
}

static bool validate_import_entry(validation_context *ctx, yyjson_val *entry, size_t index,
                                  import_validation_stack *stack)
{
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "$.imports[%zu]", index);
    if (!yyjson_is_obj(entry))
        return validation_error(ctx, path, "import entries must be objects");

    const char *import_path = json_string(entry, "path");
    char path_field[PATH_BUFFER_SIZE];
    format_path(path_field, sizeof(path_field), "%s.path", path);
    if (!import_path_is_safe_relative(import_path))
        return validation_error(ctx, path_field, "import path must be a safe relative path");

    yyjson_val *sections = obj_get(entry, "sections");
    if (sections != NULL && !yyjson_is_arr(sections))
        return validation_error(ctx, path, "import sections must be an array");

    char *resolved = import_validation_path_join(ctx->base_dir, import_path);
    if (resolved == NULL)
        return validation_error(ctx, path_field, "failed to resolve import path '%s'", import_path);

    const bool ok = validate_import_document(ctx, resolved, sections, path, stack);
    SDL_free(resolved);
    return ok;
}

static bool validate_imports_with_stack(validation_context *ctx, yyjson_val *root, import_validation_stack *stack)
{
    yyjson_val *imports = obj_get(root, "imports");
    if (imports == NULL)
        return true;
    if (!yyjson_is_arr(imports))
        return validation_error(ctx, "$.imports", "imports must be an array");
    for (size_t i = 0; i < yyjson_arr_size(imports); ++i)
    {
        if (!validate_import_entry(ctx, yyjson_arr_get(imports, i), i, stack))
            return false;
    }
    return true;
}

bool validate_imports(validation_context *ctx, yyjson_val *root)
{
    import_validation_stack stack;
    SDL_zero(stack);
    stack.paths[stack.count++] =
        ctx->source_path != NULL ? validation_asset_path_without_scheme(ctx->source_path) : "<root>";
    return validate_imports_with_stack(ctx, root, &stack);
}

static bool source_map_reserve(slayer3d_game_data_source_map *map, int required)
{
    if (map == NULL)
        return false;
    if (required <= map->capacity)
        return true;
    int next_capacity = map->capacity < 16 ? 16 : map->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;
    game_data_source_map_entry *entries =
        (game_data_source_map_entry *)SDL_realloc(map->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;
    SDL_memset(entries + map->capacity, 0, (size_t)(next_capacity - map->capacity) * sizeof(*entries));
    map->entries = entries;
    map->capacity = next_capacity;
    return true;
}

static bool source_map_add(validation_context *ctx, const char *composed_path, const char *source_json_path)
{
    if (ctx == NULL || ctx->source_map == NULL || composed_path == NULL || source_json_path == NULL)
        return true;
    slayer3d_game_data_source_map *map = (slayer3d_game_data_source_map *)ctx->source_map;
    if (!source_map_reserve(map, map->count + 1))
        return validation_error(ctx, composed_path, "failed to allocate import source map");
    game_data_source_map_entry *entry = &map->entries[map->count];
    entry->composed_path = SDL_strdup(composed_path);
    entry->source_path = SDL_strdup(ctx->source_path != NULL ? ctx->source_path : "<game-data>");
    entry->source_json_path = SDL_strdup(source_json_path);
    if (entry->composed_path == NULL || entry->source_path == NULL || entry->source_json_path == NULL)
        return validation_error(ctx, composed_path, "failed to allocate import source map entry");
    map->count++;
    return true;
}

void slayer3d_game_data_source_map_destroy(slayer3d_game_data_source_map *map)
{
    if (map == NULL)
        return;
    for (int i = 0; i < map->count; ++i)
    {
        SDL_free(map->entries[i].composed_path);
        SDL_free(map->entries[i].source_path);
        SDL_free(map->entries[i].source_json_path);
    }
    SDL_free(map->entries);
    SDL_free(map);
}

static bool compose_merge_value(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_object,
                                const char *key, yyjson_val *source_value, const char *target_path,
                                const char *source_path);

static bool compose_merge_object_body(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_object,
                                      yyjson_val *source_object, const char *target_path, const char *source_path)
{
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(source_object, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        value = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        char target_child_path[PATH_BUFFER_SIZE];
        char source_child_path[PATH_BUFFER_SIZE];
        format_path(target_child_path, sizeof(target_child_path), "%s.%s", target_path, name);
        format_path(source_child_path, sizeof(source_child_path), "%s.%s", source_path, name);
        if (!compose_merge_value(ctx, doc, target_object, name, value, target_child_path, source_child_path))
            return false;
    }
    return true;
}

static bool compose_append_array(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_array,
                                 yyjson_val *source_array, const char *target_path, const char *source_path)
{
    for (size_t i = 0; i < yyjson_arr_size(source_array); ++i)
    {
        const size_t target_index = yyjson_mut_arr_size(target_array);
        yyjson_mut_val *copy = yyjson_val_mut_copy(doc, yyjson_arr_get(source_array, i));
        if (copy == NULL || !yyjson_mut_arr_append(target_array, copy))
            return validation_error(ctx, target_path, "failed to append imported array item");
        char target_item_path[PATH_BUFFER_SIZE];
        char source_item_path[PATH_BUFFER_SIZE];
        format_path(target_item_path, sizeof(target_item_path), "%s[%zu]", target_path, target_index);
        format_path(source_item_path, sizeof(source_item_path), "%s[%zu]", source_path, i);
        if (!source_map_add(ctx, target_item_path, source_item_path))
            return false;
    }
    return true;
}

static bool compose_merge_existing(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_value,
                                   yyjson_val *source_value, const char *target_path, const char *source_path)
{
    if (yyjson_mut_is_arr(target_value) && yyjson_is_arr(source_value))
        return compose_append_array(ctx, doc, target_value, source_value, target_path, source_path);
    if (yyjson_mut_is_obj(target_value) && yyjson_is_obj(source_value))
        return compose_merge_object_body(ctx, doc, target_value, source_value, target_path, source_path);
    return validation_error(ctx, target_path, "import merge conflict for '%s'", target_path);
}

static bool compose_merge_value(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_object,
                                const char *key, yyjson_val *source_value, const char *target_path,
                                const char *source_path)
{
    yyjson_mut_val *existing = yyjson_mut_obj_get(target_object, key);
    if (existing != NULL)
        return compose_merge_existing(ctx, doc, existing, source_value, target_path, source_path);

    yyjson_mut_val *key_copy = yyjson_mut_strcpy(doc, key);
    if (key_copy == NULL)
        return validation_error(ctx, target_path, "failed to copy imported key");
    if (yyjson_is_arr(source_value))
    {
        yyjson_mut_val *array = yyjson_mut_arr(doc);
        if (array == NULL || !yyjson_mut_obj_add(target_object, key_copy, array) ||
            !source_map_add(ctx, target_path, source_path))
            return validation_error(ctx, target_path, "failed to copy imported array");
        return compose_append_array(ctx, doc, array, source_value, target_path, source_path);
    }
    if (yyjson_is_obj(source_value))
    {
        yyjson_mut_val *object = yyjson_mut_obj(doc);
        if (object == NULL || !yyjson_mut_obj_add(target_object, key_copy, object) ||
            !source_map_add(ctx, target_path, source_path))
            return validation_error(ctx, target_path, "failed to copy imported object");
        return compose_merge_object_body(ctx, doc, object, source_value, target_path, source_path);
    }

    yyjson_mut_val *copy = yyjson_val_mut_copy(doc, source_value);
    if (copy == NULL || !yyjson_mut_obj_add(target_object, key_copy, copy) ||
        !source_map_add(ctx, target_path, source_path))
        return validation_error(ctx, target_path, "failed to copy imported value");
    return true;
}

static bool compose_local_sections(validation_context *ctx, yyjson_val *root, yyjson_val *sections, yyjson_mut_doc *doc,
                                   yyjson_mut_val *target, bool is_root)
{
    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        value = yyjson_obj_iter_get_val(key);
        const char *name = yyjson_get_str(key);
        if (SDL_strcmp(name, "imports") == 0)
            continue;
        if (!is_root && SDL_strcmp(name, "schema") == 0)
            continue;
        if (!is_root && !import_section_name_allowed(name))
            continue;
        if (!is_root && !import_sections_contains(sections, name))
            continue;

        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.%s", name);
        if (!compose_merge_value(ctx, doc, target, name, value, path, path))
            return false;
    }
    return true;
}

static bool compose_import_entry(validation_context *ctx, yyjson_val *entry, size_t index,
                                 import_validation_stack *stack, yyjson_mut_doc *doc, yyjson_mut_val *target)
{
    char path[PATH_BUFFER_SIZE];
    format_path(path, sizeof(path), "$.imports[%zu]", index);
    if (!yyjson_is_obj(entry))
        return validation_error(ctx, path, "import entries must be objects");

    const char *import_path = json_string(entry, "path");
    char path_field[PATH_BUFFER_SIZE];
    format_path(path_field, sizeof(path_field), "%s.path", path);
    if (!import_path_is_safe_relative(import_path))
        return validation_error(ctx, path_field, "import path must be a safe relative path");

    yyjson_val *sections = obj_get(entry, "sections");
    if (sections != NULL && !yyjson_is_arr(sections))
        return validation_error(ctx, path, "import sections must be an array");

    char *resolved = import_validation_path_join(ctx->base_dir, import_path);
    if (resolved == NULL)
        return validation_error(ctx, path_field, "failed to resolve import path '%s'", import_path);
    if (ctx->assets == NULL)
    {
        SDL_free(resolved);
        return validation_error(ctx, path, "imports require an asset resolver");
    }
    if (stack->count >= GAME_DATA_IMPORT_MAX_DEPTH)
    {
        SDL_free(resolved);
        return validation_error(ctx, path, "import depth exceeds %d", GAME_DATA_IMPORT_MAX_DEPTH);
    }
    if (import_stack_contains(stack, resolved))
    {
        const bool ok = validation_error(ctx, path, "import cycle detected for '%s'", resolved);
        SDL_free(resolved);
        return ok;
    }

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(ctx->assets, resolved, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        const bool ok = validation_error(ctx, path, "import fragment '%s' does not exist or cannot be read", resolved);
        SDL_free(resolved);
        return ok;
    }

    yyjson_read_err err;
    yyjson_doc *fragment_doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&buffer);
    if (fragment_doc == NULL)
    {
        const bool ok = validation_error(ctx, path, "import yyjson error %u at byte %llu: %s", err.code,
                                         (unsigned long long)err.pos, err.msg != NULL ? err.msg : "");
        SDL_free(resolved);
        return ok;
    }

    char *base_dir = validation_path_dirname(validation_asset_path_without_scheme(resolved));
    validation_context child_ctx = *ctx;
    child_ctx.source_path = resolved;
    child_ctx.base_dir = base_dir;

    stack->paths[stack->count++] = resolved;
    const bool ok =
        compose_document_into(&child_ctx, yyjson_doc_get_root(fragment_doc), sections, path, stack, doc, target, false);
    stack->count--;

    SDL_free(base_dir);
    yyjson_doc_free(fragment_doc);
    SDL_free(resolved);
    return ok;
}

static bool compose_imports_into(validation_context *ctx, yyjson_val *root, import_validation_stack *stack,
                                 yyjson_mut_doc *doc, yyjson_mut_val *target)
{
    yyjson_val *imports = obj_get(root, "imports");
    if (imports == NULL)
        return true;
    if (!yyjson_is_arr(imports))
        return validation_error(ctx, "$.imports", "imports must be an array");
    for (size_t i = 0; i < yyjson_arr_size(imports); ++i)
    {
        if (!compose_import_entry(ctx, yyjson_arr_get(imports, i), i, stack, doc, target))
            return false;
    }
    return true;
}

static bool compose_document_into(validation_context *ctx, yyjson_val *root, yyjson_val *sections,
                                  const char *json_path, import_validation_stack *stack, yyjson_mut_doc *doc,
                                  yyjson_mut_val *target, bool is_root)
{
    if (!yyjson_is_obj(root))
        return validation_error(ctx, "$", is_root ? "root must be an object" : "fragment root must be an object");

    const char *schema = json_string(root, "schema");
    if (is_root)
    {
        if (SDL_strcmp(schema != NULL ? schema : "", "slayer3d.game.v0") != 0)
            return validation_error(ctx, "$.schema", "unsupported or missing game data schema");
    }
    else
    {
        if (SDL_strcmp(schema != NULL ? schema : "", "slayer3d.fragment.v0") != 0)
            return validation_error(ctx, "$.schema", "import fragment must use schema slayer3d.fragment.v0");
        if (!validate_import_sections(ctx, sections, root, json_path) || !validate_fragment_keys(ctx, root, sections))
            return false;
    }

    return compose_imports_into(ctx, root, stack, doc, target) &&
           compose_local_sections(ctx, root, sections, doc, target, is_root);
}

static const char *compose_mut_string(yyjson_mut_val *object, const char *key)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, key);
    return yyjson_mut_is_str(value) ? yyjson_mut_get_str(value) : NULL;
}

static yyjson_mut_val *compose_find_actor_archetype(yyjson_mut_val *root, const char *name)
{
    yyjson_mut_val *archetypes = yyjson_mut_obj_get(root, "actor_archetypes");
    for (size_t i = 0; name != NULL && yyjson_mut_is_arr(archetypes) && i < yyjson_mut_arr_size(archetypes); ++i)
    {
        yyjson_mut_val *archetype = yyjson_mut_arr_get(archetypes, i);
        const char *archetype_name = compose_mut_string(archetype, "name");
        if (archetype_name != NULL && SDL_strcmp(archetype_name, name) == 0)
            return archetype;
    }
    return NULL;
}

static bool compose_merge_mut_override(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target,
                                       yyjson_mut_val *source, const char *path)
{
    yyjson_mut_obj_iter iter;
    yyjson_mut_obj_iter_init(source, &iter);
    yyjson_mut_val *key;
    while ((key = yyjson_mut_obj_iter_next(&iter)) != NULL)
    {
        yyjson_mut_val *value = yyjson_mut_obj_iter_get_val(key);
        const char *name = yyjson_mut_get_str(key);
        if (name == NULL || SDL_strcmp(name, "archetype") == 0)
            continue;

        yyjson_mut_val *existing = yyjson_mut_obj_get(target, name);
        if (yyjson_mut_is_obj(existing) && yyjson_mut_is_obj(value))
        {
            char child_path[PATH_BUFFER_SIZE];
            format_path(child_path, sizeof(child_path), "%s.%s", path, name);
            if (!compose_merge_mut_override(ctx, doc, existing, value, child_path))
                return false;
            continue;
        }

        yyjson_mut_val *key_copy = yyjson_mut_strcpy(doc, name);
        yyjson_mut_val *value_copy = yyjson_mut_val_mut_copy(doc, value);
        if (key_copy == NULL || value_copy == NULL || !yyjson_mut_obj_put(target, key_copy, value_copy))
            return validation_error(ctx, path, "failed to apply actor instance override '%s'", name);
    }
    return true;
}

static yyjson_mut_val *compose_ensure_entities_array(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *entities = yyjson_mut_obj_get(root, "entities");
    if (entities == NULL)
    {
        entities = yyjson_mut_arr(doc);
        yyjson_mut_val *key = yyjson_mut_strcpy(doc, "entities");
        if (entities == NULL || key == NULL || !yyjson_mut_obj_add(root, key, entities))
        {
            (void)validation_error(ctx, "$.entities", "failed to allocate generated entities array");
            return NULL;
        }
    }
    if (!yyjson_mut_is_arr(entities))
    {
        (void)validation_error(ctx, "$.entities", "entities must be an array");
        return NULL;
    }
    return entities;
}

static bool compose_expand_actor_instances(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *instances = yyjson_mut_obj_get(root, "actor_instances");
    if (instances == NULL)
        return true;
    if (!yyjson_mut_is_arr(instances))
        return validation_error(ctx, "$.actor_instances", "actor_instances must be an array");

    yyjson_mut_val *entities = compose_ensure_entities_array(ctx, doc, root);
    if (entities == NULL)
        return false;

    for (size_t i = 0; i < yyjson_mut_arr_size(instances); ++i)
    {
        yyjson_mut_val *instance = yyjson_mut_arr_get(instances, i);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.actor_instances[%zu]", i);
        if (!yyjson_mut_is_obj(instance))
            return validation_error(ctx, path, "actor instance entries must be objects");

        const char *name = compose_mut_string(instance, "name");
        const char *archetype_name = compose_mut_string(instance, "archetype");
        if (name == NULL || name[0] == '\0')
            return validation_error(ctx, path, "actor instance requires a non-empty name");
        if (archetype_name == NULL || archetype_name[0] == '\0')
            return validation_error(ctx, path, "actor instance requires a non-empty archetype");

        yyjson_mut_val *archetype = compose_find_actor_archetype(root, archetype_name);
        if (archetype == NULL)
            return validation_error(ctx, path, "actor instance references unknown actor archetype '%s'",
                                    archetype_name);

        yyjson_mut_val *entity = yyjson_mut_val_mut_copy(doc, archetype);
        if (entity == NULL || !yyjson_mut_is_obj(entity))
            return validation_error(ctx, path, "failed to copy actor archetype '%s'", archetype_name);
        if (!compose_merge_mut_override(ctx, doc, entity, instance, path))
            return false;

        const size_t target_index = yyjson_mut_arr_size(entities);
        if (!yyjson_mut_arr_append(entities, entity))
            return validation_error(ctx, path, "failed to append generated actor instance");

        char target_path[PATH_BUFFER_SIZE];
        format_path(target_path, sizeof(target_path), "$.entities[%zu]", target_index);
        if (!source_map_add(ctx, target_path, path))
            return false;
    }
    return true;
}

static yyjson_mut_val *compose_find_sector_level(yyjson_mut_val *root, const char *name)
{
    yyjson_mut_val *levels = yyjson_mut_obj_get(root, "sector_levels");
    for (size_t i = 0; name != NULL && yyjson_mut_is_arr(levels) && i < yyjson_mut_arr_size(levels); ++i)
    {
        yyjson_mut_val *level = yyjson_mut_arr_get(levels, i);
        const char *level_name = compose_mut_string(level, "name");
        if (level_name != NULL && SDL_strcmp(level_name, name) == 0)
            return level;
    }
    return NULL;
}

static yyjson_mut_val *compose_ensure_sector_levels_array(validation_context *ctx, yyjson_mut_doc *doc,
                                                          yyjson_mut_val *root)
{
    yyjson_mut_val *levels = yyjson_mut_obj_get(root, "sector_levels");
    if (levels == NULL)
    {
        levels = yyjson_mut_arr(doc);
        yyjson_mut_val *key = yyjson_mut_strcpy(doc, "sector_levels");
        if (levels == NULL || key == NULL || !yyjson_mut_obj_add(root, key, levels))
        {
            (void)validation_error(ctx, "$.sector_levels", "failed to allocate generated sector_levels array");
            return NULL;
        }
    }
    if (!yyjson_mut_is_arr(levels))
    {
        (void)validation_error(ctx, "$.sector_levels", "sector_levels must be an array");
        return NULL;
    }
    return levels;
}

static yyjson_mut_val *compose_create_sector_level(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root,
                                                   const char *name)
{
    yyjson_mut_val *levels = compose_ensure_sector_levels_array(ctx, doc, root);
    yyjson_mut_val *level = yyjson_mut_obj(doc);
    yyjson_mut_val *name_key = yyjson_mut_strcpy(doc, "name");
    yyjson_mut_val *name_value = yyjson_mut_strcpy(doc, name);
    if (levels == NULL || level == NULL || name_key == NULL || name_value == NULL ||
        !yyjson_mut_obj_add(level, name_key, name_value) || !yyjson_mut_arr_append(levels, level))
    {
        (void)validation_error(ctx, "$.sector_levels", "failed to allocate generated sector level '%s'", name);
        return NULL;
    }
    return level;
}

static yyjson_mut_val *compose_ensure_mut_array_property(validation_context *ctx, yyjson_mut_doc *doc,
                                                         yyjson_mut_val *object, const char *key, const char *path)
{
    yyjson_mut_val *array = yyjson_mut_obj_get(object, key);
    if (array == NULL)
    {
        array = yyjson_mut_arr(doc);
        yyjson_mut_val *key_value = yyjson_mut_strcpy(doc, key);
        if (array == NULL || key_value == NULL || !yyjson_mut_obj_add(object, key_value, array))
        {
            (void)validation_error(ctx, path, "failed to allocate generated array '%s'", key);
            return NULL;
        }
    }
    if (!yyjson_mut_is_arr(array))
    {
        (void)validation_error(ctx, path, "generated array '%s' conflicts with a non-array value", key);
        return NULL;
    }
    return array;
}

static bool compose_append_mut_array_items(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *target_array,
                                           yyjson_mut_val *source_array, const char *path)
{
    if (source_array == NULL)
        return true;
    if (!yyjson_mut_is_arr(source_array))
        return validation_error(ctx, path, "sector level fragment arrays must be arrays");
    for (size_t i = 0; i < yyjson_mut_arr_size(source_array); ++i)
    {
        yyjson_mut_val *copy = yyjson_mut_val_mut_copy(doc, yyjson_mut_arr_get(source_array, i));
        if (copy == NULL || !yyjson_mut_arr_append(target_array, copy))
            return validation_error(ctx, path, "failed to append sector level fragment item");
    }
    return true;
}

static bool compose_expand_sector_level_fragments(validation_context *ctx, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *fragments = yyjson_mut_obj_get(root, "sector_level_fragments");
    if (fragments == NULL)
        return true;
    if (!yyjson_mut_is_arr(fragments))
        return validation_error(ctx, "$.sector_level_fragments", "sector_level_fragments must be an array");

    for (size_t i = 0; i < yyjson_mut_arr_size(fragments); ++i)
    {
        yyjson_mut_val *fragment = yyjson_mut_arr_get(fragments, i);
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.sector_level_fragments[%zu]", i);
        if (!yyjson_mut_is_obj(fragment))
            return validation_error(ctx, path, "sector level fragment entries must be objects");

        const char *level_name = compose_mut_string(fragment, "level");
        if (level_name == NULL || level_name[0] == '\0')
            return validation_error(ctx, path, "sector level fragment requires a non-empty level");

        yyjson_mut_val *level = compose_find_sector_level(root, level_name);
        if (level == NULL)
            level = compose_create_sector_level(ctx, doc, root, level_name);
        if (level == NULL)
            return false;

        static const char *const array_keys[] = {"materials", "sectors", "lights"};
        for (size_t key_index = 0; key_index < SDL_arraysize(array_keys); ++key_index)
        {
            const char *key = array_keys[key_index];
            yyjson_mut_val *source = yyjson_mut_obj_get(fragment, key);
            if (source == NULL)
                continue;
            char child_path[PATH_BUFFER_SIZE];
            format_path(child_path, sizeof(child_path), "%s.%s", path, key);
            yyjson_mut_val *target = compose_ensure_mut_array_property(ctx, doc, level, key, child_path);
            if (target == NULL || !compose_append_mut_array_items(ctx, doc, target, source, child_path))
                return false;
        }
    }
    return true;
}

yyjson_doc *slayer3d_game_data_compose_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                             slayer3d_game_data_source_map **out_source_map, char *error_buffer,
                                             int error_buffer_size)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
    if (out_source_map != NULL)
        *out_source_map = NULL;
    if (assets == NULL || asset_path == NULL || asset_path[0] == '\0')
    {
        validation_context ctx = {
            .source_path = asset_path != NULL ? asset_path : "<game-data>",
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "invalid game data composition arguments");
        return NULL;
    }

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(assets, asset_path, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        validation_context ctx = {
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "failed to read game data asset: %s", asset_error);
        return NULL;
    }

    yyjson_read_err err;
    yyjson_doc *source_doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, &err);
    slayer3d_asset_buffer_free(&buffer);
    if (source_doc == NULL)
    {
        validation_context ctx = {
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "yyjson error %u at byte %llu: %s", err.code, (unsigned long long)err.pos,
                               err.msg != NULL ? err.msg : "");
        return NULL;
    }

    char *base_dir = validation_path_dirname(validation_asset_path_without_scheme(asset_path));
    yyjson_mut_doc *mut_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *mut_root = mut_doc != NULL ? yyjson_mut_obj(mut_doc) : NULL;
    slayer3d_game_data_source_map *source_map =
        out_source_map != NULL ? (slayer3d_game_data_source_map *)SDL_calloc(1, sizeof(*source_map)) : NULL;
    if (base_dir == NULL || mut_doc == NULL || mut_root == NULL || (out_source_map != NULL && source_map == NULL))
    {
        slayer3d_game_data_source_map_destroy(source_map);
        yyjson_mut_doc_free(mut_doc);
        yyjson_doc_free(source_doc);
        SDL_free(base_dir);
        validation_context ctx = {
            .source_path = asset_path,
            .error_buffer = error_buffer,
            .error_buffer_size = error_buffer_size,
        };
        (void)validation_error(&ctx, "$", "failed to allocate composed game data document");
        return NULL;
    }

    yyjson_mut_doc_set_root(mut_doc, mut_root);
    validation_context ctx = {
        .source_path = asset_path,
        .base_dir = base_dir,
        .assets = assets,
        .source_map = source_map,
        .error_buffer = error_buffer,
        .error_buffer_size = error_buffer_size,
    };
    import_validation_stack stack;
    SDL_zero(stack);
    stack.paths[stack.count++] = validation_asset_path_without_scheme(asset_path);

    yyjson_doc *composed_doc = NULL;
    if (compose_document_into(&ctx, yyjson_doc_get_root(source_doc), NULL, "$", &stack, mut_doc, mut_root, true) &&
        compose_expand_sector_level_fragments(&ctx, mut_doc, mut_root) &&
        compose_expand_actor_instances(&ctx, mut_doc, mut_root))
    {
        composed_doc = yyjson_mut_doc_imut_copy(mut_doc, NULL);
    }
    if (composed_doc == NULL && (error_buffer == NULL || error_buffer_size <= 0 || error_buffer[0] == '\0'))
        (void)validation_error(&ctx, "$", "failed to compose game data document");

    if (composed_doc != NULL && out_source_map != NULL)
    {
        *out_source_map = source_map;
        source_map = NULL;
    }
    slayer3d_game_data_source_map_destroy(source_map);
    yyjson_mut_doc_free(mut_doc);
    yyjson_doc_free(source_doc);
    SDL_free(base_dir);
    return composed_doc;
}
