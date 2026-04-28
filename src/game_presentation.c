/**
 * @file game_presentation.c
 * @brief Renderer-facing helpers for JSON-authored game data.
 */

#include "sdl3d/game_presentation.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/drawing3d.h"
#include "sdl3d/image.h"
#include "sdl3d/lighting.h"
#include "sdl3d/shapes.h"

typedef struct primitive_draw_context
{
    sdl3d_render_context *renderer;
} primitive_draw_context;

typedef struct ui_draw_context
{
    const sdl3d_game_data_runtime *runtime;
    sdl3d_render_context *renderer;
    sdl3d_game_data_font_cache *font_cache;
    const sdl3d_game_data_ui_metrics *metrics;
    float pulse_phase;
    bool ok;
} ui_draw_context;

typedef struct ui_image_draw_context
{
    const sdl3d_game_data_runtime *runtime;
    sdl3d_render_context *renderer;
    sdl3d_game_data_image_cache *image_cache;
    const sdl3d_game_data_ui_metrics *metrics;
    bool ok;
} ui_image_draw_context;

typedef struct particle_update_context
{
    sdl3d_game_data_particle_cache *cache;
    float dt;
    bool ok;
} particle_update_context;

static sdl3d_camera3d default_camera(void)
{
    sdl3d_camera3d camera;
    SDL_zero(camera);
    camera.position = sdl3d_vec3_make(0.0f, 0.0f, 16.0f);
    camera.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    camera.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = 11.4f;
    camera.projection = SDL3D_CAMERA_ORTHOGRAPHIC;
    return camera;
}

static sdl3d_camera3d active_camera_or_fallback(const sdl3d_game_data_runtime *runtime, const sdl3d_camera3d *fallback)
{
    sdl3d_camera3d camera;
    const char *active_camera = sdl3d_game_data_active_camera(runtime);
    if (active_camera != NULL && sdl3d_game_data_get_camera(runtime, active_camera, &camera))
        return camera;
    if (fallback != NULL)
        return *fallback;
    return default_camera();
}

static bool apply_render_settings(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    sdl3d_game_data_render_settings settings;
    if (!sdl3d_game_data_get_render_settings(runtime, &settings))
        return false;

    bool ok = true;
    ok = sdl3d_clear_render_context(renderer, settings.clear_color) && ok;
    ok = sdl3d_set_lighting_enabled(renderer, settings.lighting_enabled) && ok;
    ok = sdl3d_set_bloom_enabled(renderer, settings.bloom_enabled) && ok;
    ok = sdl3d_set_ssao_enabled(renderer, settings.ssao_enabled) && ok;
    ok = sdl3d_set_tonemap_mode(renderer, settings.tonemap) && ok;
    return ok;
}

static bool apply_world_lights(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                               const sdl3d_game_data_render_eval *eval)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    bool ok = sdl3d_clear_lights(renderer);
    float ambient[3] = {0.015f, 0.018f, 0.026f};
    sdl3d_game_data_get_world_ambient_light(runtime, ambient);
    ok = sdl3d_set_ambient_light(renderer, ambient[0], ambient[1], ambient[2]) && ok;

    const int light_count = sdl3d_game_data_world_light_count(runtime);
    for (int i = 0; i < light_count; ++i)
    {
        sdl3d_light light;
        if (sdl3d_game_data_get_world_light_evaluated(runtime, i, eval, &light))
            ok = sdl3d_add_light(renderer, &light) && ok;
    }
    return ok;
}

static bool run_frame_hook(const sdl3d_game_data_frame_desc *frame, sdl3d_game_data_frame_hook hook)
{
    return hook == NULL || hook(frame->userdata, frame);
}

static bool ensure_font_cache_capacity(sdl3d_game_data_font_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    sdl3d_font *new_fonts = (sdl3d_font *)SDL_calloc((size_t)next_capacity, sizeof(*new_fonts));
    if (new_fonts == NULL)
        return false;

    const char **new_font_ids = (const char **)SDL_calloc((size_t)next_capacity, sizeof(*new_font_ids));
    if (new_font_ids == NULL)
    {
        SDL_free(new_fonts);
        return false;
    }

    for (int i = 0; i < cache->count; ++i)
    {
        new_fonts[i] = cache->fonts[i];
        new_font_ids[i] = cache->font_ids[i];
    }

    SDL_free(cache->fonts);
    SDL_free(cache->font_ids);
    cache->fonts = new_fonts;
    cache->font_ids = new_font_ids;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_particle_cache_capacity(sdl3d_game_data_particle_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    sdl3d_game_data_particle_cache_entry *entries =
        (sdl3d_game_data_particle_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_image_cache_capacity(sdl3d_game_data_image_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    sdl3d_game_data_image_cache_entry *entries =
        (sdl3d_game_data_image_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static sdl3d_game_data_particle_cache_entry *find_particle_entry(sdl3d_game_data_particle_cache *cache,
                                                                 const char *entity_name)
{
    if (cache == NULL || entity_name == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].entity_name != NULL && SDL_strcmp(cache->entries[i].entity_name, entity_name) == 0)
            return &cache->entries[i];
    }
    return NULL;
}

static sdl3d_game_data_particle_cache_entry *find_or_create_particle_entry(
    sdl3d_game_data_particle_cache *cache, const sdl3d_game_data_particle_emitter *emitter)
{
    if (cache == NULL || emitter == NULL || emitter->entity_name == NULL)
        return NULL;

    sdl3d_game_data_particle_cache_entry *entry = find_particle_entry(cache, emitter->entity_name);
    if (entry != NULL)
        return entry;

    if (!ensure_particle_cache_capacity(cache, cache->count + 1))
        return NULL;

    entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->entity_name = emitter->entity_name;
    entry->emitter = sdl3d_create_particle_emitter(&emitter->config);
    if (entry->emitter == NULL)
        return NULL;

    ++cache->count;
    return entry;
}

static sdl3d_font *find_or_load_font(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_font_cache *cache,
                                     const char *font_id)
{
    if (runtime == NULL || cache == NULL || font_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->font_ids[i] != NULL && SDL_strcmp(cache->font_ids[i], font_id) == 0)
            return &cache->fonts[i];
    }

    if (!ensure_font_cache_capacity(cache, cache->count + 1))
        return NULL;

    sdl3d_game_data_font_asset font;
    if (!sdl3d_game_data_get_font_asset(runtime, font_id, &font))
        return NULL;

    sdl3d_font *slot = &cache->fonts[cache->count];
    bool loaded = false;
    if (font.builtin)
        loaded = sdl3d_load_builtin_font(cache->media_dir, font.builtin_id, font.size, slot);
    else if (font.path != NULL)
        loaded = sdl3d_load_font(font.path, font.size, slot);
    if (!loaded)
        return NULL;

    cache->font_ids[cache->count] = font_id;
    cache->count++;
    return slot;
}

static sdl3d_texture2d *find_or_load_image(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_image_cache *cache,
                                           const char *image_id)
{
    if (runtime == NULL || cache == NULL || cache->assets == NULL || image_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].image_id != NULL && SDL_strcmp(cache->entries[i].image_id, image_id) == 0)
            return cache->entries[i].loaded ? &cache->entries[i].texture : NULL;
    }

    if (!ensure_image_cache_capacity(cache, cache->count + 1))
        return NULL;

    sdl3d_game_data_image_asset asset;
    if (!sdl3d_game_data_get_image_asset(runtime, image_id, &asset) || asset.path == NULL)
        return NULL;

    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);
    char error[256];
    if (!sdl3d_asset_resolver_read_file(cache->assets, asset.path, &buffer, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read UI image asset %s: %s", asset.path, error);
        return NULL;
    }

    sdl3d_image image;
    SDL_zero(image);
    const bool decoded = sdl3d_load_image_from_memory(buffer.data, buffer.size, &image);
    sdl3d_asset_buffer_free(&buffer);
    if (!decoded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to decode UI image asset %s", asset.path);
        return NULL;
    }

    sdl3d_game_data_image_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->image_id = asset.id;
    entry->loaded = sdl3d_create_texture_from_image(&image, &entry->texture);
    sdl3d_free_image(&image);
    if (!entry->loaded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture for UI image asset %s", asset.path);
        return NULL;
    }

    ++cache->count;
    return &entry->texture;
}

static bool draw_primitive(void *userdata, const sdl3d_game_data_render_primitive *primitive)
{
    primitive_draw_context *context = (primitive_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || primitive == NULL)
        return false;

    sdl3d_set_emissive(context->renderer, primitive->emissive_color.x, primitive->emissive_color.y,
                       primitive->emissive_color.z);
    if (primitive->type == SDL3D_GAME_DATA_RENDER_CUBE)
    {
        sdl3d_draw_cube(context->renderer, primitive->position, primitive->size, primitive->color);
    }
    else if (primitive->type == SDL3D_GAME_DATA_RENDER_SPHERE)
    {
        sdl3d_draw_sphere(context->renderer, primitive->position, primitive->radius, primitive->slices,
                          primitive->rings, primitive->color);
    }
    sdl3d_set_emissive(context->renderer, 0.0f, 0.0f, 0.0f);
    return true;
}

static bool draw_ui_text(void *userdata, const sdl3d_game_data_ui_text *text)
{
    ui_draw_context *draw = (ui_draw_context *)userdata;
    if (draw == NULL || text == NULL)
        return false;

    sdl3d_game_data_ui_text resolved;
    bool visible = false;
    if (!sdl3d_game_data_resolve_ui_text(draw->runtime, text, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    char content[128];
    if (!sdl3d_game_data_format_ui_text(draw->runtime, &resolved, draw->metrics, content, sizeof(content)))
        return true;

    sdl3d_font *font = find_or_load_font(draw->runtime, draw->font_cache, resolved.font);
    if (font == NULL)
    {
        draw->ok = false;
        return true;
    }

    sdl3d_color color = resolved.color;
    if (resolved.pulse_alpha)
    {
        const float pulse = 0.5f + 0.5f * SDL_sinf(draw->pulse_phase * SDL_PI_F * 2.0f);
        const float alpha = (120.0f + pulse * 135.0f) / 255.0f;
        color.a = (Uint8)SDL_clamp((int)((float)color.a * alpha + 0.5f), 0, 255);
    }

    const int width = sdl3d_get_render_context_width(draw->renderer);
    const int height = sdl3d_get_render_context_height(draw->renderer);
    const float scale = resolved.scale > 0.0f ? resolved.scale : 1.0f;
    float x = resolved.normalized ? resolved.x * (float)width : resolved.x;
    const float y = resolved.normalized ? resolved.y * (float)height : resolved.y;
    if (resolved.align == SDL3D_GAME_DATA_UI_ALIGN_CENTER || resolved.centered)
    {
        float text_w = 0.0f;
        float text_h = 0.0f;
        sdl3d_measure_text(font, content, &text_w, &text_h);
        x -= text_w * scale * 0.5f;
    }
    else if (resolved.align == SDL3D_GAME_DATA_UI_ALIGN_RIGHT)
    {
        float text_w = 0.0f;
        float text_h = 0.0f;
        sdl3d_measure_text(font, content, &text_w, &text_h);
        x -= text_w * scale;
    }

    if (!sdl3d_draw_text_overlay_scaled(draw->renderer, font, content, x, y, scale, color))
        draw->ok = false;
    return true;
}

static void resolve_ui_image_rect(const sdl3d_game_data_ui_image *image, const sdl3d_texture2d *texture, int width,
                                  int height, float *out_x, float *out_y, float *out_w, float *out_h)
{
    float w = image->normalized ? image->w * (float)width : image->w;
    float h = image->normalized ? image->h * (float)height : image->h;
    const float texture_w = (float)texture->width;
    const float texture_h = (float)texture->height;
    const float scale = image->scale > 0.0f ? image->scale : 1.0f;

    if (w <= 0.0f && h <= 0.0f)
    {
        w = texture_w;
        h = texture_h;
    }
    else if (w <= 0.0f)
    {
        w = h * texture_w / texture_h;
    }
    else if (h <= 0.0f)
    {
        h = w * texture_h / texture_w;
    }
    else if (image->preserve_aspect)
    {
        const float fit = SDL_min(w / texture_w, h / texture_h);
        w = texture_w * fit;
        h = texture_h * fit;
    }
    w *= scale;
    h *= scale;

    float x = image->normalized ? image->x * (float)width : image->x;
    float y = image->normalized ? image->y * (float)height : image->y;
    if (image->align == SDL3D_GAME_DATA_UI_ALIGN_CENTER)
        x -= w * 0.5f;
    else if (image->align == SDL3D_GAME_DATA_UI_ALIGN_RIGHT)
        x -= w;
    if (image->valign == SDL3D_GAME_DATA_UI_VALIGN_CENTER)
        y -= h * 0.5f;
    else if (image->valign == SDL3D_GAME_DATA_UI_VALIGN_BOTTOM)
        y -= h;

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

static bool draw_ui_image(void *userdata, const sdl3d_game_data_ui_image *image)
{
    ui_image_draw_context *draw = (ui_image_draw_context *)userdata;
    if (draw == NULL || image == NULL)
        return false;

    sdl3d_game_data_ui_image resolved;
    bool visible = false;
    if (!sdl3d_game_data_resolve_ui_image(draw->runtime, image, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    sdl3d_texture2d *texture = find_or_load_image(draw->runtime, draw->image_cache, resolved.image);
    if (texture == NULL)
    {
        draw->ok = false;
        return true;
    }

    const int width = sdl3d_get_render_context_width(draw->renderer);
    const int height = sdl3d_get_render_context_height(draw->renderer);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    resolve_ui_image_rect(&resolved, texture, width, height, &x, &y, &w, &h);
    if (!sdl3d_draw_texture_overlay(draw->renderer, texture, x, y, w, h, resolved.color))
        draw->ok = false;
    return true;
}

static bool update_particle(void *userdata, const sdl3d_game_data_particle_emitter *emitter)
{
    particle_update_context *context = (particle_update_context *)userdata;
    if (context == NULL || context->cache == NULL || emitter == NULL)
        return false;

    sdl3d_game_data_particle_cache_entry *entry = find_or_create_particle_entry(context->cache, emitter);
    if (entry == NULL)
    {
        context->ok = false;
        return true;
    }

    if (!sdl3d_particle_emitter_set_config(entry->emitter, &emitter->config))
    {
        context->ok = false;
        return true;
    }
    entry->draw_emissive = emitter->draw_emissive;
    entry->visible = true;
    sdl3d_particle_emitter_update(entry->emitter, context->dt);
    return true;
}

void sdl3d_game_data_font_cache_init(sdl3d_game_data_font_cache *cache, const char *media_dir)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->media_dir = media_dir;
}

void sdl3d_game_data_font_cache_free(sdl3d_game_data_font_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
        sdl3d_free_font(&cache->fonts[i]);
    SDL_free(cache->fonts);
    SDL_free(cache->font_ids);
    SDL_zero(*cache);
}

void sdl3d_game_data_image_cache_init(sdl3d_game_data_image_cache *cache, sdl3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void sdl3d_game_data_image_cache_free(sdl3d_game_data_image_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            sdl3d_free_texture(&cache->entries[i].texture);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

bool sdl3d_game_data_draw_render_primitives(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer)
{
    return sdl3d_game_data_draw_render_primitives_evaluated(runtime, renderer, NULL);
}

bool sdl3d_game_data_draw_render_primitives_evaluated(const sdl3d_game_data_runtime *runtime,
                                                      sdl3d_render_context *renderer,
                                                      const sdl3d_game_data_render_eval *eval)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    primitive_draw_context context = {renderer};
    return sdl3d_game_data_for_each_render_primitive_evaluated(runtime, eval, draw_primitive, &context);
}

bool sdl3d_game_data_draw_ui_text(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                  sdl3d_game_data_font_cache *font_cache, const sdl3d_game_data_ui_metrics *metrics,
                                  float pulse_phase)
{
    if (runtime == NULL || renderer == NULL || font_cache == NULL)
        return false;

    ui_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.font_cache = font_cache;
    context.metrics = metrics;
    context.pulse_phase = pulse_phase;
    context.ok = true;

    return sdl3d_game_data_for_each_ui_text(runtime, draw_ui_text, &context) && context.ok;
}

bool sdl3d_game_data_draw_ui_images(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                    sdl3d_game_data_image_cache *image_cache, const sdl3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL || renderer == NULL || image_cache == NULL)
        return false;

    ui_image_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.image_cache = image_cache;
    context.metrics = metrics;
    context.ok = true;

    return sdl3d_game_data_for_each_ui_image(runtime, draw_ui_image, &context) && context.ok;
}

void sdl3d_game_data_particle_cache_init(sdl3d_game_data_particle_cache *cache)
{
    if (cache != NULL)
        SDL_zero(*cache);
}

void sdl3d_game_data_particle_cache_free(sdl3d_game_data_particle_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        sdl3d_destroy_particle_emitter(cache->entries[i].emitter);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

bool sdl3d_game_data_update_particles(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_particle_cache *cache,
                                      float dt)
{
    if (runtime == NULL || cache == NULL)
        return false;

    for (int i = 0; i < cache->count; ++i)
    {
        cache->entries[i].visible = false;
    }

    particle_update_context context;
    SDL_zero(context);
    context.cache = cache;
    context.dt = dt;
    context.ok = true;

    return sdl3d_game_data_for_each_particle_emitter(runtime, update_particle, &context) && context.ok;
}

bool sdl3d_game_data_draw_particles(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                    sdl3d_game_data_particle_cache *cache)
{
    if (runtime == NULL || renderer == NULL || cache == NULL)
        return false;

    for (int i = 0; i < cache->count; ++i)
    {
        sdl3d_game_data_particle_cache_entry *entry = &cache->entries[i];
        if (!entry->visible || entry->emitter == NULL ||
            !sdl3d_game_data_active_scene_has_entity(runtime, entry->entity_name))
            continue;

        sdl3d_set_emissive(renderer, entry->draw_emissive.x, entry->draw_emissive.y, entry->draw_emissive.z);
        sdl3d_draw_particles(renderer, entry->emitter);
        sdl3d_set_emissive(renderer, 0.0f, 0.0f, 0.0f);
    }
    return true;
}

bool sdl3d_game_data_update_menus(sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input, bool *input_armed,
                                  sdl3d_game_data_menu_update_result *out_result)
{
    if (out_result != NULL)
    {
        SDL_zero(*out_result);
        out_result->selected_index = -1;
        out_result->signal_id = -1;
    }
    if (runtime == NULL || input_armed == NULL)
        return false;

    sdl3d_game_data_menu menu;
    if (!sdl3d_game_data_get_active_menu(runtime, &menu))
        return true;

    if (out_result != NULL)
    {
        out_result->menu = menu.name;
        out_result->selected_index = menu.selected_index;
    }

    if (!*input_armed)
    {
        if (sdl3d_game_data_active_menu_input_is_idle(runtime, input))
            *input_armed = true;
        return true;
    }

    bool handled = false;
    if (menu.up_action_id >= 0 && sdl3d_game_data_active_scene_allows_action(runtime, menu.up_action_id) &&
        sdl3d_input_is_pressed(input, menu.up_action_id))
    {
        handled = sdl3d_game_data_menu_move(runtime, menu.name, -1) || handled;
    }
    if (menu.down_action_id >= 0 && sdl3d_game_data_active_scene_allows_action(runtime, menu.down_action_id) &&
        sdl3d_input_is_pressed(input, menu.down_action_id))
    {
        handled = sdl3d_game_data_menu_move(runtime, menu.name, 1) || handled;
    }
    if (menu.select_action_id >= 0 && sdl3d_game_data_active_scene_allows_action(runtime, menu.select_action_id) &&
        sdl3d_input_is_pressed(input, menu.select_action_id))
    {
        handled = true;

        sdl3d_game_data_menu refreshed;
        if (!sdl3d_game_data_get_active_menu(runtime, &refreshed))
            return true;

        sdl3d_game_data_menu_item item;
        if (!sdl3d_game_data_get_menu_item(runtime, refreshed.name, refreshed.selected_index, &item))
            return true;

        if (out_result != NULL)
        {
            out_result->menu = refreshed.name;
            out_result->selected_index = refreshed.selected_index;
            out_result->selected = true;
            out_result->control_changed = sdl3d_game_data_apply_menu_item_control(runtime, &item);
            out_result->quit = !out_result->control_changed && item.quit;
            out_result->scene = !out_result->control_changed ? item.scene : NULL;
            out_result->signal_id = !out_result->control_changed ? item.signal_id : -1;
        }
        else
        {
            (void)sdl3d_game_data_apply_menu_item_control(runtime, &item);
        }
    }
    else if (handled && out_result != NULL)
    {
        sdl3d_game_data_menu refreshed;
        if (sdl3d_game_data_get_active_menu(runtime, &refreshed))
            out_result->selected_index = refreshed.selected_index;
    }

    if (out_result != NULL)
        out_result->handled_input = handled;
    return true;
}

void sdl3d_game_data_frame_state_init(sdl3d_game_data_frame_state *state)
{
    if (state == NULL)
        return;
    SDL_zero(*state);
}

bool sdl3d_game_data_update_frame(sdl3d_game_data_frame_state *state, const sdl3d_game_data_update_frame_desc *desc)
{
    if (state == NULL || desc == NULL || desc->ctx == NULL || desc->runtime == NULL)
        return false;

    sdl3d_game_context *ctx = desc->ctx;
    sdl3d_game_data_runtime *runtime = desc->runtime;
    const bool paused_at_start = ctx->paused;
    if (sdl3d_game_data_active_scene_update_phase(runtime, "app_flow", ctx->paused) && desc->app_flow != NULL)
    {
        if (!sdl3d_game_data_app_flow_update(desc->app_flow, ctx, runtime, desc->dt))
            return false;
    }

    if (paused_at_start && !ctx->paused)
    {
        state->was_paused = false;
        return true;
    }

    const bool pause_entered = !state->was_paused && ctx->paused;
    if (sdl3d_game_data_active_scene_update_phase(runtime, "presentation", ctx->paused))
    {
        state->time += desc->dt;
        if (!sdl3d_game_data_update_presentation_clocks(runtime, desc->dt, ctx->paused, pause_entered))
            return false;
        state->ui_pulse_phase = sdl3d_game_data_ui_pulse_phase(runtime, state->ui_pulse_phase);
    }
    if (sdl3d_game_data_active_scene_update_phase(runtime, "property_effects", ctx->paused) &&
        !sdl3d_game_data_update_property_effects(runtime, desc->dt))
        return false;
    if (desc->particle_cache != NULL && sdl3d_game_data_active_scene_update_phase(runtime, "particles", ctx->paused) &&
        !sdl3d_game_data_update_particles(runtime, desc->particle_cache, desc->dt))
        return false;
    if (!paused_at_start && sdl3d_game_data_active_scene_update_phase(runtime, "simulation", ctx->paused) &&
        (desc->app_flow == NULL || !sdl3d_game_data_app_flow_quit_pending(desc->app_flow)))
    {
        if (!sdl3d_game_data_update(runtime, desc->dt))
            return false;
    }

    state->was_paused = ctx->paused;
    return true;
}

void sdl3d_game_data_frame_state_record_render(sdl3d_game_data_frame_state *state, const sdl3d_game_context *ctx,
                                               const sdl3d_game_data_runtime *runtime)
{
    if (state == NULL || ctx == NULL)
        return;

    if (state->rendered_frames > 0)
    {
        const float frame_dt = ctx->real_time - state->last_render_time;
        if (frame_dt > 0.0f)
        {
            state->fps_sample_time += frame_dt;
            ++state->fps_sample_frames;
            const float sample_seconds = sdl3d_game_data_fps_sample_seconds(runtime, 0.25f);
            if (state->fps_sample_time >= sample_seconds)
            {
                state->displayed_fps = (float)state->fps_sample_frames / state->fps_sample_time;
                state->fps_sample_time = 0.0f;
                state->fps_sample_frames = 0;
            }
        }
    }
    state->last_render_time = ctx->real_time;
    ++state->rendered_frames;
    state->metrics.paused = ctx->paused;
    state->metrics.fps = state->displayed_fps;
    state->metrics.frame = state->rendered_frames;
    state->render_eval.time = state->time;
    state->ui_pulse_phase = sdl3d_game_data_ui_pulse_phase(runtime, state->ui_pulse_phase);
}

void sdl3d_game_data_scene_flow_init(sdl3d_game_data_scene_flow *flow)
{
    if (flow == NULL)
        return;
    SDL_zero(*flow);
    sdl3d_transition_reset(&flow->transition);
}

bool sdl3d_game_data_scene_flow_is_transitioning(const sdl3d_game_data_scene_flow *flow)
{
    return flow != NULL && (flow->fading_out || flow->fading_in || flow->transition.active);
}

bool sdl3d_game_data_scene_flow_request(sdl3d_game_data_scene_flow *flow, sdl3d_game_data_runtime *runtime,
                                        const char *scene_name)
{
    if (flow == NULL || runtime == NULL || scene_name == NULL)
        return false;

    sdl3d_game_data_scene_transition_policy policy;
    (void)sdl3d_game_data_get_scene_transition_policy(runtime, &policy);
    if (sdl3d_game_data_scene_flow_is_transitioning(flow) && !policy.allow_interrupt)
        return false;

    const char *active = sdl3d_game_data_active_scene(runtime);
    if (active != NULL && SDL_strcmp(active, scene_name) == 0 && !policy.allow_same_scene)
        return false;

    bool known_scene = false;
    const int scene_count = sdl3d_game_data_scene_count(runtime);
    for (int i = 0; i < scene_count; ++i)
    {
        const char *name = sdl3d_game_data_scene_name_at(runtime, i);
        if (name != NULL && SDL_strcmp(name, scene_name) == 0)
        {
            known_scene = true;
            break;
        }
    }
    if (!known_scene)
        return false;

    if (sdl3d_game_data_scene_flow_is_transitioning(flow))
        sdl3d_transition_reset(&flow->transition);
    flow->pending_scene = scene_name;
    flow->fading_out = true;
    flow->fading_in = false;

    sdl3d_game_data_transition_desc transition;
    if (active != NULL && sdl3d_game_data_get_scene_transition(runtime, active, "exit", &transition))
    {
        sdl3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                               transition.duration, transition.done_signal_id);
    }
    else
    {
        sdl3d_transition_reset(&flow->transition);
    }
    return true;
}

void sdl3d_game_data_scene_flow_update(sdl3d_game_data_scene_flow *flow, sdl3d_game_data_runtime *runtime,
                                       sdl3d_signal_bus *bus, float dt)
{
    if (flow == NULL || runtime == NULL)
        return;

    sdl3d_transition_update(&flow->transition, bus, dt);
    if (flow->fading_out && !flow->transition.active)
    {
        const char *next_scene = flow->pending_scene;
        flow->pending_scene = NULL;
        flow->fading_out = false;
        if (next_scene != NULL && sdl3d_game_data_set_active_scene(runtime, next_scene))
        {
            sdl3d_game_data_transition_desc transition;
            if (sdl3d_game_data_get_scene_transition(runtime, next_scene, "enter", &transition))
            {
                flow->fading_in = true;
                sdl3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                                       transition.duration, transition.done_signal_id);
            }
            else
            {
                sdl3d_transition_reset(&flow->transition);
            }
        }
        else
        {
            sdl3d_transition_reset(&flow->transition);
        }
    }
    else if (flow->fading_in && !flow->transition.active)
    {
        flow->fading_in = false;
    }
}

void sdl3d_game_data_scene_flow_draw(const sdl3d_game_data_scene_flow *flow, sdl3d_render_context *renderer)
{
    if (flow != NULL)
        sdl3d_transition_draw(&flow->transition, renderer);
}

static void app_flow_request_quit(sdl3d_game_data_app_flow *flow, sdl3d_game_context *ctx,
                                  sdl3d_game_data_runtime *runtime)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || flow->quit_pending)
        return;

    flow->quit_pending = true;
    sdl3d_game_data_transition_desc transition;
    if (flow->app.quit_transition == NULL ||
        !sdl3d_game_data_get_transition(runtime, flow->app.quit_transition, &transition))
    {
        ctx->quit_requested = true;
        return;
    }

    sdl3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                           transition.duration, transition.done_signal_id);
}

static bool app_flow_request_scene(sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime,
                                   const char *scene_name)
{
    if (sdl3d_game_data_scene_flow_request(&flow->scene_flow, runtime, scene_name))
    {
        sdl3d_game_data_scene_transition_policy policy;
        (void)sdl3d_game_data_get_scene_transition_policy(runtime, &policy);
        if (policy.reset_menu_input_on_request)
            flow->scene_input_armed = false;
        return true;
    }
    return false;
}

static void app_flow_consume_menu(sdl3d_game_data_app_flow *flow, sdl3d_game_context *ctx,
                                  sdl3d_game_data_runtime *runtime, sdl3d_input_manager *input, sdl3d_signal_bus *bus)
{
    if (flow->quit_pending || sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        return;

    sdl3d_game_data_menu_update_result result;
    if (!sdl3d_game_data_update_menus(runtime, input, &flow->scene_input_armed, &result) || !result.selected)
        return;

    if (result.signal_id >= 0)
        sdl3d_signal_emit(bus, result.signal_id, NULL);
    if (result.quit)
        app_flow_request_quit(flow, ctx, runtime);
    else if (result.scene != NULL)
        (void)app_flow_request_scene(flow, runtime, result.scene);
}

static void app_flow_consume_scene_shortcuts(sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime,
                                             sdl3d_input_manager *input)
{
    if (flow->quit_pending || sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        return;

    const int count = sdl3d_game_data_scene_shortcut_count(runtime);
    for (int i = 0; i < count; ++i)
    {
        sdl3d_game_data_scene_shortcut shortcut;
        if (sdl3d_game_data_scene_shortcut_at(runtime, i, &shortcut) && shortcut.action_id >= 0 &&
            sdl3d_game_data_active_scene_allows_action(runtime, shortcut.action_id) &&
            sdl3d_input_is_pressed(input, shortcut.action_id))
        {
            (void)app_flow_request_scene(flow, runtime, shortcut.scene);
            return;
        }
    }
}

static bool app_flow_set_scene_without_transition(sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime,
                                                  const char *scene_name)
{
    if (flow == NULL || runtime == NULL || scene_name == NULL)
        return false;

    if (!sdl3d_game_data_set_active_scene(runtime, scene_name))
        return false;

    sdl3d_game_data_scene_flow_init(&flow->scene_flow);
    flow->scene_input_armed = false;
    sdl3d_game_data_timeline_state_init(&flow->timeline);
    return true;
}

static bool app_flow_apply_skip_policy(sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime,
                                       const sdl3d_input_manager *input, bool capture_input, bool *out_applied)
{
    if (out_applied != NULL)
        *out_applied = false;
    if (flow == NULL || runtime == NULL)
        return false;

    sdl3d_game_data_skip_policy policy;
    if (!sdl3d_game_data_get_active_skip_policy(runtime, &policy))
    {
        flow->skip_scene = NULL;
        flow->skip_requested = false;
        return false;
    }

    const char *active_scene = sdl3d_game_data_active_scene(runtime);
    if (flow->skip_scene != active_scene)
    {
        flow->skip_scene = active_scene;
        flow->skip_requested = false;
    }

    bool consumed = false;
    if (capture_input && input != NULL)
    {
        bool matched = false;
        if (policy.input == SDL3D_GAME_DATA_SKIP_INPUT_ANY)
        {
            matched = sdl3d_input_any_pressed(input);
        }
        else if (policy.input == SDL3D_GAME_DATA_SKIP_INPUT_ACTION)
        {
            matched = policy.action_id >= 0 && sdl3d_game_data_active_scene_allows_action(runtime, policy.action_id) &&
                      sdl3d_input_is_pressed(input, policy.action_id);
        }

        if (matched)
        {
            flow->skip_requested = true;
            consumed = policy.consume_input;
        }
    }

    if (flow->skip_requested && !flow->quit_pending && !flow->transition.active &&
        !sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
    {
        const bool requested = policy.preserve_exit_transition
                                   ? app_flow_request_scene(flow, runtime, policy.scene)
                                   : app_flow_set_scene_without_transition(flow, runtime, policy.scene);
        if (requested)
        {
            flow->skip_requested = false;
            if (out_applied != NULL)
                *out_applied = true;
        }
    }

    return consumed;
}

static bool app_flow_update_timeline(sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime, float dt)
{
    if (flow == NULL || runtime == NULL || flow->quit_pending || flow->transition.active ||
        sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
    {
        return true;
    }

    sdl3d_game_data_timeline_update_result result;
    if (!sdl3d_game_data_update_timeline(runtime, &flow->timeline, dt, &result))
        return false;

    if (result.scene_request != NULL)
        (void)app_flow_request_scene(flow, runtime, result.scene_request);
    return true;
}

static void app_flow_update_transition(sdl3d_game_data_app_flow *flow, sdl3d_game_context *ctx, sdl3d_signal_bus *bus,
                                       float dt)
{
    if (flow->transition.active)
        sdl3d_transition_update(&flow->transition, bus, dt);
    if (flow->quit_pending && flow->transition.finished && !flow->transition.active)
        ctx->quit_requested = true;
}

void sdl3d_game_data_app_flow_init(sdl3d_game_data_app_flow *flow)
{
    if (flow == NULL)
        return;
    SDL_zero(*flow);
    sdl3d_game_data_scene_flow_init(&flow->scene_flow);
    sdl3d_transition_reset(&flow->transition);
    flow->app.start_signal_id = -1;
    flow->app.quit_action_id = -1;
    flow->app.pause_action_id = -1;
    flow->app.quit_signal_id = -1;
}

bool sdl3d_game_data_app_flow_start(sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime)
{
    if (flow == NULL || runtime == NULL)
        return false;

    sdl3d_game_data_scene_flow_init(&flow->scene_flow);
    sdl3d_transition_reset(&flow->transition);
    flow->quit_pending = false;
    flow->scene_input_armed = false;
    flow->skip_scene = NULL;
    flow->skip_requested = false;
    sdl3d_game_data_timeline_state_init(&flow->timeline);
    if (!sdl3d_game_data_get_app_control(runtime, &flow->app))
        return false;

    sdl3d_game_data_transition_desc transition;
    if (flow->app.startup_transition != NULL &&
        sdl3d_game_data_get_transition(runtime, flow->app.startup_transition, &transition))
    {
        sdl3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                               transition.duration, transition.done_signal_id);
    }
    return true;
}

bool sdl3d_game_data_app_flow_quit_pending(const sdl3d_game_data_app_flow *flow)
{
    return flow != NULL && flow->quit_pending;
}

bool sdl3d_game_data_app_flow_is_transitioning(const sdl3d_game_data_app_flow *flow)
{
    return flow != NULL && (flow->transition.active || sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow));
}

bool sdl3d_game_data_app_flow_update(sdl3d_game_data_app_flow *flow, sdl3d_game_context *ctx,
                                     sdl3d_game_data_runtime *runtime, float dt)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || ctx->session == NULL)
        return false;

    sdl3d_input_manager *input = sdl3d_game_session_get_input(ctx->session);
    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(ctx->session);
    bool skip_applied = false;
    const bool skip_consumed = app_flow_apply_skip_policy(flow, runtime, input, true, &skip_applied);

    if (!skip_consumed)
    {
        if (flow->app.quit_action_id >= 0 &&
            sdl3d_game_data_active_scene_allows_action(runtime, flow->app.quit_action_id) &&
            sdl3d_input_is_pressed(input, flow->app.quit_action_id))
            app_flow_request_quit(flow, ctx, runtime);

        app_flow_consume_scene_shortcuts(flow, runtime, input);
        app_flow_consume_menu(flow, ctx, runtime, input, bus);

        if (flow->app.pause_action_id >= 0 && sdl3d_input_is_pressed(input, flow->app.pause_action_id) &&
            sdl3d_game_data_active_scene_allows_action(runtime, flow->app.pause_action_id) && !flow->quit_pending &&
            !sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        {
            if (ctx->paused)
                ctx->paused = false;
            else
            {
                sdl3d_game_data_ui_metrics metrics;
                SDL_zero(metrics);
                metrics.paused = ctx->paused;
                if (sdl3d_game_data_app_pause_allowed(runtime, &metrics) &&
                    sdl3d_game_data_active_scene_updates_game(runtime))
                    ctx->paused = true;
            }
        }
    }

    const bool scene_transitioning_before = sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow);
    app_flow_update_transition(flow, ctx, bus, dt);
    sdl3d_game_data_scene_flow_update(&flow->scene_flow, runtime, bus, dt);
    bool deferred_skip_applied = false;
    (void)app_flow_apply_skip_policy(flow, runtime, input, false, &deferred_skip_applied);
    skip_applied = skip_applied || deferred_skip_applied;
    if (!scene_transitioning_before && !skip_applied && !app_flow_update_timeline(flow, runtime, dt))
        return false;
    return true;
}

void sdl3d_game_data_app_flow_draw(const sdl3d_game_data_app_flow *flow, sdl3d_render_context *renderer)
{
    if (flow == NULL)
        return;
    sdl3d_game_data_scene_flow_draw(&flow->scene_flow, renderer);
    sdl3d_transition_draw(&flow->transition, renderer);
}

bool sdl3d_game_data_draw_frame(const sdl3d_game_data_frame_desc *frame)
{
    if (frame == NULL || frame->runtime == NULL || frame->renderer == NULL)
        return false;

    bool ok = true;
    ok = apply_render_settings(frame->runtime, frame->renderer) && ok;
    ok = apply_world_lights(frame->runtime, frame->renderer, frame->render_eval) && ok;

    if (sdl3d_game_data_active_scene_renders_world(frame->runtime))
    {
        const sdl3d_camera3d camera = active_camera_or_fallback(frame->runtime, frame->fallback_camera);
        if (sdl3d_begin_mode_3d(frame->renderer, camera))
        {
            ok = run_frame_hook(frame, frame->before_world_3d) && ok;
            if (frame->particle_cache != NULL)
                ok = sdl3d_game_data_draw_particles(frame->runtime, frame->renderer, frame->particle_cache) && ok;
            ok =
                sdl3d_game_data_draw_render_primitives_evaluated(frame->runtime, frame->renderer, frame->render_eval) &&
                ok;
            ok = run_frame_hook(frame, frame->after_world_3d) && ok;
            sdl3d_end_mode_3d(frame->renderer);
        }
        else
        {
            ok = false;
        }
    }

    ok = run_frame_hook(frame, frame->before_ui) && ok;
    if (frame->image_cache != NULL)
        ok = sdl3d_game_data_draw_ui_images(frame->runtime, frame->renderer, frame->image_cache, frame->metrics) && ok;
    if (frame->font_cache != NULL)
    {
        ok = sdl3d_game_data_draw_ui_text(frame->runtime, frame->renderer, frame->font_cache, frame->metrics,
                                          frame->pulse_phase) &&
             ok;
    }
    if (frame->app_flow != NULL)
        sdl3d_game_data_app_flow_draw(frame->app_flow, frame->renderer);
    ok = run_frame_hook(frame, frame->after_ui) && ok;
    return ok;
}
