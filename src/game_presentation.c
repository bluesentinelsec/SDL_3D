/**
 * @file game_presentation.c
 * @brief Renderer-facing helpers for JSON-authored game data.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include "slayer3d/collision.h"
#include "slayer3d/drawing3d.h"
#include "slayer3d/lighting.h"
#include "slayer3d/math.h"
#include "slayer3d/shapes.h"
#include "slayer3d/time.h"

#include "game_data_internal.h"
#include "game_presentation_internal.h"
#include "render_context_internal.h"

typedef struct game_presentation_profile
{
    Uint64 last_counter;
    double assets_ms;
    double settings_ms;
    double world_ms;
    double ui_ms;
    int frames;
} game_presentation_profile;

static void record_game_presentation_profile(Uint64 frame_start, Uint64 assets_end, Uint64 settings_end,
                                             Uint64 world_end, Uint64 ui_end)
{
    static game_presentation_profile profile;
    const double frequency = (double)SDL_GetPerformanceFrequency();
    if (frequency <= 0.0)
        return;

    const double milliseconds = 1000.0 / frequency;
    profile.assets_ms += (double)(assets_end - frame_start) * milliseconds;
    profile.settings_ms += (double)(settings_end - assets_end) * milliseconds;
    profile.world_ms += (double)(world_end - settings_end) * milliseconds;
    profile.ui_ms += (double)(ui_end - world_end) * milliseconds;
    profile.frames++;
    if (profile.last_counter == 0)
        profile.last_counter = frame_start;

    if ((double)(ui_end - profile.last_counter) < frequency)
        return;

    const double frames = profile.frames > 0 ? (double)profile.frames : 1.0;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SLAYER3D presentation profile: assets=%.2fms settings=%.2fms world=%.2fms ui=%.2fms",
                profile.assets_ms / frames, profile.settings_ms / frames, profile.world_ms / frames,
                profile.ui_ms / frames);
    profile.last_counter = ui_end;
    profile.assets_ms = 0.0;
    profile.settings_ms = 0.0;
    profile.world_ms = 0.0;
    profile.ui_ms = 0.0;
    profile.frames = 0;
}

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
    ok = slayer3d_configure_dynamic_world_render_scale(
             renderer, settings.dynamic_world_render_scale_enabled, settings.dynamic_world_render_min_scale,
             settings.dynamic_world_render_max_scale, settings.dynamic_world_render_target_fps) &&
         ok;
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
    const int upload_limit = SDL_clamp(slayer3d_game_data_world_light_upload_limit(runtime), 0, SLAYER3D_MAX_LIGHTS);
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
        if (insert >= upload_limit)
            continue;
        if (selected_count < upload_limit)
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

typedef struct queue_scene_assets_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_game_data_asset_warmup_queue *queue;
} queue_scene_assets_context;

static const char *queue_scene_image_source_path(const queue_scene_assets_context *context, const char *image_id)
{
    if (context == NULL || context->runtime == NULL || image_id == NULL || image_id[0] == '\0')
        return NULL;
    slayer3d_game_data_image_asset asset;
    if (!slayer3d_game_data_get_image_asset(context->runtime, image_id, &asset))
        return NULL;
    return asset.path != NULL ? asset.path : asset.sprite;
}

static bool queue_scene_ui_image(void *userdata, const slayer3d_game_data_ui_image *image)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || image == NULL || image->image == NULL || image->image[0] == '\0')
    {
        return true;
    }
    (void)slayer3d_game_data_asset_warmup_request_ui_image_source(
        context->queue, queue_scene_image_source_path(context, image->image), image->image);
    return true;
}

static bool queue_scene_ui_widget_image(void *userdata, const char *image_id)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || image_id == NULL || image_id[0] == '\0')
        return true;
    (void)slayer3d_game_data_asset_warmup_request_ui_image_source(
        context->queue, queue_scene_image_source_path(context, image_id), image_id);
    return true;
}

static void queue_skybox_image(slayer3d_game_data_asset_warmup_queue *queue, const char *image_id)
{
    if (queue == NULL || image_id == NULL || image_id[0] == '\0')
        return;
    (void)slayer3d_game_data_asset_warmup_request_ui_image(queue, image_id);
}

#define SCENE_SKY_PATH_MAX 512
#define SCENE_SKY_PRESET_LAYER_COUNT 2

/* Active-scene sky with preset references resolved to media skybox paths. */
typedef struct scene_sky_resolved
{
    slayer3d_game_data_scene_skybox desc;
    char sphere_path[SCENE_SKY_PATH_MAX];
    char face_paths[6][SCENE_SKY_PATH_MAX];
    char layer_paths[SCENE_SKY_PRESET_LAYER_COUNT][SCENE_SKY_PATH_MAX];
    const char *faces[6];
    const char *sphere;
    bool has_faces;
    slayer3d_game_data_scene_sky_layer layers[SLAYER3D_GAME_DATA_SCENE_SKY_MAX_LAYERS];
    int layer_count;
} scene_sky_resolved;

static bool scene_sky_resolve_preset_sphere(scene_sky_resolved *sky, const char *media_dir)
{
    if (sky->desc.preset == NULL || sky->desc.preset[0] == '\0' || media_dir == NULL || media_dir[0] == '\0')
        return false;
    SDL_snprintf(sky->sphere_path, sizeof(sky->sphere_path), "%s/skyboxes/%s/sphere.png", media_dir, sky->desc.preset);
    sky->sphere = sky->sphere_path;
    return true;
}

static bool scene_sky_resolve_preset(scene_sky_resolved *sky, const char *media_dir)
{
    static const char *const face_files[6] = {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};
    if (sky->desc.preset == NULL || sky->desc.preset[0] == '\0' || media_dir == NULL || media_dir[0] == '\0')
        return false;
    for (int i = 0; i < 6; ++i)
    {
        SDL_snprintf(sky->face_paths[i], sizeof(sky->face_paths[i]), "%s/skyboxes/%s/%s", media_dir, sky->desc.preset,
                     face_files[i]);
        sky->faces[i] = sky->face_paths[i];
    }
    return true;
}

/* Presets author only textures, so layered presets scroll a slow outer
 * cloud deck under a faster, denser inner deck for a Quake-style sky. */
static void scene_sky_resolve_preset_layers(scene_sky_resolved *sky, const char *media_dir)
{
    static const char *const layer_files[SCENE_SKY_PRESET_LAYER_COUNT] = {"layer_outer.png", "layer_inner.png"};
    if (sky->desc.preset == NULL || sky->desc.preset[0] == '\0' || media_dir == NULL || media_dir[0] == '\0')
        return;
    for (int i = 0; i < SCENE_SKY_PRESET_LAYER_COUNT; ++i)
    {
        SDL_snprintf(sky->layer_paths[i], sizeof(sky->layer_paths[i]), "%s/skyboxes/%s/%s", media_dir, sky->desc.preset,
                     layer_files[i]);
    }
    sky->layers[0] = (slayer3d_game_data_scene_sky_layer){.texture = sky->layer_paths[0],
                                                          .scroll_x = 0.01f,
                                                          .scroll_y = 0.0f,
                                                          .scale = 1.0f,
                                                          .opacity = 1.0f,
                                                          .depth = 1.0f};
    sky->layers[1] = (slayer3d_game_data_scene_sky_layer){.texture = sky->layer_paths[1],
                                                          .scroll_x = -0.025f,
                                                          .scroll_y = 0.006f,
                                                          .scale = 2.0f,
                                                          .opacity = 0.65f,
                                                          .depth = 0.55f};
    sky->layer_count = SCENE_SKY_PRESET_LAYER_COUNT;
}

static bool resolve_active_scene_sky(const slayer3d_game_data_runtime *runtime, const char *media_dir,
                                     scene_sky_resolved *sky)
{
    SDL_zero(*sky);
    if (runtime == NULL || !slayer3d_game_data_get_active_scene_skybox(runtime, &sky->desc))
        return false;

    if (sky->desc.sphere != NULL)
    {
        sky->sphere = sky->desc.sphere;
    }
    else if (sky->desc.has_faces)
    {
        sky->faces[0] = sky->desc.pos_x;
        sky->faces[1] = sky->desc.neg_x;
        sky->faces[2] = sky->desc.pos_y;
        sky->faces[3] = sky->desc.neg_y;
        sky->faces[4] = sky->desc.pos_z;
        sky->faces[5] = sky->desc.neg_z;
        sky->has_faces = true;
    }
    else if (SDL_strcmp(sky->desc.mode, "sphere") == 0 || sky->desc.preset != NULL)
    {
        scene_sky_resolve_preset_sphere(sky, media_dir);
    }
    else
    {
        sky->has_faces = scene_sky_resolve_preset(sky, media_dir);
    }

    if (sky->desc.layer_count > 0)
    {
        sky->layer_count = sky->desc.layer_count;
        SDL_memcpy(sky->layers, sky->desc.layers, sizeof(sky->desc.layers));
    }
    else if (SDL_strcmp(sky->desc.mode, "layers") == 0)
    {
        scene_sky_resolve_preset_layers(sky, media_dir);
    }

    return sky->sphere != NULL || sky->has_faces || sky->layer_count > 0;
}

static void queue_active_scene_skybox_images(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_asset_warmup_queue *queue, const char *media_dir)
{
    scene_sky_resolved sky;
    if (runtime == NULL || queue == NULL || !resolve_active_scene_sky(runtime, media_dir, &sky))
        return;

    if (sky.sphere != NULL)
        queue_skybox_image(queue, sky.sphere);
    if (sky.has_faces)
    {
        for (int i = 0; i < 6; ++i)
            queue_skybox_image(queue, sky.faces[i]);
    }
    for (int i = 0; i < sky.layer_count; ++i)
        queue_skybox_image(queue, sky.layers[i].texture);
}

static bool queue_font_asset(void *userdata, const slayer3d_game_data_font_asset *font)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || font == NULL || font->id == NULL || font->id[0] == '\0')
        return true;
    (void)slayer3d_game_data_asset_warmup_request_font(context->queue, font->id);
    return true;
}

static bool queue_model_browser_asset(void *userdata, const slayer3d_game_data_model_asset *model)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || model == NULL || model->id == NULL || model->id[0] == '\0')
        return true;
    (void)slayer3d_game_data_asset_warmup_request_model(context->queue, model->id);
    return true;
}

static bool queue_sound_asset(void *userdata, const slayer3d_game_data_sound_asset *sound)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || sound == NULL || sound->path == NULL || sound->path[0] == '\0')
        return true;
    (void)slayer3d_game_data_asset_warmup_request_audio_file(context->queue, sound->path);
    return true;
}

static bool queue_music_asset(void *userdata, const slayer3d_game_data_music_asset *music)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || music == NULL || music->path == NULL || music->path[0] == '\0')
        return true;
    (void)slayer3d_game_data_asset_warmup_request_audio_file(context->queue, music->path);
    return true;
}

static bool queue_ambient_asset(void *userdata, const slayer3d_game_data_ambient_asset *ambient)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || ambient == NULL || ambient->path == NULL ||
        ambient->path[0] == '\0')
    {
        return true;
    }
    (void)slayer3d_game_data_asset_warmup_request_audio_file(context->queue, ambient->path);
    return true;
}

static bool queue_brush_world_material_textures(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || instance == NULL || instance->world == NULL ||
        instance->world->materials == NULL)
    {
        return true;
    }

    const char *source_path = instance->world->render_model != NULL ? instance->world->render_model->source_path : NULL;
    for (int i = 0; i < instance->world->material_count; ++i)
    {
        const char *texture_path = instance->world->materials[i].texture;
        if (texture_path == NULL || texture_path[0] == '\0')
            continue;
        (void)slayer3d_game_data_asset_warmup_request_texture(context->queue, source_path, texture_path);
    }
    return true;
}

static bool queue_render_primitive_assets(void *userdata, const slayer3d_game_data_render_primitive *primitive)
{
    queue_scene_assets_context *context = (queue_scene_assets_context *)userdata;
    if (context == NULL || context->queue == NULL || primitive == NULL)
        return true;
    if (primitive->texture_image != NULL && primitive->texture_image[0] != '\0')
        (void)slayer3d_game_data_asset_warmup_request_ui_image(context->queue, primitive->texture_image);
    if (primitive->sprite_asset != NULL && primitive->sprite_asset[0] != '\0')
        (void)slayer3d_game_data_asset_warmup_request_sprite(context->queue, primitive->sprite_asset);
    if (primitive->model_asset != NULL && primitive->model_asset[0] != '\0')
        (void)slayer3d_game_data_asset_warmup_request_model(context->queue, primitive->model_asset);
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE &&
        primitive->draw_mode != SLAYER3D_GAME_DATA_RENDER_DRAW_WIRE)
    {
        (void)slayer3d_game_data_asset_warmup_request_mesh_primitive(context->queue, primitive);
    }
    return true;
}

static void queue_active_scene_assets(const slayer3d_game_data_frame_desc *frame)
{
    if (frame == NULL || frame->runtime == NULL || frame->asset_warmup == NULL)
        return;

    const char *active_scene = slayer3d_game_data_active_scene(frame->runtime);
    if (active_scene != NULL && frame->asset_warmup->requested_scene != NULL &&
        SDL_strcmp(frame->asset_warmup->requested_scene, active_scene) == 0)
        return;

    if (active_scene != NULL || frame->asset_warmup->requested_scene != NULL)
        (void)slayer3d_game_data_asset_warmup_queue_cancel_pending(frame->asset_warmup);

    queue_scene_assets_context context;
    SDL_zero(context);
    context.runtime = frame->runtime;
    context.queue = frame->asset_warmup;
    (void)slayer3d_game_data_for_each_font_asset(frame->runtime, queue_font_asset, &context);
    (void)slayer3d_game_data_for_each_ui_image(frame->runtime, queue_scene_ui_image, &context);
    (void)slayer3d_game_data_for_each_ui_widget_image_id(frame->runtime, queue_scene_ui_widget_image, &context);
    queue_active_scene_skybox_images(frame->runtime, frame->asset_warmup,
                                     frame->font_cache != NULL ? frame->font_cache->media_dir : NULL);
    (void)slayer3d_game_data_for_each_model_asset(frame->runtime, queue_model_browser_asset, &context);
    (void)slayer3d_game_data_for_each_sound_asset(frame->runtime, queue_sound_asset, &context);
    (void)slayer3d_game_data_for_each_music_asset(frame->runtime, queue_music_asset, &context);
    (void)slayer3d_game_data_for_each_ambient_asset(frame->runtime, queue_ambient_asset, &context);
    (void)slayer3d_game_data_for_each_brush_world_instance(frame->runtime, queue_brush_world_material_textures,
                                                           &context);
    (void)slayer3d_game_data_for_each_render_primitive(frame->runtime, queue_render_primitive_assets, &context);

    char *requested_scene = active_scene != NULL ? SDL_strdup(active_scene) : NULL;
    if (active_scene == NULL || requested_scene != NULL)
    {
        SDL_free(frame->asset_warmup->requested_scene);
        frame->asset_warmup->requested_scene = requested_scene;
    }
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
        if (!slayer3d_game_data_primitive_asset_ready(context, SLAYER3D_GAME_DATA_ASSET_WARMUP_SPRITE,
                                                      primitive->sprite_asset))
        {
            return true;
        }
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
        if (!slayer3d_game_data_primitive_asset_ready(context, SLAYER3D_GAME_DATA_ASSET_WARMUP_MODEL,
                                                      primitive->model_asset))
        {
            return true;
        }
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
    slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache,
    const slayer3d_game_data_asset_warmup_queue *asset_warmup, const slayer3d_camera3d *camera, bool draw_world_space,
    bool draw_view_space)
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
    context.asset_warmup = asset_warmup;
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

static bool scene_sky_image_pending(const slayer3d_game_data_asset_warmup_queue *asset_warmup, const char *image_id)
{
    slayer3d_game_data_asset_warmup_state warmup_state;
    return slayer3d_game_data_asset_warmup_request_state(asset_warmup, SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE, NULL,
                                                         image_id, &warmup_state) &&
           warmup_state != SLAYER3D_GAME_DATA_ASSET_WARMUP_READY;
}

static bool draw_active_scene_sky_layers(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                         slayer3d_game_data_image_cache *image_cache, const scene_sky_resolved *sky)
{
    slayer3d_sky_layer layers[SLAYER3D_GAME_DATA_SCENE_SKY_MAX_LAYERS];
    int ready_count = 0;
    for (int i = 0; i < sky->layer_count; ++i)
    {
        slayer3d_game_data_image_cache_entry *entry =
            slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, sky->layers[i].texture);
        if (entry == NULL)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load sky layer texture '%s'", sky->layers[i].texture);
            return false;
        }
        (void)slayer3d_set_texture_wrap(&entry->texture, SLAYER3D_TEXTURE_WRAP_REPEAT, SLAYER3D_TEXTURE_WRAP_REPEAT);
        layers[ready_count].texture = &entry->texture;
        layers[ready_count].scroll_x = sky->layers[i].scroll_x;
        layers[ready_count].scroll_y = sky->layers[i].scroll_y;
        layers[ready_count].scale = sky->layers[i].scale;
        layers[ready_count].opacity = sky->layers[i].opacity;
        layers[ready_count].depth = sky->layers[i].depth;
        layers[ready_count].tint = sky->layers[i].has_tint ? sky->layers[i].tint : (slayer3d_color){255, 255, 255, 255};
        ++ready_count;
    }
    if (ready_count == 0)
        return true;
    return slayer3d_draw_sky_layers(renderer, layers, ready_count, sky->desc.size, slayer3d_time_get_real_time());
}

static bool draw_active_scene_skybox(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                     slayer3d_game_data_image_cache *image_cache,
                                     const slayer3d_game_data_asset_warmup_queue *asset_warmup, const char *media_dir)
{
    scene_sky_resolved sky;
    if (runtime == NULL || renderer == NULL || image_cache == NULL)
        return true;
    if (!resolve_active_scene_sky(runtime, media_dir, &sky))
        return true;

    if (sky.sphere != NULL && scene_sky_image_pending(asset_warmup, sky.sphere))
        return true;
    if (sky.has_faces)
    {
        for (int i = 0; i < 6; ++i)
        {
            if (scene_sky_image_pending(asset_warmup, sky.faces[i]))
                return true;
        }
    }
    for (int i = 0; i < sky.layer_count; ++i)
    {
        if (scene_sky_image_pending(asset_warmup, sky.layers[i].texture))
            return true;
    }

    if (sky.sphere != NULL)
    {
        slayer3d_game_data_image_cache_entry *sphere =
            slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, sky.sphere);
        if (sphere == NULL)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load active scene sky sphere image asset");
            return false;
        }
        slayer3d_sky_sphere_textured sky_sphere = {&sphere->texture};
        if (!slayer3d_draw_sky_sphere_textured(renderer, &sky_sphere))
            return false;
    }
    else if (sky.has_faces)
    {
        slayer3d_game_data_image_cache_entry *faces[6];
        for (int i = 0; i < 6; ++i)
        {
            faces[i] = slayer3d_game_data_find_or_load_image_entry(runtime, image_cache, sky.faces[i]);
            if (faces[i] == NULL)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load active scene skybox image assets");
                return false;
            }
        }
        slayer3d_skybox_textured skybox = {&faces[0]->texture, &faces[1]->texture, &faces[2]->texture,
                                           &faces[3]->texture, &faces[4]->texture, &faces[5]->texture,
                                           sky.desc.size};
        if (!slayer3d_draw_skybox_textured(renderer, &skybox))
            return false;
    }

    if (sky.layer_count > 0)
        return draw_active_scene_sky_layers(runtime, renderer, image_cache, &sky);
    return true;
}

bool slayer3d_game_data_draw_render_primitives(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_render_context *renderer)
{
    return draw_render_primitives_evaluated_with_cache(runtime, renderer, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                                       true, true);
}

bool slayer3d_game_data_draw_render_primitives_evaluated(const slayer3d_game_data_runtime *runtime,
                                                         slayer3d_render_context *renderer,
                                                         const slayer3d_game_data_render_eval *eval)
{
    return draw_render_primitives_evaluated_with_cache(runtime, renderer, eval, NULL, NULL, NULL, NULL, NULL, NULL,
                                                       true, true);
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
    if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HOVER_LABEL)
        return true;
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

typedef struct editor_debug_label_context
{
    slayer3d_render_context *renderer;
    const slayer3d_camera3d *camera;
    slayer3d_font *font;
    bool ok;
} editor_debug_label_context;

static const char *editor_debug_label_font_id(const slayer3d_game_data_runtime *runtime)
{
    yyjson_val *overlay = obj_get(active_editor_tooling_root(runtime), "debug_overlay");
    const char *font = json_string(overlay, "label_font", NULL);
    if (font != NULL)
        return font;

    yyjson_val *fonts = obj_get(obj_get(runtime_root(runtime), "assets"), "fonts");
    yyjson_val *first = yyjson_is_arr(fonts) ? yyjson_arr_get(fonts, 0) : NULL;
    return json_string(first, "id", NULL);
}

static bool editor_debug_project_world_to_screen(slayer3d_render_context *renderer, const slayer3d_camera3d *camera,
                                                 slayer3d_vec3 point, float *out_x, float *out_y)
{
    if (renderer == NULL || camera == NULL || out_x == NULL || out_y == NULL)
        return false;

    SDL_Rect viewport;
    if (!slayer3d_get_render_viewport(renderer, &viewport) || viewport.w <= 0 || viewport.h <= 0)
        return false;

    slayer3d_mat4 view;
    slayer3d_mat4 projection;
    if (!slayer3d_camera3d_compute_matrices(camera, viewport.w, viewport.h, renderer->near_plane, renderer->far_plane,
                                            &view, &projection))
    {
        return false;
    }

    const slayer3d_mat4 view_projection = slayer3d_mat4_multiply(projection, view);
    const slayer3d_vec4 clip = slayer3d_mat4_transform_vec4(view_projection, slayer3d_vec4_from_vec3(point, 1.0f));
    if (SDL_fabsf(clip.w) <= 0.000001f)
        return false;

    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    const float ndc_z = clip.z / clip.w;
    if (ndc_z < -1.0f || ndc_z > 1.0f)
        return false;

    *out_x = (float)viewport.x + (ndc_x + 1.0f) * 0.5f * (float)viewport.w;
    *out_y = (float)viewport.y + (1.0f - ndc_y) * 0.5f * (float)viewport.h;
    return true;
}

static bool draw_editor_debug_label_primitive(void *userdata,
                                              const slayer3d_game_data_editor_debug_primitive *primitive)
{
    editor_debug_label_context *context = (editor_debug_label_context *)userdata;
    if (context == NULL || primitive == NULL ||
        (primitive->type != SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HOVER_LABEL &&
         primitive->type != SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_STATUS_LABEL &&
         primitive->type != SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_SNAP_TARGET))
    {
        return true;
    }
    if (context->renderer == NULL || context->font == NULL || context->camera == NULL || primitive->text[0] == '\0')
        return false;

    float x = 0.0f;
    float y = 0.0f;
    if (!editor_debug_project_world_to_screen(context->renderer, context->camera, primitive->start, &x, &y))
        return true;

    const float label_x = x + 8.0f;
    const float label_y = y - 26.0f;
    if (!slayer3d_draw_text_overlay_scaled(context->renderer, context->font, primitive->text, label_x + 1.0f,
                                           label_y + 1.0f, 0.82f, (slayer3d_color){0, 0, 0, 220}) ||
        !slayer3d_draw_text_overlay_scaled(context->renderer, context->font, primitive->text, label_x, label_y, 0.82f,
                                           primitive->color))
    {
        context->ok = false;
        return false;
    }
    return true;
}

static bool draw_active_editor_debug_labels(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_render_context *renderer,
                                            slayer3d_game_data_font_cache *font_cache,
                                            const slayer3d_game_data_asset_warmup_queue *asset_warmup,
                                            const slayer3d_camera3d *camera)
{
    if (runtime == NULL || renderer == NULL || font_cache == NULL || camera == NULL)
        return false;

    const char *font_id = editor_debug_label_font_id(runtime);
    slayer3d_game_data_asset_warmup_state warmup_state;
    if (slayer3d_game_data_asset_warmup_request_state(asset_warmup, SLAYER3D_GAME_DATA_ASSET_WARMUP_FONT, NULL, font_id,
                                                      &warmup_state) &&
        warmup_state != SLAYER3D_GAME_DATA_ASSET_WARMUP_READY)
    {
        return true;
    }

    slayer3d_font *font = slayer3d_game_data_find_or_load_font(runtime, font_cache, font_id);
    if (font == NULL)
        return true;

    editor_debug_label_context context;
    SDL_zero(context);
    context.renderer = renderer;
    context.camera = camera;
    context.font = font;
    context.ok = true;
    if (!slayer3d_game_data_for_each_active_editor_debug_primitive(runtime, draw_editor_debug_label_primitive,
                                                                   &context))
        return false;
    return context.ok;
}

static slayer3d_vec3 scene_viewport_grid_tangent(slayer3d_vec3 normal)
{
    const slayer3d_vec3 axes[3] = {
        slayer3d_vec3_make(1.0f, 0.0f, 0.0f),
        slayer3d_vec3_make(0.0f, 1.0f, 0.0f),
        slayer3d_vec3_make(0.0f, 0.0f, 1.0f),
    };
    slayer3d_vec3 best = axes[0];
    float best_length = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        const slayer3d_vec3 projected =
            slayer3d_vec3_sub(axes[i], slayer3d_vec3_scale(normal, slayer3d_vec3_dot(axes[i], normal)));
        const float length = slayer3d_vec3_length_squared(projected);
        if (length > best_length)
        {
            best = projected;
            best_length = length;
        }
    }
    return best_length > 0.000001f ? slayer3d_vec3_normalize(best) : axes[0];
}

static bool draw_scene_viewport_grid(const slayer3d_game_data_frame_desc *frame,
                                     const game_data_scene_world_viewport *viewport)
{
    if (frame == NULL || viewport == NULL || !yyjson_is_obj(viewport->grid) ||
        !json_bool(viewport->grid, "enabled", true) || !yyjson_is_obj(viewport->work_plane))
    {
        return true;
    }

    slayer3d_vec3 normal = json_vec3(viewport->work_plane, "normal", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    if (slayer3d_vec3_length_squared(normal) <= 0.000001f)
        return false;
    normal = slayer3d_vec3_normalize(normal);
    const float distance = json_float(viewport->work_plane, "distance", 0.0f);
    const slayer3d_vec3 tangent = scene_viewport_grid_tangent(normal);
    const slayer3d_vec3 bitangent = slayer3d_vec3_normalize(slayer3d_vec3_cross(normal, tangent));
    const slayer3d_vec3 origin = slayer3d_vec3_scale(normal, distance);
    const float extent = json_float(viewport->grid, "extent", 256.0f);
    float spacing = json_float(viewport->grid, "spacing", 1.0f);
    const char *spacing_key = json_string(viewport->grid, "spacing_key", NULL);
    if (spacing_key != NULL && frame->runtime->scene_state != NULL)
        spacing = slayer3d_properties_get_float(frame->runtime->scene_state, spacing_key, spacing);
    if (extent <= 0.0f || spacing <= 0.0f)
        return false;

    const int max_lines_per_axis = 512;
    while (extent * 2.0f / spacing > (float)max_lines_per_axis)
        spacing *= 2.0f;
    const int first = (int)SDL_ceilf(-extent / spacing);
    const int last = (int)SDL_floorf(extent / spacing);
    const slayer3d_color color = json_color(viewport->grid, "color", (slayer3d_color){70, 90, 105, 115});
    bool ok = true;
    for (int line = first; line <= last; ++line)
    {
        const float offset = (float)line * spacing;
        const slayer3d_vec3 tangent_offset = slayer3d_vec3_scale(tangent, offset);
        const slayer3d_vec3 bitangent_offset = slayer3d_vec3_scale(bitangent, offset);
        ok = slayer3d_draw_line_3d(
                 frame->renderer,
                 slayer3d_vec3_add(slayer3d_vec3_add(origin, tangent_offset), slayer3d_vec3_scale(bitangent, -extent)),
                 slayer3d_vec3_add(slayer3d_vec3_add(origin, tangent_offset), slayer3d_vec3_scale(bitangent, extent)),
                 color) &&
             ok;
        ok = slayer3d_draw_line_3d(
                 frame->renderer,
                 slayer3d_vec3_add(slayer3d_vec3_add(origin, bitangent_offset), slayer3d_vec3_scale(tangent, -extent)),
                 slayer3d_vec3_add(slayer3d_vec3_add(origin, bitangent_offset), slayer3d_vec3_scale(tangent, extent)),
                 color) &&
             ok;
    }
    return ok;
}

static bool draw_world_for_camera(const slayer3d_game_data_frame_desc *frame, const slayer3d_camera3d *camera,
                                  const game_data_scene_world_viewport *viewport, bool draw_viewmodel)
{
    if (frame == NULL || camera == NULL)
        return false;
    bool ok = true;
    if (slayer3d_begin_mode_3d(frame->renderer, *camera))
    {
        ok = run_frame_hook(frame, frame->before_world_3d) && ok;
        if (viewport == NULL || viewport->draw_skybox)
        {
            ok = draw_active_scene_skybox(frame->runtime, frame->renderer, frame->image_cache, frame->asset_warmup,
                                          frame->font_cache != NULL ? frame->font_cache->media_dir : NULL) &&
                 ok;
        }
        ok = draw_scene_viewport_grid(frame, viewport) && ok;
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
        ok = draw_render_primitives_evaluated_with_cache(
                 frame->runtime, frame->renderer, frame->render_eval, frame->image_cache, frame->sprite_cache,
                 frame->model_cache, frame->mesh_primitive_cache, frame->asset_warmup, camera, true, false) &&
             ok;
        ok = slayer3d_game_data_draw_active_editor_debug_primitives(frame->runtime, frame->renderer) && ok;
        ok = run_frame_hook(frame, frame->after_world_3d) && ok;
        slayer3d_end_mode_3d(frame->renderer);
        if (frame->font_cache != NULL)
            ok = draw_active_editor_debug_labels(frame->runtime, frame->renderer, frame->font_cache,
                                                 frame->asset_warmup, camera) &&
                 ok;
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
            ok = draw_render_primitives_evaluated_with_cache(frame->runtime, frame->renderer, frame->render_eval,
                                                             frame->image_cache, frame->sprite_cache,
                                                             frame->model_cache, frame->mesh_primitive_cache,
                                                             frame->asset_warmup, &viewmodel_camera, false, true) &&
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
    game_data_scene_world_viewport viewports[SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX];
    int viewport_count = 0;
    if (frame == NULL || !game_data_resolve_active_scene_world_viewports(frame->runtime, viewports,
                                                                         SDL_arraysize(viewports), &viewport_count))
    {
        return false;
    }
    if (viewport_count == 0)
        return true;

    bool ok = true;
    bool drawn = false;
    for (int i = 0; i < viewport_count; ++i)
    {
        const game_data_scene_world_viewport *viewport = &viewports[i];

        slayer3d_camera3d camera;
        if (!slayer3d_game_data_get_camera(frame->runtime, viewport->camera, &camera))
        {
            ok = false;
            continue;
        }
        if (!slayer3d_set_render_viewport(frame->renderer, &viewport->rect) ||
            !slayer3d_set_scissor_rect(frame->renderer, &viewport->rect))
        {
            ok = false;
            continue;
        }
        ok = draw_world_for_camera(frame, &camera, viewport, viewport->draw_viewmodel) && ok;
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

    const bool profile_frames = SDL_getenv("SLAYER3D_PROFILE_FRAMES") != NULL;
    const Uint64 frame_start = profile_frames ? SDL_GetPerformanceCounter() : 0;
    Uint64 assets_end = 0;
    Uint64 settings_end = 0;
    Uint64 world_end = 0;
    bool ok = true;
    queue_active_scene_assets(frame);
    if (frame->asset_warmup != NULL)
    {
        slayer3d_asset_resolver *assets = NULL;
        if (frame->image_cache != NULL)
            assets = frame->image_cache->assets;
        else if (frame->sprite_cache != NULL)
            assets = frame->sprite_cache->assets;
        else if (frame->model_cache != NULL)
            assets = frame->model_cache->assets;
        (void)slayer3d_game_data_asset_warmup_queue_service(frame->asset_warmup, frame->runtime, frame->renderer,
                                                            frame->font_cache, frame->image_cache, frame->sprite_cache,
                                                            frame->model_cache, frame->mesh_primitive_cache, assets, 0);
    }
    if (profile_frames)
        assets_end = SDL_GetPerformanceCounter();
    ok = apply_render_settings(frame->runtime, frame->renderer) && ok;
    ok = apply_world_lights(frame->runtime, frame->renderer, frame->render_eval) && ok;
    slayer3d_game_data_model_cache_begin_pose_frame(frame->model_cache);
    if (profile_frames)
        settings_end = SDL_GetPerformanceCounter();

    if (slayer3d_game_data_active_scene_renders_world(frame->runtime))
    {
        bool drew_viewports = false;
        ok = draw_active_scene_world_viewports(frame, &drew_viewports) && ok;
        if (!drew_viewports)
        {
            const slayer3d_camera3d camera = active_camera_or_fallback(frame->runtime, frame->fallback_camera);
            ok = draw_world_for_camera(frame, &camera, NULL, true) && ok;
        }
    }
    if (profile_frames)
        world_end = SDL_GetPerformanceCounter();

    ok = run_frame_hook(frame, frame->before_ui) && ok;
    ok = slayer3d_game_data_draw_ui_layered(frame->runtime, frame->renderer, frame->font_cache, frame->image_cache,
                                            frame->asset_warmup, frame->metrics, frame->render_eval,
                                            frame->pulse_phase) &&
         ok;
    if (frame->app_flow != NULL)
        slayer3d_game_data_app_flow_draw(frame->app_flow, frame->renderer);
    ok = run_frame_hook(frame, frame->after_ui) && ok;
    if (profile_frames)
        record_game_presentation_profile(frame_start, assets_end, settings_end, world_end, SDL_GetPerformanceCounter());
    return ok;
}
