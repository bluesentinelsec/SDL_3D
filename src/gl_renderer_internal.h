/*
 * Private OpenGL renderer state shared by renderer implementation modules.
 *
 * This header is not part of the public SLAYER3D API. Keep declarations here
 * limited to data structures and helpers needed by multiple gl_renderer_*.c
 * files.
 */

#ifndef SLAYER3D_GL_RENDERER_INTERNAL_H
#define SLAYER3D_GL_RENDERER_INTERNAL_H

#include <stdbool.h>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include "gl_funcs.h"
#include "gl_renderer.h"
#include "render_context_internal.h"
#include "slayer3d/lighting.h"
#include "slayer3d/texture.h"
#include "slayer3d/transition.h"

#define SLAYER3D_MAX_POINT_SHADOWS 2
#define SLAYER3D_POINT_SHADOWS_ENABLED 1
#define SLAYER3D_SKIN_PALETTE_BINDING 1
#define SLAYER3D_GPU_SKINNING_PALETTE_MATRICES 256
#define SLAYER3D_CSM_CASCADE_COUNT 4

typedef struct slayer3d_gl_tex_entry
{
    const slayer3d_texture2d *key;
    Uint32 generation; /* generation at upload time */
    GLuint gl_tex;
    struct slayer3d_gl_tex_entry *next;
} slayer3d_gl_tex_entry;

/* std140 layout, must match GLSL SceneUBO. */
typedef struct slayer3d_scene_ubo_data
{
    float view_projection[16];
    float camera_pos[3];
    float _pad0;
    float ambient[3];
    int light_count;
    struct
    {
        int type;
        float _pad[3];
        float position[3];
        float _pad1;
        float direction[3];
        float _pad2;
        float color[3];
        float intensity;
        float range;
        float inner_cutoff;
        float outer_cutoff;
        float _pad3;
    } lights[SLAYER3D_MAX_SHADER_LIGHTS];
    int fog_mode;
    float fog_start;
    float fog_end;
    float fog_density;
    float fog_color[3];
    int tonemap_mode;
} slayer3d_scene_ubo_data;

typedef struct slayer3d_overlay_atlas
{
    const slayer3d_texture2d *source;
    Uint32 generation;
    GLuint gl_tex;
} slayer3d_overlay_atlas;

typedef struct slayer3d_overlay_entry
{
    float *positions;
    float *uvs;
    int vertex_count;
    float mvp[16];
    float tint[4];
    slayer3d_overlay_effect effect;
    float effect_progress;
    float effect_seed;
    float effect_columns;
    const char *shader_vertex_source;
    const char *shader_fragment_source;
    bool scissor_enabled;
    SDL_Rect scissor_rect;
    int atlas_index;
} slayer3d_overlay_entry;

typedef struct slayer3d_draw_entry
{
    const float *positions;
    const float *normals;
    const float *uvs;
    const float *lightmap_uvs;
    const float *colors;
    const unsigned int *indices;
    int vertex_count;
    int index_count;
    float view_matrix[16];
    float view_projection[16];
    float camera_pos[3];
    float model_matrix[16];
    float normal_matrix[9];
    float tint[4];
    float metallic;
    float roughness;
    float emissive[3];
    const slayer3d_texture2d *texture;
    const slayer3d_texture2d *lightmap_texture;
    bool lit;
    bool baked_light_mode;
    bool has_lightmap;
    GLenum primitive_mode;
    const char *shader_vertex_source;
    const char *shader_fragment_source;
    float mvp[16];
    bool owns_arrays;
    bool depth_prepass_eligible;
    bool bounds_valid;
    slayer3d_vec3 bounds_center;
    float bounds_radius;
    Uint32 generation;
    struct slayer3d_gl_mesh_cache_entry *mesh_cache;
    const unsigned short *joint_indices;
    const float *joint_weights;
    float *joint_matrices;
    int joint_count;
    int joint_palette_offset;
    bool use_joint_palette_buffer;
    bool gpu_skinned;
    bool disable_culling;
    bool viewport_enabled;
    SDL_Rect viewport_rect;
    bool scissor_enabled;
    SDL_Rect scissor_rect;
} slayer3d_draw_entry;

typedef struct slayer3d_custom_shader_cache_entry
{
    bool lit;
    char *vertex_source;
    char *fragment_source;
    GLuint program;
    GLint mvp_loc;
    GLint texture_loc;
    GLint has_texture_loc;
    GLint tint_loc;
    GLint model_loc;
    GLint normal_matrix_loc;
    GLint metallic_loc;
    GLint roughness_loc;
    GLint emissive_loc;
    GLint baked_light_mode_loc;
    GLint pbr_texture_loc;
    GLint pbr_has_texture_loc;
    GLint pbr_lightmap_loc;
    GLint pbr_has_lightmap_loc;
    GLint pbr_shadow_map_loc;
    GLint pbr_shadow_vp_loc;
    GLint pbr_shadow_enabled_loc;
    GLint pbr_shadow_bias_loc;
    GLint pbr_csm_vp_loc[SLAYER3D_CSM_CASCADE_COUNT];
    GLint pbr_csm_splits_loc;
    GLint pbr_csm_enabled_loc;
    GLint pbr_view_matrix_loc;
    GLint pbr_point_shadow_map_loc[SLAYER3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_light_pos_loc[SLAYER3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_far_loc[SLAYER3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_count_loc;
    GLint pbr_irradiance_map_loc;
    GLint pbr_prefilter_map_loc;
    GLint pbr_brdf_lut_loc;
    GLint pbr_ibl_enabled_loc;
    GLint pbr_max_reflection_lod_loc;
    GLint overlay_effect_loc;
    GLint overlay_effect_progress_loc;
    GLint overlay_effect_seed_loc;
    GLint overlay_effect_columns_loc;
    struct slayer3d_custom_shader_cache_entry *next;
} slayer3d_custom_shader_cache_entry;

typedef struct slayer3d_gl_mesh_cache_entry
{
    bool lit;
    GLenum primitive_mode;
    const float *positions;
    const float *normals;
    const float *uvs;
    const float *lightmap_uvs;
    const float *colors;
    const unsigned int *indices;
    const unsigned short *joint_indices;
    const float *joint_weights;
    int vertex_count;
    int index_count;
    Uint32 generation;
    bool has_lightmap_uvs;
    bool gpu_skinned;
    GLuint vao;
    GLuint position_vbo;
    GLuint normal_vbo;
    GLuint uv_vbo;
    GLuint lightmap_uv_vbo;
    GLuint color_vbo;
    GLuint joint_index_vbo;
    GLuint joint_weight_vbo;
    GLuint ebo;
    GLuint shadow_vao;
    GLuint shadow_position_vbo;
    GLuint shadow_joint_index_vbo;
    GLuint shadow_joint_weight_vbo;
    GLuint shadow_ebo;
    struct slayer3d_gl_mesh_cache_entry *next;
} slayer3d_gl_mesh_cache_entry;

struct slayer3d_gl_context
{
    SDL_Window *window;
    SDL_GLContext gl_context;
    slayer3d_gl_funcs gl;
    bool is_es;
    bool sample_queries_supported;
    Uint64 frame_index;
    bool ubo_dirty;

    GLuint pbr_program;
    GLuint unlit_program;
    GLuint copy_program;
    slayer3d_custom_shader_cache_entry *custom_shader_cache;
    GLuint depth_prepass_query;
    GLuint geometry_query;

    GLint pbr_model_loc;
    GLint pbr_normal_matrix_loc;
    GLint pbr_use_instancing_loc;
    GLint pbr_use_skinning_loc;
    GLint pbr_use_skin_palette_loc;
    GLint pbr_joint_palette_offset_loc;
    GLint pbr_joint_matrices_loc;
    GLint pbr_texture_loc;
    GLint pbr_has_texture_loc;
    GLint pbr_tint_loc;
    GLint pbr_metallic_loc;
    GLint pbr_roughness_loc;
    GLint pbr_emissive_loc;
    GLint pbr_baked_light_mode_loc;
    GLint pbr_lightmap_loc;
    GLint pbr_has_lightmap_loc;

    GLint unlit_mvp_loc;
    GLint unlit_texture_loc;
    GLint unlit_has_texture_loc;
    GLint unlit_tint_loc;
    GLint unlit_overlay_effect_loc;
    GLint unlit_overlay_effect_progress_loc;
    GLint unlit_overlay_effect_seed_loc;
    GLint unlit_overlay_effect_columns_loc;

    GLint copy_texture_loc;

    GLuint transition_program;
    GLint transition_scene_loc;
    GLint transition_type_loc;
    GLint transition_direction_loc;
    GLint transition_progress_loc;
    GLint transition_color_loc;
    GLint transition_resolution_loc;
    GLint transition_melt_offsets_loc;
    GLuint transition_melt_offsets_tex;
    bool transition_pending;
    slayer3d_transition pending_transition;

    GLuint scene_ubo;
    GLuint skin_palette_ubo;
    float skin_palette_matrices[SLAYER3D_GPU_SKINNING_PALETTE_MATRICES * 16];
    int skin_palette_matrix_count;

    GLuint lit_vao;
    GLuint lit_position_vbo;
    GLuint lit_normal_vbo;
    GLuint lit_uv_vbo;
    GLuint lit_lightmap_uv_vbo;
    GLuint lit_color_vbo;
    GLuint lit_ebo;
    GLuint instance_model_vbo;
    GLuint instance_normal_vbo;

    GLuint unlit_vao;
    GLuint unlit_position_vbo;
    GLuint unlit_uv_vbo;
    GLuint unlit_color_vbo;
    GLuint unlit_ebo;

    GLuint fullscreen_vao;

    GLuint shadow_fbo;
    GLuint shadow_depth_tex;
    GLuint shadow_program;
    GLint shadow_light_vp_loc;
    GLint shadow_model_loc;
    GLint shadow_use_instancing_loc;
    GLint shadow_use_skinning_loc;
    GLint shadow_use_skin_palette_loc;
    GLint shadow_joint_palette_offset_loc;
    GLint shadow_joint_matrices_loc;
    GLuint shadow_vao;
    GLuint shadow_position_vbo;
    GLuint shadow_ebo;
    bool in_shadow_pass;
    float shadow_light_vp[16];
    float shadow_bias;

    float csm_light_vp[SLAYER3D_CSM_CASCADE_COUNT][16];
    float csm_split_depths[SLAYER3D_CSM_CASCADE_COUNT];
    bool csm_fragment_enabled;

    GLint pbr_shadow_map_loc;
    GLint pbr_shadow_vp_loc;
    GLint pbr_shadow_enabled_loc;
    GLint pbr_shadow_bias_loc;

    GLint pbr_csm_vp_loc[SLAYER3D_CSM_CASCADE_COUNT];
    GLint pbr_csm_splits_loc;
    GLint pbr_view_matrix_loc;
    GLint pbr_csm_enabled_loc;

    GLuint point_shadow_fbo;
    GLuint point_shadow_cubemap[SLAYER3D_MAX_POINT_SHADOWS];
    GLuint point_shadow_program;
    GLint point_shadow_model_loc;
    GLint point_shadow_light_vp_loc;
    GLint point_shadow_light_pos_loc;
    GLint point_shadow_far_loc;
    GLint point_shadow_use_skinning_loc;
    GLint point_shadow_use_skin_palette_loc;
    GLint point_shadow_joint_palette_offset_loc;
    GLint point_shadow_joint_matrices_loc;
    int point_shadow_light_index[SLAYER3D_MAX_POINT_SHADOWS];
    float point_shadow_far_plane[SLAYER3D_MAX_POINT_SHADOWS];
    float point_shadow_vp[SLAYER3D_MAX_POINT_SHADOWS][6][16];
    int point_shadow_count;

    GLint pbr_point_shadow_map_loc[SLAYER3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_light_pos_loc[SLAYER3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_far_loc[SLAYER3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_count_loc;

    slayer3d_draw_entry *draw_list;
    int draw_count;
    int draw_capacity;
    slayer3d_gl_mesh_cache_entry *mesh_cache;

    slayer3d_overlay_entry *overlay_list;
    int overlay_count;
    int overlay_capacity;

    slayer3d_overlay_atlas *overlay_atlases;
    int overlay_atlas_count;
    int overlay_atlas_capacity;

    GLuint white_texture;
    GLuint black_texture;
    GLuint black_cubemap;

    slayer3d_gl_tex_entry *tex_cache;

    float *white_colors;
    int white_colors_capacity;

    GLuint fbo;
    GLuint fbo_color;
    GLuint fbo_depth;
    int logical_w;
    int logical_h;
    int world_w;
    int world_h;
    float world_render_scale;

    GLuint pp_fbo_a, pp_fbo_b;
    GLuint pp_tex_a, pp_tex_b;
    GLuint bloom_program;
    GLuint bloom_blur_program;
    GLuint composite_program;
    GLint bloom_scene_loc, bloom_threshold_loc;
    GLint blur_image_loc, blur_horizontal_loc;
    GLint comp_scene_loc, comp_bloom_loc, comp_vignette_loc, comp_contrast_loc, comp_saturation_loc;
    GLuint final_color_tex;

    GLuint retro_program;
    GLint retro_scene_loc;
    GLint retro_profile_loc;
    GLint retro_virtual_resolution_loc;
    GLint retro_output_resolution_loc;
    int active_retro_profile;
    int active_retro_virtual_w;
    int active_retro_virtual_h;
    int active_retro_filter;

    GLuint ssao_program;
    GLint ssao_scene_loc, ssao_depth_loc, ssao_texel_size_loc, ssao_near_loc, ssao_far_loc;

    slayer3d_render_context *current_ctx;

    GLuint ibl_irradiance_map;
    GLuint ibl_prefilter_map;
    GLuint ibl_brdf_lut;
    GLint pbr_irradiance_map_loc;
    GLint pbr_prefilter_map_loc;
    GLint pbr_brdf_lut_loc;
    GLint pbr_ibl_enabled_loc;
    GLint pbr_max_reflection_lod_loc;
    bool ibl_ready;

    GLuint equirect_to_cube_program;
    GLuint irradiance_program;
    GLuint prefilter_program;
    GLuint brdf_program;
    GLuint capture_fbo;
    GLuint capture_rbo;
};

GLuint slayer3d_gl_resolve_texture(slayer3d_gl_context *ctx, const slayer3d_texture2d *tex);
void slayer3d_gl_tex_cache_free(slayer3d_gl_context *ctx);
slayer3d_gl_mesh_cache_entry *slayer3d_gl_mesh_cache_lookup_or_create(
    slayer3d_gl_context *ctx, bool lit, GLenum primitive_mode, const float *positions, const float *normals,
    const float *uvs, const float *lightmap_uvs, const float *colors, const unsigned int *indices,
    const unsigned short *joint_indices, const float *joint_weights, int vertex_count, int index_count,
    Uint32 generation, bool has_lightmap_uvs, bool gpu_skinned);
void slayer3d_gl_mesh_cache_free(slayer3d_gl_context *ctx);
void slayer3d_gl_free_draw_list(slayer3d_gl_context *ctx);
slayer3d_draw_entry *slayer3d_gl_append_draw_entry(slayer3d_gl_context *ctx);
void slayer3d_gl_free_overlay_list(slayer3d_gl_context *ctx);
bool slayer3d_gl_create_world_targets(slayer3d_gl_context *ctx, int w, int h);
void slayer3d_gl_destroy_world_targets(slayer3d_gl_context *ctx);
int slayer3d_gl_scaled_world_dimension(int logical_dimension, float scale);
void slayer3d_gl_apply_transition_pass(slayer3d_gl_context *ctx);

#endif
