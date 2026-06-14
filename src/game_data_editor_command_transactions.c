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
        slayer3d_properties_set_vec3(payload, "editor_transaction_rotation_pivot", entry->rotation_pivot);
        slayer3d_properties_set_vec3(payload, "editor_transaction_rotation_axis", entry->rotation_axis);
        slayer3d_properties_set_float(payload, "editor_transaction_rotation_angle_radians",
                                      entry->rotation_angle_radians);
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
        slayer3d_properties_set_vec3(payload, "editor_transaction_rotation_pivot",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(payload, "editor_transaction_rotation_axis", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_float(payload, "editor_transaction_rotation_angle_radians", 0.0f);
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
    if (entry->has_source_box_after_snapshot)
        free_editor_brush_source_box_runtime(&entry->source_box_after_snapshot);
    for (int i = 0; i < entry->source_clip_before_count; ++i)
        free_editor_brush_source_box_runtime(&entry->source_clip_before[i]);
    for (int i = 0; i < entry->source_clip_after_count; ++i)
        free_editor_brush_source_box_runtime(&entry->source_clip_after[i]);
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

static int editor_command_transaction_group_start(const editor_command_history_state *history, int index)
{
    if (history == NULL || index < 0 || index >= history->count)
        return index;
    const int transaction_id = history->entries[index].id;
    while (index > 0 && history->entries[index - 1].id == transaction_id)
        index--;
    return index;
}

static int editor_command_transaction_group_end(const editor_command_history_state *history, int index)
{
    if (history == NULL || index < 0 || index >= history->count)
        return index;
    const int transaction_id = history->entries[index].id;
    while (index + 1 < history->count && history->entries[index + 1].id == transaction_id)
        index++;
    return index;
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

static void refresh_editor_brush_selection_for_identity(const brush_world_runtime *world_runtime,
                                                        slayer3d_game_data_editor_selection *selection,
                                                        const char *brush_name, const char *brush_stable_id,
                                                        int face_index, const char *face_stable_id);
static void publish_editor_transaction_selection_state(slayer3d_game_data_runtime *runtime);

typedef struct editor_selection_identity_snapshot
{
    bool valid;
    char *world_name;
    char *element_name;
    char *element_stable_id;
    char *face_stable_id;
    int face_index;
} editor_selection_identity_snapshot;

static void free_editor_selection_identity_snapshot(editor_selection_identity_snapshot *snapshot)
{
    if (snapshot == NULL)
        return;
    SDL_free(snapshot->world_name);
    SDL_free(snapshot->element_name);
    SDL_free(snapshot->element_stable_id);
    SDL_free(snapshot->face_stable_id);
    SDL_zero(*snapshot);
    snapshot->face_index = -1;
}

static bool copy_optional_editor_snapshot_string(const char *value, char **out_value)
{
    if (out_value == NULL)
        return false;
    *out_value = NULL;
    if (value == NULL)
        return true;
    *out_value = SDL_strdup(value);
    return *out_value != NULL;
}

static bool capture_editor_selection_identity(const slayer3d_game_data_editor_selection *selection,
                                              editor_selection_identity_snapshot *snapshot)
{
    if (snapshot == NULL)
        return false;
    SDL_zero(*snapshot);
    snapshot->face_index = -1;
    if (!editor_selection_is_selectable_brush(selection))
        return true;

    const char *element_stable_id = editor_metadata_stable_id(selection->element_editor);
    const char *face_stable_id = editor_metadata_stable_id(selection->face_editor);
    if (!copy_optional_editor_snapshot_string(selection->world_name, &snapshot->world_name) ||
        !copy_optional_editor_snapshot_string(selection->element_name, &snapshot->element_name) ||
        !copy_optional_editor_snapshot_string(element_stable_id, &snapshot->element_stable_id) ||
        !copy_optional_editor_snapshot_string(face_stable_id, &snapshot->face_stable_id))
    {
        free_editor_selection_identity_snapshot(snapshot);
        return false;
    }

    snapshot->valid = true;
    snapshot->face_index = selection->face_index;
    return true;
}

static void refresh_editor_brush_selection_for_snapshot(const slayer3d_game_data_runtime *runtime,
                                                        slayer3d_game_data_editor_selection *selection,
                                                        const editor_selection_identity_snapshot *snapshot)
{
    if (selection == NULL)
        return;
    init_editor_selection(selection);
    if (runtime == NULL || snapshot == NULL || !snapshot->valid || snapshot->world_name == NULL)
        return;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, snapshot->world_name);
    refresh_editor_brush_selection_for_identity(world_runtime, selection, snapshot->element_name,
                                                snapshot->element_stable_id, snapshot->face_index,
                                                snapshot->face_stable_id);
}

static bool capture_current_editor_selection_snapshots(
    const slayer3d_game_data_runtime *runtime,
    editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY],
    int *out_selected_count, editor_selection_identity_snapshot *active_snapshot)
{
    if (out_selected_count != NULL)
        *out_selected_count = 0;
    if (runtime == NULL || selected_snapshots == NULL || out_selected_count == NULL || active_snapshot == NULL)
        return false;

    SDL_memset(selected_snapshots, 0, sizeof(selected_snapshots[0]) * SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY);
    SDL_zero(*active_snapshot);
    active_snapshot->face_index = -1;

    const int selected_count =
        editor_selected_brushes_active_for_scene(runtime) ? runtime->editor_selected_brush_count : 0;
    for (int i = 0; i < selected_count; ++i)
    {
        if (!capture_editor_selection_identity(&runtime->editor_selected_brushes[i], &selected_snapshots[i]))
        {
            for (int cleanup = 0; cleanup < i; ++cleanup)
                free_editor_selection_identity_snapshot(&selected_snapshots[cleanup]);
            return false;
        }
    }
    *out_selected_count = selected_count;

    if (!capture_editor_selection_identity(&runtime->editor_active_selection, active_snapshot))
    {
        for (int cleanup = 0; cleanup < selected_count; ++cleanup)
            free_editor_selection_identity_snapshot(&selected_snapshots[cleanup]);
        return false;
    }
    return true;
}

static void free_current_editor_selection_snapshots(
    editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY], int selected_count,
    editor_selection_identity_snapshot *active_snapshot)
{
    if (selected_snapshots != NULL)
    {
        for (int i = 0; i < selected_count; ++i)
            free_editor_selection_identity_snapshot(&selected_snapshots[i]);
    }
    free_editor_selection_identity_snapshot(active_snapshot);
}

static void refresh_editor_selections_from_snapshots(
    slayer3d_game_data_runtime *runtime,
    const editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY],
    int selected_count, const editor_selection_identity_snapshot *active_snapshot)
{
    if (runtime == NULL)
        return;

    bool selected_changed = false;
    for (int i = selected_count - 1; i >= 0 && i < runtime->editor_selected_brush_count; --i)
    {
        refresh_editor_brush_selection_for_snapshot(runtime, &runtime->editor_selected_brushes[i],
                                                    &selected_snapshots[i]);
        if (!runtime->editor_selected_brushes[i].hit)
        {
            remove_editor_selected_brush_at(runtime, i);
            selected_changed = true;
        }
    }

    if (active_snapshot != NULL && active_snapshot->valid)
    {
        refresh_editor_brush_selection_for_snapshot(runtime, &runtime->editor_active_selection, active_snapshot);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        selected_changed = true;
    }
    else if (selected_changed)
    {
        update_active_editor_selection_from_selected_brushes(runtime);
    }

    if (selected_changed)
        publish_editor_transaction_selection_state(runtime);
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

static bool remove_editor_brush_at_index(brush_world_runtime *world_runtime, int brush_index,
                                         slayer3d_game_data_brush *out_removed)
{
    if (out_removed != NULL)
        SDL_zero(*out_removed);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || brush_index < 0 ||
        brush_index >= world_runtime->desc.brush_count)
        return false;

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
    const char *brush_identity =
        brush->editor.stable_id != NULL && brush->editor.stable_id[0] != '\0' ? brush->editor.stable_id : brush->name;
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

static bool insert_editor_brush_at_index(brush_world_runtime *world_runtime, int brush_index,
                                         const slayer3d_game_data_brush *brush)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || brush == NULL)
        return false;

    if (!editor_brush_world_insert_source_box_from_brush(world_runtime, brush_index, brush, NULL, 0))
        return false;
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

static bool apply_editor_brush_translate(slayer3d_game_data_runtime *runtime,
                                         const editor_command_transaction_entry *entry, slayer3d_vec3 offset)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;
    if (slayer3d_vec3_length_squared(offset) <= 0.0000001f)
        return true;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;
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

static bool apply_editor_brush_rotate_y(slayer3d_game_data_runtime *runtime,
                                        const editor_command_transaction_entry *entry, int quarter_turns)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;

    const int normalized_turns = quarter_turns % 4;
    if (normalized_turns == 0)
        return true;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;
    const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                     ? entry->element_stable_id
                                     : entry->element_name;
    if (editor_brush_world_rotate_source_box_y_quarter_turns(world_runtime, brush_identity, normalized_turns, NULL, 0))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }
    return false;
}

static bool apply_editor_brush_rotate(slayer3d_game_data_runtime *runtime,
                                      const editor_command_transaction_entry *entry, float angle_radians)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL)
        return false;
    if (SDL_fabsf(angle_radians) <= 0.000001f)
        return true;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;
    const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                     ? entry->element_stable_id
                                     : entry->element_name;
    if (editor_brush_world_rotate_source_box(world_runtime, brush_identity, entry->rotation_pivot, entry->rotation_axis,
                                             angle_radians, NULL, 0))
    {
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }
    return false;
}

static bool apply_editor_source_box_snapshot(slayer3d_game_data_runtime *runtime,
                                             const editor_command_transaction_entry *entry,
                                             const editor_brush_source_box_runtime *snapshot)
{
    if (runtime == NULL || entry == NULL || snapshot == NULL || entry->world_name == NULL ||
        entry->element_name == NULL)
        return false;

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;

    const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                     ? entry->element_stable_id
                                     : entry->element_name;
    const int current_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
    if (current_index >= 0 && !editor_brush_world_remove_source_box_at_index(world_runtime, current_index, NULL, 0))
        return false;

    const int restored_index = SDL_clamp(entry->brush_index, 0, world_runtime->editor_source_box_count);
    if (!editor_brush_world_insert_source_box_at_index(world_runtime, restored_index, snapshot, NULL, 0))
        return false;

    editor_brush_world_mark_dirty(world_runtime);
    return true;
}

static bool apply_editor_brush_scale(slayer3d_game_data_runtime *runtime, const editor_command_transaction_entry *entry,
                                     bool forward)
{
    if (entry == NULL || !entry->has_source_box_snapshot || !entry->has_source_box_after_snapshot)
        return false;
    return apply_editor_source_box_snapshot(runtime, entry,
                                            forward ? &entry->source_box_after_snapshot : &entry->source_box_snapshot);
}

static bool editor_float_finite(float value)
{
    return !SDL_isnan(value) && !SDL_isinf(value);
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
    const int material_index = forward ? entry->material_index : entry->previous_material_index;
    const int rollback_index = forward ? entry->previous_material_index : entry->material_index;
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || material_index < 0 ||
        material_index >= world_runtime->desc.material_count)
    {
        return false;
    }

    const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                     ? entry->element_stable_id
                                     : entry->element_name;
    const int face_index = editor_brush_world_source_box_face_index_for_identity(
        world_runtime, brush_identity, entry->face_index, entry->face_stable_id);
    if (face_index < 0)
        return false;
    if (editor_brush_world_set_source_box_face_material(world_runtime, brush_identity, face_index,
                                                        world_runtime->desc.materials[material_index].name, NULL, 0))
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

static bool apply_editor_brush_face_resize(slayer3d_game_data_runtime *runtime, editor_command_transaction_entry *entry,
                                           bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL ||
        entry->face_index < 0)
    {
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                     ? entry->element_stable_id
                                     : entry->element_name;
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;

    if (!forward)
    {
        if (!entry->has_source_box_snapshot)
            return false;
        const int current_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
        if (current_index >= 0 && !editor_brush_world_remove_source_box_at_index(world_runtime, current_index, NULL, 0))
            return false;
        const int restored_index = SDL_clamp(entry->brush_index, 0, world_runtime->editor_source_box_count);
        if (!editor_brush_world_insert_source_box_at_index(world_runtime, restored_index, &entry->source_box_snapshot,
                                                           NULL, 0))
        {
            return false;
        }
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    if (!entry->has_source_box_snapshot)
    {
        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity, &entry->source_box_snapshot,
                                                            &entry->brush_index, NULL, 0))
        {
            return false;
        }
        entry->has_source_box_snapshot = true;
    }

    slayer3d_vec3 face_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    int face_index = -1;
    if (!editor_brush_world_source_box_face_normal_for_identity(world_runtime, brush_identity, entry->face_index,
                                                                entry->face_stable_id, &face_index, &face_normal))
    {
        return false;
    }
    const float distance = slayer3d_vec3_dot(face_normal, entry->offset);
    if (!editor_brush_world_resize_source_face(world_runtime, brush_identity, entry->face_index, entry->face_stable_id,
                                               distance, NULL, NULL, 0))
    {
        return false;
    }
    editor_brush_world_mark_dirty(world_runtime);
    return true;
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
        if (!world_runtime->editor_has_source_model || !entry->has_source_box_snapshot)
            return false;
        if (!editor_brush_world_insert_source_box_at_index(world_runtime, entry->brush_index,
                                                           &entry->source_box_snapshot, NULL, 0))
        {
            return false;
        }
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    const int brush_index =
        find_editor_mutable_brush_index_by_identity(world_runtime, entry->element_name, entry->element_stable_id);
    if (brush_index < 0)
        return false;
    if (!world_runtime->editor_has_source_model)
        return false;

    const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                     ? entry->element_stable_id
                                     : entry->element_name;
    if (!entry->has_source_box_snapshot)
    {
        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity, &entry->source_box_snapshot,
                                                            &entry->brush_index, NULL, 0))
            return false;
        entry->has_source_box_snapshot = true;
    }

    const bool clears_active_selection =
        runtime->editor_active_selection.hit && runtime->editor_active_selection.world_name != NULL &&
        SDL_strcmp(runtime->editor_active_selection.world_name, entry->world_name) == 0 &&
        editor_selection_matches_transaction_element(&runtime->editor_active_selection, entry);

    if (!editor_brush_world_remove_source_box_at_index(world_runtime, entry->brush_index, NULL, 0))
        return false;
    editor_brush_world_mark_dirty(world_runtime);

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
        if (!world_runtime->editor_has_source_model)
            return false;
        const int source_index = editor_brush_world_find_source_box_index(world_runtime, brush_identity);
        if (source_index < 0)
            return true;
        if (!editor_brush_world_remove_source_box_at_index(world_runtime, source_index, NULL, 0))
            return false;
        editor_brush_world_mark_dirty(world_runtime);
        return true;
    }

    if (!world_runtime->editor_has_source_model || !entry->has_source_box_snapshot)
        return false;

    const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                     ? entry->element_stable_id
                                     : entry->element_name;
    if (editor_brush_world_find_source_box_index(world_runtime, brush_identity) >= 0)
        return true;
    if (!editor_brush_world_insert_source_box_at_index(world_runtime, entry->brush_index, &entry->source_box_snapshot,
                                                       NULL, 0))
    {
        return false;
    }
    editor_brush_world_mark_dirty(world_runtime);
    return true;
}

static bool editor_floor_fill_name(const char *brush_name, float low_y, float high_y, const char *side, char *buffer,
                                   size_t buffer_size);
static slayer3d_bounding_box editor_floor_fill_bounds(slayer3d_bounding_box floor_bounds, float low_y, float high_y,
                                                      float thickness, int side_index);
static void translate_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                              const editor_command_transaction_entry *entry,
                                                              slayer3d_vec3 offset, bool active_matches);

static void refresh_editor_brush_selection_for_identity(const brush_world_runtime *world_runtime,
                                                        slayer3d_game_data_editor_selection *selection,
                                                        const char *brush_name, const char *brush_stable_id,
                                                        int face_index, const char *face_stable_id)
{
    if (world_runtime == NULL || selection == NULL ||
        ((brush_name == NULL || brush_name[0] == '\0') && (brush_stable_id == NULL || brush_stable_id[0] == '\0')))
    {
        return;
    }

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    selection->hit = false;
    selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
    selection->world_name = world->name;
    selection->world_editor = &world->editor;
    selection->element_name = NULL;
    selection->material_name = NULL;
    selection->element_index = -1;
    selection->element_editor = NULL;
    selection->face_editor = NULL;
    selection->material_editor = NULL;

    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (!editor_brush_matches_identity(brush, brush_name, brush_stable_id))
            continue;

        selection->hit = true;
        selection->element_name = brush->name;
        selection->element_index = brush_index;
        selection->element_editor = &brush->editor;
        selection->has_bounds = brush->has_bounds;
        if (brush->has_bounds)
            selection->bounds = brush->bounds;

        const int resolved_face_index = editor_face_index_for_identity(brush, face_index, face_stable_id);
        selection->face_index = resolved_face_index;
        if (resolved_face_index >= 0 && resolved_face_index < brush->face_count)
        {
            const slayer3d_game_data_brush_face *face = &brush->faces[resolved_face_index];
            selection->face_editor = &face->editor;
            selection->material_name = face->material_name;
            if (face->material_index >= 0 && face->material_index < world->material_count)
                selection->material_editor = &world->materials[face->material_index].editor;
        }
        return;
    }
}

static void publish_editor_transaction_selection_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    publish_editor_selected_brush_count(runtime);
}

static void remove_editor_selected_brushes_for_transaction(slayer3d_game_data_runtime *runtime,
                                                           const editor_command_transaction_entry *entry)
{
    if (runtime == NULL || entry == NULL)
        return;

    bool removed_any = false;
    if (editor_selected_brushes_active_for_scene(runtime))
    {
        for (int i = runtime->editor_selected_brush_count - 1; i >= 0; --i)
        {
            if (editor_selection_matches_transaction_element(&runtime->editor_selected_brushes[i], entry))
            {
                remove_editor_selected_brush_at(runtime, i);
                removed_any = true;
            }
        }
    }

    if (runtime->editor_active_selection.hit &&
        editor_selection_matches_transaction_element(&runtime->editor_active_selection, entry))
    {
        init_editor_selection(&runtime->editor_active_selection);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        removed_any = true;
    }

    if (removed_any)
    {
        clear_editor_selected_vertices(runtime);
        update_active_editor_selection_from_selected_brushes(runtime);
        publish_editor_transaction_selection_state(runtime);
    }
}

static void refresh_editor_selected_brushes_for_transaction(slayer3d_game_data_runtime *runtime,
                                                            const editor_command_transaction_entry *entry)
{
    if (runtime == NULL || entry == NULL)
        return;

    const brush_world_runtime *world_runtime =
        entry->world_name != NULL ? find_brush_world_runtime(runtime, entry->world_name) : NULL;
    if (world_runtime == NULL)
        return;

    bool changed = false;
    if (editor_selected_brushes_active_for_scene(runtime))
    {
        for (int i = runtime->editor_selected_brush_count - 1; i >= 0; --i)
        {
            slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
            if (!editor_selection_matches_transaction_element(selection, entry))
                continue;
            const int face_index = selection->face_index;
            refresh_editor_brush_selection_for_identity(world_runtime, selection, entry->element_name,
                                                        entry->element_stable_id, face_index, entry->face_stable_id);
            if (!selection->hit)
                remove_editor_selected_brush_at(runtime, i);
            changed = true;
        }
    }

    if (runtime->editor_active_selection.hit &&
        editor_selection_matches_transaction_element(&runtime->editor_active_selection, entry))
    {
        const int face_index = runtime->editor_active_selection.face_index;
        refresh_editor_brush_selection_for_identity(world_runtime, &runtime->editor_active_selection,
                                                    entry->element_name, entry->element_stable_id, face_index,
                                                    entry->face_stable_id);
        changed = true;
    }
    else if (changed)
    {
        update_active_editor_selection_from_selected_brushes(runtime);
    }

    if (changed)
        publish_editor_transaction_selection_state(runtime);
}

static void select_editor_brush_for_transaction(slayer3d_game_data_runtime *runtime,
                                                const editor_command_transaction_entry *entry)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL)
        return;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    slayer3d_game_data_editor_selection selection;
    init_editor_selection(&selection);
    refresh_editor_brush_selection_for_identity(world_runtime, &selection, entry->element_name,
                                                entry->element_stable_id, -1, NULL);
    if (!selection.hit)
        return;

    clear_editor_selected_brushes(runtime);
    (void)add_editor_selected_brush(runtime, &selection);
    update_active_editor_selection_from_selected_brushes(runtime);
    publish_editor_transaction_selection_state(runtime);
}

static void sync_editor_selection_after_transaction(slayer3d_game_data_runtime *runtime,
                                                    const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || entry->command == NULL)
        return;

    if (SDL_strcmp(entry->command, "delete") == 0)
    {
        if (forward)
            remove_editor_selected_brushes_for_transaction(runtime, entry);
        else
            select_editor_brush_for_transaction(runtime, entry);
        return;
    }

    if (SDL_strcmp(entry->command, "duplicate") == 0)
    {
        if (forward)
            select_editor_brush_for_transaction(runtime, entry);
        else
            remove_editor_selected_brushes_for_transaction(runtime, entry);
        return;
    }

    if (SDL_strcmp(entry->command, "create") == 0)
    {
        if (!forward)
            remove_editor_selected_brushes_for_transaction(runtime, entry);
        return;
    }

    refresh_editor_selected_brushes_for_transaction(runtime, entry);
}

static const char *editor_source_box_transaction_identity(const editor_brush_source_box_runtime *box)
{
    if (box == NULL)
        return NULL;
    if (box->stable_id != NULL && box->stable_id[0] != '\0')
        return box->stable_id;
    return box->name != NULL && box->name[0] != '\0' ? box->name : NULL;
}

static bool editor_source_box_transaction_identity_matches(const editor_brush_source_box_runtime *box,
                                                           const char *identity)
{
    return box != NULL && identity != NULL && identity[0] != '\0' &&
           ((box->stable_id != NULL && SDL_strcmp(box->stable_id, identity) == 0) ||
            (box->name != NULL && SDL_strcmp(box->name, identity) == 0));
}

static int editor_source_box_transaction_index(const editor_brush_source_box_runtime *boxes, int count,
                                               const editor_brush_source_box_runtime *box)
{
    const char *identity = editor_source_box_transaction_identity(box);
    for (int i = 0; boxes != NULL && i < count; ++i)
    {
        if (editor_source_box_transaction_identity_matches(&boxes[i], identity))
            return i;
    }
    return -1;
}

static bool editor_source_box_copy_to_array(const editor_brush_source_box_runtime *source,
                                            editor_brush_source_box_runtime *dest, int *dest_count)
{
    if (source == NULL || dest == NULL || dest_count == NULL)
        return false;
    if (!copy_editor_brush_source_box_runtime(source, &dest[*dest_count]))
        return false;
    (*dest_count)++;
    return true;
}

static void editor_source_box_free_array(editor_brush_source_box_runtime *boxes, int count)
{
    for (int i = 0; boxes != NULL && i < count; ++i)
        free_editor_brush_source_box_runtime(&boxes[i]);
}

static bool editor_brush_world_commit_source_box_array(brush_world_runtime *world_runtime,
                                                       editor_brush_source_box_runtime *new_boxes, int new_count)
{
    if (world_runtime == NULL || !world_runtime->editor_has_source_model || new_count < 0 ||
        (new_count > 0 && new_boxes == NULL))
    {
        return false;
    }

    editor_brush_source_box_runtime *old_boxes = world_runtime->editor_source_boxes;
    const int old_count = world_runtime->editor_source_box_count;
    world_runtime->editor_source_boxes = new_boxes;
    world_runtime->editor_source_box_count = new_count;
    world_runtime->editor_source_box_capacity = new_count;
    if (!editor_brush_world_rebuild_from_source(world_runtime, NULL, 0))
    {
        world_runtime->editor_source_boxes = old_boxes;
        world_runtime->editor_source_box_count = old_count;
        world_runtime->editor_source_box_capacity = old_count;
        (void)editor_brush_world_rebuild_from_source(world_runtime, NULL, 0);
        return false;
    }

    for (int i = 0; i < old_count; ++i)
        free_editor_brush_source_box_runtime(&old_boxes[i]);
    SDL_free(old_boxes);
    editor_brush_world_mark_dirty(world_runtime);
    return true;
}

static bool apply_editor_source_clip_forward(brush_world_runtime *world_runtime,
                                             const editor_command_transaction_entry *entry)
{
    if (world_runtime == NULL || entry == NULL || entry->source_clip_before_count <= 0 ||
        entry->source_clip_after_count <= 0)
    {
        return false;
    }

    const int old_count = world_runtime->editor_source_box_count;
    const int new_capacity = old_count - entry->source_clip_before_count + entry->source_clip_after_count;
    if (new_capacity <= 0)
        return false;
    editor_brush_source_box_runtime *new_boxes =
        (editor_brush_source_box_runtime *)SDL_calloc((size_t)new_capacity, sizeof(*new_boxes));
    if (new_boxes == NULL)
        return false;

    int removed_count = 0;
    int write_count = 0;
    for (int i = 0; i < old_count; ++i)
    {
        const int before_index = editor_source_box_transaction_index(
            entry->source_clip_before, entry->source_clip_before_count, &world_runtime->editor_source_boxes[i]);
        if (before_index < 0)
        {
            if (!editor_source_box_copy_to_array(&world_runtime->editor_source_boxes[i], new_boxes, &write_count))
                goto fail;
            continue;
        }

        removed_count++;
        const int source_index = entry->source_clip_before_indices[before_index];
        for (int output_index = 0; output_index < entry->source_clip_after_count; ++output_index)
        {
            if (entry->source_clip_after_source_indices[output_index] == source_index &&
                !editor_source_box_copy_to_array(&entry->source_clip_after[output_index], new_boxes, &write_count))
            {
                goto fail;
            }
        }
    }

    if (removed_count != entry->source_clip_before_count || write_count != new_capacity ||
        !editor_brush_world_commit_source_box_array(world_runtime, new_boxes, write_count))
    {
        goto fail;
    }
    return true;

fail:
    editor_source_box_free_array(new_boxes, write_count);
    SDL_free(new_boxes);
    return false;
}

static int editor_source_clip_before_index_at_final_position(const editor_command_transaction_entry *entry,
                                                             int final_index)
{
    for (int i = 0; entry != NULL && i < entry->source_clip_before_count; ++i)
    {
        if (entry->source_clip_before_indices[i] == final_index)
            return i;
    }
    return -1;
}

static bool apply_editor_source_clip_undo(brush_world_runtime *world_runtime,
                                          const editor_command_transaction_entry *entry)
{
    if (world_runtime == NULL || entry == NULL || entry->source_clip_before_count <= 0 ||
        entry->source_clip_after_count <= 0)
    {
        return false;
    }

    const int old_count = world_runtime->editor_source_box_count;
    const int base_capacity = old_count - entry->source_clip_after_count;
    const int final_count = base_capacity + entry->source_clip_before_count;
    if (base_capacity < 0 || final_count <= 0)
        return false;
    editor_brush_source_box_runtime *base_boxes =
        base_capacity > 0 ? (editor_brush_source_box_runtime *)SDL_calloc((size_t)base_capacity, sizeof(*base_boxes))
                          : NULL;
    editor_brush_source_box_runtime *new_boxes =
        (editor_brush_source_box_runtime *)SDL_calloc((size_t)final_count, sizeof(*new_boxes));
    if ((base_capacity > 0 && base_boxes == NULL) || new_boxes == NULL)
    {
        SDL_free(base_boxes);
        SDL_free(new_boxes);
        return false;
    }

    int removed_count = 0;
    int base_count = 0;
    int write_count = 0;
    for (int i = 0; i < old_count; ++i)
    {
        if (editor_source_box_transaction_index(entry->source_clip_after, entry->source_clip_after_count,
                                                &world_runtime->editor_source_boxes[i]) >= 0)
        {
            removed_count++;
            continue;
        }
        if (!editor_source_box_copy_to_array(&world_runtime->editor_source_boxes[i], base_boxes, &base_count))
            goto fail;
    }
    if (removed_count != entry->source_clip_after_count || base_count != base_capacity)
        goto fail;

    int base_index = 0;
    for (int final_index = 0; final_index < final_count; ++final_index)
    {
        const int before_index = editor_source_clip_before_index_at_final_position(entry, final_index);
        const editor_brush_source_box_runtime *source =
            before_index >= 0 ? &entry->source_clip_before[before_index]
                              : (base_index < base_count ? &base_boxes[base_index++] : NULL);
        if (source == NULL || !editor_source_box_copy_to_array(source, new_boxes, &write_count))
            goto fail;
    }

    editor_source_box_free_array(base_boxes, base_count);
    SDL_free(base_boxes);
    if (write_count != final_count ||
        !editor_brush_world_commit_source_box_array(world_runtime, new_boxes, write_count))
        goto fail_new_only;
    return true;

fail:
    editor_source_box_free_array(base_boxes, base_count);
    SDL_free(base_boxes);
fail_new_only:
    editor_source_box_free_array(new_boxes, write_count);
    SDL_free(new_boxes);
    return false;
}

static bool apply_editor_source_clip_transaction(slayer3d_game_data_runtime *runtime,
                                                 const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL)
        return false;
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;
    return forward ? apply_editor_source_clip_forward(world_runtime, entry)
                   : apply_editor_source_clip_undo(world_runtime, entry);
}

static void select_editor_source_clip_transaction_brushes(slayer3d_game_data_runtime *runtime,
                                                          const editor_command_transaction_entry *entry, bool forward)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL)
        return;
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    if (world_runtime == NULL)
        return;

    const editor_brush_source_box_runtime *boxes = forward ? entry->source_clip_after : entry->source_clip_before;
    const int count = forward ? entry->source_clip_after_count : entry->source_clip_before_count;
    clear_editor_selected_brushes(runtime);
    for (int i = 0; i < count; ++i)
    {
        slayer3d_game_data_editor_selection selection;
        init_editor_selection(&selection);
        const char *identity = editor_source_box_transaction_identity(&boxes[i]);
        refresh_editor_brush_selection_for_identity(world_runtime, &selection, identity, identity, -1, NULL);
        if (selection.hit)
            (void)add_editor_selected_brush(runtime, &selection);
    }
    update_active_editor_selection_from_selected_brushes(runtime);
    publish_editor_transaction_selection_state(runtime);
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

    const bool active_matches = runtime->editor_active_selection.hit &&
                                editor_selection_matches_transaction_element(&runtime->editor_active_selection, entry);

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

    translate_active_editor_selection_for_transaction(runtime, entry, offset, active_matches);
    return true;
}

static void translate_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                              const editor_command_transaction_entry *entry,
                                                              slayer3d_vec3 offset, bool active_matches)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit || !active_matches)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0)
    {
        return;
    }

    selection->point = slayer3d_vec3_add(selection->point, offset);
    selection->world_position = slayer3d_vec3_add(selection->world_position, offset);
    if (selection->has_bounds)
        selection->bounds = translated_bounds(selection->bounds, offset);

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    refresh_editor_brush_selection_for_identity(world_runtime, selection, entry->element_name, entry->element_stable_id,
                                                selection->face_index, entry->face_stable_id);
    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static void resize_active_editor_selection_for_transaction(slayer3d_game_data_runtime *runtime,
                                                           const editor_command_transaction_entry *entry, bool forward,
                                                           bool active_matches)
{
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit || !active_matches)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0)
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
    refresh_editor_brush_selection_for_identity(world_runtime, selection, entry->element_name, entry->element_stable_id,
                                                entry->face_index, entry->face_stable_id);

    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), selection);
}

static void update_active_editor_selection_material_for_transaction(slayer3d_game_data_runtime *runtime,
                                                                    const editor_command_transaction_entry *entry,
                                                                    bool forward, bool active_matches)
{
    (void)forward;
    if (runtime == NULL || entry == NULL || !runtime->editor_active_selection.hit || !active_matches)
        return;
    slayer3d_game_data_editor_selection *selection = &runtime->editor_active_selection;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || entry->scene == NULL || SDL_strcmp(active_scene, entry->scene) != 0 ||
        selection->world_name == NULL || entry->world_name == NULL ||
        SDL_strcmp(selection->world_name, entry->world_name) != 0)
    {
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    refresh_editor_brush_selection_for_identity(world_runtime, selection, entry->element_name, entry->element_stable_id,
                                                entry->face_index, entry->face_stable_id);

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
           (SDL_strcmp(entry->command, "rotate") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "rotate_y") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "scale") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           ((SDL_strcmp(entry->command, "flip_vertical") == 0 || SDL_strcmp(entry->command, "flip_horizontal") == 0) &&
            SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "shear") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "sector_floor") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "clip") == 0 && SDL_strcmp(entry->target, "selection") == 0) ||
           ((SDL_strcmp(entry->command, "create") == 0 || SDL_strcmp(entry->command, "duplicate") == 0) &&
            SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "delete") == 0 && SDL_strcmp(entry->target, "element") == 0) ||
           (SDL_strcmp(entry->command, "paint") == 0 && SDL_strcmp(entry->target, "face") == 0) ||
           (SDL_strcmp(entry->command, "color") == 0 &&
            (SDL_strcmp(entry->target, "element") == 0 || SDL_strcmp(entry->target, "face") == 0)) ||
           ((SDL_strcmp(entry->command, "resize") == 0 || SDL_strcmp(entry->command, "extrude") == 0) &&
            SDL_strcmp(entry->target, "face") == 0);
}

static bool apply_editor_transaction_mutation(slayer3d_game_data_runtime *runtime,
                                              editor_command_transaction_entry *entry, bool forward)
{
    if (!editor_transaction_has_brush_mutation(entry))
        return true;

    const bool active_element_matches =
        runtime != NULL && runtime->editor_active_selection.hit &&
        editor_selection_matches_transaction_element(&runtime->editor_active_selection, entry);
    const bool active_face_matches =
        runtime != NULL && runtime->editor_active_selection.hit &&
        editor_selection_matches_transaction_face(&runtime->editor_active_selection, entry);

    if (SDL_strcmp(entry->command, "clip") == 0)
    {
        if (!apply_editor_source_clip_transaction(runtime, entry, forward))
            return false;
        select_editor_source_clip_transaction_brushes(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "delete") == 0)
    {
        if (forward)
            sync_editor_selection_after_transaction(runtime, entry, forward);
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_delete(runtime, entry, forward))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        if (!forward)
            sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "create") == 0 || SDL_strcmp(entry->command, "duplicate") == 0)
    {
        if (!forward)
            sync_editor_selection_after_transaction(runtime, entry, forward);
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_create(runtime, entry, forward))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        if (forward)
            sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "sector_floor") == 0)
    {
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_sector_floor(runtime, entry, forward))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "paint") == 0)
    {
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_paint(runtime, entry, forward))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        update_active_editor_selection_material_for_transaction(runtime, entry, forward, active_face_matches);
        sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "color") == 0)
    {
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_scale(runtime, entry, forward))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        update_active_editor_selection_material_for_transaction(runtime, entry, forward, active_face_matches);
        sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "resize") == 0 || SDL_strcmp(entry->command, "extrude") == 0)
    {
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_face_resize(runtime, entry, forward))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        resize_active_editor_selection_for_transaction(runtime, entry, forward, active_face_matches);
        sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "rotate_y") == 0)
    {
        const int quarter_turns = forward ? entry->rotation_quarter_turns : -entry->rotation_quarter_turns;
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_rotate_y(runtime, entry, quarter_turns))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        update_active_editor_selection_material_for_transaction(runtime, entry, forward, active_element_matches);
        sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "rotate") == 0)
    {
        const float angle_radians = forward ? entry->rotation_angle_radians : -entry->rotation_angle_radians;
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_rotate(runtime, entry, angle_radians))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        update_active_editor_selection_material_for_transaction(runtime, entry, forward, active_element_matches);
        sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }
    if (SDL_strcmp(entry->command, "scale") == 0 || SDL_strcmp(entry->command, "flip_vertical") == 0 ||
        SDL_strcmp(entry->command, "flip_horizontal") == 0 || SDL_strcmp(entry->command, "shear") == 0)
    {
        editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
        editor_selection_identity_snapshot active_snapshot;
        int selected_snapshot_count = 0;
        if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                        &active_snapshot))
        {
            return false;
        }
        if (!apply_editor_brush_scale(runtime, entry, forward))
        {
            free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
            return false;
        }
        refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count,
                                                 &active_snapshot);
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        update_active_editor_selection_material_for_transaction(runtime, entry, forward, active_element_matches);
        sync_editor_selection_after_transaction(runtime, entry, forward);
        return true;
    }

    const slayer3d_vec3 offset = forward ? entry->offset : slayer3d_vec3_scale(entry->offset, -1.0f);
    editor_selection_identity_snapshot selected_snapshots[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    editor_selection_identity_snapshot active_snapshot;
    int selected_snapshot_count = 0;
    if (!capture_current_editor_selection_snapshots(runtime, selected_snapshots, &selected_snapshot_count,
                                                    &active_snapshot))
    {
        return false;
    }
    if (!apply_editor_brush_translate(runtime, entry, offset))
    {
        free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
        return false;
    }
    refresh_editor_selections_from_snapshots(runtime, selected_snapshots, selected_snapshot_count, &active_snapshot);
    free_current_editor_selection_snapshots(selected_snapshots, selected_snapshot_count, &active_snapshot);
    translate_active_editor_selection_for_transaction(runtime, entry, offset, active_element_matches);
    sync_editor_selection_after_transaction(runtime, entry, forward);
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

static bool transaction_editor_selection_is_selectable_brush(const slayer3d_game_data_editor_selection *selection)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
           selection->world_name != NULL && selection->world_name[0] != '\0' && selection->element_name != NULL &&
           selection->element_name[0] != '\0';
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
                                                                 const editor_command_transaction_entry *entry,
                                                                 int selected_index)
{
    if (runtime == NULL || entry == NULL || entry->world_name == NULL || entry->element_name == NULL ||
        selected_index < 0 || selected_index >= runtime->editor_selected_brush_count)
    {
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, entry->world_name);
    refresh_editor_brush_selection_for_identity(
        world_runtime, &runtime->editor_selected_brushes[selected_index], entry->element_name, entry->element_stable_id,
        runtime->editor_selected_brushes[selected_index].face_index, entry->face_stable_id);
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
    const float half_thickness = SDL_max(thickness * 0.5f, 0.001f);
    switch (side_index)
    {
    case 0:
        bounds.min.x = floor_bounds.min.x - half_thickness;
        bounds.max.x = floor_bounds.min.x + half_thickness;
        break;
    case 1:
        bounds.min.x = floor_bounds.max.x - half_thickness;
        bounds.max.x = floor_bounds.max.x + half_thickness;
        break;
    case 2:
        bounds.min.z = floor_bounds.min.z - half_thickness;
        bounds.max.z = floor_bounds.min.z + half_thickness;
        break;
    default:
        bounds.min.z = floor_bounds.max.z - half_thickness;
        bounds.max.z = floor_bounds.max.z + half_thickness;
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

static bool editor_source_clip_transaction_capture_before(const brush_world_runtime *world_runtime,
                                                          const editor_brush_source_clip_desc *desc,
                                                          editor_command_transaction_entry *entry)
{
    if (world_runtime == NULL || desc == NULL || entry == NULL || desc->brush_count <= 0 ||
        desc->brush_count > SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY)
    {
        return false;
    }
    for (int i = 0; i < desc->brush_count; ++i)
    {
        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, desc->brush_identities[i],
                                                            &entry->source_clip_before[i],
                                                            &entry->source_clip_before_indices[i], NULL, 0))
        {
            return false;
        }
        entry->source_clip_before_count++;
    }
    return true;
}

static bool editor_source_clip_transaction_capture_after(const editor_brush_source_clip_result *result,
                                                         editor_command_transaction_entry *entry)
{
    if (result == NULL || entry == NULL || !result->valid || result->output_brush_count <= 0 ||
        result->output_brush_count > SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY)
    {
        return false;
    }
    for (int i = 0; i < result->output_brush_count; ++i)
    {
        if (!copy_editor_brush_source_box_runtime(&result->output_brushes[i], &entry->source_clip_after[i]))
            return false;
        entry->source_clip_after_source_indices[i] = result->output_source_indices[i];
        entry->source_clip_after_count++;
    }
    return true;
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

static void editor_command_history_discard_last(slayer3d_game_data_runtime *runtime,
                                                editor_command_transaction_entry *entry)
{
    if (runtime == NULL || entry == NULL)
        return;
    editor_command_history_state *history = &runtime->editor_command_history;
    if (history->count <= 0 || entry != &history->entries[history->count - 1])
        return;
    free_editor_command_transaction_entry(entry);
    history->count--;
    history->cursor = history->count;
}

bool slayer3d_game_data_commit_editor_source_clip(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                  const editor_brush_source_clip_desc *desc,
                                                  editor_brush_source_clip_result *out_result, char *error_buffer,
                                                  int error_buffer_size)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || desc == NULL || desc->brush_count <= 0 ||
        desc->brush_identities == NULL)
    {
        set_error(error_buffer, error_buffer_size, "source clip commit requires selected brushes");
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
    {
        set_error(error_buffer, error_buffer_size, "source clip commit requires a source-backed brush world");
        return false;
    }

    editor_brush_source_clip_result preview;
    SDL_zero(preview);
    if (!editor_brush_world_preview_source_clip_operation(world_runtime, desc, &preview, error_buffer,
                                                          error_buffer_size))
    {
        if (out_result != NULL)
            *out_result = preview;
        return false;
    }

    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (entry == NULL ||
        !editor_prepare_transaction_common(entry, slayer3d_game_data_active_scene(runtime), "clip", "selection",
                                           world_name, desc->brush_identities[0]) ||
        !copy_editor_transaction_string(desc->brush_identities[0], &entry->element_stable_id) ||
        !editor_source_clip_transaction_capture_before(world_runtime, desc, entry) ||
        !editor_source_clip_transaction_capture_after(&preview, entry))
    {
        editor_command_history_discard_last(runtime, entry);
        editor_brush_world_free_source_clip_result(&preview);
        set_error(error_buffer, error_buffer_size, "failed to record source clip transaction");
        return false;
    }

    entry->has_bounds = true;
    entry->bounds = editor_brush_source_box_bounds_meters(world_runtime, &entry->source_clip_after[0]);
    SDL_snprintf(entry->message, sizeof(entry->message), "clipped %d brush%s into %d brush%s",
                 entry->source_clip_before_count, entry->source_clip_before_count == 1 ? "" : "es",
                 entry->source_clip_after_count, entry->source_clip_after_count == 1 ? "" : "es");

    if (!apply_editor_transaction_mutation(runtime, entry, true))
    {
        editor_command_history_discard_last(runtime, entry);
        editor_brush_world_free_source_clip_result(&preview);
        set_error(error_buffer, error_buffer_size, "failed to apply source clip transaction");
        return false;
    }

    if (out_result != NULL)
        *out_result = preview;
    else
        editor_brush_world_free_source_clip_result(&preview);
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
        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry, i);
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

bool slayer3d_game_data_create_editor_source_box_brush(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                       const char *material_name, unsigned int contents,
                                                       const int source_min[3], const int source_max[3],
                                                       editor_brush_source_prefab_result *out_result)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || material_name == NULL ||
        material_name[0] == '\0' || source_min == NULL || source_max == NULL)
    {
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;

    char brush_name[256];
    if (!editor_brush_world_generate_brush_name(world_runtime, brush_name, sizeof(brush_name)))
        return false;

    editor_brush_source_prefab_desc desc;
    SDL_zero(desc);
    desc.prefab = "editor.box";
    desc.material = material_name;
    desc.contents = contents != 0u ? contents : SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;

    editor_brush_source_prefab_result result;
    char error_buffer[256] = {0};
    if (!editor_brush_world_run_source_prefab_command(world_runtime, &desc, brush_name, source_min, source_max, false,
                                                      &result, error_buffer, sizeof(error_buffer)) ||
        !result.valid || result.no_op)
    {
        if (out_result != NULL)
        {
            *out_result = result;
            if (error_buffer[0] != '\0')
                SDL_strlcpy(out_result->warning, error_buffer, sizeof(out_result->warning));
        }
        return false;
    }

    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (entry == NULL)
        return false;

    if (!editor_prepare_transaction_common(entry, slayer3d_game_data_active_scene(runtime), "create", "element",
                                           world_name, brush_name) ||
        !copy_editor_transaction_string(brush_name, &entry->element_stable_id))
    {
        editor_command_history_state *history = &runtime->editor_command_history;
        free_editor_command_transaction_entry(entry);
        history->count--;
        history->cursor = history->count;
        return false;
    }

    editor_brush_source_box_runtime *box = &entry->source_box_snapshot;
    SDL_zero(*box);
    box->stable_id = SDL_strdup(brush_name);
    box->name = SDL_strdup(brush_name);
    box->prefab = SDL_strdup("editor.box");
    box->material = SDL_strdup(material_name);
    if (box->stable_id == NULL || box->name == NULL || box->prefab == NULL || box->material == NULL)
    {
        editor_command_history_state *history = &runtime->editor_command_history;
        free_editor_command_transaction_entry(entry);
        history->count--;
        history->cursor = history->count;
        return false;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        box->min[axis] = source_min[axis];
        box->max[axis] = source_max[axis];
    }
    box->contents = desc.contents;
    entry->has_source_box_snapshot = true;
    entry->brush_index = world_runtime->editor_source_box_count;
    entry->has_bounds = true;
    entry->bounds = result.bounds;
    SDL_snprintf(entry->message, sizeof(entry->message), "created %s", brush_name);

    if (!apply_editor_transaction_mutation(runtime, entry, true))
    {
        editor_command_history_state *history = &runtime->editor_command_history;
        free_editor_command_transaction_entry(entry);
        history->count--;
        history->cursor = history->count;
        return false;
    }

    if (out_result != NULL)
        *out_result = result;
    if (runtime->scene_state != NULL)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", entry->message);
    return true;
}

static int editor_transaction_source_units_from_meters(const brush_world_runtime *world_runtime, float value)
{
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    return (int)SDL_lroundf(value / meters_per_unit);
}

static void translate_editor_source_box_snapshot(editor_brush_source_box_runtime *box, const int delta[3])
{
    if (box == NULL || delta == NULL)
        return;
    for (int axis = 0; axis < 3; ++axis)
    {
        box->min[axis] += delta[axis];
        box->max[axis] += delta[axis];
    }
    for (int vertex = 0; vertex < box->vertex_count; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
            box->vertices[vertex][axis] += delta[axis];
    }
}

static bool assign_editor_source_box_snapshot_identity(editor_brush_source_box_runtime *box, const char *identity)
{
    if (box == NULL || identity == NULL || identity[0] == '\0')
        return false;
    char *stable_id = SDL_strdup(identity);
    char *name = SDL_strdup(identity);
    if (stable_id == NULL || name == NULL)
    {
        SDL_free(stable_id);
        SDL_free(name);
        return false;
    }
    SDL_free(box->stable_id);
    SDL_free(box->name);
    box->stable_id = stable_id;
    box->name = name;
    return true;
}

static bool append_editor_brush_duplicate_transaction(slayer3d_game_data_runtime *runtime, const char *active_scene,
                                                      const slayer3d_game_data_editor_selection *selection,
                                                      const int source_delta[3],
                                                      editor_command_transaction_entry **out_entry)
{
    if (out_entry != NULL)
        *out_entry = NULL;
    if (runtime == NULL || selection == NULL || source_delta == NULL ||
        !transaction_editor_selection_is_selectable_brush(selection))
    {
        return false;
    }

    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection->world_name);
    if (world_runtime == NULL || !world_runtime->editor_has_source_model)
        return false;

    const char *source_identity = editor_metadata_stable_id(selection->element_editor);
    if (source_identity == NULL || source_identity[0] == '\0')
        source_identity = selection->element_name;

    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (entry == NULL)
        return false;

    char brush_name[256];
    if (!editor_brush_world_generate_brush_name(world_runtime, brush_name, sizeof(brush_name)) ||
        !editor_prepare_transaction_common(entry, active_scene, "duplicate", "element", selection->world_name,
                                           brush_name) ||
        !copy_editor_transaction_string(brush_name, &entry->element_stable_id) ||
        !editor_brush_world_copy_source_box_by_identity(world_runtime, source_identity, &entry->source_box_snapshot,
                                                        NULL, NULL, 0) ||
        !assign_editor_source_box_snapshot_identity(&entry->source_box_snapshot, brush_name))
    {
        return false;
    }

    entry->has_source_box_snapshot = true;
    translate_editor_source_box_snapshot(&entry->source_box_snapshot, source_delta);
    if (!editor_brush_world_validate_source_box_candidate(world_runtime, &entry->source_box_snapshot, -1, NULL, 0))
        return false;

    entry->brush_index = world_runtime->editor_source_box_count;
    entry->has_bounds = true;
    entry->bounds = editor_brush_source_box_bounds_meters(world_runtime, &entry->source_box_snapshot);
    const float meters_per_unit =
        world_runtime->editor_source_meters_per_unit > 0.0f ? world_runtime->editor_source_meters_per_unit : 0.001f;
    entry->offset =
        slayer3d_vec3_make((float)source_delta[0] * meters_per_unit, (float)source_delta[1] * meters_per_unit,
                           (float)source_delta[2] * meters_per_unit);
    SDL_snprintf(entry->message, sizeof(entry->message), "duplicated %s", selection->element_name);
    if (out_entry != NULL)
        *out_entry = entry;
    return true;
}

bool slayer3d_game_data_duplicate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset,
                                                          bool use_last_offset)
{
    if (runtime == NULL || runtime->editor_selected_brush_count <= 0 || runtime->editor_selected_brush_scene == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
        return false;

    slayer3d_vec3 duplicate_offset = offset;
    if (use_last_offset && runtime->editor_has_last_duplicate_offset &&
        slayer3d_vec3_length_squared(offset) <= 0.0000001f)
    {
        duplicate_offset = runtime->editor_last_duplicate_offset;
    }

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    int source_delta[3] = {0, 0, 0};
    int applied_count = 0;
    slayer3d_game_data_editor_selection duplicated[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    SDL_zeroa(duplicated);

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection selection = runtime->editor_selected_brushes[i];
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection.world_name);
        if (world_runtime == NULL)
            goto fail;

        source_delta[0] = editor_transaction_source_units_from_meters(world_runtime, duplicate_offset.x);
        source_delta[1] = editor_transaction_source_units_from_meters(world_runtime, duplicate_offset.y);
        source_delta[2] = editor_transaction_source_units_from_meters(world_runtime, duplicate_offset.z);

        editor_command_transaction_entry *entry = NULL;
        if (!append_editor_brush_duplicate_transaction(runtime, active_scene, &selection, source_delta, &entry) ||
            !apply_editor_transaction_mutation(runtime, entry, true))
        {
            goto fail;
        }

        brush_world_runtime *mutable_world = find_brush_world_runtime_mutable(runtime, entry->world_name);
        init_editor_selection(&duplicated[applied_count]);
        refresh_editor_brush_selection_for_identity(mutable_world, &duplicated[applied_count], entry->element_name,
                                                    entry->element_stable_id, -1, NULL);
        applied_count++;
    }

    clear_editor_selected_brushes(runtime);
    runtime->editor_selected_brush_scene = active_scene;
    for (int i = 0; i < applied_count; ++i)
    {
        if (duplicated[i].hit)
            (void)add_editor_selected_brush(runtime, &duplicated[i]);
    }
    update_active_editor_selection_from_selected_brushes(runtime);

    if (slayer3d_vec3_length_squared(duplicate_offset) > 0.0000001f)
    {
        runtime->editor_last_duplicate_offset = duplicate_offset;
        runtime->editor_has_last_duplicate_offset = true;
    }
    if (runtime->scene_state != NULL)
    {
        char message[128];
        SDL_snprintf(message, sizeof(message),
                     applied_count == 1 ? "duplicated 1 selected brush" : "duplicated %d selected brushes",
                     applied_count);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    return applied_count > 0;

fail:
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    return false;
}

static const char *editor_paint_action_material_name(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *material_name = json_string(action, "material", NULL);
    if (material_name != NULL && material_name[0] != '\0')
        return material_name;

    const char *material_key = json_string(action, "material_key", "editor.palette.material.cursor");
    if (runtime != NULL && runtime->scene_state != NULL && material_key != NULL && material_key[0] != '\0')
    {
        material_name = slayer3d_properties_get_string(runtime->scene_state, material_key, NULL);
        if (material_name != NULL && material_name[0] != '\0')
            return material_name;
    }

    if (runtime != NULL && runtime->scene_state != NULL)
    {
        material_name = slayer3d_properties_get_string(runtime->scene_state, "editor.texture.material", NULL);
        if (material_name != NULL && material_name[0] != '\0')
            return material_name;
    }
    return NULL;
}

static bool append_editor_brush_paint_transaction(slayer3d_game_data_runtime *runtime, const char *active_scene,
                                                  const brush_world_runtime *world_runtime,
                                                  const slayer3d_game_data_editor_selection *selection,
                                                  const char *element_stable_id, const slayer3d_game_data_brush *brush,
                                                  int face_index, int material_index, const char *material_name,
                                                  editor_command_transaction_entry **out_entry)
{
    if (out_entry != NULL)
        *out_entry = NULL;
    if (runtime == NULL || active_scene == NULL || world_runtime == NULL || selection == NULL || brush == NULL ||
        face_index < 0 || face_index >= brush->face_count || material_index < 0 || material_name == NULL ||
        material_name[0] == '\0')
    {
        return false;
    }

    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (entry == NULL)
        return false;

    const slayer3d_game_data_brush_face *face = &brush->faces[face_index];
    int previous_material_index = face->material_index;
    const char *previous_material_name = face->material_name;
    if (previous_material_index < 0 && previous_material_name != NULL && previous_material_name[0] != '\0')
        previous_material_index = editor_brush_material_index_by_name(world_runtime, previous_material_name);
    if ((previous_material_name == NULL || previous_material_name[0] == '\0') && previous_material_index >= 0 &&
        previous_material_index < world_runtime->desc.material_count)
    {
        previous_material_name = world_runtime->desc.materials[previous_material_index].name;
    }

    if (!editor_prepare_transaction_common(entry, active_scene, "paint", "face", selection->world_name,
                                           selection->element_name) ||
        !copy_editor_transaction_string(element_stable_id, &entry->element_stable_id) ||
        !copy_editor_transaction_string(editor_metadata_stable_id(&face->editor), &entry->face_stable_id) ||
        !copy_editor_transaction_string(material_name, &entry->material_name) ||
        !copy_editor_transaction_string(previous_material_name, &entry->previous_material_name))
    {
        return false;
    }

    entry->face_index = face_index;
    entry->material_index = material_index;
    entry->previous_material_index = previous_material_index;
    entry->has_bounds = brush->has_bounds;
    entry->bounds = brush->has_bounds ? brush->bounds
                                      : (slayer3d_bounding_box){slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                                slayer3d_vec3_make(0.0f, 0.0f, 0.0f)};
    SDL_snprintf(entry->message, sizeof(entry->message), "painted %s face %d", selection->element_name, face_index);
    if (out_entry != NULL)
        *out_entry = entry;
    return true;
}

typedef struct editor_paint_selection_target
{
    const char *world_name;
    const char *element_name;
    const char *element_stable_id;
    int face_count;
} editor_paint_selection_target;

static void free_editor_paint_selection_targets(editor_paint_selection_target *targets, int count)
{
    if (targets == NULL)
        return;
    for (int i = 0; i < count; ++i)
    {
        SDL_free((void *)targets[i].world_name);
        SDL_free((void *)targets[i].element_name);
        SDL_free((void *)targets[i].element_stable_id);
    }
    SDL_free(targets);
}

bool slayer3d_game_data_paint_selected_editor_brushes(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;

    const char *target = json_string(action, "target", "selection");
    const char *material_name = editor_paint_action_material_name(runtime, action);
    if (material_name == NULL || material_name[0] == '\0')
    {
        const char *message = json_string(action, "invalid_message", "select a texture material before painting");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        if (runtime->scene_state != NULL)
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL)
        return false;

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    editor_command_transaction_entry *last_entry = NULL;
    int applied_count = 0;
    int painted_face_count = 0;
    editor_paint_selection_target *selection_targets = NULL;
    int selection_target_count = 0;

    if (SDL_strcmp(target, "face") == 0)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_active_selection);
        if (!transaction_editor_selection_is_selectable_brush(&selection) || selection.face_index < 0)
        {
            const char *message = json_string(action, "invalid_message", "select a brush face before painting");
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            if (runtime->scene_state != NULL)
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }

        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection.world_name);
        const int brush_index = find_editor_mutable_brush_index_by_identity(
            world_runtime, selection.element_name, editor_metadata_stable_id(selection.element_editor));
        const slayer3d_game_data_brush *brush = brush_index >= 0 ? &world_runtime->desc.brushes[brush_index] : NULL;
        const int material_index = editor_brush_material_index_by_name(world_runtime, material_name);
        if (brush == NULL || material_index < 0)
            goto fail;

        if (!append_editor_brush_paint_transaction(runtime, active_scene, world_runtime, &selection,
                                                   editor_metadata_stable_id(selection.element_editor), brush,
                                                   selection.face_index, material_index, material_name, &last_entry) ||
            !apply_editor_transaction_mutation(runtime, last_entry, true))
        {
            goto fail;
        }
        painted_face_count = 1;
        applied_count = 1;
    }
    else
    {
        if (runtime->editor_selected_brush_scene == NULL ||
            SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0 ||
            runtime->editor_selected_brush_count <= 0)
        {
            const char *message = json_string(action, "invalid_message", "select brushes before painting");
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            if (runtime->scene_state != NULL)
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }

        const int selected_count = runtime->editor_selected_brush_count;
        selection_targets =
            (editor_paint_selection_target *)SDL_calloc((size_t)selected_count, sizeof(*selection_targets));
        if (selection_targets == NULL)
            goto fail;
        selection_target_count = selected_count;
        for (int i = 0; i < selected_count; ++i)
        {
            const slayer3d_game_data_editor_selection selection =
                resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
            if (!transaction_editor_selection_is_selectable_brush(&selection))
                goto fail;
            char element_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
            SDL_strlcpy(element_stable_id,
                        editor_metadata_stable_id(selection.element_editor) != NULL
                            ? editor_metadata_stable_id(selection.element_editor)
                            : "",
                        sizeof(element_stable_id));

            const brush_world_runtime *initial_world_runtime = find_brush_world_runtime(runtime, selection.world_name);
            const int initial_brush_index = find_editor_mutable_brush_index_by_identity(
                initial_world_runtime, selection.element_name, element_stable_id);
            const slayer3d_game_data_brush *initial_brush =
                initial_brush_index >= 0 ? &initial_world_runtime->desc.brushes[initial_brush_index] : NULL;
            const int face_count = initial_brush != NULL ? SDL_min(initial_brush->face_count, 6) : 0;
            const int initial_material_index =
                editor_brush_material_index_by_name(initial_world_runtime, material_name);
            if (initial_brush == NULL || initial_material_index < 0)
                goto fail;

            if (!copy_editor_transaction_string(selection.world_name, &selection_targets[i].world_name) ||
                !copy_editor_transaction_string(selection.element_name, &selection_targets[i].element_name) ||
                !copy_editor_transaction_string(element_stable_id, &selection_targets[i].element_stable_id))
            {
                goto fail;
            }
            selection_targets[i].face_count = face_count;
        }

        for (int i = 0; i < selected_count; ++i)
        {
            const editor_paint_selection_target *target_selection = &selection_targets[i];
            slayer3d_game_data_editor_selection selection;
            SDL_zero(selection);
            selection.hit = true;
            selection.type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
            selection.world_name = target_selection->world_name;
            selection.element_name = target_selection->element_name;

            for (int face_index = 0; face_index < target_selection->face_count; ++face_index)
            {
                const brush_world_runtime *world_runtime =
                    find_brush_world_runtime(runtime, target_selection->world_name);
                const int brush_index = find_editor_mutable_brush_index_by_identity(
                    world_runtime, target_selection->element_name, target_selection->element_stable_id);
                const slayer3d_game_data_brush *brush =
                    brush_index >= 0 ? &world_runtime->desc.brushes[brush_index] : NULL;
                const int material_index = editor_brush_material_index_by_name(world_runtime, material_name);
                if (brush == NULL || material_index < 0 || face_index >= brush->face_count)
                    goto fail;

                editor_command_transaction_entry *entry = NULL;
                if (!append_editor_brush_paint_transaction(runtime, active_scene, world_runtime, &selection,
                                                           target_selection->element_stable_id, brush, face_index,
                                                           material_index, material_name, &entry) ||
                    !apply_editor_transaction_mutation(runtime, entry, true))
                {
                    goto fail;
                }
                last_entry = entry;
                applied_count++;
                painted_face_count++;
            }
        }
    }

    if (painted_face_count <= 0)
        goto fail;

    publish_editor_transaction_selection_state(runtime);
    char message[160];
    if (painted_face_count == 1)
        SDL_snprintf(message, sizeof(message), "painted 1 face with %s", material_name);
    else
        SDL_snprintf(message, sizeof(message), "painted %d faces with %s", painted_face_count, material_name);
    if (runtime->scene_state != NULL)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    publish_editor_transaction(runtime, outputs, "commit", true, last_entry, message);
    free_editor_paint_selection_targets(selection_targets, selection_target_count);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "commit", true, last_entry,
                                               message);

fail: {
    const char *fail_message = json_string(action, "invalid_message", "selected brush paint failed");
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    if (runtime->scene_state != NULL)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", fail_message);
    publish_editor_transaction(runtime, outputs, "commit", false, NULL, fail_message);
    free_editor_paint_selection_targets(selection_targets, selection_target_count);
    return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, fail_message);
}
}

typedef struct editor_brush_color_request
{
    bool set_color;
    slayer3d_color color;
    bool set_tint;
    bool tint_enabled;
    slayer3d_color tint;
} editor_brush_color_request;

static bool editor_graybox_role_color(const char *role, slayer3d_color *out_color)
{
    if (role == NULL || out_color == NULL)
        return false;
    if (SDL_strcmp(role, "floor") == 0)
        *out_color = (slayer3d_color){110, 122, 132, 255};
    else if (SDL_strcmp(role, "wall") == 0)
        *out_color = (slayer3d_color){145, 152, 160, 255};
    else if (SDL_strcmp(role, "ceiling") == 0)
        *out_color = (slayer3d_color){118, 126, 138, 255};
    else if (SDL_strcmp(role, "trim") == 0)
        *out_color = (slayer3d_color){84, 96, 118, 255};
    else if (SDL_strcmp(role, "hazard") == 0)
        *out_color = (slayer3d_color){235, 86, 56, 220};
    else if (SDL_strcmp(role, "trigger") == 0)
        *out_color = (slayer3d_color){80, 220, 255, 120};
    else if (SDL_strcmp(role, "placeholder") == 0)
        *out_color = (slayer3d_color){170, 174, 184, 255};
    else
        return false;
    return true;
}

static editor_brush_color_request editor_brush_color_request_from_action(slayer3d_game_data_runtime *runtime,
                                                                         yyjson_val *action)
{
    editor_brush_color_request request;
    SDL_zero(request);
    request.color = (slayer3d_color){180, 184, 192, 255};
    request.tint = (slayer3d_color){255, 255, 255, 255};

    const char *role = json_string(action, "role", NULL);
    if (editor_graybox_role_color(role, &request.color))
        request.set_color = true;

    if (obj_get(action, "color") != NULL)
    {
        request.set_color = true;
        request.color = json_color(action, "color", request.color);
    }
    const char *color_key = json_string(action, "color_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && color_key != NULL && color_key[0] != '\0')
    {
        request.set_color = true;
        request.color = slayer3d_properties_get_color(runtime->scene_state, color_key, request.color);
    }

    if (obj_get(action, "tint") != NULL)
    {
        request.set_tint = true;
        request.tint_enabled = true;
        request.tint = json_color(action, "tint", request.tint);
    }
    const char *tint_key = json_string(action, "tint_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && tint_key != NULL && tint_key[0] != '\0')
    {
        request.set_tint = true;
        request.tint_enabled = true;
        request.tint = slayer3d_properties_get_color(runtime->scene_state, tint_key, request.tint);
    }
    if (obj_get(action, "tint_enabled") != NULL)
    {
        request.set_tint = true;
        request.tint_enabled = json_bool(action, "tint_enabled", request.tint_enabled);
    }
    return request;
}

static bool editor_brush_color_request_valid(const editor_brush_color_request *request)
{
    return request != NULL && (request->set_color || request->set_tint);
}

static void editor_apply_visual_request(editor_brush_visual_override_runtime *visual,
                                        const editor_brush_color_request *request)
{
    if (visual == NULL || request == NULL)
        return;
    if (request->set_color)
    {
        visual->has_color = true;
        visual->color = request->color;
    }
    if (request->set_tint)
    {
        visual->tint_enabled = request->tint_enabled;
        visual->tint = request->tint_enabled ? request->tint : (slayer3d_color){255, 255, 255, 255};
    }
}

static bool append_editor_brush_color_transaction(slayer3d_game_data_runtime *runtime, const char *active_scene,
                                                  const brush_world_runtime *world_runtime,
                                                  const slayer3d_game_data_editor_selection *selection,
                                                  const char *element_stable_id, int source_face_index,
                                                  const editor_brush_color_request *request,
                                                  editor_command_transaction_entry **out_entry)
{
    if (out_entry != NULL)
        *out_entry = NULL;
    if (runtime == NULL || active_scene == NULL || world_runtime == NULL || selection == NULL ||
        !editor_brush_color_request_valid(request))
    {
        return false;
    }

    editor_command_transaction_entry *entry = editor_command_history_append(runtime);
    if (entry == NULL)
        return false;
    const bool face_target = source_face_index >= 0;
    if (!editor_prepare_transaction_common(entry, active_scene, "color", face_target ? "face" : "element",
                                           selection->world_name, selection->element_name) ||
        !copy_editor_transaction_string(element_stable_id, &entry->element_stable_id))
    {
        return false;
    }
    if (face_target)
    {
        const slayer3d_game_data_brush_face *face =
            selection->face_index >= 0 && selection->element_index >= 0 &&
                    selection->element_index < world_runtime->desc.brush_count &&
                    selection->face_index < world_runtime->desc.brushes[selection->element_index].face_count
                ? &world_runtime->desc.brushes[selection->element_index].faces[selection->face_index]
                : NULL;
        if (!copy_editor_transaction_string(face != NULL ? editor_metadata_stable_id(&face->editor) : NULL,
                                            &entry->face_stable_id))
        {
            return false;
        }
        entry->face_index = selection->face_index;
    }

    const char *brush_identity =
        element_stable_id != NULL && element_stable_id[0] != '\0' ? element_stable_id : selection->element_name;
    if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity, &entry->source_box_snapshot,
                                                        &entry->brush_index, NULL, 0))
    {
        return false;
    }
    entry->has_source_box_snapshot = true;
    if (!copy_editor_brush_source_box_runtime(&entry->source_box_snapshot, &entry->source_box_after_snapshot))
        return false;
    entry->has_source_box_after_snapshot = true;
    if (face_target)
        editor_apply_visual_request(&entry->source_box_after_snapshot.face_visuals[source_face_index], request);
    else
        editor_apply_visual_request(&entry->source_box_after_snapshot.visual, request);

    entry->has_bounds = selection->has_bounds;
    entry->bounds = selection->has_bounds ? selection->bounds
                                          : (slayer3d_bounding_box){slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                                    slayer3d_vec3_make(0.0f, 0.0f, 0.0f)};
    SDL_snprintf(entry->message, sizeof(entry->message), face_target ? "colored %s face %d" : "colored %s",
                 selection->element_name != NULL ? selection->element_name : "brush", source_face_index);
    if (out_entry != NULL)
        *out_entry = entry;
    return true;
}

bool slayer3d_game_data_color_selected_editor_brushes(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    (void)payload;
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL)
        return false;

    const editor_brush_color_request request = editor_brush_color_request_from_action(runtime, action);
    if (!editor_brush_color_request_valid(&request))
    {
        const char *message = json_string(action, "invalid_message", "choose a brush color or texture tint");
        publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
        if (runtime->scene_state != NULL)
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, message);
    }

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    const char *target = json_string(action, "target", "selection");
    if (active_scene == NULL)
        return false;

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    editor_command_transaction_entry *last_entry = NULL;
    int applied_count = 0;
    int colored_count = 0;

    if (SDL_strcmp(target, "face") == 0)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_active_selection);
        if (!transaction_editor_selection_is_selectable_brush(&selection) || selection.face_index < 0)
        {
            const char *message = json_string(action, "invalid_message", "select a brush face before coloring");
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            if (runtime->scene_state != NULL)
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }

        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection.world_name);
        const char *element_stable_id = editor_metadata_stable_id(selection.element_editor);
        const int source_face_index = editor_brush_world_source_box_face_index_for_identity(
            world_runtime,
            element_stable_id != NULL && element_stable_id[0] != '\0' ? element_stable_id : selection.element_name,
            selection.face_index, editor_metadata_stable_id(selection.face_editor));
        if (world_runtime == NULL || source_face_index < 0 ||
            !append_editor_brush_color_transaction(runtime, active_scene, world_runtime, &selection, element_stable_id,
                                                   source_face_index, &request, &last_entry) ||
            !apply_editor_transaction_mutation(runtime, last_entry, true))
        {
            goto fail;
        }
        applied_count = 1;
        colored_count = 1;
    }
    else
    {
        if (runtime->editor_selected_brush_scene == NULL ||
            SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0 ||
            runtime->editor_selected_brush_count <= 0)
        {
            const char *message = json_string(action, "invalid_message", "select brushes before coloring");
            publish_editor_transaction(runtime, outputs, "commit", false, NULL, message);
            if (runtime->scene_state != NULL)
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
            return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL,
                                                       message);
        }

        editor_paint_selection_target *selection_targets = (editor_paint_selection_target *)SDL_calloc(
            (size_t)runtime->editor_selected_brush_count, sizeof(*selection_targets));
        if (selection_targets == NULL)
            goto fail;
        int selection_target_count = runtime->editor_selected_brush_count;
        for (int i = 0; i < selection_target_count; ++i)
        {
            const slayer3d_game_data_editor_selection selection =
                resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
            if (!transaction_editor_selection_is_selectable_brush(&selection))
            {
                free_editor_paint_selection_targets(selection_targets, selection_target_count);
                goto fail;
            }
            if (!copy_editor_transaction_string(selection.world_name, &selection_targets[i].world_name) ||
                !copy_editor_transaction_string(selection.element_name, &selection_targets[i].element_name) ||
                !copy_editor_transaction_string(editor_metadata_stable_id(selection.element_editor),
                                                &selection_targets[i].element_stable_id))
            {
                free_editor_paint_selection_targets(selection_targets, selection_target_count);
                goto fail;
            }
        }
        for (int i = 0; i < selection_target_count; ++i)
        {
            const editor_paint_selection_target *target_selection = &selection_targets[i];
            slayer3d_game_data_editor_selection selection;
            init_editor_selection(&selection);
            const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, target_selection->world_name);
            refresh_editor_brush_selection_for_identity(world_runtime, &selection, target_selection->element_name,
                                                        target_selection->element_stable_id, -1, NULL);
            if (!selection.hit ||
                !append_editor_brush_color_transaction(runtime, active_scene, world_runtime, &selection,
                                                       target_selection->element_stable_id, -1, &request,
                                                       &last_entry) ||
                !apply_editor_transaction_mutation(runtime, last_entry, true))
            {
                free_editor_paint_selection_targets(selection_targets, selection_target_count);
                goto fail;
            }
            applied_count++;
            colored_count++;
        }
        free_editor_paint_selection_targets(selection_targets, selection_target_count);
    }

    if (colored_count <= 0)
        goto fail;

    publish_editor_transaction_selection_state(runtime);
    char message[160];
    SDL_snprintf(message, sizeof(message), colored_count == 1 ? "colored 1 brush" : "colored %d brushes",
                 colored_count);
    if (runtime->scene_state != NULL)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    publish_editor_transaction(runtime, outputs, "commit", true, last_entry, message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "commit", true, last_entry,
                                               message);

fail: {
    const char *fail_message = json_string(action, "invalid_message", "selected brush color failed");
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    if (runtime->scene_state != NULL)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", fail_message);
    publish_editor_transaction(runtime, outputs, "commit", false, NULL, fail_message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "commit", false, NULL, fail_message);
}
}

bool slayer3d_game_data_translate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 offset)
{
    if (runtime == NULL || slayer3d_vec3_length_squared(offset) <= 0.0000001f ||
        runtime->editor_selected_brush_count <= 0 || runtime->editor_selected_brush_scene == NULL)
    {
        return false;
    }
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
    {
        return false;
    }

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    int applied_count = 0;
    editor_command_transaction_entry *last_entry = NULL;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        if (!transaction_editor_selection_is_selectable_brush(selection))
            goto fail;

        editor_command_transaction_entry *entry = editor_command_history_append(runtime);
        if (entry == NULL)
            goto fail;
        if (!editor_prepare_transaction_common(entry, active_scene, "translate", "element", selection->world_name,
                                               selection->element_name) ||
            !copy_editor_transaction_string(editor_metadata_stable_id(selection->element_editor),
                                            &entry->element_stable_id))
        {
            goto fail;
        }
        entry->offset = offset;
        entry->has_bounds = selection->has_bounds;
        entry->bounds = selection->has_bounds ? translated_bounds(selection->bounds, offset)
                                              : (slayer3d_bounding_box){slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                                        slayer3d_vec3_make(0.0f, 0.0f, 0.0f)};
        SDL_snprintf(entry->message, sizeof(entry->message), "moved %s", selection->element_name);

        if (!apply_editor_transaction_mutation(runtime, entry, true))
            goto fail;
        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry, i);
        last_entry = entry;
        applied_count++;
    }

    if (runtime->scene_state != NULL)
    {
        char message[128];
        SDL_snprintf(message, sizeof(message),
                     applied_count == 1 ? "moved 1 selected brush" : "moved %d selected brushes", applied_count);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    if (last_entry != NULL && runtime->editor_selected_brush_count > 0)
    {
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
        runtime->editor_selection_scene = active_scene;
    }
    return applied_count > 0;

fail:
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    return false;
}

bool slayer3d_game_data_rotate_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 pivot,
                                                       slayer3d_vec3 axis, float angle_radians)
{
    if (runtime == NULL || SDL_fabsf(angle_radians) <= 0.000001f || runtime->editor_selected_brush_count <= 0 ||
        runtime->editor_selected_brush_scene == NULL)
    {
        return false;
    }
    if (!editor_float_finite(pivot.x) || !editor_float_finite(pivot.y) || !editor_float_finite(pivot.z) ||
        !editor_float_finite(axis.x) || !editor_float_finite(axis.y) || !editor_float_finite(axis.z) ||
        !editor_float_finite(angle_radians) || slayer3d_vec3_length_squared(axis) <= 0.000001f)
    {
        return false;
    }
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
        return false;

    axis = slayer3d_vec3_normalize(axis);
    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    int applied_count = 0;
    editor_command_transaction_entry *last_entry = NULL;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        if (!transaction_editor_selection_is_selectable_brush(selection))
            goto fail;

        editor_command_transaction_entry *entry = editor_command_history_append(runtime);
        if (entry == NULL)
            goto fail;
        if (!editor_prepare_transaction_common(entry, active_scene, "rotate", "element", selection->world_name,
                                               selection->element_name) ||
            !copy_editor_transaction_string(editor_metadata_stable_id(selection->element_editor),
                                            &entry->element_stable_id))
        {
            goto fail;
        }
        entry->rotation_pivot = pivot;
        entry->rotation_axis = axis;
        entry->rotation_angle_radians = angle_radians;
        entry->has_bounds = selection->has_bounds;
        entry->bounds = selection->bounds;
        SDL_snprintf(entry->message, sizeof(entry->message), "rotated %s", selection->element_name);

        if (!apply_editor_transaction_mutation(runtime, entry, true))
            goto fail;
        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry, i);
        last_entry = entry;
        applied_count++;
    }

    if (runtime->scene_state != NULL)
    {
        char message[128];
        SDL_snprintf(message, sizeof(message),
                     applied_count == 1 ? "rotated 1 selected brush" : "rotated %d selected brushes", applied_count);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    if (last_entry != NULL && runtime->editor_selected_brush_count > 0)
    {
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
        runtime->editor_selection_scene = active_scene;
    }
    return applied_count > 0;

fail:
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    return false;
}

bool slayer3d_game_data_scale_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 anchor,
                                                      slayer3d_vec3 factors)
{
    if (runtime == NULL || runtime->editor_selected_brush_count <= 0 || runtime->editor_selected_brush_scene == NULL)
        return false;
    if (!editor_float_finite(anchor.x) || !editor_float_finite(anchor.y) || !editor_float_finite(anchor.z) ||
        !editor_float_finite(factors.x) || !editor_float_finite(factors.y) || !editor_float_finite(factors.z) ||
        factors.x <= 0.000001f || factors.y <= 0.000001f || factors.z <= 0.000001f)
    {
        return false;
    }
    if (SDL_fabsf(factors.x - 1.0f) <= 0.000001f && SDL_fabsf(factors.y - 1.0f) <= 0.000001f &&
        SDL_fabsf(factors.z - 1.0f) <= 0.000001f)
    {
        return false;
    }
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
        return false;

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    int applied_count = 0;
    editor_command_transaction_entry *last_entry = NULL;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        if (!transaction_editor_selection_is_selectable_brush(selection))
            goto fail;

        editor_command_transaction_entry *entry = editor_command_history_append(runtime);
        if (entry == NULL)
            goto fail;
        if (!editor_prepare_transaction_common(entry, active_scene, "scale", "element", selection->world_name,
                                               selection->element_name) ||
            !copy_editor_transaction_string(editor_metadata_stable_id(selection->element_editor),
                                            &entry->element_stable_id))
        {
            goto fail;
        }
        entry->has_bounds = selection->has_bounds;
        entry->bounds = selection->bounds;
        SDL_snprintf(entry->message, sizeof(entry->message), "scaled %s", selection->element_name);

        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection->world_name);
        if (world_runtime == NULL || !world_runtime->editor_has_source_model)
            goto fail;
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity, &entry->source_box_snapshot,
                                                            &entry->brush_index, NULL, 0))
        {
            goto fail;
        }
        entry->has_source_box_snapshot = true;

        if (!editor_brush_world_scale_source_box(world_runtime, brush_identity, anchor, factors, NULL, 0))
            goto fail;
        editor_brush_world_mark_dirty(world_runtime);

        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity,
                                                            &entry->source_box_after_snapshot, NULL, NULL, 0))
        {
            (void)apply_editor_source_box_snapshot(runtime, entry, &entry->source_box_snapshot);
            goto fail;
        }
        entry->has_source_box_after_snapshot = true;

        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry, i);
        last_entry = entry;
        applied_count++;
    }

    if (runtime->scene_state != NULL)
    {
        char message[128];
        SDL_snprintf(message, sizeof(message),
                     applied_count == 1 ? "scaled 1 selected brush" : "scaled %d selected brushes", applied_count);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    if (last_entry != NULL && runtime->editor_selected_brush_count > 0)
    {
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
        runtime->editor_selection_scene = active_scene;
    }
    return applied_count > 0;

fail:
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    return false;
}

static bool slayer3d_game_data_mirror_selected_editor_brushes(slayer3d_game_data_runtime *runtime,
                                                              slayer3d_vec3 plane_point, slayer3d_vec3 plane_normal,
                                                              const char *command_name, const char *adverb)
{
    if (runtime == NULL || command_name == NULL || command_name[0] == '\0' || adverb == NULL || adverb[0] == '\0' ||
        runtime->editor_selected_brush_count <= 0 || runtime->editor_selected_brush_scene == NULL)
    {
        return false;
    }
    if (!editor_float_finite(plane_point.x) || !editor_float_finite(plane_point.y) ||
        !editor_float_finite(plane_point.z) || !editor_float_finite(plane_normal.x) ||
        !editor_float_finite(plane_normal.y) || !editor_float_finite(plane_normal.z) ||
        slayer3d_vec3_length_squared(plane_normal) <= 0.000001f)
    {
        return false;
    }
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
        return false;

    plane_normal = slayer3d_vec3_normalize(plane_normal);
    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    int entry_count = 0;
    int applied_count = 0;
    bool mutation_applied[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    bool group_processed[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY];
    SDL_zeroa(mutation_applied);
    SDL_zeroa(group_processed);
    editor_command_transaction_entry *last_entry = NULL;
    int transaction_group_id = -1;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!transaction_editor_selection_is_selectable_brush(&selection))
            goto fail;

        editor_command_transaction_entry *entry = editor_command_history_append(runtime);
        if (entry == NULL)
            goto fail;
        if (transaction_group_id < 0)
            transaction_group_id = entry->id;
        else
            entry->id = transaction_group_id;
        if (!editor_prepare_transaction_common(entry, active_scene, command_name, "element", selection.world_name,
                                               selection.element_name) ||
            !copy_editor_transaction_string(editor_metadata_stable_id(selection.element_editor),
                                            &entry->element_stable_id))
        {
            goto fail;
        }
        entry->has_bounds = selection.has_bounds;
        entry->bounds = selection.bounds;
        SDL_snprintf(entry->message, sizeof(entry->message), "flipped %s %s", selection.element_name, adverb);

        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection.world_name);
        if (world_runtime == NULL || !world_runtime->editor_has_source_model)
            goto fail;
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity, &entry->source_box_snapshot,
                                                            &entry->brush_index, NULL, 0))
        {
            goto fail;
        }
        entry->has_source_box_snapshot = true;
        entry_count++;
    }

    for (int i = 0; i < entry_count; ++i)
    {
        if (group_processed[i])
            continue;

        editor_command_transaction_entry *entry = &history->entries[first_entry + i];
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
        if (world_runtime == NULL || !world_runtime->editor_has_source_model)
            goto fail;

        const char *brush_identities[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY] = {0};
        int group_indices[SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY] = {0};
        int group_count = 0;
        for (int j = i; j < entry_count; ++j)
        {
            editor_command_transaction_entry *candidate = &history->entries[first_entry + j];
            if (group_processed[j] || candidate->world_name == NULL ||
                SDL_strcmp(candidate->world_name, entry->world_name) != 0)
            {
                continue;
            }
            brush_identities[group_count] =
                candidate->element_stable_id != NULL && candidate->element_stable_id[0] != '\0'
                    ? candidate->element_stable_id
                    : candidate->element_name;
            group_indices[group_count] = j;
            group_count++;
        }

        if (group_count <= 0 || !editor_brush_world_mirror_source_boxes(world_runtime, brush_identities, group_count,
                                                                        plane_point, plane_normal, NULL, 0))
        {
            goto fail;
        }
        editor_brush_world_mark_dirty(world_runtime);
        for (int group = 0; group < group_count; ++group)
        {
            const int entry_index = group_indices[group];
            mutation_applied[entry_index] = true;
            group_processed[entry_index] = true;
            applied_count++;
        }
    }

    for (int i = 0; i < entry_count; ++i)
    {
        editor_command_transaction_entry *entry = &history->entries[first_entry + i];
        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, entry->world_name);
        if (world_runtime == NULL || !world_runtime->editor_has_source_model)
            goto fail;
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity,
                                                            &entry->source_box_after_snapshot, NULL, NULL, 0))
        {
            (void)apply_editor_source_box_snapshot(runtime, entry, &entry->source_box_snapshot);
            goto fail;
        }
        entry->has_source_box_after_snapshot = true;

        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry, i);
        last_entry = entry;
    }

    if (runtime->scene_state != NULL)
    {
        char message[128];
        if (applied_count == 1)
            SDL_snprintf(message, sizeof(message), "flipped 1 selected brush %s", adverb);
        else
            SDL_snprintf(message, sizeof(message), "flipped %d selected brushes %s", applied_count, adverb);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    if (last_entry != NULL && runtime->editor_selected_brush_count > 0)
    {
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
        runtime->editor_selection_scene = active_scene;
    }
    return applied_count > 0;

fail:
    for (int rollback = entry_count - 1; rollback >= 0; --rollback)
    {
        if (mutation_applied[rollback])
        {
            editor_command_transaction_entry *entry = &history->entries[first_entry + rollback];
            (void)apply_editor_source_box_snapshot(runtime, entry, &entry->source_box_snapshot);
        }
    }
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    return false;
}

bool slayer3d_game_data_flip_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_vec3 plane_point,
                                                     slayer3d_vec3 plane_normal)
{
    return slayer3d_game_data_mirror_selected_editor_brushes(runtime, plane_point, plane_normal, "flip_vertical",
                                                             "vertically");
}

bool slayer3d_game_data_flip_selected_editor_brushes_horizontal(slayer3d_game_data_runtime *runtime,
                                                                slayer3d_vec3 plane_point, slayer3d_vec3 plane_normal)
{
    return slayer3d_game_data_mirror_selected_editor_brushes(runtime, plane_point, plane_normal, "flip_horizontal",
                                                             "horizontally");
}

bool slayer3d_game_data_shear_selected_editor_brushes(slayer3d_game_data_runtime *runtime, slayer3d_bounding_box bounds,
                                                      slayer3d_vec3 side_normal, slayer3d_vec3 delta)
{
    if (runtime == NULL || runtime->editor_selected_brush_count <= 0 || runtime->editor_selected_brush_scene == NULL)
        return false;
    if (!editor_float_finite(bounds.min.x) || !editor_float_finite(bounds.min.y) ||
        !editor_float_finite(bounds.min.z) || !editor_float_finite(bounds.max.x) ||
        !editor_float_finite(bounds.max.y) || !editor_float_finite(bounds.max.z) ||
        !editor_float_finite(side_normal.x) || !editor_float_finite(side_normal.y) ||
        !editor_float_finite(side_normal.z) || !editor_float_finite(delta.x) || !editor_float_finite(delta.y) ||
        !editor_float_finite(delta.z) || slayer3d_vec3_length_squared(side_normal) <= 0.000001f ||
        slayer3d_vec3_length_squared(delta) <= 0.0000001f)
    {
        return false;
    }
    const float normal_abs_x = SDL_fabsf(side_normal.x);
    const float normal_abs_y = SDL_fabsf(side_normal.y);
    const float normal_abs_z = SDL_fabsf(side_normal.z);
    if (normal_abs_x > 0.5f && normal_abs_y <= 0.0001f && normal_abs_z <= 0.0001f)
        delta.x = 0.0f;
    else if (normal_abs_y > 0.5f && normal_abs_x <= 0.0001f && normal_abs_z <= 0.0001f)
        delta.y = 0.0f;
    else if (normal_abs_z > 0.5f && normal_abs_x <= 0.0001f && normal_abs_y <= 0.0001f)
        delta.z = 0.0f;
    else
        return false;
    if (slayer3d_vec3_length_squared(delta) <= 0.0000001f)
        return false;

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
        return false;

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    int applied_count = 0;
    editor_command_transaction_entry *last_entry = NULL;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        if (!transaction_editor_selection_is_selectable_brush(selection))
            goto fail;

        editor_command_transaction_entry *entry = editor_command_history_append(runtime);
        if (entry == NULL)
            goto fail;
        if (!editor_prepare_transaction_common(entry, active_scene, "shear", "element", selection->world_name,
                                               selection->element_name) ||
            !copy_editor_transaction_string(editor_metadata_stable_id(selection->element_editor),
                                            &entry->element_stable_id))
        {
            goto fail;
        }
        entry->has_bounds = selection->has_bounds;
        entry->bounds = selection->bounds;
        SDL_snprintf(entry->message, sizeof(entry->message), "sheared %s", selection->element_name);

        brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, selection->world_name);
        if (world_runtime == NULL || !world_runtime->editor_has_source_model)
            goto fail;
        const char *brush_identity = entry->element_stable_id != NULL && entry->element_stable_id[0] != '\0'
                                         ? entry->element_stable_id
                                         : entry->element_name;
        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity, &entry->source_box_snapshot,
                                                            &entry->brush_index, NULL, 0))
        {
            goto fail;
        }
        entry->has_source_box_snapshot = true;

        if (!editor_brush_world_shear_source_box(world_runtime, brush_identity, bounds, side_normal, delta, NULL, 0))
            goto fail;
        editor_brush_world_mark_dirty(world_runtime);

        if (!editor_brush_world_copy_source_box_by_identity(world_runtime, brush_identity,
                                                            &entry->source_box_after_snapshot, NULL, NULL, 0))
        {
            (void)apply_editor_source_box_snapshot(runtime, entry, &entry->source_box_snapshot);
            goto fail;
        }
        entry->has_source_box_after_snapshot = true;

        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry, i);
        last_entry = entry;
        applied_count++;
    }

    if (runtime->scene_state != NULL)
    {
        char message[128];
        SDL_snprintf(message, sizeof(message),
                     applied_count == 1 ? "sheared 1 selected brush" : "sheared %d selected brushes", applied_count);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    if (last_entry != NULL && runtime->editor_selected_brush_count > 0)
    {
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
        runtime->editor_selection_scene = active_scene;
    }
    return applied_count > 0;

fail:
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    return false;
}

bool slayer3d_game_data_rotate_selected_editor_brushes_y(slayer3d_game_data_runtime *runtime, int quarter_turns)
{
    if (runtime == NULL || quarter_turns == 0 || runtime->editor_selected_brush_count <= 0 ||
        runtime->editor_selected_brush_scene == NULL)
    {
        return false;
    }
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (active_scene == NULL || SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) != 0)
        return false;

    editor_command_history_state *history = &runtime->editor_command_history;
    const int first_entry = history->count;
    int applied_count = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        if (!transaction_editor_selection_is_selectable_brush(selection))
            goto fail;

        editor_command_transaction_entry *entry = editor_command_history_append(runtime);
        if (entry == NULL)
            goto fail;
        if (!editor_prepare_transaction_common(entry, active_scene, "rotate_y", "element", selection->world_name,
                                               selection->element_name) ||
            !copy_editor_transaction_string(editor_metadata_stable_id(selection->element_editor),
                                            &entry->element_stable_id))
        {
            goto fail;
        }
        entry->rotation_quarter_turns = quarter_turns;
        entry->has_bounds = selection->has_bounds;
        entry->bounds = selection->bounds;
        SDL_snprintf(entry->message, sizeof(entry->message), "rotated %s", selection->element_name);

        if (!apply_editor_transaction_mutation(runtime, entry, true))
            goto fail;
        refresh_selected_editor_brush_bounds_for_transaction(runtime, entry, i);
        applied_count++;
    }

    if (runtime->scene_state != NULL)
    {
        char message[128];
        SDL_snprintf(message, sizeof(message),
                     applied_count == 1 ? "rotated 1 selected brush" : "rotated %d selected brushes", applied_count);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    return applied_count > 0;

fail:
    for (int rollback = first_entry + applied_count - 1; rollback >= first_entry; --rollback)
        (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
    for (int clear = first_entry; clear < history->count; ++clear)
        free_editor_command_transaction_entry(&history->entries[clear]);
    history->count = first_entry;
    history->cursor = first_entry;
    return false;
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

    const int group_end = history->cursor - 1;
    const int group_start = editor_command_transaction_group_start(history, group_end);
    editor_command_transaction_entry *entry = &history->entries[group_end];
    int failed_index = -1;
    for (int i = group_end; i >= group_start; --i)
    {
        if (!apply_editor_transaction_mutation(runtime, &history->entries[i], false))
        {
            failed_index = i;
            break;
        }
    }
    if (failed_index >= 0)
    {
        for (int rollback = failed_index + 1; rollback <= group_end; ++rollback)
            (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], true);
        const char *message = json_string(action, "invalid_message", "editor command undo failed");
        publish_editor_transaction(runtime, outputs, "undo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "undo", false, NULL, message);
    }
    history->cursor = group_start;
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

    const int group_start = history->cursor;
    const int group_end = editor_command_transaction_group_end(history, group_start);
    editor_command_transaction_entry *entry = &history->entries[group_start];
    int failed_index = -1;
    for (int i = group_start; i <= group_end; ++i)
    {
        if (!apply_editor_transaction_mutation(runtime, &history->entries[i], true))
        {
            failed_index = i;
            break;
        }
    }
    if (failed_index >= 0)
    {
        for (int rollback = failed_index - 1; rollback >= group_start; --rollback)
            (void)apply_editor_transaction_mutation(runtime, &history->entries[rollback], false);
        const char *message = json_string(action, "invalid_message", "editor command redo failed");
        publish_editor_transaction(runtime, outputs, "redo", false, NULL, message);
        return run_editor_transaction_action_array(runtime, obj_get(action, "else"), "redo", false, NULL, message);
    }
    history->cursor = group_end + 1;
    char message[128];
    format_editor_transaction_message(runtime, "redo", true, entry,
                                      json_string(action, "message", "redo {editor_command}"), message,
                                      sizeof(message));
    publish_editor_transaction(runtime, outputs, "redo", true, entry, message);
    return run_editor_transaction_action_array(runtime, obj_get(action, "actions"), "redo", true, entry, message);
}
