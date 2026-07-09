/**
 * @file game_data_brush_editor_import.c
 * @brief Editable-level JSON import helpers for editor tooling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <math.h>
#include <stdlib.h>

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
    free_editor_actors_runtime(runtime);
    free_editor_prefabs_runtime(runtime);
    free_editor_connections_runtime(runtime);
    SDL_free(runtime->editor_player_start_source_path);
    runtime->editor_player_start_source_path = NULL;
}

#define EDITOR_IMPORT_GLOBAL_PROPERTY_CAP 64

static bool import_float_near(float a, float b)
{
    return fabsf(a - b) <= 0.001f;
}

static bool import_color_matches(slayer3d_color color, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    return color.r == r && color.g == g && color.b == b && color.a == a;
}

static bool import_map_global_matches_survival_horror(const slayer3d_map_global_state *global,
                                                      bool has_directional_light)
{
    return global != NULL && !has_directional_light && import_color_matches(global->ambient_light, 4, 5, 7, 255) &&
           import_color_matches(global->clear_color, 1, 1, 2, 255) && import_float_near(global->exposure, 0.42f) &&
           global->tonemap != NULL && SDL_strcmp(global->tonemap, "reinhard") == 0 &&
           global->lighting_preview_quality != NULL && SDL_strcmp(global->lighting_preview_quality, "quality") == 0 &&
           global->fog.enabled && global->fog.mode != NULL && SDL_strcmp(global->fog.mode, "exp") == 0;
}

static bool import_map_find_global_directional_light(const slayer3d_map_document *map, slayer3d_map_light *out_light)
{
    if (map == NULL || out_light == NULL)
        return false;

    slayer3d_map_light first_directional;
    SDL_zero(first_directional);
    bool found_directional = false;
    const size_t count = slayer3d_map_get_light_count(map);
    for (size_t i = 0u; i < count; ++i)
    {
        slayer3d_map_light light;
        SDL_zero(light);
        if (!slayer3d_map_get_light(map, i, &light) || light.type == NULL || SDL_strcmp(light.type, "directional") != 0)
            continue;
        if (light.id != NULL && SDL_strcmp(light.id, "global.directional.light") == 0)
        {
            *out_light = light;
            return true;
        }
        if (!found_directional)
        {
            first_directional = light;
            found_directional = true;
        }
    }

    if (found_directional)
        *out_light = first_directional;
    return found_directional;
}

static void import_set_global_color_pair(slayer3d_properties *scene_state, const char *active_key,
                                         const char *pending_key, slayer3d_color color)
{
    slayer3d_properties_set_color(scene_state, active_key, color);
    slayer3d_properties_set_color(scene_state, pending_key, color);
}

static void import_set_global_float_pair(slayer3d_properties *scene_state, const char *active_key,
                                         const char *pending_key, float value)
{
    slayer3d_properties_set_float(scene_state, active_key, value);
    slayer3d_properties_set_float(scene_state, pending_key, value);
}

static void import_set_global_string_pair(slayer3d_properties *scene_state, const char *active_key,
                                          const char *pending_key, const char *value)
{
    slayer3d_properties_set_string(scene_state, active_key, value != NULL ? value : "");
    slayer3d_properties_set_string(scene_state, pending_key, value != NULL ? value : "");
}

static const char *import_global_property_json_to_editor_value(const char *json, size_t json_size, char *buffer,
                                                               size_t buffer_size)
{
    if (buffer != NULL && buffer_size > 0u)
        buffer[0] = '\0';
    if (json == NULL || json_size == 0u || buffer == NULL || buffer_size == 0u)
        return "";

    yyjson_doc *doc = yyjson_read(json, json_size, YYJSON_READ_NOFLAG);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    if (yyjson_is_str(root))
    {
        const char *value = yyjson_get_str(root);
        SDL_snprintf(buffer, buffer_size, "%s", value != NULL ? value : "");
    }
    else
    {
        const size_t copy_size = json_size < buffer_size - 1u ? json_size : buffer_size - 1u;
        SDL_memcpy(buffer, json, copy_size);
        buffer[copy_size] = '\0';
    }
    yyjson_doc_free(doc);
    return buffer;
}

static void import_apply_map_global_properties(slayer3d_properties *scene_state, const slayer3d_map_document *map)
{
    if (scene_state == NULL || map == NULL)
        return;

    for (int i = 0; i < EDITOR_IMPORT_GLOBAL_PROPERTY_CAP; ++i)
    {
        char state_key[96];
        SDL_snprintf(state_key, sizeof(state_key), "editor.global.property.%d.available", i);
        slayer3d_properties_set_bool(scene_state, state_key, false);
        SDL_snprintf(state_key, sizeof(state_key), "editor.global.property.%d.key", i);
        slayer3d_properties_set_string(scene_state, state_key, "");
        SDL_snprintf(state_key, sizeof(state_key), "editor.global.property.%d.value", i);
        slayer3d_properties_set_string(scene_state, state_key, "");
    }

    int imported_count = 0;
    slayer3d_map_global_state global;
    if (!slayer3d_map_get_global_state(map, &global))
    {
        slayer3d_properties_set_int(scene_state, "editor.global.property.count", 0);
        return;
    }

    const size_t property_count = global.property_count;
    for (size_t i = 0u; i < property_count && imported_count < EDITOR_IMPORT_GLOBAL_PROPERTY_CAP; ++i)
    {
        const char *key = slayer3d_map_get_global_property_key(map, i);
        if (key == NULL || key[0] == '\0')
            continue;

        char *json = NULL;
        size_t json_size = 0u;
        char property_error[256];
        property_error[0] = '\0';
        if (!slayer3d_map_get_global_property_json(map, key, &json, &json_size, property_error,
                                                   sizeof(property_error)) ||
            json == NULL)
        {
            continue;
        }

        char value[512];
        char state_key[96];
        SDL_snprintf(state_key, sizeof(state_key), "editor.global.property.%d.available", imported_count);
        slayer3d_properties_set_bool(scene_state, state_key, true);
        SDL_snprintf(state_key, sizeof(state_key), "editor.global.property.%d.key", imported_count);
        slayer3d_properties_set_string(scene_state, state_key, key);
        SDL_snprintf(state_key, sizeof(state_key), "editor.global.property.%d.value", imported_count);
        slayer3d_properties_set_string(
            scene_state, state_key, import_global_property_json_to_editor_value(json, json_size, value, sizeof(value)));
        ++imported_count;
        slayer3d_map_free_string(json);
    }
    slayer3d_properties_set_int(scene_state, "editor.global.property.count", imported_count);
    slayer3d_properties_set_string(scene_state, "editor.global.data.edit.key", "");
    slayer3d_properties_set_string(scene_state, "editor.global.data.edit.value", "");
    slayer3d_properties_set_string(scene_state, "editor.global.data.edit.focus", "");
    slayer3d_properties_set_bool(scene_state, "editor.global.data.edit.replace_on_text", false);
    slayer3d_properties_set_int(scene_state, "editor.global.data.edit.selected_slot", -1);
}

static bool import_reference_is_relative(const char *reference)
{
    if (reference == NULL || reference[0] == '\0')
        return false;
    if (reference[0] == '/' || reference[0] == '\\')
        return false;
    if (SDL_strlen(reference) > 2u && reference[1] == ':')
        return false;
    return SDL_strstr(reference, "://") == NULL;
}

/* Saved maps reference sky images relative to the map file. Resolve them
 * against the map's directory so the editor preview loads the materialized
 * copies saved beside the map. */
static const char *import_resolve_map_reference(const char *reference, const char *map_dir, char *buffer,
                                                size_t buffer_size)
{
    if (!import_reference_is_relative(reference) || map_dir == NULL || map_dir[0] == '\0')
        return reference;
    SDL_snprintf(buffer, buffer_size, "%s/%s", map_dir, reference);
    return buffer;
}

static char *import_map_source_dir(const char *source_path)
{
    if (source_path == NULL || source_path[0] == '\0')
        return NULL;
    const char *last = NULL;
    for (const char *p = source_path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p;
    }
    if (last == NULL)
        return NULL;
    const size_t length = last == source_path ? 1u : (size_t)(last - source_path);
    char *dir = (char *)SDL_malloc(length + 1u);
    if (dir == NULL)
        return NULL;
    SDL_memcpy(dir, source_path, length);
    dir[length] = '\0';
    return dir;
}

static bool import_resolve_owned_map_reference(const char **reference, const char *map_dir, char *error_buffer,
                                               int error_buffer_size)
{
    if (reference == NULL || *reference == NULL || !import_reference_is_relative(*reference) || map_dir == NULL ||
        map_dir[0] == '\0')
    {
        return true;
    }
    char resolved[1024];
    SDL_snprintf(resolved, sizeof(resolved), "%s/%s", map_dir, *reference);
    char *copy = SDL_strdup(resolved);
    if (copy == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate resolved editable map asset path");
        return false;
    }
    SDL_free((void *)*reference);
    *reference = copy;
    return true;
}

static bool import_resolve_owned_map_reference_mutable(char **reference, const char *map_dir, char *error_buffer,
                                                       int error_buffer_size)
{
    if (reference == NULL)
        return true;
    const char *resolved = *reference;
    const bool ok = import_resolve_owned_map_reference(&resolved, map_dir, error_buffer, error_buffer_size);
    *reference = (char *)resolved;
    return ok;
}

static bool import_resolve_property_asset_reference(slayer3d_properties *properties, const char *key,
                                                    const char *map_dir, char *error_buffer, int error_buffer_size)
{
    const slayer3d_value *value = slayer3d_properties_get_value(properties, key);
    if (value == NULL || value->type != SLAYER3D_VALUE_STRING || value->as_string == NULL ||
        !import_reference_is_relative(value->as_string) || map_dir == NULL || map_dir[0] == '\0')
    {
        return true;
    }

    char resolved[1024];
    SDL_snprintf(resolved, sizeof(resolved), "%s/%s", map_dir, value->as_string);
    char *copy = SDL_strdup(resolved);
    if (copy == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate resolved editable map property asset path");
        return false;
    }
    slayer3d_properties_set_string(properties, key, copy);
    SDL_free(copy);
    return true;
}

static bool import_resolve_property_asset_references(slayer3d_properties *properties, const char *map_dir,
                                                     char *error_buffer, int error_buffer_size)
{
    return properties == NULL || (import_resolve_property_asset_reference(properties, "effect_asset", map_dir,
                                                                          error_buffer, error_buffer_size) &&
                                  import_resolve_property_asset_reference(properties, "effect_texture", map_dir,
                                                                          error_buffer, error_buffer_size) &&
                                  import_resolve_property_asset_reference(properties, "effect_sprite", map_dir,
                                                                          error_buffer, error_buffer_size));
}

static bool import_resolve_staged_asset_references(slayer3d_game_data_runtime *staged, const char *source_path,
                                                   char *error_buffer, int error_buffer_size)
{
    char *map_dir = import_map_source_dir(source_path);
    if (map_dir == NULL)
        return true;

    bool ok = true;
    for (int world_index = 0; ok && world_index < staged->brush_world_count; ++world_index)
    {
        slayer3d_game_data_brush_world *world = &staged->brush_worlds[world_index].desc;
        slayer3d_game_data_brush_material *materials = (slayer3d_game_data_brush_material *)(void *)world->materials;
        for (int material_index = 0; ok && material_index < world->material_count; ++material_index)
            ok = import_resolve_owned_map_reference(&materials[material_index].texture, map_dir, error_buffer,
                                                    error_buffer_size);
    }
    for (int actor_index = 0; ok && actor_index < staged->editor_actor_count; ++actor_index)
    {
        ok = import_resolve_owned_map_reference_mutable(&staged->editor_actors[actor_index].model, map_dir,
                                                        error_buffer, error_buffer_size);
        ok = ok && import_resolve_property_asset_references(staged->editor_actors[actor_index].properties, map_dir,
                                                            error_buffer, error_buffer_size);
    }
    for (int prefab_index = 0; ok && prefab_index < staged->editor_prefab_count; ++prefab_index)
    {
        ok = import_resolve_owned_map_reference_mutable(&staged->editor_prefabs[prefab_index].model, map_dir,
                                                        error_buffer, error_buffer_size);
        ok = ok && import_resolve_property_asset_references(staged->editor_prefabs[prefab_index].properties, map_dir,
                                                            error_buffer, error_buffer_size);
    }

    SDL_free(map_dir);
    return ok;
}

/* Restore the saved map's global sky as the runtime sky override so the
 * reopened map previews and re-exports the sky it was saved with. */
static void import_apply_map_sky(slayer3d_game_data_runtime *runtime, const slayer3d_map_document *map,
                                 const char *source_path)
{
    slayer3d_properties *scene_state = runtime->scene_state;
    slayer3d_map_sky sky;
    if (!slayer3d_map_get_sky(map, &sky) || SDL_strcmp(sky.mode, "none") == 0)
    {
        (void)slayer3d_game_data_set_scene_sky_override_json(runtime, NULL);
        slayer3d_properties_set_string(scene_state, "editor.sky.active", "default");
        slayer3d_properties_set_string(scene_state, "editor.sky.mode", "none");
        return;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *obj = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    bool ok = doc != NULL && obj != NULL;
    if (ok)
    {
        yyjson_mut_doc_set_root(doc, obj);
        ok = yyjson_mut_obj_add_strcpy(doc, obj, "mode", sky.mode) &&
             (sky.preset == NULL || yyjson_mut_obj_add_strcpy(doc, obj, "preset", sky.preset)) &&
             yyjson_mut_obj_add_real(doc, obj, "size", sky.size);
    }
    char *map_dir = import_map_source_dir(source_path);
    if (ok && sky.has_faces)
    {
        static const char *const face_keys[6] = {"pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z"};
        for (int i = 0; ok && i < 6; ++i)
        {
            char face_buffer[1024];
            ok = yyjson_mut_obj_add_strcpy(
                doc, obj, face_keys[i],
                import_resolve_map_reference(sky.faces[i], map_dir, face_buffer, sizeof(face_buffer)));
        }
    }
    if (ok && sky.layer_count > 0u)
    {
        yyjson_mut_val *layers = yyjson_mut_arr(doc);
        ok = layers != NULL && yyjson_mut_obj_add_val(doc, obj, "layers", layers);
        for (size_t i = 0; ok && i < sky.layer_count; ++i)
        {
            slayer3d_map_sky_layer layer;
            if (!slayer3d_map_get_sky_layer(map, i, &layer))
                continue;
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_val *scroll = yyjson_mut_arr(doc);
            char texture_buffer[1024];
            ok = entry != NULL && scroll != NULL && yyjson_mut_arr_add_val(layers, entry) &&
                 yyjson_mut_obj_add_strcpy(
                     doc, entry, "texture",
                     import_resolve_map_reference(layer.texture, map_dir, texture_buffer, sizeof(texture_buffer))) &&
                 yyjson_mut_obj_add_val(doc, entry, "scroll", scroll) &&
                 yyjson_mut_arr_add_real(doc, scroll, layer.scroll_x) &&
                 yyjson_mut_arr_add_real(doc, scroll, layer.scroll_y) &&
                 yyjson_mut_obj_add_real(doc, entry, "scale", layer.scale) &&
                 yyjson_mut_obj_add_real(doc, entry, "opacity", layer.opacity) &&
                 yyjson_mut_obj_add_real(doc, entry, "depth", layer.depth);
            if (ok && layer.has_tint)
            {
                yyjson_mut_val *tint = yyjson_mut_arr(doc);
                ok = tint != NULL && yyjson_mut_obj_add_val(doc, entry, "tint", tint) &&
                     yyjson_mut_arr_add_int(doc, tint, layer.tint.r) &&
                     yyjson_mut_arr_add_int(doc, tint, layer.tint.g) && yyjson_mut_arr_add_int(doc, tint, layer.tint.b);
            }
        }
    }

    SDL_free(map_dir);
    char *json = ok ? yyjson_mut_write(doc, 0, NULL) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL || !slayer3d_game_data_set_scene_sky_override_json(runtime, json))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to restore saved map skybox");
        free(json);
        return;
    }
    free(json);

    const char *label = sky.preset != NULL ? sky.preset : "imported";
    slayer3d_properties_set_string(scene_state, "editor.sky.active", label);
    slayer3d_properties_set_string(scene_state, "editor.sky.mode", sky.mode);
}

static void import_apply_map_global_state(slayer3d_game_data_runtime *runtime, const slayer3d_map_document *map,
                                          const char *source_path)
{
    if (runtime == NULL || runtime->scene_state == NULL || map == NULL)
        return;

    slayer3d_map_global_state global;
    if (!slayer3d_map_get_global_state(map, &global))
        return;

    slayer3d_properties *scene_state = runtime->scene_state;
    slayer3d_map_light directional_light;
    SDL_zero(directional_light);
    const bool has_directional_light = import_map_find_global_directional_light(map, &directional_light);
    const bool survival_horror = import_map_global_matches_survival_horror(&global, has_directional_light);

    import_set_global_color_pair(scene_state, "editor.global.ambient_light", "editor.global.pending_ambient_light",
                                 global.ambient_light);
    import_set_global_color_pair(scene_state, "editor.global.clear_color", "editor.global.pending_clear_color",
                                 global.clear_color);
    import_set_global_float_pair(scene_state, "editor.global.exposure", "editor.global.pending_exposure",
                                 global.exposure);
    import_set_global_string_pair(scene_state, "editor.global.tonemap", "editor.global.pending_tonemap",
                                  global.tonemap != NULL ? global.tonemap : "aces");
    import_set_global_string_pair(
        scene_state, "editor.global.lighting_preview_quality", "editor.global.pending_lighting_preview_quality",
        global.lighting_preview_quality != NULL ? global.lighting_preview_quality : "balanced");
    const char *fog_mode = global.fog.enabled && global.fog.mode != NULL ? global.fog.mode : "none";
    import_set_global_string_pair(scene_state, "editor.global.fog", "editor.global.pending_fog", fog_mode);

    if (survival_horror)
    {
        slayer3d_properties_set_string(scene_state, "editor.global.preset", "survival_horror");
        slayer3d_properties_set_string(scene_state, "editor.global.preset.label", "Survival Horror");
        slayer3d_properties_set_string(scene_state, "editor.global.preset.description",
                                       "Very dark base lighting intended for local static and dynamic lights.");
        slayer3d_properties_set_string(scene_state, "editor.global.pending_preset", "survival_horror");
        slayer3d_properties_set_string(scene_state, "editor.global.pending_preset.label", "Survival Horror");
        slayer3d_properties_set_string(scene_state, "editor.global.pending_preset.description",
                                       "Very dark base lighting intended for local static and dynamic lights.");
    }
    else
    {
        slayer3d_properties_set_string(scene_state, "editor.global.preset", "custom");
        slayer3d_properties_set_string(scene_state, "editor.global.preset.label", "Imported Map");
        slayer3d_properties_set_string(scene_state, "editor.global.preset.description",
                                       "Imported map lighting values.");
        slayer3d_properties_set_string(scene_state, "editor.global.pending_preset", "custom");
        slayer3d_properties_set_string(scene_state, "editor.global.pending_preset.label", "Imported Map");
        slayer3d_properties_set_string(scene_state, "editor.global.pending_preset.description",
                                       "Imported map lighting values.");
    }

    const bool directional_enabled = has_directional_light;
    slayer3d_properties_set_bool(scene_state, "editor.global.directional.enabled", directional_enabled);
    slayer3d_properties_set_bool(scene_state, "editor.global.pending_directional.enabled", directional_enabled);
    slayer3d_properties_set_string(scene_state, "editor.global.directional.enabled.label",
                                   directional_enabled ? "on" : "off");
    slayer3d_properties_set_string(scene_state, "editor.global.pending_directional.enabled.label",
                                   directional_enabled ? "on" : "off");
    const char *direction_label = directional_enabled ? "imported" : "off";
    slayer3d_properties_set_string(scene_state, "editor.global.directional.direction.label", direction_label);
    slayer3d_properties_set_string(scene_state, "editor.global.pending_directional.direction.label", direction_label);

    const slayer3d_vec3 direction = directional_enabled && directional_light.has_direction
                                        ? directional_light.direction
                                        : slayer3d_vec3_make(0.0f, -1.0f, 0.0f);
    const slayer3d_color color = directional_enabled && directional_light.has_color
                                     ? directional_light.color
                                     : (slayer3d_color){92, 110, 142, 255};
    const float intensity = directional_enabled && directional_light.has_intensity ? directional_light.intensity : 0.0f;
    slayer3d_properties_set_vec3(scene_state, "editor.global.directional.direction", direction);
    slayer3d_properties_set_vec3(scene_state, "editor.global.pending_directional.direction", direction);
    slayer3d_properties_set_color(scene_state, "editor.global.directional.color", color);
    slayer3d_properties_set_color(scene_state, "editor.global.pending_directional.color", color);
    slayer3d_properties_set_float(scene_state, "editor.global.directional.intensity", intensity);
    slayer3d_properties_set_float(scene_state, "editor.global.pending_directional.intensity", intensity);
    slayer3d_properties_set_string(scene_state, "editor.global.directional.kind",
                                   directional_enabled && directional_light.kind != NULL ? directional_light.kind
                                                                                         : "baked");
    slayer3d_properties_set_string(
        scene_state, "editor.global.directional.shadow_mode",
        directional_enabled && directional_light.shadow_mode != NULL ? directional_light.shadow_mode : "baked");
    slayer3d_properties_set_bool(
        scene_state, "editor.global.directional.casts_shadow",
        directional_enabled && directional_light.has_casts_shadow ? directional_light.casts_shadow : true);

    import_apply_map_global_properties(scene_state, map);
    import_apply_map_sky(runtime, map, source_path);
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
        !load_editor_player_starts(staged, root, error_buffer, error_buffer_size) ||
        !load_editor_prefabs(staged, root, error_buffer, error_buffer_size) ||
        !load_editor_actors(staged, root, error_buffer, error_buffer_size) ||
        !load_editor_connections(staged, root, error_buffer, error_buffer_size))
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
    if (!import_resolve_staged_asset_references(staged, source_path, error_buffer, error_buffer_size))
    {
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

    free_editor_actors_runtime(runtime);
    runtime->editor_actors = staged->editor_actors;
    runtime->editor_actor_count = staged->editor_actor_count;
    runtime->editor_actor_capacity = staged->editor_actor_capacity;
    runtime->editor_actor_revision = staged->editor_actor_revision;
    runtime->editor_actor_dirty = staged->editor_actor_dirty;
    staged->editor_actors = NULL;
    staged->editor_actor_count = 0;
    staged->editor_actor_capacity = 0;

    free_editor_prefabs_runtime(runtime);
    runtime->editor_prefabs = staged->editor_prefabs;
    runtime->editor_prefab_count = staged->editor_prefab_count;
    runtime->editor_prefab_capacity = staged->editor_prefab_capacity;
    runtime->editor_prefab_revision = staged->editor_prefab_revision;
    runtime->editor_prefab_dirty = staged->editor_prefab_dirty;
    staged->editor_prefabs = NULL;
    staged->editor_prefab_count = 0;
    staged->editor_prefab_capacity = 0;

    free_editor_connections_runtime(runtime);
    runtime->editor_connections = staged->editor_connections;
    runtime->editor_connection_count = staged->editor_connection_count;
    runtime->editor_connection_capacity = staged->editor_connection_capacity;
    runtime->editor_connection_revision = staged->editor_connection_revision;
    runtime->editor_connection_dirty = staged->editor_connection_dirty;
    staged->editor_connections = NULL;
    staged->editor_connection_count = 0;
    staged->editor_connection_capacity = 0;

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

bool slayer3d_game_data_load_editable_level_map_json(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                     const void *json, size_t json_size, const char *source_path,
                                                     char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || json == NULL || json_size == 0u)
    {
        set_error(error_buffer, error_buffer_size,
                  "editable level map load requires runtime, world name, and non-empty JSON buffer");
        return false;
    }

    slayer3d_map_document *map = NULL;
    if (!slayer3d_map_load_json((const char *)json, json_size, NULL, &map, error_buffer, error_buffer_size))
        return false;

    yyjson_read_err read_error;
    SDL_zero(read_error);
    yyjson_doc *doc = yyjson_read_opts((char *)json, json_size, 0, NULL, &read_error);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *editor = yyjson_is_obj(root) ? yyjson_obj_get(root, "editor") : NULL;
    yyjson_val *fragment = yyjson_is_obj(editor) ? yyjson_obj_get(editor, "editable_level_fragment") : NULL;
    if (!yyjson_is_obj(fragment))
    {
        yyjson_doc_free(doc);
        slayer3d_map_destroy(map);
        set_error(error_buffer, error_buffer_size,
                  "Slayer3D map must contain editor.editable_level_fragment for editable editor load");
        return false;
    }

    size_t fragment_size = 0u;
    char *fragment_json =
        yyjson_val_write(fragment, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &fragment_size);
    yyjson_doc_free(doc);
    if (fragment_json == NULL)
    {
        slayer3d_map_destroy(map);
        set_error(error_buffer, error_buffer_size, "failed to extract editable level fragment from Slayer3D map");
        return false;
    }

    const bool ok = slayer3d_game_data_load_editable_level_fragment_json(
        runtime, world_name, fragment_json, fragment_size, source_path, error_buffer, error_buffer_size);
    if (ok)
        import_apply_map_global_state(runtime, map, source_path);
    slayer3d_map_destroy(map);
    free(fragment_json);
    return ok;
}

bool slayer3d_game_data_load_editable_level_map_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                     const char *path, char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || path == NULL || path[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size,
                  "editable level map load requires runtime, world name, and non-empty file path");
        return false;
    }

    size_t size = 0u;
    void *bytes = load_editable_fragment_file(path, &size, error_buffer, error_buffer_size);
    if (bytes == NULL)
        return false;

    const bool ok = slayer3d_game_data_load_editable_level_map_json(runtime, world_name, bytes, size, path,
                                                                    error_buffer, error_buffer_size);
    SDL_free(bytes);
    return ok;
}
