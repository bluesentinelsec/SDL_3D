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

typedef struct editor_drag_create_state
{
    bool active;
    bool moved;
    const char *scene;
    const char *world_name;
    const char *material_name;
    unsigned int contents;
    float grid_size;
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
    char vertex_stable_id[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    int coord[3];
} editor_drag_vertex_origin;

typedef struct editor_drag_move_state
{
    bool active;
    bool moved;
    bool axis_lock_y;
    bool axis_lock_dominant;
    bool face_resize;
    bool vertex_drag;
    bool vertex_lasso;
    bool lasso_additive;
    const char *scene;
    slayer3d_vec3 start_point;
    slayer3d_vec3 applied_offset;
    slayer3d_game_data_editor_selection face_selection;
    int vertex_origin_count;
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

typedef struct editor_brush_source_box_runtime
{
    char *stable_id;
    char *name;
    char *prefab;
    char *material;
    char *face_materials[6];
    int min[3];
    int max[3];
    int vertex_count;
    int vertices[16][3];
    unsigned int contents;
} editor_brush_source_box_runtime;

#define SLAYER3D_EDITOR_SOURCE_BOX_VERTEX_COUNT 8
#define SLAYER3D_EDITOR_SOURCE_BOX_EDGE_COUNT 12
#define SLAYER3D_EDITOR_SOURCE_BOX_FACE_COUNT 6
#define SLAYER3D_EDITOR_SOURCE_CONVEX_VERTEX_CAPACITY 16
#define SLAYER3D_EDITOR_SOURCE_CONVEX_EDGE_CAPACITY 64
#define SLAYER3D_EDITOR_SOURCE_CONVEX_FACE_CAPACITY 32
#define SLAYER3D_EDITOR_SOURCE_STABLE_ID_MAX 320

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
    bool has_bounds;
    slayer3d_bounding_box bounds;
    int brush_index;
    bool has_source_box_snapshot;
    editor_brush_source_box_runtime source_box_snapshot;
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
