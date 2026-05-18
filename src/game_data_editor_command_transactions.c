/**
 * @file game_data_editor_command_transactions.c
 * @brief Editor command commit, undo, redo, and transaction mutation helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/math.h"

static int editor_command_history_undo_count(const editor_command_history_state *history)
{
    return history != NULL ? history->cursor : 0;
}

static int editor_command_history_redo_count(const editor_command_history_state *history)
{
    return history != NULL ? history->count - history->cursor : 0;
}

static slayer3d_properties *create_editor_transaction_payload(const slayer3d_game_data_runtime *runtime,
                                                              const char *event, bool valid,
                                                              const editor_command_transaction_entry *entry,
                                                              const char *message)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    const editor_command_history_state *history = runtime != NULL ? &runtime->editor_command_history : NULL;
    slayer3d_properties_set_bool(payload, "editor_transaction_valid", valid);
    slayer3d_properties_set_string(payload, "editor_transaction_event", event != NULL ? event : "");
    slayer3d_properties_set_string(payload, "editor_transaction_message", message != NULL ? message : "");
    slayer3d_properties_set_int(payload, "editor_transaction_undo_count", editor_command_history_undo_count(history));
    slayer3d_properties_set_int(payload, "editor_transaction_redo_count", editor_command_history_redo_count(history));

    if (entry != NULL)
    {
        char id_text[32];
        SDL_snprintf(id_text, sizeof(id_text), "%d", entry->id);
        slayer3d_properties_set_int(payload, "editor_transaction_id", entry->id);
        slayer3d_properties_set_string(payload, "editor_transaction_id_text", id_text);
        slayer3d_properties_set_string(payload, "editor_command", entry->command != NULL ? entry->command : "");
        slayer3d_properties_set_string(payload, "editor_command_target", entry->target != NULL ? entry->target : "");
        slayer3d_properties_set_string(payload, "editor_transaction_scene", entry->scene != NULL ? entry->scene : "");
        slayer3d_properties_set_string(payload, "editor_transaction_world",
                                       entry->world_name != NULL ? entry->world_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_element",
                                       entry->element_name != NULL ? entry->element_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_material",
                                       entry->material_name != NULL ? entry->material_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_previous_material",
                                       entry->previous_material_name != NULL ? entry->previous_material_name : "");
        slayer3d_properties_set_int(payload, "editor_transaction_face_index", entry->face_index);
        slayer3d_properties_set_vec3(payload, "editor_transaction_offset", entry->offset);
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_min",
                                     entry->has_bounds ? entry->bounds.min : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_max",
                                     entry->has_bounds ? entry->bounds.max : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
    else
    {
        slayer3d_properties_set_int(payload, "editor_transaction_id", -1);
        slayer3d_properties_set_string(payload, "editor_transaction_id_text", "");
        slayer3d_properties_set_string(payload, "editor_command", "");
        slayer3d_properties_set_string(payload, "editor_command_target", "");
        slayer3d_properties_set_string(payload, "editor_transaction_scene", "");
        slayer3d_properties_set_string(payload, "editor_transaction_world", "");
        slayer3d_properties_set_string(payload, "editor_transaction_element", "");
        slayer3d_properties_set_string(payload, "editor_transaction_material", "");
        slayer3d_properties_set_string(payload, "editor_transaction_previous_material", "");
        slayer3d_properties_set_int(payload, "editor_transaction_face_index", -1);
        slayer3d_properties_set_vec3(payload, "editor_transaction_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(payload, "editor_transaction_bounds_max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
    return payload;
}

static void publish_editor_transaction(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, const char *event,
                                       bool valid, const editor_command_transaction_entry *entry, const char *message)
{
    if (runtime == NULL || outputs == NULL)
        return;

    const editor_command_history_state *history = &runtime->editor_command_history;
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", valid);
    editor_set_string_output(scene_state, outputs, "event_key", event != NULL ? event : "");
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_int_output(scene_state, outputs, "transaction_id_key", valid && entry != NULL ? entry->id : -1);
    editor_set_int_output(scene_state, outputs, "undo_count_key", editor_command_history_undo_count(history));
    editor_set_int_output(scene_state, outputs, "redo_count_key", editor_command_history_redo_count(history));
    editor_set_string_output(scene_state, outputs, "command_key",
                             valid && entry != NULL && entry->command != NULL ? entry->command : "");
    editor_set_string_output(scene_state, outputs, "target_key",
                             valid && entry != NULL && entry->target != NULL ? entry->target : "");
    editor_set_string_output(scene_state, outputs, "world_key",
                             valid && entry != NULL && entry->world_name != NULL ? entry->world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key",
                             valid && entry != NULL && entry->element_name != NULL ? entry->element_name : "");
    editor_set_int_output(scene_state, outputs, "face_index_key", valid && entry != NULL ? entry->face_index : -1);
    editor_set_vec3_output(scene_state, outputs, "bounds_min_key",
                           valid && entry != NULL && entry->has_bounds ? entry->bounds.min
                                                                       : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "bounds_max_key",
                           valid && entry != NULL && entry->has_bounds ? entry->bounds.max
                                                                       : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    (void)publish_editor_brush_world_status(runtime, outputs, valid && entry != NULL ? entry->world_name : NULL, NULL,
                                            false);
}

static bool run_editor_transaction_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                                const char *event, bool valid,
                                                const editor_command_transaction_entry *entry, const char *message)
{
    if (actions == NULL)
        return true;
    slayer3d_properties *payload = create_editor_transaction_payload(runtime, event, valid, entry, message);
    if (payload == NULL)
        return false;
    const bool ok = execute_optional_action_array(runtime, actions, payload);
    slayer3d_properties_destroy(payload);
    return ok;
}

static void format_editor_transaction_message(const slayer3d_game_data_runtime *runtime, const char *event, bool valid,
                                              const editor_command_transaction_entry *entry, const char *format,
                                              char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    SDL_strlcpy(buffer, format != NULL ? format : "", buffer_size);
    slayer3d_properties *payload = create_editor_transaction_payload(runtime, event, valid, entry, buffer);
    if (payload != NULL)
    {
        (void)format_payload_string(payload, format != NULL ? format : "", buffer, buffer_size);
        slayer3d_properties_destroy(payload);
    }
}

static bool copy_editor_metadata_snapshot(const slayer3d_game_data_editor_metadata *source,
                                          slayer3d_game_data_editor_metadata *dest)
{
    if (dest == NULL)
        return false;
    SDL_zero(*dest);
    if (source == NULL)
        return true;

#define COPY_EDITOR_METADATA_STRING(field)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (source->field != NULL)                                                                                     \
        {                                                                                                              \
            dest->field = SDL_strdup(source->field);                                                                   \
            if (dest->field == NULL)                                                                                   \
                goto fail;                                                                                             \
        }                                                                                                              \
    } while (0)

    COPY_EDITOR_METADATA_STRING(stable_id);
    COPY_EDITOR_METADATA_STRING(display_name);
    COPY_EDITOR_METADATA_STRING(description);
    COPY_EDITOR_METADATA_STRING(category);
    COPY_EDITOR_METADATA_STRING(group);
    COPY_EDITOR_METADATA_STRING(prefab);
    COPY_EDITOR_METADATA_STRING(archetype);
    COPY_EDITOR_METADATA_STRING(icon);
    COPY_EDITOR_METADATA_STRING(preview_asset);
#undef COPY_EDITOR_METADATA_STRING

    dest->tag_count = source->tag_count;
    if (dest->tag_count > 0)
    {
        const char **tags = (const char **)SDL_calloc((size_t)dest->tag_count, sizeof(*tags));
        if (tags == NULL)
            goto fail;
        dest->tags = tags;
        for (int i = 0; i < dest->tag_count; ++i)
        {
            tags[i] = source->tags[i] != NULL ? SDL_strdup(source->tags[i]) : NULL;
            if (source->tags[i] != NULL && tags[i] == NULL)
                goto fail;
        }
    }
    dest->has_snap_grid = source->has_snap_grid;
    dest->snap_grid = source->snap_grid;
    dest->snap_rotation_degrees = source->snap_rotation_degrees;
    dest->snap_align_to_floor = source->snap_align_to_floor;
    return true;

fail:
    free_editor_metadata(dest);
    return false;
}

static void free_editor_runtime_brush_copy(slayer3d_game_data_brush *brush)
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

static bool copy_editor_brush_snapshot(const slayer3d_game_data_brush *source, slayer3d_game_data_brush *dest)
{
    if (source == NULL || dest == NULL)
        return false;
    SDL_zero(*dest);
    *dest = *source;
    dest->name = source->name != NULL ? SDL_strdup(source->name) : NULL;
    dest->tags = NULL;
    dest->faces = NULL;
    SDL_zero(dest->editor);
    if (source->name != NULL && dest->name == NULL)
        goto fail;
    if (!copy_editor_metadata_snapshot(&source->editor, &dest->editor))
        goto fail;

    if (source->tag_count > 0)
    {
        const char **tags = (const char **)SDL_calloc((size_t)source->tag_count, sizeof(*tags));
        if (tags == NULL)
            goto fail;
        dest->tags = tags;
        for (int i = 0; i < source->tag_count; ++i)
        {
            tags[i] = source->tags[i] != NULL ? SDL_strdup(source->tags[i]) : NULL;
            if (source->tags[i] != NULL && tags[i] == NULL)
                goto fail;
        }
    }

    if (source->face_count > 0)
    {
        slayer3d_game_data_brush_face *faces =
            (slayer3d_game_data_brush_face *)SDL_calloc((size_t)source->face_count, sizeof(*faces));
        if (faces == NULL)
            goto fail;
        dest->faces = faces;
        for (int i = 0; i < source->face_count; ++i)
        {
            faces[i] = source->faces[i];
            SDL_zero(faces[i].editor);
            if (!copy_editor_metadata_snapshot(&source->faces[i].editor, &faces[i].editor))
                goto fail;
        }
    }
    return true;

fail:
    free_editor_runtime_brush_copy(dest);
    return false;
}

static void free_editor_command_transaction_entry(editor_command_transaction_entry *entry)
{
    if (entry == NULL)
        return;
    SDL_free((void *)entry->scene);
    SDL_free((void *)entry->command);
    SDL_free((void *)entry->target);
    SDL_free((void *)entry->world_name);
    SDL_free((void *)entry->element_name);
    SDL_free((void *)entry->material_name);
    SDL_free((void *)entry->previous_material_name);
    if (entry->has_brush_snapshot)
        free_editor_runtime_brush_copy(&entry->brush_snapshot);
    SDL_zero(*entry);
}

static bool copy_editor_transaction_string(const char *source, const char **dest)
{
    if (dest == NULL)
        return false;
    *dest = NULL;
    if (source == NULL)
        return true;
    *dest = SDL_strdup(source);
    return *dest != NULL;
}

static bool copy_editor_transaction_strings(editor_command_transaction_entry *entry,
                                            const editor_command_preview_state *preview)
{
    if (entry == NULL || preview == NULL)
        return false;
    return copy_editor_transaction_string(preview->scene, &entry->scene) &&
           copy_editor_transaction_string(preview->command, &entry->command) &&
           copy_editor_transaction_string(preview->target, &entry->target) &&
           copy_editor_transaction_string(preview->world_name, &entry->world_name) &&
           copy_editor_transaction_string(preview->element_name, &entry->element_name) &&
           copy_editor_transaction_string(preview->material_name, &entry->material_name) &&
           copy_editor_transaction_string(preview->previous_material_name, &entry->previous_material_name);
}

void free_editor_command_history(editor_command_history_state *history)
{
    if (history == NULL)
        return;
    for (int i = 0; i < history->count; ++i)
        free_editor_command_transaction_entry(&history->entries[i]);
    SDL_zero(*history);
}

static editor_command_transaction_entry *editor_command_history_append(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return NULL;
    editor_command_history_state *history = &runtime->editor_command_history;
    for (int i = history->cursor; i < history->count; ++i)
        free_editor_command_transaction_entry(&history->entries[i]);
    if (history->cursor < history->count)
        history->count = history->cursor;
    if (history->count >= SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY)
    {
        free_editor_command_transaction_entry(&history->entries[0]);
        SDL_memmove(&history->entries[0], &history->entries[1],
                    sizeof(history->entries[0]) * (SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY - 1));
        history->count = SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY - 1;
        history->cursor = history->count;
    }

    editor_command_transaction_entry *entry = &history->entries[history->count];
    SDL_zero(*entry);
    entry->id = ++history->next_id;
    entry->face_index = -1;
    entry->material_index = -1;
    entry->previous_material_index = -1;
    entry->brush_index = -1;
    history->count++;
    history->cursor = history->count;
    return entry;
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

static int find_editor_mutable_brush_index(const brush_world_runtime *world_runtime, const char *brush_name)
{
    if (world_runtime == NULL || brush_name == NULL || brush_name[0] == '\0')
        return -1;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[i];
        if (brush->name != NULL && SDL_strcmp(brush->name, brush_name) == 0)
            return i;
    }
    return -1;
}

static bool rebuild_editor_brush_world(brush_world_runtime *world_runtime)
{
    return rebuild_brush_world_runtime_artifacts(world_runtime, NULL, 0);
}

static bool remove_editor_brush_at_index(brush_world_runtime *world_runtime, int brush_index,
                                         slayer3d_game_data_brush *out_removed)
{
    if (out_removed != NULL)
        SDL_zero(*out_removed);
    if (world_runtime == NULL || brush_index < 0 || brush_index >= world_runtime->desc.brush_count)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    slayer3d_game_data_brush *old_brushes = (slayer3d_game_data_brush *)world->brushes;
    const int old_count = world->brush_count;
    const int new_count = old_count - 1;
    slayer3d_game_data_brush *new_brushes =
        new_count > 0 ? (slayer3d_game_data_brush *)SDL_calloc((size_t)new_count, sizeof(*new_brushes)) : NULL;
    if (new_count > 0 && new_brushes == NULL)
        return false;

    slayer3d_game_data_brush removed = old_brushes[brush_index];
    int write_index = 0;
    for (int read_index = 0; read_index < old_count; ++read_index)
    {
        if (read_index != brush_index)
            new_brushes[write_index++] = old_brushes[read_index];
    }

    world->brushes = new_brushes;
    world->brush_count = new_count;
    if (!rebuild_editor_brush_world(world_runtime))
    {
        world->brushes = old_brushes;
        world->brush_count = old_count;
        SDL_free(new_brushes);
        return false;
    }

    SDL_free(old_brushes);
    if (out_removed != NULL)
        *out_removed = removed;
    else
        free_editor_runtime_brush_copy(&removed);
    editor_brush_world_mark_dirty(world_runtime);
    return true;
}

static bool insert_editor_brush_at_index(brush_world_runtime *world_runtime, int brush_index,
                                         const slayer3d_game_data_brush *brush)
{
    if (world_runtime == NULL || brush == NULL)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    const int old_count = world->brush_count;
    const int insert_index = SDL_clamp(brush_index, 0, old_count);
    slayer3d_game_data_brush *old_brushes = (slayer3d_game_data_brush *)world->brushes;
    slayer3d_game_data_brush *new_brushes =
        (slayer3d_game_data_brush *)SDL_calloc((size_t)old_count + 1u, sizeof(*new_brushes));
    if (new_brushes == NULL)
        return false;

    for (int i = 0; i < insert_index; ++i)
        new_brushes[i] = old_brushes[i];
    if (!copy_editor_brush_snapshot(brush, &new_brushes[insert_index]))
    {
        SDL_free(new_brushes);
        return false;
    }
    for (int i = insert_index; i < old_count; ++i)
        new_brushes[i + 1] = old_brushes[i];

    world->brushes = new_brushes;
    world->brush_count = old_count + 1;
    if (!rebuild_editor_brush_world(world_runtime))
    {
        world->brushes = old_brushes;
        world->brush_count = old_count;
        free_editor_runtime_brush_copy(&new_brushes[insert_index]);
        SDL_free(new_brushes);
        (void)rebuild_editor_brush_world(world_runtime);
        return false;
    }

    SDL_free(old_brushes);
    editor_brush_world_mark_dirty(world_runtime);
    return true;
}

static void translate_editor_brush_planes(slayer3d_game_data_brush *brush, slayer3d_vec3 offset)
{
    if (brush == NULL)
        return;
    for (int i = 0; i < brush->face_count; ++i)
    {
        slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[i];
        face->distance += slayer3d_vec3_dot(face->normal, offset);
    }
}

static bool apply_editor_brush_translate(slayer3d_game_data_runtime *runtime,
                                         const editor_command_transaction_entry *entry, slayer3d_vec3 offset)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;
    if (slayer3d_vec3_length_squared(offset) <= 0.0000001f)
        return true;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, entry->element_name);
    if (brush == NULL)
        return false;

    translate_editor_brush_planes(brush, offset);
    if (rebuild_editor_brush_world(world_runtime))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    translate_editor_brush_planes(brush, slayer3d_vec3_scale(offset, -1.0f));
    (void)rebuild_editor_brush_world(world_runtime);
    return false;
}

static bool set_editor_brush_face_material(brush_world_runtime *world_runtime, slayer3d_game_data_brush *brush,
                                           int face_index, int material_index)
{
    if (world_runtime == NULL || brush == NULL || face_index < 0 || face_index >= brush->face_count ||
        material_index < 0 || material_index >= world_runtime->desc.material_count)
    {
        return false;
    }
    slayer3d_game_data_brush_face *face = (slayer3d_game_data_brush_face *)&brush->faces[face_index];
    face->material_index = material_index;
    face->material_name = world_runtime->desc.materials[material_index].name;
    return true;
}

static bool apply_editor_brush_paint(slayer3d_game_data_runtime *runtime, const editor_command_transaction_entry *entry,
                                     bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL ||
        entry->face_index < 0)
    {
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, entry->element_name);
    const int material_index = forward ? entry->material_index : entry->previous_material_index;
    const int rollback_index = forward ? entry->previous_material_index : entry->material_index;
    if (!set_editor_brush_face_material(world_runtime, brush, entry->face_index, material_index))
        return false;
    if (rebuild_editor_brush_world(world_runtime))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    (void)set_editor_brush_face_material(world_runtime, brush, entry->face_index, rollback_index);
    (void)rebuild_editor_brush_world(world_runtime);
    return false;
}

static bool apply_editor_brush_face_resize(slayer3d_game_data_runtime *runtime,
                                           const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL ||
        entry->face_index < 0)
    {
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, entry->element_name);
    if (brush == NULL || entry->face_index >= brush->face_count)
        return false;

    const slayer3d_game_data_brush_face *face = &brush->faces[entry->face_index];
    const float distance = slayer3d_vec3_dot(slayer3d_vec3_normalize(face->normal),
                                             forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f));
    slayer3d_game_data_resize_brush_face_desc desc;
    SDL_zero(desc);
    desc.world_name = entry->world_name;
    desc.brush_name = entry->element_name;
    desc.face_index = entry->face_index;
    desc.distance = distance;
    return slayer3d_game_data_resize_brush_face(runtime, &desc, NULL, 0);
}

static bool apply_editor_brush_delete(slayer3d_game_data_runtime *runtime, editor_command_transaction_entry *entry,
                                      bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    if (world_runtime == NULL)
        return false;

    if (!forward)
    {
        if (!entry->has_brush_snapshot)
            return false;
        return insert_editor_brush_at_index(world_runtime, entry->brush_index, &entry->brush_snapshot);
    }

    const int brush_index = find_editor_mutable_brush_index(world_runtime, entry->element_name);
    if (brush_index < 0)
        return false;
    const slayer3d_game_data_brush *brush = &world_runtime->desc.brushes[brush_index];
    if (!entry->has_brush_snapshot)
    {
        if (!copy_editor_brush_snapshot(brush, &entry->brush_snapshot))
            return false;
        entry->has_brush_snapshot = true;
        entry->brush_index = brush_index;
    }

    const bool clears_active_selection =
        runtime->editor_active_selection.hit && runtime->editor_active_selection.world_name != NULL &&
        SDL_strcmp(runtime->editor_active_selection.world_name, entry->world_name) == 0 &&
        runtime->editor_active_selection.element_name != NULL &&
        SDL_strcmp(runtime->editor_active_selection.element_name, entry->element_name) == 0;

    slayer3d_game_data_brush removed;
    if (!remove_editor_brush_at_index(world_runtime, brush_index, &removed))
        return false;
    free_editor_runtime_brush_copy(&removed);

    if (clears_active_selection)
    {
        init_editor_selection(&runtime->editor_active_selection);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
        publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    }
    return true;
}

static void translate_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                              const editor_command_transaction_entry *entry,
                                                              slayer3d_vec3 offset)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0 || selection->element_name == NULL ||
        entry->element_name == NULL || SDL_strcmp(selection->element_name, entry->element_name) != 0)
    {
        return;
    }

    selection->point = slayer3d_vec3_add(selection->point, offset);
    selection->world_position = slayer3d_vec3_add(selection->world_position, offset);
    if (selection->has_bounds)
        selection->bounds = translated_bounds(selection->bounds, offset);

    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static void resize_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                           const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0 || selection->element_name == NULL ||
        entry->element_name == NULL || SDL_strcmp(selection->element_name, entry->element_name) != 0 ||
        selection->face_index != entry->face_index)
    {
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    const slayer3d_game_data_brush *brush = NULL;
    if (world_runtime != NULL)
    {
        const slayer3d_game_data_brush_world *world = &world_runtime->desc;
        for (int i = 0; i < world->brush_count; ++i)
        {
            if (world->brushes[i].name != NULL && SDL_strcmp(world->brushes[i].name, entry->element_name) == 0)
            {
                brush = &world->brushes[i];
                break;
            }
        }
    }

    const slayer3d_vec3 offset = forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f);
    selection->point = slayer3d_vec3_add(selection->point, offset);
    selection->world_position = slayer3d_vec3_add(selection->world_position, offset);
    if (brush != NULL && brush->has_bounds)
        selection->bounds = brush->bounds;
    else if (selection->has_bounds)
        selection->bounds = editor_resized_preview_bounds(selection->bounds, selection->normal,
                                                          slayer3d_vec3_dot(selection->normal, offset));
    selection->has_bounds = brush != NULL ? brush->has_bounds : selection->has_bounds;

    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static void update_active_editor_selection_material_for_transaction(slayer3d_game_data_runtime *runtime,
                                                                    const editor_command_transaction_entry *entry,
                                                                    bool forward)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0 || selection->element_name == NULL ||
        entry->element_name == NULL || SDL_strcmp(selection->element_name, entry->element_name) != 0 ||
        selection->face_index != entry->face_index)
    {
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    const int material_index = forward ? entry->material_index : entry->previous_material_index;
    selection->material_name = forward ? entry->material_name : entry->previous_material_name;
    selection->material_editor =
        world_runtime != NULL && material_index >= 0 && material_index < world_runtime->desc.material_count
            ? &world_runtime->desc.materials[material_index].editor
            : NULL;

    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static bool editor_transaction_has_brush_mutation(const editor_command_transaction_entry *entry)
{
    if (entry == NULL || entry->command == NULL || entry->target == NULL || entry->world_name == NULL ||
        entry->world_name[0] == '\0' || entry->element_name == NULL || entry->element_name[0] == '\0')
    {
        return false;
    }
    return (SDL_strcmp(entry->command, "translate") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "delete") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "paint") == 0 && SDL_strcmp(entry->target, "face") == 0) ||
           ((SDL_strcmp(entry->command, "resize") == 0 || SDL_strcmp(entry->command, "extrude") == 0) &&
            SDL_strcmp(entry->target, "face") == 0);
}

static bool apply_editor_transaction_mutation(slayer3d_game_data_runtime *runtime,
                                              editor_command_transaction_entry *entry, bool forward)
{
    if (!editor_transaction_has_brush_mutation(entry))
        return true;
    if (SDL_strcmp(entry->command, "delete") == 0)
        return apply_editor_brush_delete(runtime, entry, forward);
    if (SDL_strcmp(entry->command, "paint") == 0)
    {
        if (!apply_editor_brush_paint(runtime, entry, forward))
            return false;
        update_active_editor_selection_material_for_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "resize") == 0 || SDL_strcmp(entry->command, "extrude") == 0)
    {
        if (!apply_editor_brush_face_resize(runtime, entry, forward))
            return false;
        resize_active_editor_selection_for_transaction(runtime, entry, forward);
        return true;
    }

    const slayer3d_vec3 offset = forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f);
    if (!apply_editor_brush_translate(runtime, entry, offset))
        return false;
    translate_active_editor_selection_for_transaction(runtime, entry, offset);
    return true;
}

bool slayer3d_game_data_commit_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                              const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;

    if (!editor_command_preview_active_for_scene(runtime))
    {
        const char *message = json_string(action, "invalid_message", "no active editor command preview");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    const editor_command_preview_state *preview = &runtime->editor_command_preview;
    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (entry == NULL)
        return false;

    if (!copy_editor_transaction_strings(entry, preview))
    {
        editor_command_history_state *history = &runtime->editor_command_history;
        free_editor_command_transaction_entry(entry);
        history->count--;
        history->cursor = history->count;
        return false;
    }
    entry->face_index = preview->face_index;
    entry->material_index = preview->material_index;
    entry->previous_material_index = preview->previous_material_index;
    entry->offset = preview->offset;
    entry->has_bounds = preview->has_bounds;
    entry->bounds = preview->bounds;
    format_editor_transaction_message(runtime, "commit", true, entry,
                                      json_string(action, "message", "committed {editor_command}"), entry->message,
                                      sizeof(entry->message));

    if (!apply_editor_transaction_mutation(runtime, entry, true))
    {
        editor_command_history_state *history = &runtime->editor_command_history;
        free_editor_command_transaction_entry(entry);
        history->count--;
        history->cursor = history->count;
        const char *message = json_string(action, "invalid_message", "editor command mutation failed");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    publish_editor_command_preview(runtime, preview->outputs, false, "", "", "preview committed", NULL, NULL);
    clear_editor_command_preview(runtime);
    publish_editor_transaction(runtime, outputs, "commit", true, entry, entry->message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "commit", true, entry,
                                               entry->message);
}

bool slayer3d_game_data_undo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;
    editor_command_history_state *history = &runtime->editor_command_history;
    if (history->cursor <= 0)
    {
        const char *message = json_string(action, "invalid_message", "nothing to undo");
        publish_editor_transaction(runtime, outputs, "undo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "undo", false, NULL, message);
    }

    editor_command_transaction_entry *entry = &history->entries[history->cursor - 1];
    if (!apply_editor_transaction_mutation(runtime, entry, false))
    {
        const char *message = json_string(action, "invalid_message", "editor command undo failed");
        publish_editor_transaction(runtime, outputs, "undo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "undo", false, NULL, message);
    }
    history->cursor--;
    char message[128];
    format_editor_transaction_message(runtime, "undo", true, entry,
                                      json_string(action, "message", "undo {editor_command}"), message,
                                      sizeof(message));
    publish_editor_transaction(runtime, outputs, "undo", true, entry, message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "undo", true, entry, message);
}

bool slayer3d_game_data_redo_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;
    editor_command_history_state *history = &runtime->editor_command_history;
    if (history->cursor >= history->count)
    {
        const char *message = json_string(action, "invalid_message", "nothing to redo");
        publish_editor_transaction(runtime, outputs, "redo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "redo", false, NULL, message);
    }

    editor_command_transaction_entry *entry = &history->entries[history->cursor];
    if (!apply_editor_transaction_mutation(runtime, entry, true))
    {
        const char *message = json_string(action, "invalid_message", "editor command redo failed");
        publish_editor_transaction(runtime, outputs, "redo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "redo", false, NULL, message);
    }
    history->cursor++;
    char message[128];
    format_editor_transaction_message(runtime, "redo", true, entry,
                                      json_string(action, "message", "redo {editor_command}"), message,
                                      sizeof(message));
    publish_editor_transaction(runtime, outputs, "redo", true, entry, message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "redo", true, entry, message);
}
