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
        slayer3d_properties_set_string(payload, "editor_transaction_element_stable_id",
                                       entry->element_stable_id != NULL ? entry->element_stable_id : "");
        slayer3d_properties_set_string(payload, "editor_transaction_material",
                                       entry->material_name != NULL ? entry->material_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_previous_material",
                                       entry->previous_material_name != NULL ? entry->previous_material_name : "");
        slayer3d_properties_set_string(payload, "editor_transaction_face_stable_id",
                                       entry->face_stable_id != NULL ? entry->face_stable_id : "");
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
        slayer3d_properties_set_string(payload, "editor_transaction_element_stable_id", "");
        slayer3d_properties_set_string(payload, "editor_transaction_material", "");
        slayer3d_properties_set_string(payload, "editor_transaction_previous_material", "");
        slayer3d_properties_set_string(payload, "editor_transaction_face_stable_id", "");
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
    editor_set_string_output(scene_state, outputs, "element_stable_id_key",
                             valid && entry != NULL && entry->element_stable_id != NULL ? entry->element_stable_id
                                                                                        : "");
    editor_set_string_output(scene_state, outputs, "face_stable_id_key",
                             valid && entry != NULL && entry->face_stable_id != NULL ? entry->face_stable_id : "");
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
    SDL_free((void *)entry->element_stable_id);
    SDL_free((void *)entry->material_name);
    SDL_free((void *)entry->previous_material_name);
    SDL_free((void *)entry->face_stable_id);
    if (entry->has_source_box_snapshot)
        free_editor_brush_source_box_runtime(&entry->source_box_snapshot);
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
           copy_editor_transaction_string(preview->element_stable_id, &entry->element_stable_id) &&
           copy_editor_transaction_string(preview->material_name, &entry->material_name) &&
           copy_editor_transaction_string(preview->previous_material_name, &entry->previous_material_name) &&
           copy_editor_transaction_string(preview->face_stable_id, &entry->face_stable_id);
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

static const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : NULL;
}

static bool editor_metadata_matches_stable_id(const slayer3d_game_data_editor_metadata *metadata, const char *stable_id)
{
    return stable_id != NULL && stable_id[0] != '\0' && metadata != NULL && metadata->stable_id != NULL &&
           SDL_strcmp(metadata->stable_id, stable_id) == 0;
}

static bool editor_brush_matches_identity(const slayer3d_game_data_brush *brush, const char *brush_name,
                                          const char *brush_stable_id)
{
    if (brush == NULL)
        return false;
    if (brush_stable_id != NULL && brush_stable_id[0] != '\0')
        return editor_metadata_matches_stable_id(&brush->editor, brush_stable_id);
    return brush_name != NULL && brush_name[0] != '\0' && brush->name != NULL &&
           SDL_strcmp(brush->name, brush_name) == 0;
}

static int editor_face_index_for_identity(const slayer3d_game_data_brush *brush, int face_index,
                                          const char *face_stable_id)
{
    if (brush == NULL)
        return -1;
    if (face_stable_id != NULL && face_stable_id[0] != '\0')
    {
        for (int i = 0; i < brush->face_count; ++i)
        {
            if (editor_metadata_matches_stable_id(&brush->faces[i].editor, face_stable_id))
                return i;
        }
        return -1;
    }
    return face_index >= 0 && face_index < brush->face_count ? face_index : -1;
}

static bool editor_selection_matches_transaction_element(const slayer3d_game_data_editor_selection *selection,
                                                         const editor_command_transaction_entry *entry)
{
    if (selection == NULL || entry == NULL || selection->element_name == NULL)
        return false;
    if (entry->element_name != NULL && SDL_strcmp(selection->element_name, entry->element_name) == 0)
        return true;
    if (entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0')
        return editor_metadata_matches_stable_id(selection->element_editor, entry->element_stable_id);
    return false;
}

static bool editor_selection_matches_transaction_face(const slayer3d_game_data_editor_selection *selection,
                                                      const editor_command_transaction_entry *entry)
{
    if (!editor_selection_matches_transaction_element(selection, entry))
        return false;
    if (selection != NULL && selection->face_index == entry->face_index)
        return true;
    if (entry->face_stable_id != NULL && entry->face_stable_id[0] != '\0')
        return editor_metadata_matches_stable_id(selection->face_editor, entry->face_stable_id);
    return false;
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

static slayer3d_game_data_brush *find_editor_mutable_brush_by_identity(brush_world_runtime *world_runtime,
                                                                       const char *brush_name,
                                                                       const char *brush_stable_id)
{
    if (world_runtime == NULL ||
        ((brush_name == NULL || brush_name[0] == '\0') && (brush_stable_id == NULL || brush_stable_id[0] == '\0')))
    {
        return NULL;
    }
    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        slayer3d_game_data_brush *brush = (slayer3d_game_data_brush *)&world->brushes[i];
        if (editor_brush_matches_identity(brush, brush_name, brush_stable_id))
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

static int find_editor_mutable_brush_index_by_identity(const brush_world_runtime *world_runtime, const char *brush_name,
                                                       const char *brush_stable_id)
{
    if (world_runtime == NULL ||
        ((brush_name == NULL || brush_name[0] == '\0') && (brush_stable_id == NULL || brush_stable_id[0] == '\0')))
    {
        return -1;
    }
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        if (editor_brush_matches_identity(&world->brushes[i], brush_name, brush_stable_id))
            return i;
    }
    return -1;
}

static int editor_brush_material_index_by_name(const brush_world_runtime *world_runtime, const char *material_name)
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

static bool rebuild_editor_brush_world(brush_world_runtime *world_runtime)
{
    return rebuild_brush_world_runtime_artifacts(world_runtime, NULL, 0) &&
           editor_brush_world_sync_source_from_runtime(world_runtime, NULL, 0);
}

static bool remove_editor_brush_at_index(brush_world_runtime *world_runtime, int brush_index,
                                         slayer3d_game_data_brush *out_removed)
{
    if (out_removed != NULL)
        SDL_zero(*out_removed);
    if (world_runtime == NULL || brush_index < 0 || brush_index >= world_runtime->desc.brush_count)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (world_runtime->editor_has_source_model)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        const char *brush_identity = brush->editor.stable_id != NULL && brush->editor.stable_id[0] != '\0'
                                         ? brush->editor.stable_id
                                         : brush->name;
        const int source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
        if (source_index < 0)
            return false;
        if (out_removed != NULL && !copy_editor_brush_snapshot(brush, out_removed))
            return false;
        if (!editor_brush_world_remove_source_box_at_index(world_runtime, source_index, NULL, 0))
        {
            if (out_removed != NULL)
                free_editor_runtime_brush_copy(out_removed);
            return false;
        }
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

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

    if (world_runtime->editor_has_source_model)
    {
        if (!editor_brush_world_insert_source_box_from_brush(world_runtime, brush_index, brush, NULL, 0))
            return false;
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

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

static void init_editor_box_transaction_face(slayer3d_game_data_brush_face *face, slayer3d_vec3 normal, float distance,
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

static bool create_editor_box_brush_snapshot(const brush_world_runtime *world_runtime, const char *brush_name,
                                             slayer3d_bounding_box bounds, int material_index,
                                             slayer3d_game_data_brush *out_brush)
{
    if (world_runtime == NULL || brush_name == NULL || brush_name[0] == '\0' || out_brush == NULL ||
        material_index < 0 || material_index >= world_runtime->desc.material_count ||
        !(bounds.min.x < bounds.max.x && bounds.min.y < bounds.max.y && bounds.min.z < bounds.max.z))
    {
        return false;
    }

    SDL_zero(*out_brush);
    out_brush->name = SDL_strdup(brush_name);
    out_brush->contents = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP;
    out_brush->visibility = SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_AUTO;
    out_brush->visibility_cullable = true;
    out_brush->face_count = 6;
    out_brush->faces = (slayer3d_game_data_brush_face *)SDL_calloc(6u, sizeof(*out_brush->faces));
    out_brush->bounds = bounds;
    out_brush->has_bounds = true;
    if (out_brush->name == NULL || out_brush->faces == NULL)
    {
        free_editor_runtime_brush_copy(out_brush);
        return false;
    }

    const char *material_name = world_runtime->desc.materials[material_index].name;
    slayer3d_game_data_brush_face *faces = (slayer3d_game_data_brush_face *)out_brush->faces;
    init_editor_box_transaction_face(&faces[0], slayer3d_vec3_make(1.0f, 0.0f, 0.0f), bounds.max.x, material_index,
                                     material_name);
    init_editor_box_transaction_face(&faces[1], slayer3d_vec3_make(-1.0f, 0.0f, 0.0f), -bounds.min.x, material_index,
                                     material_name);
    init_editor_box_transaction_face(&faces[2], slayer3d_vec3_make(0.0f, 1.0f, 0.0f), bounds.max.y, material_index,
                                     material_name);
    init_editor_box_transaction_face(&faces[3], slayer3d_vec3_make(0.0f, -1.0f, 0.0f), -bounds.min.y, material_index,
                                     material_name);
    init_editor_box_transaction_face(&faces[4], slayer3d_vec3_make(0.0f, 0.0f, 1.0f), bounds.max.z, material_index,
                                     material_name);
    init_editor_box_transaction_face(&faces[5], slayer3d_vec3_make(0.0f, 0.0f, -1.0f), -bounds.min.z, material_index,
                                     material_name);
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
    if (world_runtime != NULL && world_runtime->editor_has_source_model)
    {
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (editor_brush_world_translate_source_box(world_runtime, brush_identity, offset, NULL, 0))
        {
            editor_brush_world_mark_dirty(world_runtime);
            return true;
        }
        return false;
    }

    slayer3d_game_data_brush *brush =
        find_editor_mutable_brush_by_identity(world_runtime, entry->element_name, entry->element_stable_id);
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
    slayer3d_game_data_brush *brush =
        find_editor_mutable_brush_by_identity(world_runtime, entry->element_name, entry->element_stable_id);
    const int face_index = editor_face_index_for_identity(brush, entry->face_index, entry->face_stable_id);
    const int material_index = forward ? entry->material_index : entry->previous_material_index;
    const int rollback_index = forward ? entry->previous_material_index : entry->material_index;
    if (world_runtime != NULL && world_runtime->editor_has_source_model && material_index >= 0 &&
        material_index < world_runtime->desc.material_count && face_index >= 0)
    {
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (editor_brush_world_set_source_box_face_material(
                world_runtime, brush_identity, face_index, world_runtime->desc.materials[material_index].name, NULL, 0))
        {
            editor_brush_world_mark_dirty(world_runtime);
            return true;
        }
        if (rollback_index >= 0 && rollback_index < world_runtime->desc.material_count)
        {
            (void)editor_brush_world_set_source_box_face_material(
                world_runtime, brush_identity, face_index, world_runtime->desc.materials[rollback_index].name, NULL, 0);
        }
        return false;
    }
    if (!set_editor_brush_face_material(world_runtime, brush, face_index, material_index))
        return false;
    if (rebuild_editor_brush_world(world_runtime))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    (void)set_editor_brush_face_material(world_runtime, brush, face_index, rollback_index);
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
    slayer3d_game_data_brush *brush =
        find_editor_mutable_brush_by_identity(world_runtime, entry->element_name, entry->element_stable_id);
    const int face_index = editor_face_index_for_identity(brush, entry->face_index, entry->face_stable_id);
    if (brush == NULL || face_index < 0)
        return false;

    const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
    const float distance = slayer3d_vec3_dot(slayer3d_vec3_normalize(face->normal),
                                             forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f));
    slayer3d_game_data_resize_brush_face_desc desc;
    SDL_zero(desc);
    desc.world_name = entry->world_name;
    desc.brush_name = world_runtime != NULL && world_runtime->editor_has_source_model &&
                              entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                          ? entry->element_stable_id
                          : entry->element_name;
    desc.face_index = face_index;
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
        if (world_runtime->editor_has_source_model)
        {
            if (!entry->has_source_box_snapshot)
                return false;
            if (!editor_brush_world_insert_source_box_at_index(world_runtime, entry->brush_index,
                                                               &entry->source_box_snapshot, NULL, 0))
            {
                return false;
            }
            editor_brush_world_mark_dirty(world_runtime);
            return true;
        }
        if (!entry->has_brush_snapshot)
            return false;
        return insert_editor_brush_at_index(world_runtime, entry->brush_index, &entry->brush_snapshot);
    }

    const int brush_index =
        find_editor_mutable_brush_index_by_identity(world_runtime, entry->element_name, entry->element_stable_id);
    if (brush_index < 0)
        return false;
    const slayer3d_game_data_brush *brush = &world_runtime->desc.brushes[brush_index];
    if (world_runtime->editor_has_source_model)
    {
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (!entry->has_source_box_snapshot)
        {
            if (!editor_brush_world_copy_source_box_by_identity(
                    world_runtime, brush_identity, &entry->source_box_snapshot, &entry->brush_index, NULL, 0))
            {
                return false;
            }
            entry->has_source_box_snapshot = true;
        }
    }
    else if (!entry->has_brush_snapshot)
    {
        if (!copy_editor_brush_snapshot(brush, &entry->brush_snapshot))
            return false;
        entry->has_brush_snapshot = true;
        entry->brush_index = brush_index;
    }

    const bool clears_active_selection =
        runtime->editor_active_selection.hit && runtime->editor_active_selection.world_name != NULL &&
        SDL_strcmp(runtime->editor_active_selection.world_name, entry->world_name) == 0 &&
        editor_selection_matches_transaction_element(&runtime->editor_active_selection, entry);

    if (world_runtime->editor_has_source_model)
    {
        if (!editor_brush_world_remove_source_box_at_index(world_runtime, entry->brush_index, NULL, 0))
            return false;
        editor_brush_world_mark_dirty(world_runtime);
    }
    else
    {
        slayer3d_game_data_brush removed;
        if (!remove_editor_brush_at_index(world_runtime, brush_index, &removed))
            return false;
        free_editor_runtime_brush_copy(&removed);
    }

    if (clears_active_selection)
    {
        init_editor_selection(&runtime->editor_active_selection);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
        publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    }
    return true;
}

static bool apply_editor_brush_create(slayer3d_game_data_runtime *runtime, editor_command_transaction_entry *entry,
                                      bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    if (world_runtime == NULL)
        return false;

    if (!forward)
    {
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (world_runtime->editor_has_source_model)
        {
            const int source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
            if (source_index < 0)
                return true;
            if (!editor_brush_world_remove_source_box_at_index(world_runtime, source_index, NULL, 0))
                return false;
            editor_brush_world_mark_dirty(world_runtime);
            return true;
        }
        const int brush_index =
            find_editor_mutable_brush_index_by_identity(world_runtime, entry->element_name, entry->element_stable_id);
        if (brush_index < 0)
            return true;
        slayer3d_game_data_brush removed;
        if (!remove_editor_brush_at_index(world_runtime, brush_index, &removed))
            return false;
        free_editor_runtime_brush_copy(&removed);
        return true;
    }

    if (world_runtime->editor_has_source_model)
    {
        if (!entry->has_source_box_snapshot)
            return false;
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (editor_brush_world_find_source_box_index(world_runtime, brush_identity) >= 0)
            return true;
        if (!editor_brush_world_insert_source_box_at_index(world_runtime, entry->brush_index,
                                                           &entry->source_box_snapshot, NULL, 0))
        {
            return false;
        }
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    if (!entry->has_brush_snapshot)
        return false;
    if (find_editor_mutable_brush_index_by_identity(world_runtime, entry->element_name, entry->element_stable_id) >= 0)
        return true;
    return insert_editor_brush_at_index(world_runtime, entry->brush_index, &entry->brush_snapshot);
}

static bool editor_floor_fill_name(const char *brush_name, float low_y, float high_y, const char *side, char *buffer,
                                   size_t buffer_size);
static slayer3d_bounding_box editor_floor_fill_bounds(slayer3d_bounding_box floor_bounds, float low_y, float high_y,
                                                      float thickness, int side_index);
static void translate_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                              const editor_command_transaction_entry *entry,
                                                              slayer3d_vec3 offset);

static void refresh_editor_brush_selection_for_transaction(const brush_world_runtime *world_runtime,
                                                           slayer3d_game_data_editor_selection *selection)
{
    if (world_runtime == NULL || selection == NULL || selection->world_name == NULL || selection->element_name == NULL)
        return;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    selection->world_name = world->name;
    selection->world_editor = &world->editor;
    selection->element_index = -1;
    selection->element_editor = NULL;
    selection->face_editor = NULL;
    selection->material_editor = NULL;

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (!editor_brush_matches_identity(brush, selection->element_name, NULL))
            continue;

        selection->element_name = brush->name;
        selection->element_index = brush_index;
        selection->element_editor = &brush->editor;
        selection->has_bounds = brush->has_bounds;
        if (brush->has_bounds)
            selection->bounds = brush->bounds;

        if (selection->face_index >= 0 && selection->face_index < brush->face_count)
        {
            const slayer3d_game_data_brush_face *face = &brush->faces[selection->face_index];
            selection->face_editor = &face->editor;
            selection->material_name = face->material_name;
            if (face->material_index >= 0 && face->material_index < world->material_count)
                selection->material_editor = &world->materials[face->material_index].editor;
        }
        return;
    }
}

static bool delete_editor_floor_fill_brushes(brush_world_runtime *world_runtime, const char *brush_name, float low_y,
                                             float high_y)
{
    static const char *const fill_side_names[] = {"west", "east", "north", "south"};
    for (int side = 0; side < 4; ++side)
    {
        char fill_name[256];
        if (!editor_floor_fill_name(brush_name, low_y, high_y, fill_side_names[side], fill_name, sizeof(fill_name)))
            return false;
        const int fill_index = find_editor_mutable_brush_index(world_runtime, fill_name);
        if (fill_index >= 0)
        {
            slayer3d_game_data_brush removed;
            if (!remove_editor_brush_at_index(world_runtime, fill_index, &removed))
                return false;
            free_editor_runtime_brush_copy(&removed);
        }
    }
    return true;
}

static bool create_editor_floor_fill_brushes(brush_world_runtime *world_runtime, const char *brush_name,
                                             slayer3d_bounding_box floor_bounds, float low_y, float high_y,
                                             float fill_thickness, int material_index)
{
    static const char *const fill_side_names[] = {"west", "east", "north", "south"};
    int created_count = 0;
    for (int side = 0; side < 4; ++side)
    {
        char fill_name[256];
        if (!editor_floor_fill_name(brush_name, low_y, high_y, fill_side_names[side], fill_name, sizeof(fill_name)))
            goto fail;
        if (find_editor_mutable_brush_index(world_runtime, fill_name) >= 0)
            continue;

        slayer3d_game_data_brush fill_brush;
        const slayer3d_bounding_box fill_bounds =
            editor_floor_fill_bounds(floor_bounds, low_y, high_y, fill_thickness, side);
        if (!create_editor_box_brush_snapshot(world_runtime, fill_name, fill_bounds, material_index, &fill_brush))
            goto fail;
        if (!insert_editor_brush_at_index(world_runtime, world_runtime->desc.brush_count, &fill_brush))
        {
            free_editor_runtime_brush_copy(&fill_brush);
            goto fail;
        }
        free_editor_runtime_brush_copy(&fill_brush);
        created_count++;
    }
    return true;

fail:
    for (int side = 0; side < created_count; ++side)
    {
        char fill_name[256];
        if (editor_floor_fill_name(brush_name, low_y, high_y, fill_side_names[side], fill_name, sizeof(fill_name)))
        {
            const int fill_index = find_editor_mutable_brush_index(world_runtime, fill_name);
            if (fill_index >= 0)
            {
                slayer3d_game_data_brush removed;
                if (remove_editor_brush_at_index(world_runtime, fill_index, &removed))
                    free_editor_runtime_brush_copy(&removed);
            }
        }
    }
    return false;
}

static bool apply_editor_brush_sector_floor(slayer3d_game_data_runtime *runtime,
                                            const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    slayer3d_game_data_brush *brush = find_editor_mutable_brush(world_runtime, entry->element_name);
    if (world_runtime == NULL || brush == NULL || !brush->has_bounds)
        return false;

    const float authored_distance = forward ? entry->offset.y : -entry->offset.y;
    const slayer3d_vec3 offset = slayer3d_vec3_make(0.0f, authored_distance, 0.0f);
    const slayer3d_bounding_box current_bounds = brush->bounds;
    const float low_y = SDL_min(current_bounds.max.y, current_bounds.max.y + offset.y);
    const float high_y = SDL_max(current_bounds.max.y, current_bounds.max.y + offset.y);
    const float fill_thickness = entry->offset.x > 0.0f ? entry->offset.x : 0.2f;
    const int fill_material_index = entry->material_index >= 0
                                        ? entry->material_index
                                        : editor_brush_material_index_by_name(world_runtime, entry->material_name);
    if (fill_material_index < 0)
        return false;

    if (offset.y > 0.0f && !delete_editor_floor_fill_brushes(world_runtime, entry->element_name, low_y, high_y))
        return false;

    if (!apply_editor_brush_translate(runtime, entry, offset))
        return false;

    if (offset.y < 0.0f && !create_editor_floor_fill_brushes(world_runtime, entry->element_name, current_bounds, low_y,
                                                             high_y, fill_thickness, fill_material_index))
    {
        (void)apply_editor_brush_translate(runtime, entry, slayer3d_vec3_scale(offset, -1.0f));
        (void)delete_editor_floor_fill_brushes(world_runtime, entry->element_name, low_y, high_y);
        return false;
    }

    translate_active_editor_selection_for_transaction(runtime, entry, offset);
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
        !editor_selection_matches_transaction_element(selection, entry))
    {
        return;
    }

    selection->point = slayer3d_vec3_add(selection->point, offset);
    selection->world_position = slayer3d_vec3_add(selection->world_position, offset);
    if (selection->has_bounds)
        selection->bounds = translated_bounds(selection->bounds, offset);

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    refresh_editor_brush_selection_for_transaction(world_runtime, selection);
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
        !editor_selection_matches_transaction_face(selection, entry))
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
            if (editor_brush_matches_identity(&world->brushes[i], entry->element_name, entry->element_stable_id))
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
    refresh_editor_brush_selection_for_transaction(world_runtime, selection);

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
        !editor_selection_matches_transaction_face(selection, entry))
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
           (SDL_strcmp(entry->command, "sector_floor") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "create") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
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
    if (SDL_strcmp(entry->command, "create") == 0)
        return apply_editor_brush_create(runtime, entry, forward);
    if (SDL_strcmp(entry->command, "sector_floor") == 0)
        return apply_editor_brush_sector_floor(runtime, entry, forward);
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

typedef struct selected_editor_brush_delete_target
{
    char *scene;
    char *world;
    char *element;
    char *element_stable_id;
    bool has_bounds;
    slayer3d_bounding_box bounds;
} selected_editor_brush_delete_target;

static void free_selected_editor_brush_delete_targets(selected_editor_brush_delete_target *targets, int count)
{
    if (targets == NULL)
        return;
    for (int i = 0; i < count; ++i)
    {
        SDL_free(targets[i].scene);
        SDL_free(targets[i].world);
        SDL_free(targets[i].element);
        SDL_free(targets[i].element_stable_id);
    }
    SDL_free(targets);
}

static bool copy_selected_editor_brush_delete_target(selected_editor_brush_delete_target *target,
                                                     const slayer3d_game_data_editor_selection *selection,
                                                     const char *active_scene)
{
    if (target == NULL || selection == NULL || !selection->hit ||
        selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD || selection->world_name == NULL ||
        selection->element_name == NULL)
    {
        return false;
    }
    SDL_zero(*target);
    target->scene = active_scene != NULL ? SDL_strdup(active_scene) : NULL;
    target->world = SDL_strdup(selection->world_name);
    target->element = SDL_strdup(selection->element_name);
    const char *stable_id = editor_metadata_stable_id(selection->element_editor);
    target->element_stable_id = stable_id != NULL ? SDL_strdup(stable_id) : NULL;
    target->has_bounds = selection->has_bounds;
    target->bounds = selection->bounds;
    return (active_scene == NULL || target->scene != NULL) && target->world != NULL && target->element != NULL &&
           (stable_id == NULL || target->element_stable_id != NULL);
}

static int editor_brush_top_y_face_index(const slayer3d_game_data_brush *brush)
{
    int best_index = -1;
    float best_y = 0.0f;
    for (int i = 0; brush != NULL && i < brush->face_count; ++i)
    {
        const float y = brush->faces[i].normal.y;
        if (y > 0.5f && (best_index < 0 || y > best_y))
        {
            best_index = i;
            best_y = y;
        }
    }
    return best_index;
}

static float editor_selected_brush_resize_y_distance(const slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const int direction = json_int(action, "direction", 1);
    const char *distance_key = json_string(action, "distance_key", json_string(action, "grid_key", NULL));
    float distance = json_float(action, "distance", 0.0f);
    if (distance == 0.0f && distance_key != NULL && distance_key[0] != '\0' && runtime != NULL &&
        runtime->scene_state != NULL)
    {
        distance = slayer3d_properties_get_float(runtime->scene_state, distance_key, 0.0f);
    }
    if (distance == 0.0f)
        distance = json_float(action, "default_distance", 1.0f);
    distance = SDL_fabsf(distance);
    return direction < 0 ? -distance : distance;
}

static bool selected_editor_brush_can_resize_y(const brush_world_runtime *world_runtime,
                                               const slayer3d_game_data_editor_selection *selection, float distance,
                                               float min_height)
{
    if (world_runtime == NULL || selection == NULL || !selection->hit ||
        selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD || selection->element_name == NULL)
    {
        return false;
    }
    const slayer3d_game_data_brush *brush = NULL;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->brush_count; ++i)
    {
        if (world->brushes[i].name != NULL && SDL_strcmp(world->brushes[i].name, selection->element_name) == 0)
        {
            brush = &world->brushes[i];
            break;
        }
    }
    if (brush == NULL || !brush->has_bounds || editor_brush_top_y_face_index(brush) < 0)
        return false;
    return brush->bounds.max.y + distance - brush->bounds.min.y >= min_height;
}

static void refresh_selected_editor_brush_bounds_for_transaction(slayer3d_game_data_runtime *runtime,
                                                                 const editor_command_transaction_entry *entry)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        if (selection->world_name != NULL && selection->element_name != NULL &&
            SDL_strcmp(selection->world_name, entry->world_name) == 0 &&
            editor_selection_matches_transaction_element(selection, entry))
        {
            refresh_editor_brush_selection_for_transaction(world_runtime, selection);
        }
    }
}

static bool editor_brush_uses_material(const slayer3d_game_data_brush *brush, const char *material_name)
{
    if (brush == NULL || material_name == NULL || material_name[0] == '\0')
        return false;
    for (int i = 0; i < brush->face_count; ++i)
    {
        if (brush->faces[i].material_name != NULL && SDL_strcmp(brush->faces[i].material_name, material_name) == 0)
            return true;
    }
    return false;
}

static bool editor_brush_is_floor_slab(const slayer3d_game_data_brush *brush, yyjson_val *action)
{
    if (brush == NULL || !brush->has_bounds)
        return false;
    const float slab_max_height = json_float(action, "slab_max_height", 0.5f);
    if (slab_max_height <= 0.0f || brush->bounds.max.y - brush->bounds.min.y > slab_max_height)
        return false;
    return editor_brush_uses_material(brush, json_string(action, "floor_material", "mat.editor.floor"));
}

static void editor_fill_elevation_token(float value, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u)
        return;
    const int millimeters = (int)SDL_lroundf(value * 1000.0f);
    SDL_snprintf(buffer, buffer_size, "%s%d", millimeters < 0 ? "n" : "p", SDL_abs(millimeters));
}

static bool editor_floor_fill_name(const char *brush_name, float low_y, float high_y, const char *side, char *buffer,
                                   size_t buffer_size)
{
    if (brush_name == NULL || side == NULL || buffer == NULL || buffer_size == 0u)
        return false;
    char low_token[32];
    char high_token[32];
    editor_fill_elevation_token(low_y, low_token, sizeof(low_token));
    editor_fill_elevation_token(high_y, high_token, sizeof(high_token));
    SDL_snprintf(buffer, buffer_size, "%s.auto_fill.%s_%s.%s", brush_name, low_token, high_token, side);
    return buffer[0] != '\0';
}

static slayer3d_bounding_box editor_floor_fill_bounds(slayer3d_bounding_box floor_bounds, float low_y, float high_y,
                                                      float thickness, int side_index)
{
    slayer3d_bounding_box bounds = floor_bounds;
    bounds.min.y = low_y;
    bounds.max.y = high_y;
    const float inset_x = SDL_min(thickness, SDL_max((floor_bounds.max.x - floor_bounds.min.x) * 0.5f, 0.001f));
    const float inset_z = SDL_min(thickness, SDL_max((floor_bounds.max.z - floor_bounds.min.z) * 0.5f, 0.001f));
    switch (side_index)
    {
    case 0:
        bounds.min.x = floor_bounds.min.x;
        bounds.max.x = floor_bounds.min.x + inset_x;
        break;
    case 1:
        bounds.min.x = floor_bounds.max.x - inset_x;
        bounds.max.x = floor_bounds.max.x;
        break;
    case 2:
        bounds.min.x = floor_bounds.min.x + inset_x;
        bounds.max.x = floor_bounds.max.x - inset_x;
        bounds.min.z = floor_bounds.min.z;
        bounds.max.z = floor_bounds.min.z + inset_z;
        break;
    default:
        bounds.min.x = floor_bounds.min.x + inset_x;
        bounds.max.x = floor_bounds.max.x - inset_x;
        bounds.min.z = floor_bounds.max.z - inset_z;
        bounds.max.z = floor_bounds.max.z;
        break;
    }
    return bounds;
}

static bool editor_prepare_transaction_common(editor_command_transaction_entry *entry, const char *active_scene,
                                              const char *command, const char *target, const char *world_name,
                                              const char *element_name)
{
    return entry != NULL && copy_editor_transaction_string(active_scene, &entry->scene) &&
           copy_editor_transaction_string(command, &entry->command) &&
           copy_editor_transaction_string(target, &entry->target) &&
           copy_editor_transaction_string(world_name, &entry->world_name) &&
           copy_editor_transaction_string(element_name, &entry->element_name);
}

static bool editor_append_sector_floor_transaction(slayer3d_game_data_runtime *runtime, const char *active_scene,
                                                   const slayer3d_game_data_editor_selection *selection,
                                                   const slayer3d_game_data_brush *brush, float distance,
                                                   float fill_thickness, int fill_material_index,
                                                   const char *fill_material_name,
                                                   editor_command_transaction_entry **out_entry)
{
    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (!editor_prepare_transaction_common(entry, active_scene, "sector_floor", "element", selection->world_name,
                                           selection->element_name) ||
        !copy_editor_transaction_string(editor_metadata_stable_id(selection->element_editor),
                                        &entry->element_stable_id) ||
        !copy_editor_transaction_string(fill_material_name, &entry->material_name))
    {
        return false;
    }
    entry->material_index = fill_material_index;
    entry->offset = slayer3d_vec3_make(fill_thickness, distance, 0.0f);
    entry->has_bounds = brush != NULL && brush->has_bounds;
    entry->bounds = entry->has_bounds ? translated_bounds(brush->bounds, slayer3d_vec3_make(0.0f, distance, 0.0f))
                                      : (slayer3d_bounding_box){slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                                slayer3d_vec3_make(0.0f, 0.0f, 0.0f)};
    SDL_snprintf(entry->message, sizeof(entry->message), "%s %s by %.3f", distance >= 0.0f ? "raised" : "lowered",
                 selection->element_name != NULL ? selection->element_name : "selected brush",
                 (double)SDL_fabsf(distance));
    *out_entry = entry;
    return true;
}

static bool editor_append_resize_y_transaction(slayer3d_game_data_runtime *runtime, const char *active_scene,
                                               const slayer3d_game_data_editor_selection *selection,
                                               const slayer3d_game_data_brush *brush, float distance,
                                               editor_command_transaction_entry **out_entry)
{
    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (!editor_prepare_transaction_common(entry, active_scene, "resize", "face", selection->world_name,
                                           selection->element_name) ||
        !copy_editor_transaction_string(editor_metadata_stable_id(selection->element_editor),
                                        &entry->element_stable_id))
    {
        return false;
    }
    entry->face_index = editor_brush_top_y_face_index(brush);
    if (entry->face_index >= 0 && brush != NULL)
    {
        if (!copy_editor_transaction_string(editor_metadata_stable_id(&brush->faces[entry->face_index].editor),
                                            &entry->face_stable_id))
            return false;
    }
    entry->offset = slayer3d_vec3_make(0.0f, distance, 0.0f);
    entry->has_bounds = brush != NULL && brush->has_bounds;
    entry->bounds =
        entry->has_bounds
            ? editor_resized_preview_bounds(brush->bounds, slayer3d_vec3_make(0.0f, 1.0f, 0.0f), distance)
            : (slayer3d_bounding_box){slayer3d_vec3_make(0.0f, 0.0f, 0.0f), slayer3d_vec3_make(0.0f, 0.0f, 0.0f)};
    SDL_snprintf(entry->message, sizeof(entry->message), "%s %s by %.3f", distance >= 0.0f ? "raised" : "lowered",
                 selection->element_name != NULL ? selection->element_name : "selected brush",
                 (double)SDL_fabsf(distance));
    *out_entry = entry;
    return true;
}

bool slayer3d_game_data_delete_selected_editor_brushes(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                       const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (runtime->editor_selected_brush_scene == NULL || active_scene == NULL ||
        SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0 ||
        runtime->editor_selected_brush_count <= 0)
    {
        const char *message = json_string(action, "invalid_message", "nothing selected to delete");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    const int target_count = runtime->editor_selected_brush_count;
    selected_editor_brush_delete_target *targets =
        (selected_editor_brush_delete_target *)SDL_calloc((size_t)target_count, sizeof(*targets));
    if (targets == NULL)
        return false;

    for (int i = 0; i < target_count; ++i)
    {
        if (!copy_selected_editor_brush_delete_target(&targets[i], &runtime->editor_selected_brushes[i], active_scene))
        {
            free_selected_editor_brush_delete_targets(targets, target_count);
            return false;
        }
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, targets[i].world);
        if (find_editor_mutable_brush_index_by_identity(world_runtime, targets[i].element,
                                                        targets[i].element_stable_id) < 0)
        {
            const char *message = json_string(action, "invalid_message", "selected brush no longer exists");
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            free_selected_editor_brush_delete_targets(targets, target_count);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }
    }

    editor_command_transaction_entry *last_entry = NULL;
    int deleted_count = 0;
    for (int i = target_count - 1; i >= 0; --i)
    {
        editor_command_transaction_entry *entry = editor_command_history_append(runtime);
        if (entry == NULL)
        {
            free_selected_editor_brush_delete_targets(targets, target_count);
            return false;
        }
        if (!copy_editor_transaction_string(targets[i].scene, &entry->scene) ||
            !copy_editor_transaction_string("delete", &entry->command) ||
            !copy_editor_transaction_string("element", &entry->target) ||
            !copy_editor_transaction_string(targets[i].world, &entry->world_name) ||
            !copy_editor_transaction_string(targets[i].element, &entry->element_name) ||
            !copy_editor_transaction_string(targets[i].element_stable_id, &entry->element_stable_id))
        {
            editor_command_history_state *history = &runtime->editor_command_history;
            free_editor_command_transaction_entry(entry);
            history->count--;
            history->cursor = history->count;
            free_selected_editor_brush_delete_targets(targets, target_count);
            return false;
        }
        entry->face_index = -1;
        entry->has_bounds = targets[i].has_bounds;
        entry->bounds = targets[i].bounds;
        SDL_snprintf(entry->message, sizeof(entry->message), "deleted %s", targets[i].element);

        if (!apply_editor_transaction_mutation(runtime, entry, true))
        {
            const char *message = json_string(action, "invalid_message", "selected brush delete failed");
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            free_selected_editor_brush_delete_targets(targets, target_count);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }
        last_entry = entry;
        deleted_count++;
    }

    char message[128];
    SDL_snprintf(message, sizeof(message),
                 deleted_count == 1 ? "deleted 1 selected brush" : "deleted %d selected brushes", deleted_count);
    (void)slayer3d_game_data_clear_active_editor_selection(runtime);
    publish_editor_transaction(runtime, outputs, "commit", deleted_count > 0, last_entry, message);
    free_selected_editor_brush_delete_targets(targets, target_count);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "commit", deleted_count > 0,
                                               last_entry, message);
}

bool slayer3d_game_data_resize_selected_editor_brushes_y(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                         const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (runtime->editor_selected_brush_scene == NULL || active_scene == NULL ||
        SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0 ||
        runtime->editor_selected_brush_count <= 0)
    {
        const char *message = json_string(action, "invalid_message", "nothing selected to resize");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    const float distance = editor_selected_brush_resize_y_distance(runtime, action);
    const float min_height = json_float(action, "min_height", 0.1f);
    const float min_elevation = json_float(action, "min_elevation", -4096.0f);
    const float max_elevation = json_float(action, "max_elevation", 4096.0f);
    if (SDL_fabsf(distance) <= 0.000001f || min_height <= 0.0f || min_elevation >= max_elevation)
    {
        const char *message = json_string(action, "invalid_message", "invalid selected brush resize distance");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        const brush_world_runtime *world_runtime =
            selection->world_name != NULL ? find_brush_world_runtime(runtime, selection->world_name) : NULL;
        const int brush_index = find_editor_mutable_brush_index(world_runtime, selection->element_name);
        const slayer3d_game_data_brush *brush = brush_index >= 0 ? &world_runtime->desc.brushes[brush_index] : NULL;
        const bool floor_sector_move =
            editor_brush_is_floor_slab(brush, action) && (distance < 0.0f || brush->bounds.max.y < 0.0f);
        const float candidate_top = brush != NULL && brush->has_bounds ? brush->bounds.max.y + distance : 0.0f;
        if ((floor_sector_move &&
             (brush == NULL || !brush->has_bounds || candidate_top < min_elevation || candidate_top > max_elevation)) ||
            (!floor_sector_move && !selected_editor_brush_can_resize_y(world_runtime, selection, distance, min_height)))
        {
            const char *message = json_string(action, "invalid_message", "selected brush resize would be invalid");
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }
    }

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    editor_command_transaction_entry *last_entry = NULL;
    int resized_count = 0;
    int applied_count = 0;
    const char *fill_material_name = json_string(action, "fill_material", "mat.editor.wall");
    const float fill_thickness = SDL_max(json_float(action, "fill_thickness", 0.2f), 0.001f);
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
        const int brush_index = find_editor_mutable_brush_index(world_runtime, selection->element_name);
        const slayer3d_game_data_brush *brush = brush_index >= 0 ? &world_runtime->desc.brushes[brush_index] : NULL;
        const bool floor_sector_move =
            editor_brush_is_floor_slab(brush, action) && (distance < 0.0f || brush->bounds.max.y < 0.0f);

        editor_command_transaction_entry *entry = NULL;
        if (floor_sector_move)
        {
            const int fill_material_index = editor_brush_material_index_by_name(world_runtime, fill_material_name);
            if (fill_material_index < 0 || !editor_append_sector_floor_transaction(
                                               runtime, active_scene, selection, brush, distance, fill_thickness,
                                               fill_material_index, fill_material_name, &entry))
            {
                goto resize_record_fail;
            }
        }
        else if (!editor_append_resize_y_transaction(runtime, active_scene, selection, brush, distance, &entry))
        {
            goto resize_record_fail;
        }

        if (!apply_editor_transaction_mutation(runtime, entry, true))
        {
            const char *message = json_string(action, "invalid_message", "selected brush resize failed");
            for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
                (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
            for (int clear = first_entry; clear < history->count; ++clear)
                free_editor_command_transaction_entry(&history->entries[clear]);
            history->count = first_entry;
            history->cursor = first_entry;
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }
        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry);
        last_entry = entry;
        resized_count++;
        applied_count++;
    }

    char message[128];
    SDL_snprintf(message, sizeof(message), resized_count == 1 ? "%s 1 selected brush" : "%s %d selected brushes",
                 distance >= 0.0f ? "raised" : "lowered", resized_count);
    publish_editor_transaction(runtime, outputs, "commit", resized_count > 0, last_entry, message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "commit", resized_count > 0,
                                               last_entry, message);

resize_record_fail: {
    const char *error_message = json_string(action, "invalid_message", "failed to record selected brush resize");
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    publish_editor_transaction(runtime, outputs, "commit", false, NULL, error_message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, error_message);
}
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
