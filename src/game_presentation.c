/**
 * @file game_presentation.c
 * @brief Renderer-facing helpers for JSON-authored game data.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/collision.h"
#include "slayer3d/drawing3d.h"
#include "slayer3d/lighting.h"
#include "slayer3d/math.h"
#include "slayer3d/shapes.h"

#include "game_data_internal.h"
#include "game_presentation_internal.h"
#include "render_context_internal.h"

typedef struct scene_world_viewport
{
    const char *name;
    const char *camera;
    SDL_Rect rect;
    bool draw_viewmodel;
} scene_world_viewport;

static slayer3d_camera3d default_camera(void)
{
    slayer3d_camera3d camera;
    SDL_zero(camera);
    camera.position = slayer3d_vec3_make(0.0f, 0.0f, 16.0f);
    camera.target = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    camera.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = 11.4f;
    camera.projection = SLAYER3D_CAMERA_ORTHOGRAPHIC;
    return camera;
}

static slayer3d_camera3d active_camera_or_fallback(const slayer3d_game_data_runtime *runtime,
                                                   const slayer3d_camera3d *fallback)
{
    slayer3d_camera3d camera;
    const char *active_camera = slayer3d_game_data_active_camera(runtime);
    if (active_camera != NULL && slayer3d_game_data_get_camera(runtime, active_camera, &camera))
        return camera;
    if (fallback != NULL)
        return *fallback;
    return default_camera();
}

static bool apply_render_settings(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    slayer3d_game_data_render_settings settings;
    if (!slayer3d_game_data_get_render_settings(runtime, &settings))
        return false;

    bool ok = true;
    if (!settings.lighting_enabled)
        ok = slayer3d_set_lighting_enabled(renderer, false) && ok;
    else if (!settings.has_profile)
        ok = slayer3d_set_lighting_enabled(renderer, true) && ok;
    if (settings.has_profile)
        ok = slayer3d_set_render_profile(renderer, &settings.profile) && ok;
    ok = slayer3d_set_bloom_enabled(renderer, settings.bloom_enabled) && ok;
    ok = slayer3d_set_ssao_enabled(renderer, settings.ssao_enabled) && ok;
    ok = slayer3d_set_depth_prepass_enabled(renderer, settings.depth_prepass_enabled) && ok;
    ok = slayer3d_set_per_object_light_selection_enabled(renderer, settings.per_object_light_selection_enabled) && ok;
    ok = slayer3d_set_per_object_light_limit(renderer, settings.per_object_light_limit) && ok;
    ok = slayer3d_set_render_sample_queries_enabled(renderer, settings.performance_queries_enabled) && ok;
    ok = slayer3d_set_world_render_scale(renderer, settings.world_render_scale) && ok;
    ok = slayer3d_set_tonemap_mode(renderer, settings.tonemap) && ok;
    ok = slayer3d_clear_render_context(renderer, settings.clear_color) && ok;
    return ok;
}

static bool apply_world_lights(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                               const slayer3d_game_data_render_eval *eval)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    bool ok = slayer3d_clear_lights(renderer);
    float ambient[3] = {0.015f, 0.018f, 0.026f};
    slayer3d_game_data_get_world_ambient_light(runtime, ambient);
    ok = slayer3d_set_ambient_light(renderer, ambient[0], ambient[1], ambient[2]) && ok;

    slayer3d_light selected[SLAYER3D_MAX_LIGHTS];
    float scores[SLAYER3D_MAX_LIGHTS];
    int selected_count = 0;
    const int light_count = slayer3d_game_data_world_light_count(runtime);
    for (int i = 0; i < light_count; ++i)
    {
        slayer3d_light light;
        if (!slayer3d_game_data_get_world_light_evaluated(runtime, i, eval, &light))
            continue;
        const float score =
            light.type == SLAYER3D_LIGHT_DIRECTIONAL ? 1000000.0f + light.intensity : light.intensity * light.range;
        int insert = selected_count;
        for (int candidate = 0; candidate < selected_count; ++candidate)
        {
            if (score > scores[candidate])
            {
                insert = candidate;
                break;
            }
        }
        if (insert >= SLAYER3D_MAX_LIGHTS)
            continue;
        if (selected_count < SLAYER3D_MAX_LIGHTS)
            ++selected_count;
        for (int move = selected_count - 1; move > insert; --move)
        {
            selected[move] = selected[move - 1];
            scores[move] = scores[move - 1];
        }
        selected[insert] = light;
        scores[insert] = score;
    }
    for (int i = 0; i < selected_count; ++i)
        ok = slayer3d_add_light(renderer, &selected[i]) && ok;
    return ok;
}

static bool run_frame_hook(const slayer3d_game_data_frame_desc *frame, slayer3d_game_data_frame_hook hook)
{
    return hook == NULL || hook(frame->userdata, frame);
}

static slayer3d_mat4 camera_to_world_matrix(const slayer3d_camera3d *camera, slayer3d_vec3 forward, slayer3d_vec3 right,
                                            slayer3d_vec3 up)
{
    slayer3d_mat4 matrix = slayer3d_mat4_identity();
    matrix.m[0] = right.x;
    matrix.m[1] = right.y;
    matrix.m[2] = right.z;
    matrix.m[3] = 0.0f;
    matrix.m[4] = up.x;
    matrix.m[5] = up.y;
    matrix.m[6] = up.z;
    matrix.m[7] = 0.0f;
    matrix.m[8] = -forward.x;
    matrix.m[9] = -forward.y;
    matrix.m[10] = -forward.z;
    matrix.m[11] = 0.0f;
    matrix.m[12] = camera->position.x;
    matrix.m[13] = camera->position.y;
    matrix.m[14] = camera->position.z;
    matrix.m[15] = 1.0f;
    return matrix;
}

static bool camera_space_model_matrix(const slayer3d_camera3d *camera, slayer3d_vec3 local_offset,
                                      slayer3d_vec3 local_rotation, slayer3d_vec3 local_scale,
                                      slayer3d_mat4 *out_matrix, slayer3d_vec3 *out_position)
{
    if (camera == NULL || out_matrix == NULL || out_position == NULL)
        return false;

    const slayer3d_vec3 forward_raw = slayer3d_vec3_sub(camera->target, camera->position);
    if (slayer3d_vec3_length_squared(forward_raw) <= 0.000001f)
        return false;
    const slayer3d_vec3 forward = slayer3d_vec3_normalize(forward_raw);
    slayer3d_vec3 right = slayer3d_vec3_cross(forward, camera->up);
    if (slayer3d_vec3_length_squared(right) <= 0.000001f)
        right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    else
        right = slayer3d_vec3_normalize(right);
    const slayer3d_vec3 up = slayer3d_vec3_normalize(slayer3d_vec3_cross(right, forward));

    slayer3d_mat4 local = slayer3d_mat4_translate(slayer3d_vec3_make(local_offset.x, local_offset.y, -local_offset.z));
    if (local_rotation.y != 0.0f)
        local =
            slayer3d_mat4_multiply(local, slayer3d_mat4_rotate(slayer3d_vec3_make(0.0f, 1.0f, 0.0f), local_rotation.y));
    if (local_rotation.x != 0.0f)
        local =
            slayer3d_mat4_multiply(local, slayer3d_mat4_rotate(slayer3d_vec3_make(1.0f, 0.0f, 0.0f), local_rotation.x));
    if (local_rotation.z != 0.0f)
        local =
            slayer3d_mat4_multiply(local, slayer3d_mat4_rotate(slayer3d_vec3_make(0.0f, 0.0f, 1.0f), local_rotation.z));
    local = slayer3d_mat4_multiply(local, slayer3d_mat4_scale(local_scale));

    *out_matrix = slayer3d_mat4_multiply(camera_to_world_matrix(camera, forward, right, up), local);
    const slayer3d_vec4 position =
        slayer3d_mat4_transform_vec4(*out_matrix, slayer3d_vec4_make(0.0f, 0.0f, 0.0f, 1.0f));
    *out_position = slayer3d_vec3_make(position.x, position.y, position.z);
    return true;
}

static bool model_has_euler_rotation(slayer3d_vec3 rotation)
{
    return SDL_fabsf(rotation.x) > 0.000001f || SDL_fabsf(rotation.y) > 0.000001f || SDL_fabsf(rotation.z) > 0.000001f;
}

static bool draw_model_with_matrix(slayer3d_render_context *renderer, const slayer3d_asset_resolver *assets,
                                   const slayer3d_model *model, slayer3d_mat4 matrix, slayer3d_color tint)
{
    if (!slayer3d_push_matrix(renderer))
        return false;
    bool ok = slayer3d_multiply_matrix(renderer, matrix);
    if (ok)
        ok = slayer3d_draw_model_ex_with_assets(renderer, assets, model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f,
                                                slayer3d_vec3_make(1.0f, 1.0f, 1.0f), tint);
    const bool pop_ok = slayer3d_pop_matrix(renderer);
    return ok && pop_ok;
}

static bool draw_skinned_model_with_matrix(slayer3d_render_context *renderer, const slayer3d_asset_resolver *assets,
                                           const slayer3d_model *model, slayer3d_mat4 matrix, slayer3d_color tint,
                                           const slayer3d_mat4 *joint_matrices)
{
    if (!slayer3d_push_matrix(renderer))
        return false;
    bool ok = slayer3d_multiply_matrix(renderer, matrix);
    if (ok)
        ok = slayer3d_draw_model_skinned_with_assets(renderer, assets, model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                     slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f,
                                                     slayer3d_vec3_make(1.0f, 1.0f, 1.0f), tint, joint_matrices);
    const bool pop_ok = slayer3d_pop_matrix(renderer);
    return ok && pop_ok;
}

static bool draw_primitive(void *userdata, const slayer3d_game_data_render_primitive *primitive)
{
    primitive_draw_context *context = (primitive_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || primitive == NULL)
        return false;
    slayer3d_game_data_render_primitive resolved = *primitive;
    if ((primitive->view_space && !context->draw_view_space) || (!primitive->view_space && !context->draw_world_space))
        return true;
    slayer3d_game_data_apply_primitive_lod(context, &resolved);
    primitive = &resolved;
    if (slayer3d_game_data_primitive_sphere_can_batch(primitive))
        return slayer3d_game_data_append_sphere_draw_batch(context, primitive);
    if (!slayer3d_game_data_flush_sphere_draw_batch(context))
        return false;

    const bool restore_lighting = slayer3d_is_lighting_enabled(context->renderer);
    if (!primitive->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, false);
    slayer3d_set_emissive(context->renderer, primitive->emissive_color.x, primitive->emissive_color.y,
                          primitive->emissive_color.z);
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
    {
        const slayer3d_texture2d *texture = slayer3d_game_data_primitive_texture(context, primitive);
        if (!slayer3d_draw_cube_textured(context->renderer, primitive->position, primitive->size,
                                         primitive->rotation_axis, primitive->rotation_angle, texture,
                                         primitive->color))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE)
    {
        const slayer3d_texture2d *texture = slayer3d_game_data_primitive_texture(context, primitive);
        if (!slayer3d_draw_sphere_textured(context->renderer, primitive->position, primitive->radius, primitive->rings,
                                           primitive->slices, primitive->rotation_axis, primitive->rotation_angle,
                                           texture, primitive->color))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
    {
        if (!slayer3d_game_data_draw_mesh_primitive(context, primitive))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        if (!slayer3d_game_data_draw_sphere_batch(context->renderer, primitive))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPRITE)
    {
        if (context->sprite_cache == NULL)
            return true;
        slayer3d_game_data_sprite_cache_entry *entry = slayer3d_game_data_find_or_load_sprite_entry(
            context->runtime, context->sprite_cache, primitive->sprite_asset);
        if (entry == NULL)
            return false;

        slayer3d_sprite_actor actor;
        SDL_zero(actor);
        slayer3d_sprite_asset_apply_actor(&actor, &entry->sprite);
        actor.position = primitive->position;
        actor.size = primitive->sprite_size.x > 0.0f && primitive->sprite_size.y > 0.0f ? primitive->sprite_size
                                                                                        : (slayer3d_vec2){1.0f, 1.0f};
        actor.tint = primitive->color;
        actor.visible = true;
        actor.sector_id = -1;
        slayer3d_sprite_actor_set_facing_yaw(&actor, primitive->sprite_facing_yaw);
        if (context->eval != NULL)
            actor.animation_time = context->eval->time;

        slayer3d_sprite_scene scene;
        SDL_zero(scene);
        scene.actors = &actor;
        scene.count = 1;
        scene.capacity = 1;
        const slayer3d_vec3 camera_position =
            context->camera != NULL ? context->camera->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        slayer3d_sprite_scene_draw(&scene, context->renderer, camera_position, NULL);
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_MODEL)
    {
        if (context->model_cache == NULL)
            return true;
        slayer3d_game_data_model_cache_entry *entry =
            slayer3d_game_data_find_or_load_model_entry(context->runtime, context->model_cache, primitive->model_asset);
        if (entry == NULL)
            return false;
        if (slayer3d_game_data_model_lod_should_cull(context, &entry->model, primitive))
        {
            slayer3d_set_emissive(context->renderer, 0.0f, 0.0f, 0.0f);
            if (!primitive->lighting_enabled)
                slayer3d_set_lighting_enabled(context->renderer, restore_lighting);
            return true;
        }
        slayer3d_vec3 model_position = primitive->position;
        slayer3d_vec3 model_rotation = primitive->euler_rotation;
        slayer3d_mat4 model_matrix = slayer3d_mat4_identity();
        bool use_model_matrix = false;
        if (primitive->view_space &&
            !camera_space_model_matrix(context->camera, primitive->position, primitive->euler_rotation,
                                       primitive->model_scale, &model_matrix, &model_position))
        {
            return true;
        }
        if (primitive->view_space)
        {
            model_rotation = primitive->euler_rotation;
            use_model_matrix = true;
        }
        if (primitive->animation_clip >= 0 && entry->model.skeleton != NULL && entry->model.animation_count > 0)
        {
            int joint_count = 0;
            const slayer3d_mat4 *joint_matrices = slayer3d_game_data_model_cache_evaluate_pose(
                context->model_cache, context->renderer, &entry->model, primitive->animation_clip,
                primitive->animation_time, primitive->animation_loop, &joint_count);
            if (joint_matrices == NULL || joint_count <= 0)
                return false;
            const bool drawn =
                use_model_matrix
                    ? draw_skinned_model_with_matrix(context->renderer, context->model_cache->assets, &entry->model,
                                                     model_matrix, primitive->color, joint_matrices)
                    : slayer3d_draw_model_skinned_with_assets(context->renderer, context->model_cache->assets,
                                                              &entry->model, model_position, primitive->rotation_axis,
                                                              primitive->rotation_angle, primitive->model_scale,
                                                              primitive->color, joint_matrices);
            if (!drawn)
                return false;
        }
        else if (use_model_matrix)
        {
            if (!draw_model_with_matrix(context->renderer, context->model_cache->assets, &entry->model, model_matrix,
                                        primitive->color))
            {
                return false;
            }
        }
        else if (model_has_euler_rotation(model_rotation))
        {
            if (!slayer3d_draw_model_euler_with_assets(context->renderer, context->model_cache->assets, &entry->model,
                                                       model_position, model_rotation, primitive->model_scale,
                                                       primitive->color))
            {
                return false;
            }
        }
        else if (!slayer3d_draw_model_ex_with_assets(
                     context->renderer, context->model_cache->assets, &entry->model, model_position,
                     primitive->rotation_axis, primitive->rotation_angle, primitive->model_scale, primitive->color))
        {
            return false;
        }
    }
    slayer3d_set_emissive(context->renderer, 0.0f, 0.0f, 0.0f);
    if (!primitive->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, restore_lighting);
    return true;
}

static bool draw_render_primitives_evaluated_with_cache(
    const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
    const slayer3d_game_data_render_eval *eval, slayer3d_game_data_image_cache *image_cache,
    slayer3d_game_data_sprite_cache *sprite_cache, slayer3d_game_data_model_cache *model_cache,
    slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache, const slayer3d_camera3d *camera,
    bool draw_world_space, bool draw_view_space)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    primitive_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.image_cache = image_cache;
    context.sprite_cache = sprite_cache;
    context.model_cache = model_cache;
    context.mesh_primitive_cache = mesh_primitive_cache;
    context.camera = camera;
    context.eval = eval;
    context.draw_world_space = draw_world_space;
    context.draw_view_space = draw_view_space;
    (void)slayer3d_game_data_get_render_settings(runtime, &context.render_settings);
    bool ok = slayer3d_game_data_for_each_render_primitive_evaluated(runtime, eval, draw_primitive, &context);
    ok = slayer3d_game_data_flush_sphere_draw_batch(&context) && ok;
    SDL_free(context.sphere_batch_positions);
    return ok;
}

static bool draw_active_scene_skybox(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                     slayer3d_game_data_image_cache *image_cache)
{
    slayer3d_game_data_scene_skybox skybox_desc;
    if (runtime == NULL || renderer == NULL || image_cache == NULL)
        return true;
    if (!slayer3d_game_data_get_active_scene_skybox(runtime, &skybox_desc))
        return true;

    slayer3d_game_data_image_cache_entry *pos_x =
        slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, skybox_desc.pos_x);
    slayer3d_game_data_image_cache_entry *neg_x =
        slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, skybox_desc.neg_x);
    slayer3d_game_data_image_cache_entry *pos_y =
        slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, skybox_desc.pos_y);
    slayer3d_game_data_image_cache_entry *neg_y =
        slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, skybox_desc.neg_y);
    slayer3d_game_data_image_cache_entry *pos_z =
        slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, skybox_desc.pos_z);
    slayer3d_game_data_image_cache_entry *neg_z =
        slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, skybox_desc.neg_z);
    if (pos_x == NULL || neg_x == NULL || pos_y == NULL || neg_y == NULL || pos_z == NULL || neg_z == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load active scene skybox image assets");
        return false;
    }

    slayer3d_skybox_textured skybox = {&pos_x->texture, &neg_x->texture, &pos_y->texture, &neg_y->texture,
                                       &pos_z->texture, &neg_z->texture, skybox_desc.size};
    return slayer3d_draw_skybox_textured(renderer, &skybox);
}

bool slayer3d_game_data_draw_render_primitives(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_render_context *renderer)
{
    return draw_render_primitives_evaluated_with_cache(runtime, renderer, NULL, NULL, NULL, NULL, NULL, NULL, true,
                                                       true);
}

bool slayer3d_game_data_draw_render_primitives_evaluated(const slayer3d_game_data_runtime *runtime,
                                                         slayer3d_render_context *renderer,
                                                         const slayer3d_game_data_render_eval *eval)
{
    return draw_render_primitives_evaluated_with_cache(runtime, renderer, eval, NULL, NULL, NULL, NULL, NULL, true,
                                                       true);
}

static slayer3d_camera3d game_data_viewmodel_camera(const slayer3d_camera3d *scene_camera)
{
    slayer3d_camera3d camera;
    SDL_zero(camera);
    camera.projection = scene_camera != NULL ? scene_camera->projection : SLAYER3D_CAMERA_PERSPECTIVE;
    camera.fovy = scene_camera != NULL ? scene_camera->fovy : SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES;
    camera.fov_axis = scene_camera != NULL ? scene_camera->fov_axis : SLAYER3D_CAMERA_FOV_VERTICAL;
    camera.position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    camera.target = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
    camera.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    return camera;
}

typedef struct editor_debug_draw_context
{
    slayer3d_render_context *renderer;
    bool ok;
} editor_debug_draw_context;

static bool draw_editor_debug_primitive(void *userdata, const slayer3d_game_data_editor_debug_primitive *primitive)
{
    editor_debug_draw_context *context = (editor_debug_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || primitive == NULL)
        return false;
    if (!slayer3d_draw_line_3d(context->renderer, primitive->start, primitive->end, primitive->color))
    {
        context->ok = false;
        return false;
    }
    return true;
}

bool slayer3d_game_data_draw_editor_debug_primitives(const slayer3d_game_data_runtime *runtime,
                                                     slayer3d_render_context *renderer,
                                                     const slayer3d_game_data_editor_debug_desc *desc)
{
    if (runtime == NULL || renderer == NULL || desc == NULL)
        return false;

    editor_debug_draw_context context;
    SDL_zero(context);
    context.renderer = renderer;
    context.ok = true;
    if (!slayer3d_game_data_for_each_editor_debug_primitive(runtime, desc, draw_editor_debug_primitive, &context))
        return false;
    return context.ok;
}

bool slayer3d_game_data_draw_active_editor_debug_primitives(const slayer3d_game_data_runtime *runtime,
                                                            slayer3d_render_context *renderer)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    editor_debug_draw_context context;
    SDL_zero(context);
    context.renderer = renderer;
    context.ok = true;
    if (!slayer3d_game_data_for_each_active_editor_debug_primitive(runtime, draw_editor_debug_primitive, &context))
        return false;
    return context.ok;
}

static bool scene_world_viewport_rect(yyjson_val *viewport, SDL_Rect *out_rect)
{
    if (viewport == NULL || out_rect == NULL)
        return false;
    yyjson_val *rect = obj_get(viewport, "rect");
    if (!yyjson_is_arr(rect) || yyjson_arr_size(rect) != 4)
        return false;
    const double x = yyjson_get_num(yyjson_arr_get(rect, 0));
    const double y = yyjson_get_num(yyjson_arr_get(rect, 1));
    const double w = yyjson_get_num(yyjson_arr_get(rect, 2));
    const double h = yyjson_get_num(yyjson_arr_get(rect, 3));
    if (!(w > 0.0) || !(h > 0.0))
        return false;
    *out_rect = (SDL_Rect){(int)SDL_lround(x), (int)SDL_lround(y), (int)SDL_lround(w), (int)SDL_lround(h)};
    return true;
}

static bool scene_world_viewport_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *viewport,
                                           scene_world_viewport *out_viewport)
{
    if (runtime == NULL || !yyjson_is_obj(viewport) || out_viewport == NULL)
        return false;
    yyjson_val *active_if = obj_get(viewport, "active_if");
    if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
        return false;
    SDL_zero(*out_viewport);
    out_viewport->name = json_string(viewport, "name", NULL);
    out_viewport->camera = json_string(viewport, "camera", NULL);
    out_viewport->draw_viewmodel = json_bool(viewport, "viewmodel", false);
    return out_viewport->camera != NULL && scene_world_viewport_rect(viewport, &out_viewport->rect);
}

static bool draw_world_for_camera(const slayer3d_game_data_frame_desc *frame, const slayer3d_camera3d *camera,
                                  bool draw_viewmodel)
{
    if (frame == NULL || camera == NULL)
        return false;
    bool ok = true;
    if (slayer3d_begin_mode_3d(frame->renderer, *camera))
    {
        ok = run_frame_hook(frame, frame->before_world_3d) && ok;
        ok = draw_active_scene_skybox(frame->runtime, frame->renderer, frame->image_cache) && ok;
        ok = slayer3d_game_data_draw_sector_levels_with_assets(
                 frame->runtime, frame->renderer, frame->image_cache != NULL ? frame->image_cache->assets : NULL,
                 camera) &&
             ok;
        ok = slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(
                 frame->runtime, frame->renderer, frame->image_cache != NULL ? frame->image_cache->assets : NULL,
                 camera) &&
             ok;
        if (frame->particle_cache != NULL)
            ok = slayer3d_game_data_draw_particles_filtered(frame->runtime, frame->renderer, frame->particle_cache,
                                                            true, false) &&
                 ok;
        ok = draw_render_primitives_evaluated_with_cache(frame->runtime, frame->renderer, frame->render_eval,
                                                         frame->image_cache, frame->sprite_cache, frame->model_cache,
                                                         frame->mesh_primitive_cache, camera, true, false) &&
             ok;
        ok = slayer3d_game_data_draw_active_editor_debug_primitives(frame->runtime, frame->renderer) && ok;
        ok = run_frame_hook(frame, frame->after_world_3d) && ok;
        slayer3d_end_mode_3d(frame->renderer);
    }
    else
    {
        ok = false;
    }

    if (draw_viewmodel && ok)
    {
        const slayer3d_camera3d viewmodel_camera = game_data_viewmodel_camera(camera);
        if (slayer3d_begin_mode_3d(frame->renderer, viewmodel_camera))
        {
            ok = draw_render_primitives_evaluated_with_cache(
                     frame->runtime, frame->renderer, frame->render_eval, frame->image_cache, frame->sprite_cache,
                     frame->model_cache, frame->mesh_primitive_cache, &viewmodel_camera, false, true) &&
                 ok;
            if (frame->particle_cache != NULL)
                ok = slayer3d_game_data_draw_particles_filtered(frame->runtime, frame->renderer, frame->particle_cache,
                                                                false, true) &&
                     ok;
            slayer3d_end_mode_3d(frame->renderer);
        }
        else
        {
            ok = false;
        }
    }
    return ok;
}

static bool draw_active_scene_world_viewports(const slayer3d_game_data_frame_desc *frame, bool *out_drawn)
{
    if (out_drawn != NULL)
        *out_drawn = false;
    const scene_entry *scene = active_scene_entry_const(frame != NULL ? frame->runtime : NULL);
    yyjson_val *viewports = obj_get(scene != NULL ? scene->root : NULL, "world_viewports");
    if (!yyjson_is_arr(viewports))
        return true;

    bool ok = true;
    bool drawn = false;
    for (size_t i = 0; i < yyjson_arr_size(viewports); ++i)
    {
        scene_world_viewport viewport;
        if (!scene_world_viewport_from_json(frame->runtime, yyjson_arr_get(viewports, i), &viewport))
            continue;

        slayer3d_camera3d camera;
        if (!slayer3d_game_data_get_camera(frame->runtime, viewport.camera, &camera))
        {
            ok = false;
            continue;
        }
        if (!slayer3d_set_render_viewport(frame->renderer, &viewport.rect) ||
            !slayer3d_set_scissor_rect(frame->renderer, &viewport.rect))
        {
            ok = false;
            continue;
        }
        ok = draw_world_for_camera(frame, &camera, viewport.draw_viewmodel) && ok;
        drawn = true;
    }

    (void)slayer3d_set_scissor_rect(frame->renderer, NULL);
    (void)slayer3d_set_render_viewport(frame->renderer, NULL);
    if (out_drawn != NULL)
        *out_drawn = drawn;
    return ok;
}

bool slayer3d_game_data_draw_frame(const slayer3d_game_data_frame_desc *frame)
{
    if (frame == NULL || frame->runtime == NULL || frame->renderer == NULL)
        return false;

    bool ok = true;
    ok = apply_render_settings(frame->runtime, frame->renderer) && ok;
    ok = apply_world_lights(frame->runtime, frame->renderer, frame->render_eval) && ok;
    slayer3d_game_data_model_cache_begin_pose_frame(frame->model_cache);

    if (slayer3d_game_data_active_scene_renders_world(frame->runtime))
    {
        bool drew_viewports = false;
        ok = draw_active_scene_world_viewports(frame, &drew_viewports) && ok;
        if (!drew_viewports)
        {
            const slayer3d_camera3d camera = active_camera_or_fallback(frame->runtime, frame->fallback_camera);
            ok = draw_world_for_camera(frame, &camera, true) && ok;
        }
    }

    ok = run_frame_hook(frame, frame->before_ui) && ok;
    ok = slayer3d_game_data_draw_ui_rects(frame->runtime, frame->renderer, frame->metrics, frame->render_eval) && ok;
    if (frame->image_cache != NULL)
        ok = slayer3d_game_data_draw_ui_images(frame->runtime, frame->renderer, frame->image_cache, frame->metrics,
                                               frame->render_eval) &&
             ok;
    if (frame->font_cache != NULL)
    {
        ok = slayer3d_game_data_draw_ui_text(frame->runtime, frame->renderer, frame->font_cache, frame->metrics,
                                             frame->pulse_phase) &&
             ok;
    }
    if (frame->app_flow != NULL)
        slayer3d_game_data_app_flow_draw(frame->app_flow, frame->renderer);
    ok = run_frame_hook(frame, frame->after_ui) && ok;
    return ok;
}
