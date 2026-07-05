#ifndef SLAYER3D_GAME_DATA_EDITOR_TYPES_H
#define SLAYER3D_GAME_DATA_EDITOR_TYPES_H

typedef struct editor_command_preview_state
{
    bool active;
    const char *scene;
    const char *command;
    const char *target;
    const char *world_name;
    const char *element_name;
    const char *element_stable_id;
    const char *material_name;
    const char *previous_material_name;
    const char *face_stable_id;
    int face_index;
    int material_index;
    int previous_material_index;
    yyjson_val *outputs;
    slayer3d_vec3 offset;
    bool has_bounds;
    slayer3d_bounding_box bounds;
} editor_command_preview_state;

typedef struct editor_placement_preview_state
{
    bool active;
    const char *scene;
    const char *mode;
    const char *kind;
    const char *axis;
    const char *shape;
    const char *world_name;
    const char *material_name;
    unsigned int contents;
    slayer3d_vec3 anchor;
    float snap;
    bool has_bounds;
    slayer3d_bounding_box bounds;
    bool has_source_candidate;
    int source_min[3];
    int source_max[3];
    int source_positive_overlap_count;
    char source_warning[256];
} editor_placement_preview_state;

typedef struct editor_actor_runtime
{
    char *name;
    char *scene;
    char *display_name;
    char *archetype;
    char *mesh;
    char *model;
    char *group;
    slayer3d_vec3 position;
    slayer3d_vec3 rotation;
    slayer3d_vec3 scale;
    slayer3d_color color;
    char *prefab;
    bool prefab_linked;
    bool hidden;
    bool locked;
    slayer3d_properties *prefab_overrides;
    slayer3d_properties *properties;
} editor_actor_runtime;

typedef struct editor_prefab_runtime
{
    char *id;
    char *label;
    char *category;
    char *kind;
    char *archetype;
    char *mesh;
    char *model;
    char *group;
    slayer3d_vec3 position;
    slayer3d_vec3 rotation;
    slayer3d_vec3 scale;
    slayer3d_color color;
    slayer3d_properties *properties;
} editor_prefab_runtime;

typedef struct editor_brush_visual_override_runtime
{
    bool has_color;
    slayer3d_color color;
    bool tint_enabled;
    slayer3d_color tint;
} editor_brush_visual_override_runtime;

#define SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT 8
#define SLAYER3D_EDITOR_SOURCE_BOX_EDGE_COUNT 12
#define SLAYER3D_EDITOR_SOURCE_BOX_FACE_COUNT 6
#define SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY 16
#define SLAYER3D_EDITOR_SOURCE_CONVEX_EDGE_CAPACITY 64
#define SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY 32
#define SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX 320

typedef struct editor_connection_endpoint_runtime
{
    char *entity;
    char *event;
    char *action;
    bool external;
} editor_connection_endpoint_runtime;

typedef struct editor_connection_runtime
{
    char *id;
    editor_connection_endpoint_runtime from;
    editor_connection_endpoint_runtime to;
    slayer3d_properties *properties;
} editor_connection_runtime;

typedef enum editor_drag_create_phase
{
    EDITOR_DRAG_CREATE_IDLE = 0,
    EDITOR_DRAG_CREATE_DRAWING_FOOTPRINT,
    EDITOR_DRAG_CREATE_PENDING_FOOTPRINT,
    EDITOR_DRAG_CREATE_ADJUSTING_DEPTH,
    EDITOR_DRAG_CREATE_COMMITTED,
    EDITOR_DRAG_CREATE_CANCELED
} editor_drag_create_phase;

typedef struct editor_drag_create_state
{
    bool active;
    bool moved;
    bool commit_on_release;
    editor_drag_create_phase phase;
    const char *scene;
    const char *world_name;
    const char *material_name;
    const char *shape;
    unsigned int contents;
    float grid_size;
    int extrusion_axis;
    int depth_cells;
    int depth_drag_start_cell;
    float depth_drag_start_mouse_y;
    int start_cell[3];
    int current_cell[3];
    int source_min[3];
    int source_max[3];
} editor_drag_create_state;

#define SLAYER3D_EDITOR_DRAG_VERTEX_ORIGIN_CAPACITY 512

typedef struct editor_drag_vertex_origin
{
    char world_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_stable_id[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char vertex_stable_id[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    int source_index;
    int vertex_index;
    int coord[3];
} editor_drag_vertex_origin;

typedef struct editor_drag_move_state
{
    bool active;
    bool moved;
    bool axis_lock_y;
    bool axis_lock_dominant;
    bool face_resize;
    bool duplicate_drag;
    bool vertex_drag;
    bool vertex_add_drag;
    bool vertex_lasso;
    bool edge_drag;
    bool edge_lasso;
    bool rotate_drag;
    bool scale_drag;
    bool shear_drag;
    bool target_actor;
    bool vertex_toggle_on_click;
    bool lasso_additive;
    const char *scene;
    slayer3d_vec3 start_point;
    slayer3d_vec3 applied_offset;
    slayer3d_game_data_editor_selection face_selection;
    slayer3d_vec3 rotate_pivot;
    slayer3d_vec3 rotate_axis;
    slayer3d_vec3 rotate_hover_axis;
    float rotate_angle_radians;
    float rotate_preview_angle_radians;
    float rotate_start_angle_radians;
    float rotate_pivot_screen_x;
    float rotate_pivot_screen_y;
    float rotate_axis_screen_a_x;
    float rotate_axis_screen_a_y;
    float rotate_axis_screen_b_x;
    float rotate_axis_screen_b_y;
    bool rotate_screen_basis_valid;
    bool rotate_hovered;
    bool rotate_preview_valid;
    slayer3d_bounding_box scale_start_bounds;
    slayer3d_vec3 scale_anchor;
    slayer3d_vec3 scale_handle_signs;
    slayer3d_vec3 scale_factors;
    slayer3d_vec3 scale_start_handle;
    bool scale_center_anchor;
    bool scale_proportional;
    bool scale_hovered;
    bool scale_preview_valid;
    slayer3d_bounding_box shear_start_bounds;
    slayer3d_vec3 shear_side_normal;
    slayer3d_vec3 shear_delta;
    slayer3d_vec3 shear_axis;
    bool shear_vertical;
    bool shear_hovered;
    bool shear_preview_valid;
    int vertex_origin_count;
    editor_drag_vertex_origin vertex_toggle_origin;
    editor_drag_vertex_origin vertex_origins[SLAYER3D_EDITOR_DRAG_VERTEX_ORIGIN_CAPACITY];
    float grid_size;
    float start_mouse_x;
    float start_mouse_y;
    float current_mouse_x;
    float current_mouse_y;
} editor_drag_move_state;

typedef struct editor_camera_orbit_state
{
    bool active;
    slayer3d_vec3 pivot;
    float radius;
} editor_camera_orbit_state;

typedef struct editor_camera_move_state
{
    bool active;
    slayer3d_vec3 direction;
    float hold_seconds;
} editor_camera_move_state;

typedef struct editor_brush_source_box_runtime
{
    char *stable_id;
    char *name;
    char *prefab;
    char *material;
    char *face_materials[SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY];
    editor_brush_visual_override_runtime visual;
    editor_brush_visual_override_runtime face_visuals[SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY];
    int min[3];
    int max[3];
    int vertex_count;
    int vertices[16][3];
    unsigned int contents;
    bool hidden;
    bool locked;
    slayer3d_properties *properties;
} editor_brush_source_box_runtime;

typedef int editor_brush_source_coord[3];

typedef struct editor_brush_source_vertex
{
    int brush_index;
    int vertex_index;
    int coord[3];
    char stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_vertex;

typedef struct editor_brush_source_edge
{
    int brush_index;
    int edge_index;
    int vertex_indices[2];
    char stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_edge;

typedef struct editor_brush_source_face_ref
{
    int brush_index;
    int face_index;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int vertex_count;
    char stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_face_ref;

typedef struct editor_brush_source_vertex_model
{
    int brush_index;
    char brush_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    int vertex_count;
    editor_brush_source_vertex vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int edge_count;
    editor_brush_source_edge edges[SLAYER3D_EDITOR_SOURCE_CONVEX_EDGE_CAPACITY];
    int face_count;
    editor_brush_source_face_ref faces[SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY];
} editor_brush_source_vertex_model;

typedef struct editor_brush_source_shared_vertex
{
    int coord[3];
    int first_brush_index;
    int first_vertex_index;
    int reference_count;
} editor_brush_source_shared_vertex;

typedef struct editor_brush_source_vertex_diagnostics
{
    bool valid;
    int brush_count;
    int vertex_count;
    int edge_count;
    int face_count;
    int shared_vertex_count;
    int off_snap_count;
    int degenerate_count;
    int concave_count;
    int non_finite_count;
    char first_issue[256];
    char first_issue_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
} editor_brush_source_vertex_diagnostics;

typedef struct editor_brush_source_prefab_desc
{
    const char *prefab;
    const char *material;
    const char *axis;
    unsigned int contents;
    slayer3d_vec3 anchor;
    bool use_grid_bounds;
    slayer3d_vec3 min;
    slayer3d_vec3 max;
    slayer3d_vec3 grid_min;
    slayer3d_vec3 grid_max;
    float grid_size;
} editor_brush_source_prefab_desc;

typedef struct editor_brush_source_prefab_result
{
    bool valid;
    bool no_op;
    char brush_name[256];
    slayer3d_bounding_box bounds;
    int source_min[3];
    int source_max[3];
    int positive_overlap_count;
    char warning[256];
} editor_brush_source_prefab_result;

#define SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY 32
#define SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY 512
#define SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY 512
#define SLAYER3D_EDITOR_SELECTED_EDGE_CAPACITY 512

typedef struct editor_source_vertex_selection
{
    char world_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    char vertex_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    int source_index;
    int vertex_index;
    int coord[3];
} editor_source_vertex_selection;

typedef struct editor_source_edge_selection
{
    char world_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    char edge_stable_id[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    int source_index;
    int edge_index;
    int coord[2][3];
} editor_source_edge_selection;

typedef struct editor_source_box_bounds_update
{
    int source_index;
    int min[3];
    int max[3];
} editor_source_box_bounds_update;

typedef enum editor_brush_source_vertex_operation_type
{
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_ADD,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MOVE,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MOVE_MANY,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_DELETE_MANY,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MERGE,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_MERGE_MANY_TO_TARGET,
    EDITOR_BRUSH_SOURCE_VERTEX_OPERATION_SNAP,
} editor_brush_source_vertex_operation_type;

typedef struct editor_brush_source_vertex_operation_desc
{
    const char *brush_identity;
    editor_brush_source_vertex_operation_type type;
    int vertex_index;
    int target_vertex_index;
    int vertex_indices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY];
    int vertex_index_count;
    int coord[3];
    int coords[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    int snap_units;
} editor_brush_source_vertex_operation_desc;

typedef struct editor_brush_source_vertex_operation_result
{
    bool valid;
    int vertex_count;
    int changed_count;
    int vertices[SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY][3];
    int face_count;
    char diagnostic[256];
    slayer3d_game_data_brush brush;
} editor_brush_source_vertex_operation_result;

#define SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY 64

typedef enum editor_brush_source_clip_keep_mode
{
    EDITOR_BRUSH_SOURCE_CLIP_KEEP_FRONT = 0,
    EDITOR_BRUSH_SOURCE_CLIP_KEEP_BACK,
    EDITOR_BRUSH_SOURCE_CLIP_KEEP_BOTH
} editor_brush_source_clip_keep_mode;

typedef struct editor_brush_source_clip_desc
{
    const char *const *brush_identities;
    int brush_count;
    slayer3d_vec3 normal;
    float distance_source_units;
    editor_brush_source_clip_keep_mode keep_mode;
} editor_brush_source_clip_desc;

typedef struct editor_brush_source_clip_result
{
    bool valid;
    int input_brush_count;
    int output_brush_count;
    int removed_brush_count;
    int output_source_indices[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    editor_brush_source_box_runtime output_brushes[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    char diagnostic[256];
} editor_brush_source_clip_result;

#define SLAYER3D_EDITOR_CLIP_TOOL_MAX_POINTS 3

typedef struct editor_clip_tool_state
{
    bool active;
    char scene[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char world_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    char brush_identities[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY][SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    const char *brush_identity_refs[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    int selected_brush_count;
    slayer3d_vec3 points[SLAYER3D_EDITOR_CLIP_TOOL_MAX_POINTS];
    int point_count;
    int hovered_point;
    int dragged_point;
    bool has_snap_target;
    char snap_kind[16];
    char snap_target[SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX];
    slayer3d_vec3 snap_point;
    bool has_work_plane_normal;
    slayer3d_vec3 work_plane_normal;
    bool has_drag_plane;
    slayer3d_vec3 drag_plane_normal;
    float drag_plane_distance_source_units;
    editor_brush_source_clip_keep_mode keep_mode;
    bool preview_valid;
    bool preview_has_results;
    int preview_kept_count;
    int preview_discarded_count;
    editor_brush_source_box_runtime preview_kept[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    editor_brush_source_box_runtime preview_discarded[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    char message[256];
} editor_clip_tool_state;

typedef struct editor_command_transaction_entry
{
    int id;
    const char *scene;
    const char *command;
    const char *target;
    const char *world_name;
    const char *element_name;
    const char *element_stable_id;
    const char *material_name;
    const char *previous_material_name;
    const char *face_stable_id;
    int face_index;
    int material_index;
    int previous_material_index;
    slayer3d_vec3 offset;
    int rotation_quarter_turns;
    slayer3d_vec3 rotation_pivot;
    slayer3d_vec3 rotation_axis;
    float rotation_angle_radians;
    bool has_bounds;
    slayer3d_bounding_box bounds;
    int brush_index;
    bool has_source_box_snapshot;
    editor_brush_source_box_runtime source_box_snapshot;
    bool has_source_box_after_snapshot;
    editor_brush_source_box_runtime source_box_after_snapshot;
    int source_clip_before_count;
    int source_clip_after_count;
    int source_clip_before_indices[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    int source_clip_after_source_indices[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    editor_brush_source_box_runtime source_clip_before[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    editor_brush_source_box_runtime source_clip_after[SLAYER3D_EDITOR_SOURCE_CLIP_BRUSH_CAPACITY];
    char message[128];
} editor_command_transaction_entry;

typedef struct editor_command_history_state
{
    editor_command_transaction_entry entries[SLAYER3D_EDITOR_COMMAND_HISTORY_CAPACITY];
    int count;
    int cursor;
    int next_id;
} editor_command_history_state;

typedef struct editor_player_start_runtime
{
    char *name;
    char *scene;
    char *target;
    slayer3d_vec3 position;
    float yaw;
    float pitch;
} editor_player_start_runtime;

#endif
