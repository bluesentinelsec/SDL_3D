/**
 * @file game_data_editor_clip_tool.c
 * @brief Modal editor clip-tool state and command entry points.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static const char *editor_clip_keep_mode_name(editor_brush_source_clip_keep_mode mode)
{
    switch (mode)
    {
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT:
        return "front";
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK:
        return "back";
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH:
        return "both";
    default:
        return "front";
    }
}

static editor_brush_source_clip_keep_mode editor_clip_next_keep_mode(editor_brush_source_clip_keep_mode mode)
{
    switch (mode)
    {
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT:
        return EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK;
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK:
        return EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH;
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH:
    default:
        return EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;
    }
}

static const char *editor_clip_strip_tool_prefix(const char *message)
{
    const char *prefix = "Clip Tool: ";
    const size_t prefix_len = SDL_strlen(prefix);
    return message != NULL && SDL_strncmp(message, prefix, prefix_len) == 0 ? message + prefix_len : message;
}

static void editor_clip_tool_set_message(editor_clip_tool_state *tool, const char *message)
{
    if (tool == NULL)
        return;
    SDL_snprintf(tool->message, sizeof(tool->message), "%s", message != NULL ? message : "");
}

static void editor_clip_tool_set_invalid_message(editor_clip_tool_state *tool, const char *reason)
{
    if (tool == NULL)
        return;
    reason = editor_clip_strip_tool_prefix(reason);
    SDL_snprintf(tool->message, sizeof(tool->message), "Clip invalid: %s",
                 reason != NULL && reason[0] != '\0' ? reason : "invalid clip");
}

static void editor_clip_tool_publish_message(slayer3d_game_data_runtime *runtime, const char *message,
                                             bool publish_console)
{
    if (runtime == NULL || runtime->scene_state == NULL || message == NULL)
        return;
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    if (publish_console)
        editor_publish_console_message(runtime, message);
}

static void editor_clip_tool_publish_issue(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL || message == NULL || message[0] == '\0')
        return;
    const char *current = slayer3d_properties_get_string(runtime->scene_state, "editor.issues.line0", "");
    const bool changed = SDL_strcmp(current, message) != 0;
    slayer3d_properties_set_string(runtime->scene_state, "editor.issues.line0", message);
    if (changed)
    {
        slayer3d_properties_set_int(runtime->scene_state, "editor.issues.count",
                                    slayer3d_properties_get_int(runtime->scene_state, "editor.issues.count", 0) + 1);
    }
}

static void editor_clip_tool_clear_issue(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_string(runtime->scene_state, "editor.issues.line0", "");
}

static bool editor_clip_discard_keep_mode(editor_brush_source_clip_keep_mode mode,
                                          editor_brush_source_clip_keep_mode *out_mode)
{
    if (out_mode == NULL)
        return false;
    switch (mode)
    {
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT:
        *out_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK;
        return true;
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK:
        *out_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;
        return true;
    case EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH:
    default:
        return false;
    }
}

static bool editor_clip_tool_operation_active(const editor_clip_tool_state *tool)
{
    return tool != NULL && (tool->point_count > 0 || tool->dragged_point >= 0);
}

static void editor_clip_tool_refresh_preview(slayer3d_game_data_runtime *runtime);
static bool capture_editor_clip_tool_selection(slayer3d_game_data_runtime *runtime, editor_clip_tool_state *tool);

static void editor_clip_tool_clear_preview(editor_clip_tool_state *tool)
{
    if (tool == NULL)
        return;
    for (int i = 0; i < tool->preview_kept_count && i < SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY; ++i)
        free_editor_brush_source_box_runtime(&tool->preview_kept[i]);
    for (int i = 0; i < tool->preview_discarded_count && i < SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY; ++i)
        free_editor_brush_source_box_runtime(&tool->preview_discarded[i]);
    tool->preview_has_results = false;
    tool->preview_kept_count = 0;
    tool->preview_discarded_count = 0;
}

static bool editor_clip_tool_copy_preview_boxes(editor_brush_source_box_runtime *dest, int *dest_count,
                                                const editor_brush_source_clip_result *result)
{
    if (dest == NULL || dest_count == NULL || result == NULL)
        return false;

    *dest_count = 0;
    if (result->output_brush_count > SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY)
        return false;
    for (int i = 0; i < result->output_brush_count; ++i)
    {
        if (!copy_editor_brush_source_box_runtime(&result->output_brushes[i], &dest[i]))
        {
            for (int j = 0; j < *dest_count; ++j)
                free_editor_brush_source_box_runtime(&dest[j]);
            *dest_count = 0;
            return false;
        }
        (*dest_count)++;
    }
    return true;
}

static slayer3d_vec3 editor_clip_tool_work_plane_normal(const editor_clip_tool_state *tool)
{
    if (tool != NULL && tool->has_work_plane_normal &&
        slayer3d_vec3_length_squared(tool->work_plane_normal) > 0.000001f)
    {
        return slayer3d_vec3_normalize(tool->work_plane_normal);
    }
    return slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
}

static void editor_clip_tool_clear_drag_plane(editor_clip_tool_state *tool)
{
    if (tool == NULL)
        return;
    tool->has_drag_plane = false;
    tool->drag_plane_normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    tool->drag_plane_distance_source_units = 0.0f;
}

static void editor_clip_tool_set_drag_plane(editor_clip_tool_state *tool, slayer3d_vec3 normal, const int coord[3])
{
    if (tool == NULL || coord == NULL || slayer3d_vec3_length_squared(normal) <= 0.000001f)
        return;
    tool->has_drag_plane = true;
    tool->drag_plane_normal = slayer3d_vec3_normalize(normal);
    tool->drag_plane_distance_source_units = tool->drag_plane_normal.x * (float)coord[0] +
                                             tool->drag_plane_normal.y * (float)coord[1] +
                                             tool->drag_plane_normal.z * (float)coord[2];
}

static void publish_editor_clip_tool_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.active", tool->active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.operation_active",
                                 editor_clip_tool_operation_active(tool));
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.scene", tool->active ? tool->scene : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.world", tool->active ? tool->world_name : "");
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.selected_count",
                                tool->active ? tool->selected_brush_count : 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.point_count", tool->active ? tool->point_count : 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.hovered_point",
                                tool->active ? tool->hovered_point : -1);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.dragged_point",
                                tool->active ? tool->dragged_point : -1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.keep_mode",
                                   editor_clip_keep_mode_name(tool->keep_mode));
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.valid", tool->active ? tool->preview_valid : false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.preview.has_results",
                                 tool->active ? tool->preview_has_results : false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.preview.kept_count",
                                tool->active ? tool->preview_kept_count : 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.clip.preview.discarded_count",
                                tool->active ? tool->preview_discarded_count : 0);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.clip.snap.active",
                                 tool->active ? tool->has_snap_target : false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.snap.kind",
                                   tool->active && tool->has_snap_target ? tool->snap_kind : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.snap.target",
                                   tool->active && tool->has_snap_target ? tool->snap_target : "");
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.clip.snap.point",
                                 tool->active && tool->has_snap_target ? tool->snap_point
                                                                       : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_string(runtime->scene_state, "editor.clip.message", tool->message);
    for (int i = 0; i < SLAYER3D_EDITOR_CLIP_TOOL_MAX_POINTS; ++i)
    {
        char key[64];
        SDL_snprintf(key, sizeof(key), "editor.clip.point%d", i);
        slayer3d_properties_set_vec3(runtime->scene_state, key,
                                     tool->active && i < tool->point_count ? tool->points[i]
                                                                           : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
}

void reset_editor_clip_tool_state(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL)
        return;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    editor_clip_tool_clear_preview(tool);
    SDL_zero(*tool);
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    tool->has_work_plane_normal = false;
    tool->work_plane_normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    editor_clip_tool_clear_drag_plane(tool);
    tool->keep_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;
    SDL_snprintf(tool->message, sizeof(tool->message), "%s", message != NULL ? message : "");
    publish_editor_clip_tool_state(runtime);
}

static bool reset_editor_clip_tool_for_next_operation(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    editor_clip_tool_clear_preview(tool);
    tool->point_count = 0;
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    tool->has_snap_target = false;
    tool->has_work_plane_normal = false;
    tool->work_plane_normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    editor_clip_tool_clear_drag_plane(tool);
    tool->keep_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;
    tool->preview_valid = false;
    tool->preview_has_results = false;
    tool->selected_brush_count = 0;
    tool->world_name[0] = '\0';
    for (int i = 0; i < SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY; ++i)
    {
        tool->brush_identities[i][0] = '\0';
        tool->brush_identity_refs[i] = NULL;
    }
    tool->active = true;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    SDL_snprintf(tool->scene, sizeof(tool->scene), "%s", active_scene != NULL ? active_scene : "");

    const bool selection_valid = capture_editor_clip_tool_selection(runtime, tool);
    if (selection_valid && tool->selected_brush_count > 0)
        editor_clip_tool_set_message(tool, message);
    else if (selection_valid)
        editor_clip_tool_set_message(tool, "Clip Tool: select brushes to clip");

    publish_editor_clip_tool_state(runtime);
    return selection_valid;
}

static int editor_clip_source_snap_units(const slayer3d_game_data_runtime *runtime,
                                         const brush_world_runtime *world_runtime)
{
    const float fallback = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                               ? world_runtime->editor_source_meters_per_unit
                               : 0.001f;
    const float grid_size =
        runtime != NULL && runtime->scene_state != NULL
            ? SDL_max(slayer3d_properties_get_float(runtime->scene_state, "editor.grid.size", 1.0f), fallback)
            : fallback;
    return SDL_max(editor_source_units_from_meters(world_runtime, grid_size), 1);
}

static int editor_clip_snap_source_coord(int coord, int snap_units)
{
    if (snap_units <= 1)
        return coord;
    const int half = snap_units / 2;
    return coord >= 0 ? ((coord + half) / snap_units) * snap_units : ((coord - half) / snap_units) * snap_units;
}

typedef enum editor_clip_snap_kind
{
    EDITOR_CLIP_SNAP_NONE = 0,
    EDITOR_CLIP_SNAP_GRID,
    EDITOR_CLIP_SNAP_FACE,
    EDITOR_CLIP_SNAP_EDGE,
    EDITOR_CLIP_SNAP_VERTEX
} editor_clip_snap_kind;

typedef struct editor_clip_snap_target
{
    bool valid;
    editor_clip_snap_kind kind;
    int coord[3];
    int distance_sq;
    char stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_clip_snap_target;

static const char *editor_clip_snap_kind_name(editor_clip_snap_kind kind)
{
    switch (kind)
    {
    case EDITOR_CLIP_SNAP_GRID:
        return "grid";
    case EDITOR_CLIP_SNAP_FACE:
        return "face";
    case EDITOR_CLIP_SNAP_EDGE:
        return "edge";
    case EDITOR_CLIP_SNAP_VERTEX:
        return "vertex";
    case EDITOR_CLIP_SNAP_NONE:
    default:
        return "none";
    }
}

static int editor_clip_snap_kind_priority(editor_clip_snap_kind kind)
{
    switch (kind)
    {
    case EDITOR_CLIP_SNAP_VERTEX:
        return 4;
    case EDITOR_CLIP_SNAP_EDGE:
        return 3;
    case EDITOR_CLIP_SNAP_FACE:
        return 2;
    case EDITOR_CLIP_SNAP_GRID:
        return 1;
    case EDITOR_CLIP_SNAP_NONE:
    default:
        return 0;
    }
}

static int editor_clip_coord_distance_sq(const int a[3], const int b[3])
{
    if (a == NULL || b == NULL)
        return 0;
    const int dx = a[0] - b[0];
    const int dy = a[1] - b[1];
    const int dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

static bool editor_clip_snap_target_is_better(const editor_clip_snap_target *candidate,
                                              const editor_clip_snap_target *current)
{
    if (candidate == NULL || !candidate->valid)
        return false;
    if (current == NULL || !current->valid)
        return true;

    const int candidate_priority = editor_clip_snap_kind_priority(candidate->kind);
    const int current_priority = editor_clip_snap_kind_priority(current->kind);
    if (candidate_priority != current_priority)
        return candidate_priority > current_priority;
    if (candidate->distance_sq != current->distance_sq)
        return candidate->distance_sq < current->distance_sq;
    return SDL_strcmp(candidate->stable_id, current->stable_id) < 0;
}

static void editor_clip_snap_target_init(editor_clip_snap_target *target, editor_clip_snap_kind kind,
                                         const int coord[3], int distance_sq, const char *stable_id)
{
    if (target == NULL || coord == NULL)
        return;
    SDL_zero(*target);
    target->valid = true;
    target->kind = kind;
    target->coord[0] = coord[0];
    target->coord[1] = coord[1];
    target->coord[2] = coord[2];
    target->distance_sq = distance_sq;
    SDL_snprintf(target->stable_id, sizeof(target->stable_id), "%s", stable_id != NULL ? stable_id : "");
}

static void editor_clip_tool_publish_snap_target(editor_clip_tool_state *tool, const editor_clip_snap_target *target)
{
    if (tool == NULL)
        return;
    if (target == NULL || !target->valid)
    {
        tool->has_snap_target = false;
        tool->snap_kind[0] = '\0';
        tool->snap_target[0] = '\0';
        tool->snap_point = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        return;
    }
    tool->has_snap_target = true;
    SDL_snprintf(tool->snap_kind, sizeof(tool->snap_kind), "%s", editor_clip_snap_kind_name(target->kind));
    SDL_snprintf(tool->snap_target, sizeof(tool->snap_target), "%s", target->stable_id);
    tool->snap_point = slayer3d_vec3_make((float)target->coord[0], (float)target->coord[1], (float)target->coord[2]);
}

static void editor_clip_snap_source_coord3(int coord[3], int snap_units)
{
    if (coord == NULL)
        return;
    for (int axis = 0; axis < 3; ++axis)
        coord[axis] = editor_clip_snap_source_coord(coord[axis], snap_units);
}

static slayer3d_vec3 editor_clip_point_from_coord(const int coord[3])
{
    return coord != NULL ? slayer3d_vec3_make((float)coord[0], (float)coord[1], (float)coord[2])
                         : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static void editor_clip_coord_from_point(slayer3d_vec3 point, int out_coord[3])
{
    if (out_coord == NULL)
        return;
    out_coord[0] = (int)SDL_roundf(point.x);
    out_coord[1] = (int)SDL_roundf(point.y);
    out_coord[2] = (int)SDL_roundf(point.z);
}

static int editor_clip_snap_threshold_units(int snap_units)
{
    return SDL_max(snap_units / 2, 1);
}

static bool editor_clip_edge_projection_coord(const int point[3], const int a[3], const int b[3], int out_coord[3])
{
    if (point == NULL || a == NULL || b == NULL || out_coord == NULL)
        return false;

    const float ab[3] = {(float)(b[0] - a[0]), (float)(b[1] - a[1]), (float)(b[2] - a[2])};
    const float ap[3] = {(float)(point[0] - a[0]), (float)(point[1] - a[1]), (float)(point[2] - a[2])};
    const float denom = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    if (denom <= 0.0001f)
        return false;

    const float t = (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / denom;
    if (t < 0.0f || t > 1.0f)
        return false;

    out_coord[0] = (int)SDL_roundf((float)a[0] + ab[0] * t);
    out_coord[1] = (int)SDL_roundf((float)a[1] + ab[1] * t);
    out_coord[2] = (int)SDL_roundf((float)a[2] + ab[2] * t);
    return true;
}

static void editor_clip_consider_snap_candidate(editor_clip_snap_target *best, const editor_clip_snap_target *candidate)
{
    if (editor_clip_snap_target_is_better(candidate, best))
        *best = *candidate;
}

static void editor_clip_consider_model_vertices(const editor_brush_source_vertex_model *model, const int raw_coord[3],
                                                int max_distance_sq, editor_clip_snap_target *best)
{
    if (model == NULL || raw_coord == NULL || best == NULL)
        return;

    for (int i = 0; i < model->vertex_count; ++i)
    {
        const int distance_sq = editor_clip_coord_distance_sq(raw_coord, model->vertices[i].coord);
        if (distance_sq > max_distance_sq)
            continue;

        editor_clip_snap_target candidate;
        editor_clip_snap_target_init(&candidate, EDITOR_CLIP_SNAP_VERTEX, model->vertices[i].coord, distance_sq,
                                     model->vertices[i].stable_id);
        editor_clip_consider_snap_candidate(best, &candidate);
    }
}

static void editor_clip_consider_model_edges(const editor_brush_source_vertex_model *model, const int raw_coord[3],
                                             int snap_units, int max_distance_sq, editor_clip_snap_target *best)
{
    if (model == NULL || raw_coord == NULL || best == NULL)
        return;

    for (int i = 0; i < model->edge_count; ++i)
    {
        const editor_brush_source_edge *edge = &model->edges[i];
        const int a = edge->vertex_indices[0];
        const int b = edge->vertex_indices[1];
        if (a < 0 || a >= model->vertex_count || b < 0 || b >= model->vertex_count)
            continue;

        int projected[3];
        if (!editor_clip_edge_projection_coord(raw_coord, model->vertices[a].coord, model->vertices[b].coord,
                                               projected))
        {
            continue;
        }
        editor_clip_snap_source_coord3(projected, snap_units);
        const int distance_sq = editor_clip_coord_distance_sq(raw_coord, projected);
        if (distance_sq > max_distance_sq)
            continue;

        editor_clip_snap_target candidate;
        editor_clip_snap_target_init(&candidate, EDITOR_CLIP_SNAP_EDGE, projected, distance_sq, edge->stable_id);
        editor_clip_consider_snap_candidate(best, &candidate);
    }
}

static bool editor_clip_hover_face_matches_source_index(const brush_world_runtime *world_runtime,
                                                        const slayer3d_game_data_editor_selection *hover_selection,
                                                        int source_index)
{
    if (world_runtime == NULL || hover_selection == NULL || hover_selection->element_editor == NULL ||
        hover_selection->element_editor->stable_id == NULL || source_index < 0 ||
        source_index >= world_runtime->editor_source_box_count)
    {
        return false;
    }
    const editor_brush_source_box_runtime *box = &world_runtime->editor_source_boxes[source_index];
    const char *stable_id = box->stable_id != NULL ? box->stable_id : box->name;
    return stable_id != NULL && SDL_strcmp(hover_selection->element_editor->stable_id, stable_id) == 0;
}

static void editor_clip_consider_hover_face(const brush_world_runtime *world_runtime,
                                            const slayer3d_game_data_editor_selection *hover_selection,
                                            const editor_brush_source_vertex_model *model, const int grid_coord[3],
                                            const int raw_coord[3], editor_clip_snap_target *best)
{
    if (world_runtime == NULL || hover_selection == NULL || model == NULL || grid_coord == NULL || raw_coord == NULL ||
        best == NULL || hover_selection->face_index < 0 ||
        !editor_clip_hover_face_matches_source_index(world_runtime, hover_selection, model->brush_index))
    {
        return;
    }

    for (int i = 0; i < model->face_count; ++i)
    {
        if (model->faces[i].face_index != hover_selection->face_index)
            continue;

        editor_clip_snap_target candidate;
        editor_clip_snap_target_init(&candidate, EDITOR_CLIP_SNAP_FACE, grid_coord,
                                     editor_clip_coord_distance_sq(raw_coord, grid_coord), model->faces[i].stable_id);
        editor_clip_consider_snap_candidate(best, &candidate);
        return;
    }
}

static bool editor_clip_resolve_snap_target(const slayer3d_game_data_runtime *runtime, editor_clip_tool_state *tool,
                                            const slayer3d_game_data_editor_selection *hover_selection,
                                            const brush_world_runtime *world_runtime, const int raw_coord[3],
                                            int out_coord[3])
{
    if (tool == NULL || world_runtime == NULL || raw_coord == NULL || out_coord == NULL)
    {
        editor_clip_tool_publish_snap_target(tool, NULL);
        return false;
    }

    const int snap_units = editor_clip_source_snap_units(runtime, world_runtime);
    int grid_coord[3] = {raw_coord[0], raw_coord[1], raw_coord[2]};
    editor_clip_snap_source_coord3(grid_coord, snap_units);

    editor_clip_snap_target best;
    editor_clip_snap_target_init(&best, EDITOR_CLIP_SNAP_GRID, grid_coord,
                                 editor_clip_coord_distance_sq(raw_coord, grid_coord), "grid");

    const int threshold = editor_clip_snap_threshold_units(snap_units);
    const int max_distance_sq = threshold * threshold * 3;
    for (int i = 0; i < tool->selected_brush_count; ++i)
    {
        const int source_index = editor_brush_world_find_source_box_index(world_runtime, tool->brush_identity_refs[i]);
        if (source_index < 0 || source_index >= world_runtime->editor_source_box_count)
            continue;

        editor_brush_source_vertex_model model;
        if (!editor_brush_source_box_build_vertex_model(world_runtime, source_index, &model, NULL, 0))
            continue;

        editor_clip_consider_hover_face(world_runtime, hover_selection, &model, grid_coord, raw_coord, &best);
        editor_clip_consider_model_edges(&model, raw_coord, snap_units, max_distance_sq, &best);
        editor_clip_consider_model_vertices(&model, raw_coord, max_distance_sq, &best);
    }

    out_coord[0] = best.coord[0];
    out_coord[1] = best.coord[1];
    out_coord[2] = best.coord[2];
    editor_clip_tool_publish_snap_target(tool, &best);
    return true;
}

static slayer3d_vec3 editor_clip_world_origin_for_points(const slayer3d_game_data_runtime *runtime,
                                                         const editor_clip_tool_state *tool,
                                                         const slayer3d_game_data_editor_selection *hover_selection)
{
    if (hover_selection != NULL && hover_selection->world_name != NULL && tool != NULL &&
        SDL_strcmp(hover_selection->world_name, tool->world_name) == 0)
    {
        return hover_selection->world_position;
    }

    if (runtime != NULL && tool != NULL && editor_selected_brushes_active_for_scene(runtime))
    {
        for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
        {
            const slayer3d_game_data_editor_selection selection =
                resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
            if (selection.world_name != NULL && SDL_strcmp(selection.world_name, tool->world_name) == 0)
                return selection.world_position;
        }
    }
    return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static void editor_clip_coord_from_world_point(const brush_world_runtime *world_runtime, slayer3d_vec3 world_point,
                                               slayer3d_vec3 origin, int out_coord[3])
{
    const slayer3d_vec3 local_point = slayer3d_vec3_sub(world_point, origin);
    out_coord[0] = editor_source_units_from_meters(world_runtime, local_point.x);
    out_coord[1] = editor_source_units_from_meters(world_runtime, local_point.y);
    out_coord[2] = editor_source_units_from_meters(world_runtime, local_point.z);
}

static bool editor_clip_coord_from_hover_selection(const slayer3d_game_data_runtime *runtime,
                                                   editor_clip_tool_state *tool,
                                                   const slayer3d_game_data_editor_selection *hover_selection,
                                                   const brush_world_runtime *world_runtime, int out_coord[3])
{
    if (runtime == NULL || tool == NULL || hover_selection == NULL || !hover_selection->hit || world_runtime == NULL ||
        out_coord == NULL)
    {
        editor_clip_tool_publish_snap_target(tool, NULL);
        return false;
    }
    editor_clip_coord_from_world_point(world_runtime, hover_selection->point,
                                       editor_clip_world_origin_for_points(runtime, tool, hover_selection), out_coord);
    return editor_clip_resolve_snap_target(runtime, tool, hover_selection, world_runtime, out_coord, out_coord);
}

static bool editor_clip_coord_from_drag_plane_trace(const slayer3d_game_data_runtime *runtime,
                                                    editor_clip_tool_state *tool, yyjson_val *selection_json,
                                                    const slayer3d_game_data_editor_selection *hover_selection,
                                                    const brush_world_runtime *world_runtime, int out_coord[3])
{
    if (runtime == NULL || tool == NULL || !tool->has_drag_plane || selection_json == NULL || world_runtime == NULL ||
        out_coord == NULL)
    {
        return false;
    }

    slayer3d_game_data_world_trace_desc trace;
    if (!editor_trace_desc_from_json(runtime, selection_json, &trace))
        return false;

    const slayer3d_vec3 origin = editor_clip_world_origin_for_points(runtime, tool, hover_selection);
    int start_coord[3];
    int end_coord[3];
    editor_clip_coord_from_world_point(world_runtime, trace.start, origin, start_coord);
    editor_clip_coord_from_world_point(world_runtime, trace.end, origin, end_coord);

    const slayer3d_vec3 start = slayer3d_vec3_make((float)start_coord[0], (float)start_coord[1], (float)start_coord[2]);
    const slayer3d_vec3 end = slayer3d_vec3_make((float)end_coord[0], (float)end_coord[1], (float)end_coord[2]);
    const slayer3d_vec3 delta = slayer3d_vec3_sub(end, start);
    const float denom = slayer3d_vec3_dot(tool->drag_plane_normal, delta);
    if (SDL_fabsf(denom) <= 0.000001f)
        return false;

    const float t =
        (tool->drag_plane_distance_source_units - slayer3d_vec3_dot(tool->drag_plane_normal, start)) / denom;
    if (t < -0.0001f || t > 1.0001f)
        return false;

    const slayer3d_vec3 hit = slayer3d_vec3_add(start, slayer3d_vec3_scale(delta, SDL_clamp(t, 0.0f, 1.0f)));
    const int raw_coord[3] = {(int)SDL_lroundf(hit.x), (int)SDL_lroundf(hit.y), (int)SDL_lroundf(hit.z)};
    return editor_clip_resolve_snap_target(runtime, tool, hover_selection, world_runtime, raw_coord, out_coord);
}

static bool editor_clip_coord_from_selection(const slayer3d_game_data_runtime *runtime, editor_clip_tool_state *tool,
                                             yyjson_val *selection_json,
                                             const slayer3d_game_data_editor_selection *hover_selection,
                                             const brush_world_runtime *world_runtime, int out_coord[3])
{
    if (editor_clip_coord_from_hover_selection(runtime, tool, hover_selection, world_runtime, out_coord))
        return true;
    if (editor_clip_coord_from_drag_plane_trace(runtime, tool, selection_json, hover_selection, world_runtime,
                                                out_coord))
        return true;
    editor_clip_tool_publish_snap_target(tool, NULL);
    return false;
}

static int editor_clip_hovered_point_index(const editor_clip_tool_state *tool, const int coord[3], int snap_units)
{
    if (tool == NULL || coord == NULL)
        return -1;
    const int threshold = SDL_max(snap_units / 2, 1);
    const int max_distance_sq = threshold * threshold * 3;
    int best_index = -1;
    int best_distance_sq = max_distance_sq + 1;
    for (int i = 0; i < tool->point_count; ++i)
    {
        int point_coord[3];
        editor_clip_coord_from_point(tool->points[i], point_coord);
        const int dx = point_coord[0] - coord[0];
        const int dy = point_coord[1] - coord[1];
        const int dz = point_coord[2] - coord[2];
        const int distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq <= max_distance_sq && distance_sq < best_distance_sq)
        {
            best_index = i;
            best_distance_sq = distance_sq;
        }
    }
    return best_index;
}

static const char *editor_selection_clip_identity(const slayer3d_game_data_editor_selection *selection)
{
    if (selection == NULL)
        return NULL;
    if (selection->element_editor != NULL && selection->element_editor->stable_id != NULL &&
        selection->element_editor->stable_id[0] != '\0')
    {
        return selection->element_editor->stable_id;
    }
    return selection->element_name;
}

static bool capture_editor_clip_tool_selection(slayer3d_game_data_runtime *runtime, editor_clip_tool_state *tool)
{
    if (runtime == NULL || tool == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return true;

    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || selection.type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD ||
            selection.world_name == NULL)
        {
            continue;
        }

        if (tool->world_name[0] == '\0')
            SDL_snprintf(tool->world_name, sizeof(tool->world_name), "%s", selection.world_name);
        else if (SDL_strcmp(tool->world_name, selection.world_name) != 0)
        {
            editor_clip_tool_set_message(tool, "Clip Tool: select brushes from one world");
            return false;
        }

        const char *identity = editor_selection_clip_identity(&selection);
        if (identity == NULL || identity[0] == '\0')
            continue;
        if (tool->selected_brush_count >= SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY)
        {
            editor_clip_tool_set_message(tool, "Clip Tool: too many selected brushes");
            return false;
        }
        SDL_snprintf(tool->brush_identities[tool->selected_brush_count],
                     sizeof(tool->brush_identities[tool->selected_brush_count]), "%s", identity);
        tool->brush_identity_refs[tool->selected_brush_count] = tool->brush_identities[tool->selected_brush_count];
        tool->selected_brush_count++;
    }
    return true;
}

bool slayer3d_game_data_enter_editor_clip_tool(slayer3d_game_data_runtime *runtime, const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    reset_editor_clip_tool_state(runtime, "");
    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    tool->active = true;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    SDL_snprintf(tool->scene, sizeof(tool->scene), "%s", active_scene != NULL ? active_scene : "");
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    tool->keep_mode = EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT;

    const bool selection_valid = capture_editor_clip_tool_selection(runtime, tool);
    if (!selection_valid)
    {
        tool->preview_valid = false;
    }
    else if (tool->selected_brush_count <= 0)
    {
        editor_clip_tool_set_message(tool, message_override != NULL && message_override[0] != '\0'
                                               ? message_override
                                               : "Clip Tool: select brushes to clip");
    }
    else
    {
        editor_clip_tool_set_message(tool, message_override != NULL && message_override[0] != '\0'
                                               ? message_override
                                               : "Clip Tool: click to place clip points");
    }

    publish_editor_clip_tool_state(runtime);
    editor_clip_tool_publish_message(runtime, tool->message, true);
    editor_clip_tool_clear_issue(runtime);
    return selection_valid;
}

bool slayer3d_game_data_cancel_editor_clip_tool(slayer3d_game_data_runtime *runtime, const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active)
    {
        reset_editor_clip_tool_state(runtime, message_override != NULL ? message_override : "Clip Tool inactive");
        editor_clip_tool_publish_message(runtime, runtime->editor_clip_tool.message, true);
        return true;
    }

    tool->point_count = 0;
    tool->hovered_point = -1;
    tool->dragged_point = -1;
    editor_clip_tool_clear_drag_plane(tool);
    tool->preview_valid = false;
    editor_clip_tool_clear_preview(tool);
    editor_clip_tool_set_message(tool, message_override != NULL && message_override[0] != '\0' ? message_override
                                                                                               : "Clip Tool cancelled");
    publish_editor_clip_tool_state(runtime);
    editor_clip_tool_publish_message(runtime, tool->message, true);
    return true;
}

bool slayer3d_game_data_escape_editor_clip_tool(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    if (runtime->editor_clip_tool.active && editor_clip_tool_operation_active(&runtime->editor_clip_tool))
        return slayer3d_game_data_cancel_editor_clip_tool(runtime, NULL);
    return slayer3d_game_data_set_editor_tool_mode(runtime, "select", NULL);
}

bool slayer3d_game_data_cycle_editor_clip_keep_mode(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active)
    {
        editor_clip_tool_set_message(tool, "Clip Tool inactive");
        publish_editor_clip_tool_state(runtime);
        editor_clip_tool_publish_message(runtime, tool->message, true);
        return true;
    }

    tool->keep_mode = editor_clip_next_keep_mode(tool->keep_mode);
    editor_clip_tool_refresh_preview(runtime);
    if (tool->preview_valid)
        editor_clip_tool_set_message(tool, "Clip Tool: Enter applies, Ctrl/Cmd+Enter cycles keep mode");
    else
    {
        char message[128];
        SDL_snprintf(message, sizeof(message), "Clip Tool: keep %s", editor_clip_keep_mode_name(tool->keep_mode));
        editor_clip_tool_set_message(tool, message);
    }
    publish_editor_clip_tool_state(runtime);
    editor_clip_tool_publish_message(runtime, tool->message, true);
    return true;
}

static bool editor_clip_tool_build_desc(editor_clip_tool_state *tool, editor_brush_source_clip_desc *out_desc,
                                        char *error_buffer, int error_buffer_size)
{
    if (tool == NULL || out_desc == NULL)
        return false;
    SDL_zero(*out_desc);
    if (!tool->active || tool->selected_brush_count <= 0)
    {
        set_error(error_buffer, error_buffer_size, "Clip Tool: select brushes to clip");
        return false;
    }
    if (tool->point_count < 2)
    {
        set_error(error_buffer, error_buffer_size, "Clip Tool: place at least two clip points");
        return false;
    }

    slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (tool->point_count >= 3)
    {
        normal = slayer3d_vec3_cross(slayer3d_vec3_sub(tool->points[1], tool->points[0]),
                                     slayer3d_vec3_sub(tool->points[2], tool->points[0]));
    }
    else
        normal = slayer3d_vec3_cross(slayer3d_vec3_sub(tool->points[1], tool->points[0]),
                                     editor_clip_tool_work_plane_normal(tool));
    if (slayer3d_vec3_length_squared(normal) <= 0.000001f)
    {
        set_error(error_buffer, error_buffer_size, "Clip Tool: clip points do not define a plane");
        return false;
    }

    normal = slayer3d_vec3_normalize(normal);
    out_desc->brush_identities = tool->brush_identity_refs;
    out_desc->brush_count = tool->selected_brush_count;
    out_desc->normal = normal;
    out_desc->distance_source_units = slayer3d_vec3_dot(normal, tool->points[0]);
    out_desc->keep_mode = tool->keep_mode;
    return true;
}

static void editor_clip_tool_refresh_preview(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    editor_clip_tool_clear_preview(tool);
    tool->preview_valid = false;
    if (!tool->active)
    {
        publish_editor_clip_tool_state(runtime);
        return;
    }
    if (tool->selected_brush_count <= 0)
    {
        editor_clip_tool_set_message(tool, "Clip Tool: select brushes to clip");
        publish_editor_clip_tool_state(runtime);
        return;
    }
    if (tool->point_count == 0)
    {
        editor_clip_tool_set_message(tool, "Clip Tool: click to place clip points");
        publish_editor_clip_tool_state(runtime);
        return;
    }
    if (tool->point_count == 1)
    {
        editor_clip_tool_set_message(tool, "Clip Tool: place second point");
        publish_editor_clip_tool_state(runtime);
        return;
    }

    editor_brush_source_clip_desc desc;
    char error_buffer[256];
    if (!editor_clip_tool_build_desc(tool, &desc, error_buffer, sizeof(error_buffer)))
    {
        editor_clip_tool_set_invalid_message(tool, error_buffer);
        editor_clip_tool_publish_issue(runtime, tool->message);
        publish_editor_clip_tool_state(runtime);
        return;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, tool->world_name);
    editor_brush_source_clip_result result;
    SDL_zero(result);
    if (editor_brush_world_preview_source_clip_operation(world_runtime, &desc, &result, error_buffer,
                                                         sizeof(error_buffer)))
    {
        const bool copied = editor_clip_tool_copy_preview_boxes(tool->preview_kept, &tool->preview_kept_count, &result);
        if (copied)
        {
            editor_brush_source_clip_keep_mode discard_keep_mode;
            if (editor_clip_discard_keep_mode(tool->keep_mode, &discard_keep_mode))
            {
                editor_brush_source_clip_desc discard_desc = desc;
                discard_desc.keep_mode = discard_keep_mode;
                editor_brush_source_clip_result discard_result;
                SDL_zero(discard_result);
                if (editor_brush_world_preview_source_clip_operation(world_runtime, &discard_desc, &discard_result,
                                                                     error_buffer, sizeof(error_buffer)))
                {
                    if (!editor_clip_tool_copy_preview_boxes(tool->preview_discarded, &tool->preview_discarded_count,
                                                             &discard_result))
                    {
                        editor_clip_tool_clear_preview(tool);
                        editor_clip_tool_set_invalid_message(tool, "preview allocation failed");
                        editor_clip_tool_publish_issue(runtime, tool->message);
                        editor_brush_world_free_source_clip_result(&discard_result);
                        editor_brush_world_free_source_clip_result(&result);
                        publish_editor_clip_tool_state(runtime);
                        return;
                    }
                }
                editor_brush_world_free_source_clip_result(&discard_result);
            }
            tool->preview_valid = true;
            tool->preview_has_results = tool->preview_kept_count > 0;
            editor_clip_tool_clear_issue(runtime);
            editor_clip_tool_set_message(tool, "Clip Tool: Enter applies, Ctrl/Cmd+Enter cycles keep mode");
        }
        else
        {
            editor_clip_tool_clear_preview(tool);
            editor_clip_tool_set_invalid_message(tool, "preview allocation failed");
            editor_clip_tool_publish_issue(runtime, tool->message);
        }
    }
    else
    {
        editor_clip_tool_set_invalid_message(tool, error_buffer[0] != '\0' ? error_buffer : result.diagnostic);
        editor_clip_tool_publish_issue(runtime, tool->message);
    }
    editor_brush_world_free_source_clip_result(&result);
    publish_editor_clip_tool_state(runtime);
}

static void editor_clip_tool_set_work_plane_normal(editor_clip_tool_state *tool, slayer3d_vec3 work_plane_normal)
{
    if (tool == NULL || slayer3d_vec3_length_squared(work_plane_normal) <= 0.000001f)
        return;
    tool->has_work_plane_normal = true;
    tool->work_plane_normal = slayer3d_vec3_normalize(work_plane_normal);
}

bool slayer3d_game_data_place_editor_clip_point_source(slayer3d_game_data_runtime *runtime, const int coord[3],
                                                       slayer3d_vec3 work_plane_normal)
{
    if (runtime == NULL || coord == NULL)
        return false;
    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active)
        return true;
    if (tool->point_count >= SLAYER3D_EDITOR_CLIP_TOOL_MAX_POINTS)
    {
        editor_clip_tool_set_message(tool, "Clip Tool: maximum clip points placed");
        publish_editor_clip_tool_state(runtime);
        return true;
    }

    editor_clip_tool_set_work_plane_normal(tool, work_plane_normal);
    editor_clip_tool_set_drag_plane(tool, editor_clip_tool_work_plane_normal(tool), coord);
    tool->points[tool->point_count] = editor_clip_point_from_coord(coord);
    tool->dragged_point = tool->point_count;
    tool->hovered_point = tool->point_count;
    tool->point_count++;
    editor_clip_tool_refresh_preview(runtime);
    return true;
}

bool slayer3d_game_data_move_editor_clip_point_source(slayer3d_game_data_runtime *runtime, int point_index,
                                                      const int coord[3], slayer3d_vec3 work_plane_normal)
{
    if (runtime == NULL || coord == NULL)
        return false;
    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    if (!tool->active || point_index < 0 || point_index >= tool->point_count)
        return true;

    editor_clip_tool_set_work_plane_normal(tool, work_plane_normal);
    tool->points[point_index] = editor_clip_point_from_coord(coord);
    tool->hovered_point = point_index;
    editor_clip_tool_refresh_preview(runtime);
    return true;
}

static slayer3d_vec3 editor_clip_work_plane_normal_from_selection_json(
    const slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
    const slayer3d_game_data_editor_selection *hover_selection)
{
    if (hover_selection != NULL && hover_selection->hit &&
        hover_selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
        slayer3d_vec3_length_squared(hover_selection->normal) > 0.000001f)
    {
        return slayer3d_vec3_normalize(hover_selection->normal);
    }

    slayer3d_vec3 normal;
    float distance = 0.0f;
    if (editor_work_plane_desc_from_trace_json(runtime, obj_get(selection_json, "trace"), &normal, &distance))
        return normal;
    if (hover_selection != NULL && slayer3d_vec3_length_squared(hover_selection->normal) > 0.000001f)
        return slayer3d_vec3_normalize(hover_selection->normal);
    return slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
}

bool editor_handle_clip_tool_input(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                   const slayer3d_game_data_editor_selection *hover_selection, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    if (runtime == NULL || runtime->scene_state == NULL || !runtime->editor_clip_tool.active)
        return true;

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return true;

    const bool left_pressed = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool left_released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    const bool has_left_event = left_pressed || left_down || left_released;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, tool->world_name);
    int coord[3] = {0, 0, 0};
    const bool has_coord =
        editor_clip_coord_from_selection(runtime, tool, selection_json, hover_selection, world_runtime, coord);
    const int snap_units = editor_clip_source_snap_units(runtime, world_runtime);
    const slayer3d_vec3 work_plane_normal =
        editor_clip_work_plane_normal_from_selection_json(runtime, selection_json, hover_selection);

    if (has_coord)
        tool->hovered_point = editor_clip_hovered_point_index(tool, coord, snap_units);
    else
        tool->hovered_point = -1;

    if (left_pressed)
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (!has_coord || world_runtime == NULL)
        {
            editor_clip_tool_set_message(tool, "Clip Tool: point placement requires a hit");
            publish_editor_clip_tool_state(runtime);
            editor_clip_tool_publish_message(runtime, tool->message, true);
            return true;
        }
        if (tool->hovered_point >= 0)
        {
            tool->dragged_point = tool->hovered_point;
            int point_coord[3];
            editor_clip_coord_from_point(tool->points[tool->hovered_point], point_coord);
            editor_clip_tool_set_drag_plane(tool, work_plane_normal, point_coord);
        }
        else if (!slayer3d_game_data_place_editor_clip_point_source(runtime, coord, work_plane_normal))
            return false;
    }
    else if (left_down && tool->dragged_point >= 0)
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (has_coord &&
            !slayer3d_game_data_move_editor_clip_point_source(runtime, tool->dragged_point, coord, work_plane_normal))
        {
            return false;
        }
    }
    else if (left_released && tool->dragged_point >= 0)
    {
        tool->dragged_point = -1;
        editor_clip_tool_clear_drag_plane(tool);
        editor_clip_tool_refresh_preview(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
    }

    if (!has_left_event)
        publish_editor_clip_tool_state(runtime);
    return true;
}

bool slayer3d_game_data_commit_editor_clip_tool(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    editor_clip_tool_state *tool = &runtime->editor_clip_tool;
    editor_brush_source_clip_desc desc;
    char error_buffer[256];
    if (!editor_clip_tool_build_desc(tool, &desc, error_buffer, sizeof(error_buffer)))
    {
        editor_clip_tool_set_invalid_message(tool, error_buffer);
        tool->preview_valid = false;
        publish_editor_clip_tool_state(runtime);
        editor_clip_tool_publish_issue(runtime, tool->message);
        editor_clip_tool_publish_message(runtime, tool->message, true);
        return true;
    }

    editor_brush_source_clip_result result;
    SDL_zero(result);
    if (!slayer3d_game_data_commit_editor_source_clip(runtime, tool->world_name, &desc, &result, error_buffer,
                                                      sizeof(error_buffer)))
    {
        editor_clip_tool_set_invalid_message(tool, error_buffer);
        tool->preview_valid = false;
        editor_brush_world_free_source_clip_result(&result);
        publish_editor_clip_tool_state(runtime);
        editor_clip_tool_publish_issue(runtime, tool->message);
        editor_clip_tool_publish_message(runtime, tool->message, true);
        return true;
    }

    editor_brush_world_free_source_clip_result(&result);
    reset_editor_clip_tool_for_next_operation(runtime, "Clip applied; click to place clip points");
    editor_clip_tool_clear_issue(runtime);
    editor_clip_tool_publish_message(runtime, runtime->editor_clip_tool.message, true);
    return true;
}
