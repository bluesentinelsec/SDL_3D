/**
 * @file game_presentation.c
 * @brief Renderer-facing helpers for JSON-authored game data.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/animation.h"
#include "slayer3d/drawing3d.h"
#include "slayer3d/image.h"
#include "slayer3d/lighting.h"
#include "slayer3d/math.h"
#include "slayer3d/shapes.h"

#include "game_data_internal.h"
#include "render_context_internal.h"

typedef struct primitive_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    slayer3d_game_data_image_cache *image_cache;
    slayer3d_game_data_sprite_cache *sprite_cache;
    slayer3d_game_data_model_cache *model_cache;
    slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache;
    const slayer3d_camera3d *camera;
    const slayer3d_game_data_render_eval *eval;
    slayer3d_game_data_render_settings render_settings;
    slayer3d_game_data_render_primitive sphere_batch;
    slayer3d_vec3 *sphere_batch_positions;
    int sphere_batch_count;
    int sphere_batch_capacity;
    bool sphere_batch_active;
    bool draw_world_space;
    bool draw_view_space;
} primitive_draw_context;

static const float SLAYER3D_GAME_PRESENTATION_PI = 3.14159265358979323846f;

typedef struct sector_level_draw_context
{
    slayer3d_render_context *renderer;
    const slayer3d_asset_resolver *assets;
    const slayer3d_camera3d *camera;
    bool *sector_visible;
    int sector_visible_capacity;
    bool ok;
} sector_level_draw_context;

typedef struct brush_world_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    const slayer3d_asset_resolver *assets;
    const slayer3d_camera3d *camera;
    bool ok;
} brush_world_draw_context;

typedef struct ui_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    slayer3d_game_data_font_cache *font_cache;
    const slayer3d_game_data_ui_metrics *metrics;
    float pulse_phase;
    bool ok;
} ui_draw_context;

typedef struct ui_image_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    slayer3d_game_data_image_cache *image_cache;
    const slayer3d_game_data_ui_metrics *metrics;
    const slayer3d_game_data_render_eval *render_eval;
    bool ok;
} ui_image_draw_context;

typedef struct ui_rect_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    const slayer3d_game_data_ui_metrics *metrics;
    const slayer3d_game_data_render_eval *render_eval;
    bool ok;
} ui_rect_draw_context;

static Uint32 ui_image_hash_string(const char *s)
{
    Uint32 h = 2166136261u;
    if (s == NULL)
        return h;
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p)
    {
        h ^= (Uint32)(*p);
        h *= 16777619u;
    }
    return h != 0u ? h : 1u;
}

static slayer3d_overlay_effect ui_image_effect_from_name(const char *effect)
{
    if (effect == NULL)
        return SLAYER3D_OVERLAY_EFFECT_NONE;
    if (SDL_strcasecmp(effect, "melt") == 0)
        return SLAYER3D_OVERLAY_EFFECT_MELT;
    return SLAYER3D_OVERLAY_EFFECT_NONE;
}

typedef struct particle_update_context
{
    slayer3d_game_data_particle_cache *cache;
    float dt;
    bool ok;
} particle_update_context;

static slayer3d_window_mode parse_window_mode_setting(const char *value, slayer3d_window_mode fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;
    if (SDL_strcasecmp(value, "windowed") == 0 || SDL_strcasecmp(value, "window") == 0)
        return SLAYER3D_WINDOW_MODE_WINDOWED;
    if (SDL_strcasecmp(value, "fullscreen_exclusive") == 0 || SDL_strcasecmp(value, "exclusive") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE;
    if (SDL_strcasecmp(value, "fullscreen_borderless") == 0 || SDL_strcasecmp(value, "borderless") == 0 ||
        SDL_strcasecmp(value, "desktop_fullscreen") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS;
    return fallback;
}

static const char *window_mode_setting_name(slayer3d_window_mode mode)
{
    switch (mode)
    {
    case SLAYER3D_WINDOW_MODE_WINDOWED:
        return "windowed";
    case SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE:
        return "fullscreen_exclusive";
    case SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS:
        return "fullscreen_borderless";
    case SLAYER3D_WINDOW_MODE_DEFAULT:
    default:
        return "default";
    }
}

static slayer3d_backend parse_backend_setting(const char *value, slayer3d_backend fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;
    if (SDL_strcasecmp(value, "software") == 0 || SDL_strcasecmp(value, "sw") == 0)
        return SLAYER3D_BACKEND_SOFTWARE;
    if (SDL_strcasecmp(value, "opengl") == 0 || SDL_strcasecmp(value, "gl") == 0)
        return SLAYER3D_BACKEND_OPENGL;
    if (SDL_strcasecmp(value, "auto") == 0)
        return SLAYER3D_BACKEND_AUTO;
    return fallback;
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

static bool menu_action_pressed(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                int action_id, const char *fallback_name)
{
    if (action_id >= 0 && slayer3d_game_data_active_scene_allows_action(runtime, action_id) &&
        slayer3d_input_is_pressed(input, action_id))
    {
        return true;
    }

    if (fallback_name != NULL && input != NULL)
    {
        const int fallback_id = slayer3d_input_find_action(input, fallback_name);
        if (fallback_id >= 0 && slayer3d_input_is_pressed(input, fallback_id))
        {
            return true;
        }
    }

    return false;
}

static bool menu_action_held(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                             int action_id, const char *fallback_name)
{
    if (action_id >= 0 && slayer3d_game_data_active_scene_allows_action(runtime, action_id) &&
        slayer3d_input_is_held(input, action_id))
    {
        return true;
    }

    if (fallback_name != NULL && input != NULL)
    {
        const int fallback_id = slayer3d_input_find_action(input, fallback_name);
        if (fallback_id >= 0 && slayer3d_input_is_held(input, fallback_id))
        {
            return true;
        }
    }

    return false;
}

static bool ensure_font_cache_capacity(slayer3d_game_data_font_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_font *new_fonts = (slayer3d_font *)SDL_calloc((size_t)next_capacity, sizeof(*new_fonts));
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

static bool ensure_particle_cache_capacity(slayer3d_game_data_particle_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_particle_cache_entry *entries = (slayer3d_game_data_particle_cache_entry *)SDL_realloc(
        cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_image_cache_capacity(slayer3d_game_data_image_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_image_cache_entry *entries =
        (slayer3d_game_data_image_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_sprite_cache_capacity(slayer3d_game_data_sprite_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_sprite_cache_entry *entries =
        (slayer3d_game_data_sprite_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_model_cache_capacity(slayer3d_game_data_model_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_model_cache_entry *entries =
        (slayer3d_game_data_model_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static bool ensure_model_pose_cache_capacity(slayer3d_game_data_model_cache *cache, int required)
{
    if (cache == NULL || required <= cache->pose_capacity)
        return cache != NULL;

    int next_capacity = cache->pose_capacity < 16 ? 16 : cache->pose_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_model_pose_cache_entry *entries = (slayer3d_game_data_model_pose_cache_entry *)SDL_realloc(
        cache->pose_entries, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->pose_capacity, 0, (size_t)(next_capacity - cache->pose_capacity) * sizeof(*entries));
    cache->pose_entries = entries;
    cache->pose_capacity = next_capacity;
    return true;
}

static bool ensure_mesh_primitive_cache_capacity(slayer3d_game_data_mesh_primitive_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 16 ? 16 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    slayer3d_game_data_mesh_primitive_cache_entry *entries =
        (slayer3d_game_data_mesh_primitive_cache_entry *)SDL_realloc(cache->entries,
                                                                     (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + cache->capacity, 0, (size_t)(next_capacity - cache->capacity) * sizeof(*entries));
    cache->entries = entries;
    cache->capacity = next_capacity;
    return true;
}

static slayer3d_game_data_particle_cache_entry *find_particle_entry(slayer3d_game_data_particle_cache *cache,
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

static slayer3d_game_data_particle_cache_entry *find_or_create_particle_entry(
    slayer3d_game_data_particle_cache *cache, const slayer3d_game_data_particle_emitter *emitter)
{
    if (cache == NULL || emitter == NULL || emitter->entity_name == NULL)
        return NULL;

    slayer3d_game_data_particle_cache_entry *entry = find_particle_entry(cache, emitter->entity_name);
    if (entry != NULL)
        return entry;

    if (!ensure_particle_cache_capacity(cache, cache->count + 1))
        return NULL;

    entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->entity_name = emitter->entity_name;
    entry->emitter = slayer3d_create_particle_emitter(&emitter->config);
    if (entry->emitter == NULL)
        return NULL;

    ++cache->count;
    return entry;
}

static slayer3d_font *find_or_load_font(const slayer3d_game_data_runtime *runtime, slayer3d_game_data_font_cache *cache,
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

    slayer3d_game_data_font_asset font;
    if (!slayer3d_game_data_get_font_asset(runtime, font_id, &font))
        return NULL;

    slayer3d_font *slot = &cache->fonts[cache->count];
    bool loaded = false;
    if (font.builtin)
        loaded = slayer3d_load_builtin_font(cache->media_dir, font.builtin_id, font.size, slot);
    else if (font.path != NULL)
        loaded = slayer3d_load_font(font.path, font.size, slot);
    if (!loaded)
        return NULL;

    cache->font_ids[cache->count] = font_id;
    cache->count++;
    return slot;
}

static slayer3d_game_data_image_cache_entry *find_or_load_image_entry(const slayer3d_game_data_runtime *runtime,
                                                                      slayer3d_game_data_image_cache *cache,
                                                                      const char *image_id)
{
    if (runtime == NULL || cache == NULL || cache->assets == NULL || image_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].image_id != NULL && SDL_strcmp(cache->entries[i].image_id, image_id) == 0)
            return cache->entries[i].loaded ? &cache->entries[i] : NULL;
    }

    if (!ensure_image_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_image_asset asset;
    if (!slayer3d_game_data_get_image_asset(runtime, image_id, &asset))
        return NULL;

    if (asset.sprite != NULL)
    {
        slayer3d_sprite_asset_runtime sprite;
        SDL_zero(sprite);
        char error[256];
        if (!slayer3d_game_data_load_sprite_asset(runtime, asset.sprite, &sprite, error, (int)sizeof(error)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load sprite-backed UI image %s: %s", asset.sprite,
                         error);
            return NULL;
        }

        if (sprite.base_texture_count <= 0 || sprite.base_textures == NULL || sprite.base_textures[0].pixels == NULL ||
            sprite.base_textures[0].width <= 0 || sprite.base_textures[0].height <= 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sprite-backed UI image %s has no base texture", asset.sprite);
            slayer3d_sprite_asset_free(&sprite);
            return NULL;
        }

        slayer3d_image image;
        SDL_zero(image);
        image.pixels = sprite.base_textures[0].pixels;
        image.width = sprite.base_textures[0].width;
        image.height = sprite.base_textures[0].height;

        slayer3d_game_data_image_cache_entry *entry = &cache->entries[cache->count];
        SDL_zero(*entry);
        entry->image_id = asset.id;
        entry->effect = sprite.effect;
        entry->effect_delay = sprite.effect_delay;
        entry->effect_duration = sprite.effect_duration;
        if (sprite.shader_vertex_source != NULL)
            entry->shader_vertex_source = SDL_strdup(sprite.shader_vertex_source);
        if (sprite.shader_fragment_source != NULL)
            entry->shader_fragment_source = SDL_strdup(sprite.shader_fragment_source);
        if ((sprite.shader_vertex_source != NULL && entry->shader_vertex_source == NULL) ||
            (sprite.shader_fragment_source != NULL && entry->shader_fragment_source == NULL))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to copy sprite-backed UI image shader source %s",
                         asset.sprite);
            SDL_free(entry->shader_vertex_source);
            SDL_free(entry->shader_fragment_source);
            slayer3d_sprite_asset_free(&sprite);
            return NULL;
        }
        entry->loaded = slayer3d_create_texture_from_image(&image, &entry->texture);
        slayer3d_sprite_asset_free(&sprite);
        if (!entry->loaded)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture for sprite-backed UI image %s",
                         asset.sprite);
            SDL_free(entry->shader_vertex_source);
            SDL_free(entry->shader_fragment_source);
            SDL_zero(*entry);
            return NULL;
        }

        ++cache->count;
        return entry;
    }

    if (asset.path == NULL)
        return NULL;

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char error[256];
    if (!slayer3d_asset_resolver_read_file(cache->assets, asset.path, &buffer, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read UI image asset %s: %s", asset.path, error);
        return NULL;
    }

    slayer3d_image image;
    SDL_zero(image);
    const bool decoded = slayer3d_load_image_from_memory(buffer.data, buffer.size, &image);
    slayer3d_asset_buffer_free(&buffer);
    if (!decoded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to decode UI image asset %s", asset.path);
        return NULL;
    }

    slayer3d_game_data_image_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->image_id = asset.id;
    entry->effect = NULL;
    entry->effect_delay = 0.0f;
    entry->effect_duration = 1.0f;
    entry->shader_vertex_source = NULL;
    entry->shader_fragment_source = NULL;
    entry->loaded = slayer3d_create_texture_from_image(&image, &entry->texture);
    slayer3d_free_image(&image);
    if (!entry->loaded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture for UI image asset %s", asset.path);
        return NULL;
    }

    ++cache->count;
    return entry;
}

static slayer3d_game_data_sprite_cache_entry *find_or_load_sprite_entry(const slayer3d_game_data_runtime *runtime,
                                                                        slayer3d_game_data_sprite_cache *cache,
                                                                        const char *sprite_id)
{
    if (runtime == NULL || cache == NULL || cache->assets == NULL || sprite_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].sprite_id != NULL && SDL_strcmp(cache->entries[i].sprite_id, sprite_id) == 0)
            return cache->entries[i].loaded ? &cache->entries[i] : NULL;
    }

    if (!ensure_sprite_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_sprite_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->sprite_id = sprite_id;

    char error[256];
    if (!slayer3d_game_data_load_sprite_asset(runtime, sprite_id, &entry->sprite, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load world sprite asset %s: %s", sprite_id, error);
        SDL_zero(entry);
        return NULL;
    }

    entry->loaded = true;
    ++cache->count;
    return entry;
}

static slayer3d_game_data_model_cache_entry *find_or_load_model_entry(const slayer3d_game_data_runtime *runtime,
                                                                      slayer3d_game_data_model_cache *cache,
                                                                      const char *model_id)
{
    if (runtime == NULL || cache == NULL || cache->assets == NULL || model_id == NULL)
        return NULL;

    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].model_id != NULL && SDL_strcmp(cache->entries[i].model_id, model_id) == 0)
            return cache->entries[i].loaded ? &cache->entries[i] : NULL;
    }

    if (!ensure_model_cache_capacity(cache, cache->count + 1))
        return NULL;

    slayer3d_game_data_model_asset asset;
    if (!slayer3d_game_data_get_model_asset(runtime, model_id, &asset))
        return NULL;

    char error[256];
    char *filesystem_path = NULL;
    if (!slayer3d_asset_resolver_resolve_file_path(cache->assets, asset.path, &filesystem_path, error,
                                                   (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to resolve model asset %s (%s): %s. Model assets currently require a directory mount.",
                     model_id, asset.path != NULL ? asset.path : "<null>", error);
        return NULL;
    }

    slayer3d_game_data_model_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->model_id = model_id;
    entry->loaded = slayer3d_load_model_from_file(filesystem_path, &entry->model);
    slayer3d_asset_resolver_free_path(filesystem_path);
    if (!entry->loaded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load model asset %s: %s", model_id, SDL_GetError());
        SDL_zero(entry);
        return NULL;
    }

    ++cache->count;
    return entry;
}

void slayer3d_game_data_model_cache_begin_pose_frame(slayer3d_game_data_model_cache *cache)
{
    if (cache != NULL)
        cache->pose_count = 0;
}

static const slayer3d_mat4 *model_pose_cache_evaluate_clip(slayer3d_game_data_model_cache *cache,
                                                           slayer3d_render_context *renderer,
                                                           const slayer3d_model *model,
                                                           const slayer3d_animation_clip *clip, int animation_clip,
                                                           float animation_time, int *out_joint_count)
{
    if (out_joint_count != NULL)
        *out_joint_count = 0;
    if (cache == NULL || model == NULL || model->skeleton == NULL || clip == NULL || out_joint_count == NULL)
        return NULL;

    const int joint_count = model->skeleton->joint_count;
    if (joint_count <= 0)
        return NULL;

    for (int i = 0; i < cache->pose_count; ++i)
    {
        slayer3d_game_data_model_pose_cache_entry *entry = &cache->pose_entries[i];
        if (entry->model == model && entry->animation_clip == animation_clip &&
            entry->animation_time == animation_time && entry->joint_count == joint_count)
        {
            *out_joint_count = joint_count;
            if (renderer != NULL)
                renderer->stats.animation_pose_cache_hits += 1u;
            return entry->joint_matrices;
        }
    }

    if (!ensure_model_pose_cache_capacity(cache, cache->pose_count + 1))
        return NULL;

    slayer3d_game_data_model_pose_cache_entry *entry = &cache->pose_entries[cache->pose_count];
    if (entry->joint_capacity < joint_count)
    {
        slayer3d_mat4 *matrices =
            (slayer3d_mat4 *)SDL_realloc(entry->joint_matrices, (size_t)joint_count * sizeof(*matrices));
        if (matrices == NULL)
            return NULL;
        entry->joint_matrices = matrices;
        entry->joint_capacity = joint_count;
    }

    if (!slayer3d_evaluate_animation(model->skeleton, clip, animation_time, entry->joint_matrices))
        return NULL;

    entry->model = model;
    entry->animation_clip = animation_clip;
    entry->animation_time = animation_time;
    entry->joint_count = joint_count;
    ++cache->pose_count;
    *out_joint_count = joint_count;
    if (renderer != NULL)
        renderer->stats.animation_pose_evaluations += 1u;
    return entry->joint_matrices;
}

const slayer3d_mat4 *slayer3d_game_data_model_cache_evaluate_pose(slayer3d_game_data_model_cache *cache,
                                                                  slayer3d_render_context *renderer,
                                                                  const slayer3d_model *model, int animation_clip,
                                                                  float animation_time, bool loop, int *out_joint_count)
{
    if (out_joint_count != NULL)
        *out_joint_count = 0;
    if (cache == NULL || model == NULL || model->skeleton == NULL || model->animations == NULL ||
        model->animation_count <= 0 || animation_clip < 0 || out_joint_count == NULL)
    {
        return NULL;
    }

    int clip_index = animation_clip;
    if (clip_index >= model->animation_count)
        clip_index = 0;
    const slayer3d_animation_clip *clip = &model->animations[clip_index];
    if (loop && clip->duration > 0.0f)
    {
        animation_time = SDL_fmodf(animation_time, clip->duration);
        if (animation_time < 0.0f)
            animation_time += clip->duration;
    }
    return model_pose_cache_evaluate_clip(cache, renderer, model, clip, clip_index, animation_time, out_joint_count);
}

static bool draw_sphere_batch(slayer3d_render_context *renderer, const slayer3d_game_data_render_primitive *primitive)
{
    if (renderer == NULL || primitive == NULL || primitive->instances == NULL || primitive->instance_count <= 0)
        return true;

    const int rings = SDL_max(primitive->rings, 3);
    const int slices = SDL_max(primitive->slices, 3);
    const int verts_per_sphere = (rings + 1) * (slices + 1);
    const int indices_per_sphere = rings * slices * 6;
    const int vertex_count = verts_per_sphere * primitive->instance_count;
    const int index_count = indices_per_sphere * primitive->instance_count;
    float *positions = (float *)SDL_malloc((size_t)vertex_count * 3U * sizeof(*positions));
    float *normals = (float *)SDL_malloc((size_t)vertex_count * 3U * sizeof(*normals));
    float *uvs = (float *)SDL_malloc((size_t)vertex_count * 2U * sizeof(*uvs));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)index_count * sizeof(*indices));
    if (positions == NULL || normals == NULL || uvs == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(uvs);
        SDL_free(indices);
        return false;
    }

    int vertex_offset = 0;
    int index_offset = 0;
    for (int instance = 0; instance < primitive->instance_count; ++instance)
    {
        const slayer3d_vec3 center = primitive->instances[instance];
        for (int ring = 0; ring <= rings; ++ring)
        {
            const float theta = SDL_PI_F * (float)ring / (float)rings;
            const float sin_t = SDL_sinf(theta);
            const float cos_t = SDL_cosf(theta);
            for (int slice = 0; slice <= slices; ++slice)
            {
                const float phi = 2.0f * SDL_PI_F * (float)slice / (float)slices;
                const float nx = sin_t * SDL_cosf(phi);
                const float ny = cos_t;
                const float nz = sin_t * SDL_sinf(phi);
                const int vertex = vertex_offset + ring * (slices + 1) + slice;
                positions[vertex * 3 + 0] = center.x + primitive->radius * nx;
                positions[vertex * 3 + 1] = center.y + primitive->radius * ny;
                positions[vertex * 3 + 2] = center.z + primitive->radius * nz;
                normals[vertex * 3 + 0] = nx;
                normals[vertex * 3 + 1] = ny;
                normals[vertex * 3 + 2] = nz;
                uvs[vertex * 2 + 0] = (float)slice / (float)slices;
                uvs[vertex * 2 + 1] = 1.0f - (float)ring / (float)rings;
            }
        }
        for (int ring = 0; ring < rings; ++ring)
        {
            for (int slice = 0; slice < slices; ++slice)
            {
                const unsigned int v00 = (unsigned int)(vertex_offset + ring * (slices + 1) + slice);
                const unsigned int v01 = v00 + 1U;
                const unsigned int v10 = (unsigned int)(vertex_offset + (ring + 1) * (slices + 1) + slice);
                const unsigned int v11 = v10 + 1U;
                indices[index_offset++] = v00;
                indices[index_offset++] = v01;
                indices[index_offset++] = v11;
                indices[index_offset++] = v00;
                indices[index_offset++] = v11;
                indices[index_offset++] = v10;
            }
        }
        vertex_offset += verts_per_sphere;
    }

    slayer3d_mesh mesh;
    SDL_zero(mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.indices = indices;
    mesh.vertex_count = vertex_count;
    mesh.index_count = index_count;
    const bool ok = slayer3d_draw_mesh(renderer, &mesh, NULL, primitive->color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(uvs);
    SDL_free(indices);
    return ok;
}

static bool primitive_sphere_can_batch(const slayer3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL || primitive->type != SLAYER3D_GAME_DATA_RENDER_SPHERE || primitive->texture_image != NULL)
        return false;
    return SDL_fabsf(primitive->rotation_angle) <= 0.0001f && SDL_fabsf(primitive->rotation_axis.x) <= 0.0001f &&
           SDL_fabsf(primitive->rotation_axis.y) <= 0.0001f && SDL_fabsf(primitive->rotation_axis.z) <= 0.0001f;
}

static float primitive_lod_bounding_radius(const slayer3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL)
        return 0.0f;
    switch (primitive->type)
    {
    case SLAYER3D_GAME_DATA_RENDER_SPHERE:
    case SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH:
        return SDL_max(primitive->radius, 0.0f);
    case SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE:
        switch (primitive->mesh_primitive)
        {
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
            return SDL_max(primitive->radius, 0.0f);
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
            return SDL_sqrtf(primitive->radius * primitive->radius +
                             (primitive->height * 0.5f) * (primitive->height * 0.5f));
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
            return SDL_max(primitive->major_radius + primitive->minor_radius, 0.0f);
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
            return 0.5f * SDL_sqrtf(primitive->size.x * primitive->size.x + primitive->size.y * primitive->size.y +
                                    primitive->size.z * primitive->size.z);
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
        default:
            return 0.0f;
        }
    case SLAYER3D_GAME_DATA_RENDER_CUBE:
    case SLAYER3D_GAME_DATA_RENDER_SPRITE:
    case SLAYER3D_GAME_DATA_RENDER_MODEL:
    default:
        return 0.0f;
    }
}

static float projected_primitive_pixels(const primitive_draw_context *context,
                                        const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || context->camera == NULL || primitive == NULL)
        return 0.0f;
    const float radius = primitive_lod_bounding_radius(primitive);
    if (radius <= 0.0f)
        return 0.0f;

    slayer3d_vec3 center = primitive->position;
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH && primitive->instances != NULL &&
        primitive->instance_count > 0)
    {
        float best = 0.0f;
        slayer3d_game_data_render_primitive sample = *primitive;
        sample.type = SLAYER3D_GAME_DATA_RENDER_SPHERE;
        sample.instances = NULL;
        sample.instance_count = 0;
        for (int i = 0; i < primitive->instance_count; ++i)
        {
            sample.position = primitive->instances[i];
            best = SDL_max(best, projected_primitive_pixels(context, &sample));
        }
        return best;
    }

    const slayer3d_vec3 forward =
        slayer3d_vec3_normalize(slayer3d_vec3_sub(context->camera->target, context->camera->position));
    if (slayer3d_vec3_length_squared(forward) <= 0.000001f)
        return 0.0f;
    const float distance = slayer3d_vec3_dot(slayer3d_vec3_sub(center, context->camera->position), forward);
    if (distance <= 0.01f)
        return context->render_settings.procedural_lod_near_pixels;

    const int axis_pixels = context->camera->fov_axis == SLAYER3D_CAMERA_FOV_HORIZONTAL
                                ? slayer3d_get_render_context_width(context->renderer)
                                : slayer3d_get_render_context_height(context->renderer);
    const float fov = SDL_clamp(context->camera->fovy, 1.0f, 175.0f) * SLAYER3D_GAME_PRESENTATION_PI / 180.0f;
    const float denominator = 2.0f * distance * SDL_tanf(fov * 0.5f);
    if (axis_pixels <= 0 || denominator <= 0.0001f)
        return 0.0f;
    return ((radius * 2.0f) * (float)axis_pixels / denominator) * SDL_max(primitive->lod_bias, 0.001f);
}

static bool primitive_mesh_uses_procedural_lod(slayer3d_game_data_mesh_primitive_kind primitive)
{
    switch (primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return true;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return false;
    }
}

static int lod_segment_count(int authored, int minimum, float projected_pixels, float near_pixels, float far_pixels)
{
    authored = SDL_max(authored, 3);
    minimum = SDL_clamp(minimum, 3, authored);
    if (authored <= minimum || near_pixels <= far_pixels + 0.001f)
        return authored;
    if (projected_pixels >= near_pixels)
        return authored;
    if (projected_pixels <= far_pixels)
        return minimum;
    const float t = (projected_pixels - far_pixels) / (near_pixels - far_pixels);
    const float multiplier = t >= 0.66f ? 0.75f : (t >= 0.33f ? 0.5f : 0.0f);
    const int resolved = multiplier > 0.0f ? (int)SDL_floorf((float)authored * multiplier + 0.5f) : minimum;
    return SDL_clamp(resolved, minimum, authored);
}

static Uint64 procedural_lod_triangle_count(const slayer3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL)
        return 0u;

    const int slices = SDL_max(primitive->slices, 3);
    const int rings = SDL_max(primitive->rings, 3);
    const int tube_segments = SDL_max(primitive->tube_segments, 3);
    const int instance_count = SDL_max(primitive->instance_count, 1);
    Uint64 triangles = 0u;

    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE ||
        primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        triangles = (Uint64)rings * (Uint64)slices * 2u;
        return triangles * (Uint64)instance_count;
    }

    if (primitive->type != SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
        return 0u;

    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        return (Uint64)rings * (Uint64)slices * 2u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
        return (Uint64)slices * (Uint64)(SDL_max(rings, 1) * 2 + 1) * 2u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        return (Uint64)slices * 4u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return (Uint64)slices * (Uint64)tube_segments * 2u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        return (Uint64)slices;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        return (Uint64)rings * (Uint64)slices * 2u + (Uint64)slices;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        return (Uint64)SDL_max(rings, 1) * 72u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return 0u;
    }
}

static void record_procedural_lod_stats(primitive_draw_context *context,
                                        const slayer3d_game_data_render_primitive *authored,
                                        const slayer3d_game_data_render_primitive *resolved)
{
    if (context == NULL || context->renderer == NULL || authored == NULL || resolved == NULL)
        return;

    const Uint64 authored_triangles = procedural_lod_triangle_count(authored);
    const Uint64 resolved_triangles = procedural_lod_triangle_count(resolved);
    ++context->renderer->stats.procedural_lod_candidates;
    context->renderer->stats.procedural_lod_authored_triangles += authored_triangles;
    context->renderer->stats.procedural_lod_resolved_triangles += resolved_triangles;
    if (authored_triangles > resolved_triangles)
    {
        ++context->renderer->stats.procedural_lod_reduced;
        context->renderer->stats.procedural_lod_triangles_saved += authored_triangles - resolved_triangles;
    }
}

static void apply_primitive_lod(primitive_draw_context *context, slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL || !context->render_settings.procedural_lod_enabled ||
        !primitive->lod_enabled || primitive->view_space)
    {
        return;
    }
    if (primitive->type != SLAYER3D_GAME_DATA_RENDER_SPHERE &&
        primitive->type != SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH &&
        (primitive->type != SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE ||
         !primitive_mesh_uses_procedural_lod(primitive->mesh_primitive)))
    {
        return;
    }

    const float projected = projected_primitive_pixels(context, primitive);
    if (projected <= 0.0f)
        return;
    const slayer3d_game_data_render_primitive authored = *primitive;
    const int minimum = context->render_settings.procedural_lod_min_segments;
    const float near_pixels = context->render_settings.procedural_lod_near_pixels;
    const float far_pixels = context->render_settings.procedural_lod_far_pixels;
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE ||
        primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        primitive->rings = lod_segment_count(primitive->rings, minimum, projected, near_pixels, far_pixels);
        record_procedural_lod_stats(context, &authored, primitive);
        return;
    }

    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        primitive->rings = lod_segment_count(primitive->rings, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        primitive->tube_segments =
            lod_segment_count(primitive->tube_segments, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        primitive->rings = lod_segment_count(primitive->rings, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        break;
    }
    record_procedural_lod_stats(context, &authored, primitive);
}

static bool primitive_sphere_batch_matches(const slayer3d_game_data_render_primitive *batch,
                                           const slayer3d_game_data_render_primitive *primitive)
{
    if (batch == NULL || primitive == NULL)
        return false;
    return batch->type == SLAYER3D_GAME_DATA_RENDER_SPHERE && primitive_sphere_can_batch(primitive) &&
           SDL_fabsf(batch->radius - primitive->radius) <= 0.0001f && batch->rings == primitive->rings &&
           batch->slices == primitive->slices && batch->lighting_enabled == primitive->lighting_enabled &&
           batch->emissive == primitive->emissive &&
           SDL_fabsf(batch->emissive_color.x - primitive->emissive_color.x) <= 0.0001f &&
           SDL_fabsf(batch->emissive_color.y - primitive->emissive_color.y) <= 0.0001f &&
           SDL_fabsf(batch->emissive_color.z - primitive->emissive_color.z) <= 0.0001f &&
           batch->color.r == primitive->color.r && batch->color.g == primitive->color.g &&
           batch->color.b == primitive->color.b && batch->color.a == primitive->color.a;
}

static bool flush_sphere_draw_batch(primitive_draw_context *context)
{
    if (context == NULL || !context->sphere_batch_active || context->sphere_batch_count <= 0)
        return true;
    slayer3d_game_data_render_primitive primitive = context->sphere_batch;
    primitive.type = SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH;
    primitive.instances = context->sphere_batch_positions;
    primitive.instance_count = context->sphere_batch_count;
    const bool ok = draw_sphere_batch(context->renderer, &primitive);
    context->sphere_batch_active = false;
    context->sphere_batch_count = 0;
    return ok;
}

static bool append_sphere_draw_batch(primitive_draw_context *context,
                                     const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL)
        return false;
    if (!context->sphere_batch_active || !primitive_sphere_batch_matches(&context->sphere_batch, primitive))
    {
        if (!flush_sphere_draw_batch(context))
            return false;
        context->sphere_batch = *primitive;
        context->sphere_batch.instances = NULL;
        context->sphere_batch.instance_count = 0;
        context->sphere_batch_active = true;
    }
    if (context->sphere_batch_count >= context->sphere_batch_capacity)
    {
        const int next_capacity = context->sphere_batch_capacity > 0 ? context->sphere_batch_capacity * 2 : 16;
        slayer3d_vec3 *positions =
            (slayer3d_vec3 *)SDL_realloc(context->sphere_batch_positions, (size_t)next_capacity * sizeof(*positions));
        if (positions == NULL)
            return false;
        context->sphere_batch_positions = positions;
        context->sphere_batch_capacity = next_capacity;
    }
    context->sphere_batch_positions[context->sphere_batch_count++] = primitive->position;
    return true;
}

static const slayer3d_texture2d *primitive_texture(primitive_draw_context *context,
                                                   const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL || primitive->texture_image == NULL || context->image_cache == NULL)
        return NULL;
    slayer3d_game_data_image_cache_entry *entry =
        find_or_load_image_entry(context->runtime, context->image_cache, primitive->texture_image);
    return entry != NULL ? &entry->texture : NULL;
}

static void mesh_primitive_free_mesh(slayer3d_mesh *mesh)
{
    if (mesh == NULL)
        return;
    SDL_free(mesh->positions);
    SDL_free(mesh->normals);
    SDL_free(mesh->uvs);
    SDL_free(mesh->indices);
    SDL_zero(*mesh);
}

static bool mesh_primitive_alloc_mesh(slayer3d_mesh *mesh, int vertex_count, int index_count, bool with_uvs)
{
    if (mesh == NULL || vertex_count <= 0 || index_count <= 0)
        return false;
    SDL_zero(*mesh);
    mesh->positions = (float *)SDL_calloc((size_t)vertex_count * 3U, sizeof(float));
    mesh->normals = (float *)SDL_calloc((size_t)vertex_count * 3U, sizeof(float));
    mesh->uvs = with_uvs ? (float *)SDL_calloc((size_t)vertex_count * 2U, sizeof(float)) : NULL;
    mesh->indices = (unsigned int *)SDL_calloc((size_t)index_count, sizeof(unsigned int));
    if (mesh->positions == NULL || mesh->normals == NULL || (with_uvs && mesh->uvs == NULL) || mesh->indices == NULL)
    {
        mesh_primitive_free_mesh(mesh);
        return SDL_OutOfMemory();
    }
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;
    mesh->material_index = -1;
    return true;
}

static void mesh_primitive_set_vertex(slayer3d_mesh *mesh, int index, slayer3d_vec3 position, slayer3d_vec3 normal,
                                      float u, float v)
{
    mesh->positions[index * 3 + 0] = position.x;
    mesh->positions[index * 3 + 1] = position.y;
    mesh->positions[index * 3 + 2] = position.z;
    mesh->normals[index * 3 + 0] = normal.x;
    mesh->normals[index * 3 + 1] = normal.y;
    mesh->normals[index * 3 + 2] = normal.z;
    if (mesh->uvs != NULL)
    {
        mesh->uvs[index * 2 + 0] = u;
        mesh->uvs[index * 2 + 1] = v;
    }
}

static slayer3d_vec3 mesh_primitive_face_normal(slayer3d_vec3 a, slayer3d_vec3 b, slayer3d_vec3 c)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_cross(slayer3d_vec3_sub(b, a), slayer3d_vec3_sub(c, a)));
}

static bool mesh_primitive_cache_entry_matches(const slayer3d_game_data_mesh_primitive_cache_entry *entry,
                                               const slayer3d_game_data_render_primitive *primitive)
{
    if (entry == NULL || primitive == NULL || !entry->loaded)
        return false;
    return entry->primitive == primitive->mesh_primitive && entry->size.x == primitive->size.x &&
           entry->size.y == primitive->size.y && entry->size.z == primitive->size.z &&
           entry->radius == primitive->radius && entry->radius_top == primitive->radius_top &&
           entry->radius_bottom == primitive->radius_bottom && entry->height == primitive->height &&
           entry->major_radius == primitive->major_radius && entry->minor_radius == primitive->minor_radius &&
           entry->bevel_radius == primitive->bevel_radius && entry->arc_angle == primitive->arc_angle &&
           entry->slices == primitive->slices && entry->rings == primitive->rings &&
           entry->tube_segments == primitive->tube_segments;
}

static void mesh_primitive_cache_entry_set_key(slayer3d_game_data_mesh_primitive_cache_entry *entry,
                                               const slayer3d_game_data_render_primitive *primitive)
{
    entry->primitive = primitive->mesh_primitive;
    entry->size = primitive->size;
    entry->radius = primitive->radius;
    entry->radius_top = primitive->radius_top;
    entry->radius_bottom = primitive->radius_bottom;
    entry->height = primitive->height;
    entry->major_radius = primitive->major_radius;
    entry->minor_radius = primitive->minor_radius;
    entry->bevel_radius = primitive->bevel_radius;
    entry->arc_angle = primitive->arc_angle;
    entry->slices = primitive->slices;
    entry->rings = primitive->rings;
    entry->tube_segments = primitive->tube_segments;
}

static bool build_cube_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 24, 36, true))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const float hz = primitive->size.z * 0.5f;
    const slayer3d_vec3 c[8] = {{-hx, -hy, -hz}, {hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz},
                                {-hx, -hy, hz},  {hx, -hy, hz},  {-hx, hy, hz},  {hx, hy, hz}};
    static const int faces[6][4] = {
        {4, 5, 7, 6}, {1, 0, 2, 3}, {5, 1, 3, 7}, {0, 4, 6, 2}, {6, 7, 3, 2}, {0, 1, 5, 4},
    };
    static const slayer3d_vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int f = 0; f < 6; ++f)
    {
        const int base = f * 4;
        for (int v = 0; v < 4; ++v)
            mesh_primitive_set_vertex(mesh, base + v, c[faces[f][v]], normals[f], uvs[v][0], uvs[v][1]);
        const unsigned int b = (unsigned int)base;
        const int ii = f * 6;
        mesh->indices[ii + 0] = b;
        mesh->indices[ii + 1] = b + 1U;
        mesh->indices[ii + 2] = b + 2U;
        mesh->indices[ii + 3] = b;
        mesh->indices[ii + 4] = b + 2U;
        mesh->indices[ii + 5] = b + 3U;
    }
    return true;
}

static bool build_quad_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 4, 6, true))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const slayer3d_vec3 n = {0.0f, 0.0f, 1.0f};
    mesh_primitive_set_vertex(mesh, 0, slayer3d_vec3_make(-hx, -hy, 0.0f), n, 0.0f, 1.0f);
    mesh_primitive_set_vertex(mesh, 1, slayer3d_vec3_make(hx, -hy, 0.0f), n, 1.0f, 1.0f);
    mesh_primitive_set_vertex(mesh, 2, slayer3d_vec3_make(hx, hy, 0.0f), n, 1.0f, 0.0f);
    mesh_primitive_set_vertex(mesh, 3, slayer3d_vec3_make(-hx, hy, 0.0f), n, 0.0f, 0.0f);
    const unsigned int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_memcpy(mesh->indices, indices, sizeof(indices));
    return true;
}

static bool build_disc_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int segments = primitive->slices;
    if (!mesh_primitive_alloc_mesh(mesh, segments + 1, segments * 3, true))
        return false;
    const slayer3d_vec3 n = {0.0f, 0.0f, 1.0f};
    mesh_primitive_set_vertex(mesh, 0, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), n, 0.5f, 0.5f);
    for (int i = 0; i < segments; ++i)
    {
        const float a = (float)i / (float)segments * SLAYER3D_GAME_PRESENTATION_PI * 2.0f;
        const float x = SDL_cosf(a) * primitive->radius;
        const float y = SDL_sinf(a) * primitive->radius;
        mesh_primitive_set_vertex(mesh, i + 1, slayer3d_vec3_make(x, y, 0.0f), n,
                                  0.5f + x / SDL_max(primitive->radius * 2.0f, 0.0001f),
                                  0.5f + y / SDL_max(primitive->radius * 2.0f, 0.0001f));
    }
    int ii = 0;
    for (int i = 0; i < segments; ++i)
    {
        mesh->indices[ii++] = 0U;
        mesh->indices[ii++] = (unsigned int)(i + 1);
        mesh->indices[ii++] = (unsigned int)((i + 1) % segments + 1);
    }
    return true;
}

static bool build_sphere_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int rings = primitive->rings;
    const int slices = primitive->slices;
    if (!mesh_primitive_alloc_mesh(mesh, (rings + 1) * (slices + 1), rings * slices * 6, true))
        return false;
    for (int r = 0; r <= rings; ++r)
    {
        const float theta = SLAYER3D_GAME_PRESENTATION_PI * (float)r / (float)rings;
        for (int s = 0; s <= slices; ++s)
        {
            const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
            const slayer3d_vec3 normal =
                slayer3d_vec3_make(SDL_sinf(theta) * SDL_cosf(phi), SDL_cosf(theta), SDL_sinf(theta) * SDL_sinf(phi));
            const slayer3d_vec3 position = slayer3d_vec3_scale(normal, primitive->radius);
            mesh_primitive_set_vertex(mesh, r * (slices + 1) + s, position, normal, (float)s / (float)slices,
                                      1.0f - (float)r / (float)rings);
        }
    }
    int ii = 0;
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
            const unsigned int a = (unsigned int)(r * (slices + 1) + s);
            const unsigned int b = a + 1U;
            const unsigned int c = (unsigned int)((r + 1) * (slices + 1) + s);
            const unsigned int d = c + 1U;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = b;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = c;
        }
    }
    return true;
}

static bool build_cylinder_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int slices = primitive->slices;
    const int side_verts = 2 * (slices + 1);
    const int cap_verts = (slices + 2) * 2;
    if (!mesh_primitive_alloc_mesh(mesh, side_verts + cap_verts, slices * 12, false))
        return false;
    const float hh = primitive->height * 0.5f;
    for (int s = 0; s <= slices; ++s)
    {
        const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
        const float c = SDL_cosf(phi);
        const float z = SDL_sinf(phi);
        const slayer3d_vec3 normal = slayer3d_vec3_make(c, 0.0f, z);
        mesh_primitive_set_vertex(mesh, s, slayer3d_vec3_make(primitive->radius_top * c, hh, primitive->radius_top * z),
                                  normal, 0.0f, 0.0f);
        mesh_primitive_set_vertex(mesh, slices + 1 + s,
                                  slayer3d_vec3_make(primitive->radius_bottom * c, -hh, primitive->radius_bottom * z),
                                  normal, 0.0f, 0.0f);
    }
    int ii = 0;
    for (int s = 0; s < slices; ++s)
    {
        const unsigned int t0 = (unsigned int)s;
        const unsigned int t1 = (unsigned int)(s + 1);
        const unsigned int b0 = (unsigned int)(slices + 1 + s);
        const unsigned int b1 = (unsigned int)(slices + 1 + s + 1);
        mesh->indices[ii++] = b0;
        mesh->indices[ii++] = t0;
        mesh->indices[ii++] = t1;
        mesh->indices[ii++] = b0;
        mesh->indices[ii++] = t1;
        mesh->indices[ii++] = b1;
    }
    int cap = side_verts;
    mesh_primitive_set_vertex(mesh, cap, slayer3d_vec3_make(0.0f, hh, 0.0f), slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0,
                              0);
    for (int s = 0; s <= slices; ++s)
    {
        const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
        mesh_primitive_set_vertex(
            mesh, cap + 1 + s,
            slayer3d_vec3_make(primitive->radius_top * SDL_cosf(phi), hh, primitive->radius_top * SDL_sinf(phi)),
            slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0, 0);
    }
    for (int s = 0; s < slices; ++s)
    {
        mesh->indices[ii++] = (unsigned int)cap;
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s + 1);
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s);
    }
    cap += slices + 2;
    mesh_primitive_set_vertex(mesh, cap, slayer3d_vec3_make(0.0f, -hh, 0.0f), slayer3d_vec3_make(0.0f, -1.0f, 0.0f), 0,
                              0);
    for (int s = 0; s <= slices; ++s)
    {
        const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
        mesh_primitive_set_vertex(
            mesh, cap + 1 + s,
            slayer3d_vec3_make(primitive->radius_bottom * SDL_cosf(phi), -hh, primitive->radius_bottom * SDL_sinf(phi)),
            slayer3d_vec3_make(0.0f, -1.0f, 0.0f), 0, 0);
    }
    for (int s = 0; s < slices; ++s)
    {
        mesh->indices[ii++] = (unsigned int)cap;
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s);
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s + 1);
    }
    return true;
}

static bool build_capsule_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int slices = primitive->slices;
    const int rings = primitive->rings;
    const int total_rings = 2 * rings + 2;
    const int verts_per_ring = slices + 1;
    if (!mesh_primitive_alloc_mesh(mesh, total_rings * verts_per_ring, (total_rings - 1) * slices * 6, false))
        return false;

    const float cylinder_span = SDL_max(primitive->height - primitive->radius * 2.0f, 0.0f);
    const float start_y = -cylinder_span * 0.5f;
    const float end_y = cylinder_span * 0.5f;
    for (int k = 0; k < total_rings; ++k)
    {
        float ring_radius = 0.0f;
        float center_y = 0.0f;
        float normal_y = 0.0f;
        if (k <= rings)
        {
            const float t = (float)k / (float)rings;
            const float theta = SLAYER3D_GAME_PRESENTATION_PI * 0.5f * t;
            ring_radius = primitive->radius * SDL_sinf(theta);
            center_y = end_y + primitive->radius * SDL_cosf(theta);
            normal_y = SDL_cosf(theta);
        }
        else
        {
            const int lower_k = k - (rings + 1);
            const float t = (float)lower_k / (float)rings;
            const float theta = SLAYER3D_GAME_PRESENTATION_PI * 0.5f * t;
            ring_radius = primitive->radius * SDL_cosf(theta);
            center_y = start_y - primitive->radius * SDL_sinf(theta);
            normal_y = -SDL_sinf(theta);
        }
        for (int s = 0; s <= slices; ++s)
        {
            const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
            const float x = ring_radius * SDL_cosf(phi);
            const float z = ring_radius * SDL_sinf(phi);
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(slayer3d_vec3_make(
                SDL_cosf(phi) * ring_radius, normal_y * primitive->radius, SDL_sinf(phi) * ring_radius));
            mesh_primitive_set_vertex(mesh, k * verts_per_ring + s, slayer3d_vec3_make(x, center_y, z), normal, 0, 0);
        }
    }

    int ii = 0;
    for (int k = 0; k + 1 < total_rings; ++k)
    {
        for (int s = 0; s < slices; ++s)
        {
            const unsigned int a = (unsigned int)(k * verts_per_ring + s);
            const unsigned int b = a + 1U;
            const unsigned int c = (unsigned int)((k + 1) * verts_per_ring + s);
            const unsigned int d = c + 1U;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = c;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = b;
        }
    }
    return true;
}

static bool build_torus_like_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh,
                                  bool full_torus)
{
    const int segments = primitive->slices;
    const int tube_segments = primitive->tube_segments;
    const int arc_vertices = segments + 1;
    const int tube_vertices = tube_segments + 1;
    if (!mesh_primitive_alloc_mesh(mesh, arc_vertices * tube_vertices, segments * tube_segments * 6, false))
        return false;
    const float arc_angle = full_torus ? SLAYER3D_GAME_PRESENTATION_PI * 2.0f : primitive->arc_angle;
    const float start_angle = full_torus ? 0.0f : -arc_angle * 0.5f;
    for (int a = 0; a <= segments; ++a)
    {
        const float u = start_angle + (float)a / (float)segments * arc_angle;
        const slayer3d_vec3 radial = slayer3d_vec3_make(SDL_cosf(u), 0.0f, SDL_sinf(u));
        const slayer3d_vec3 center = slayer3d_vec3_scale(radial, primitive->major_radius);
        for (int t = 0; t <= tube_segments; ++t)
        {
            const float v = (float)t / (float)tube_segments * SLAYER3D_GAME_PRESENTATION_PI * 2.0f;
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(
                slayer3d_vec3_make(radial.x * SDL_cosf(v), SDL_sinf(v), radial.z * SDL_cosf(v)));
            mesh_primitive_set_vertex(mesh, a * tube_vertices + t,
                                      slayer3d_vec3_add(center, slayer3d_vec3_scale(normal, primitive->minor_radius)),
                                      normal, 0, 0);
        }
    }
    int ii = 0;
    for (int a = 0; a < segments; ++a)
    {
        for (int t = 0; t < tube_segments; ++t)
        {
            const unsigned int p00 = (unsigned int)(a * tube_vertices + t);
            const unsigned int p01 = p00 + 1U;
            const unsigned int p10 = (unsigned int)((a + 1) * tube_vertices + t);
            const unsigned int p11 = p10 + 1U;
            mesh->indices[ii++] = p00;
            mesh->indices[ii++] = p10;
            mesh->indices[ii++] = p01;
            mesh->indices[ii++] = p01;
            mesh->indices[ii++] = p10;
            mesh->indices[ii++] = p11;
        }
    }
    return true;
}

static bool build_pyramid_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 18, 18, false))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const float hz = primitive->size.z * 0.5f;
    const slayer3d_vec3 apex = slayer3d_vec3_make(0.0f, hy, 0.0f);
    const slayer3d_vec3 bl = slayer3d_vec3_make(-hx, -hy, -hz);
    const slayer3d_vec3 br = slayer3d_vec3_make(hx, -hy, -hz);
    const slayer3d_vec3 tr = slayer3d_vec3_make(hx, -hy, hz);
    const slayer3d_vec3 tl = slayer3d_vec3_make(-hx, -hy, hz);
    const slayer3d_vec3 faces[6][3] = {{bl, apex, br}, {br, apex, tr}, {tr, apex, tl},
                                       {tl, apex, bl}, {bl, br, tr},   {bl, tr, tl}};
    for (int f = 0; f < 6; ++f)
    {
        const slayer3d_vec3 normal = mesh_primitive_face_normal(faces[f][0], faces[f][1], faces[f][2]);
        for (int v = 0; v < 3; ++v)
        {
            const int vi = f * 3 + v;
            mesh_primitive_set_vertex(mesh, vi, faces[f][v], normal, 0, 0);
            mesh->indices[vi] = (unsigned int)vi;
        }
    }
    return true;
}

static bool build_wedge_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 24, 24, false))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const float hz = primitive->size.z * 0.5f;
    const slayer3d_vec3 p[6] = {slayer3d_vec3_make(-hx, -hy, -hz), slayer3d_vec3_make(hx, -hy, -hz),
                                slayer3d_vec3_make(-hx, -hy, hz),  slayer3d_vec3_make(hx, -hy, hz),
                                slayer3d_vec3_make(-hx, hy, hz),   slayer3d_vec3_make(hx, hy, hz)};
    const slayer3d_vec3 triangles[8][3] = {
        {p[0], p[3], p[1]}, {p[0], p[2], p[3]}, {p[2], p[5], p[3]}, {p[2], p[4], p[5]},
        {p[0], p[4], p[2]}, {p[0], p[1], p[5]}, {p[0], p[5], p[4]}, {p[1], p[3], p[5]},
    };
    for (int f = 0; f < 8; ++f)
    {
        const slayer3d_vec3 normal = mesh_primitive_face_normal(triangles[f][0], triangles[f][1], triangles[f][2]);
        for (int v = 0; v < 3; ++v)
        {
            const int vi = f * 3 + v;
            mesh_primitive_set_vertex(mesh, vi, triangles[f][v], normal, 0, 0);
            mesh->indices[vi] = (unsigned int)vi;
        }
    }
    return true;
}

static bool build_hemisphere_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int rings = primitive->rings;
    const int slices = primitive->slices;
    const int curved_vertices = (rings + 1) * (slices + 1);
    const int cap_center = curved_vertices;
    if (!mesh_primitive_alloc_mesh(mesh, curved_vertices + 1, rings * slices * 6 + slices * 3, false))
        return false;
    const float y_offset = primitive->radius * 0.5f;
    for (int r = 0; r <= rings; ++r)
    {
        const float theta = (float)r / (float)rings * SLAYER3D_GAME_PRESENTATION_PI * 0.5f;
        const float rr = SDL_sinf(theta) * primitive->radius;
        const float y = SDL_cosf(theta) * primitive->radius - y_offset;
        for (int s = 0; s <= slices; ++s)
        {
            const float phi = (float)s / (float)slices * SLAYER3D_GAME_PRESENTATION_PI * 2.0f;
            const slayer3d_vec3 position = slayer3d_vec3_make(SDL_cosf(phi) * rr, y, SDL_sinf(phi) * rr);
            const slayer3d_vec3 normal =
                slayer3d_vec3_normalize(slayer3d_vec3_make(position.x, y + y_offset, position.z));
            mesh_primitive_set_vertex(mesh, r * (slices + 1) + s, position, normal, 0, 0);
        }
    }
    int ii = 0;
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
            const unsigned int a = (unsigned int)(r * (slices + 1) + s);
            const unsigned int b = a + 1U;
            const unsigned int c = (unsigned int)((r + 1) * (slices + 1) + s);
            const unsigned int d = c + 1U;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = c;
            mesh->indices[ii++] = b;
            mesh->indices[ii++] = b;
            mesh->indices[ii++] = c;
            mesh->indices[ii++] = d;
        }
    }
    mesh_primitive_set_vertex(mesh, cap_center, slayer3d_vec3_make(0.0f, -y_offset, 0.0f),
                              slayer3d_vec3_make(0.0f, -1.0f, 0.0f), 0, 0);
    const int base_row = rings * (slices + 1);
    for (int s = 0; s < slices; ++s)
    {
        mesh->indices[ii++] = (unsigned int)cap_center;
        mesh->indices[ii++] = (unsigned int)(base_row + s + 1);
        mesh->indices[ii++] = (unsigned int)(base_row + s);
    }
    return true;
}

static bool build_rounded_box_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int grid = SDL_max(primitive->rings + 2, 3);
    const int face_vertices = grid * grid;
    if (!mesh_primitive_alloc_mesh(mesh, face_vertices * 6, (grid - 1) * (grid - 1) * 36, false))
        return false;
    const slayer3d_vec3 half = slayer3d_vec3_scale(primitive->size, 0.5f);
    const float radius = SDL_min(primitive->bevel_radius,
                                 SDL_min(primitive->size.x, SDL_min(primitive->size.y, primitive->size.z)) * 0.5f);
    const slayer3d_vec3 inner = slayer3d_vec3_make(SDL_max(half.x - radius, 0.0f), SDL_max(half.y - radius, 0.0f),
                                                   SDL_max(half.z - radius, 0.0f));
    int vi = 0;
    for (int face = 0; face < 6; ++face)
    {
        const int axis = face / 2;
        const float sign = (face % 2) == 0 ? -1.0f : 1.0f;
        for (int y = 0; y < grid; ++y)
        {
            const float v = -1.0f + 2.0f * (float)y / (float)(grid - 1);
            for (int x = 0; x < grid; ++x)
            {
                const float u = -1.0f + 2.0f * (float)x / (float)(grid - 1);
                slayer3d_vec3 local;
                if (axis == 0)
                    local = slayer3d_vec3_make(sign * half.x, u * half.y, v * half.z);
                else if (axis == 1)
                    local = slayer3d_vec3_make(u * half.x, sign * half.y, v * half.z);
                else
                    local = slayer3d_vec3_make(u * half.x, v * half.y, sign * half.z);
                const slayer3d_vec3 clamped =
                    slayer3d_vec3_make(SDL_clamp(local.x, -inner.x, inner.x), SDL_clamp(local.y, -inner.y, inner.y),
                                       SDL_clamp(local.z, -inner.z, inner.z));
                const slayer3d_vec3 delta = slayer3d_vec3_sub(local, clamped);
                slayer3d_vec3 face_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
                if (axis == 0)
                    face_normal.x = sign;
                else if (axis == 1)
                    face_normal.y = sign;
                else
                    face_normal.z = sign;
                const slayer3d_vec3 normal =
                    slayer3d_vec3_length_squared(delta) > 0.000001f ? slayer3d_vec3_normalize(delta) : face_normal;
                mesh_primitive_set_vertex(mesh, vi++, slayer3d_vec3_add(clamped, slayer3d_vec3_scale(normal, radius)),
                                          normal, 0, 0);
            }
        }
    }
    int ii = 0;
    for (int face = 0; face < 6; ++face)
    {
        const int base = face * face_vertices;
        for (int y = 0; y < grid - 1; ++y)
        {
            for (int x = 0; x < grid - 1; ++x)
            {
                const unsigned int a = (unsigned int)(base + y * grid + x);
                const unsigned int b = a + 1U;
                const unsigned int c = (unsigned int)(base + (y + 1) * grid + x);
                const unsigned int d = c + 1U;
                mesh->indices[ii++] = a;
                mesh->indices[ii++] = c;
                mesh->indices[ii++] = b;
                mesh->indices[ii++] = b;
                mesh->indices[ii++] = c;
                mesh->indices[ii++] = d;
            }
        }
    }
    return true;
}

static bool build_mesh_primitive_cache_entry(slayer3d_game_data_mesh_primitive_cache_entry *entry,
                                             const slayer3d_game_data_render_primitive *primitive)
{
    bool ok = false;
    mesh_primitive_cache_entry_set_key(entry, primitive);
    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        ok = build_cube_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        ok = build_sphere_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
        ok = build_capsule_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        ok = build_cylinder_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        ok = build_torus_like_mesh(primitive, &entry->mesh, true);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        ok = build_torus_like_mesh(primitive, &entry->mesh, false);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        ok = build_pyramid_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        ok = build_wedge_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
        ok = build_quad_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        ok = build_disc_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        ok = build_hemisphere_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        ok = build_rounded_box_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        ok = false;
        break;
    }
    entry->loaded = ok;
    if (!ok)
        mesh_primitive_free_mesh(&entry->mesh);
    return ok;
}

static const slayer3d_mesh *find_or_build_mesh_primitive(slayer3d_game_data_mesh_primitive_cache *cache,
                                                         const slayer3d_game_data_render_primitive *primitive)
{
    if (cache == NULL || primitive == NULL)
        return NULL;
    for (int i = 0; i < cache->count; ++i)
    {
        if (mesh_primitive_cache_entry_matches(&cache->entries[i], primitive))
        {
            ++cache->hits;
            return &cache->entries[i].mesh;
        }
    }
    if (!ensure_mesh_primitive_cache_capacity(cache, cache->count + 1))
        return NULL;
    slayer3d_game_data_mesh_primitive_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    if (!build_mesh_primitive_cache_entry(entry, primitive))
        return NULL;
    ++cache->count;
    ++cache->misses;
    return &entry->mesh;
}

static bool draw_mesh_primitive_solid(primitive_draw_context *context,
                                      const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL)
        return false;
    const slayer3d_texture2d *texture = primitive_texture(context, primitive);
    const slayer3d_mesh *cached_mesh = find_or_build_mesh_primitive(context->mesh_primitive_cache, primitive);
    if (cached_mesh != NULL)
        return slayer3d_draw_static_mesh(context->renderer, cached_mesh, cached_mesh->uvs != NULL ? texture : NULL,
                                         primitive->color);
    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        return slayer3d_draw_cube_textured(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                           slayer3d_vec3_make(0.0f, 0.0f, 0.0f), 0.0f, texture, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        return slayer3d_draw_sphere_textured(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                             primitive->rings, primitive->slices, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                             0.0f, texture, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE: {
        const float cylinder_span = SDL_max(primitive->height - primitive->radius * 2.0f, 0.0f);
        const slayer3d_vec3 start = slayer3d_vec3_make(0.0f, -cylinder_span * 0.5f, 0.0f);
        const slayer3d_vec3 end = slayer3d_vec3_make(0.0f, cylinder_span * 0.5f, 0.0f);
        return slayer3d_draw_capsule(context->renderer, start, end, primitive->radius, primitive->slices,
                                     primitive->rings, primitive->color);
    }
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
        return slayer3d_draw_cylinder(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius_top,
                                      primitive->radius_bottom, primitive->height, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        return slayer3d_draw_cylinder(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius_top,
                                      primitive->radius_bottom, primitive->height, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        return slayer3d_draw_torus(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->major_radius,
                                   primitive->minor_radius, primitive->slices, primitive->tube_segments,
                                   primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        return slayer3d_draw_pyramid(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                     primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        return slayer3d_draw_wedge(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                   primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
        return slayer3d_draw_quad(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                  (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        return slayer3d_draw_disc(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                  primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        return slayer3d_draw_hemisphere(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                        primitive->rings, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        return slayer3d_draw_rounded_box(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                         primitive->bevel_radius, primitive->rings, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return slayer3d_draw_tube_segment(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                          primitive->major_radius, primitive->minor_radius, primitive->arc_angle,
                                          primitive->slices, primitive->tube_segments, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
        return slayer3d_draw_arrow(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                   primitive->height, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
        return slayer3d_draw_quad(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                  (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return false;
    }
}

static bool draw_mesh_primitive_wires(primitive_draw_context *context,
                                      const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL)
        return false;
    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        return slayer3d_draw_cube_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                        primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        return slayer3d_draw_sphere_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                          primitive->rings, primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE: {
        const float cylinder_span = SDL_max(primitive->height - primitive->radius * 2.0f, 0.0f);
        const slayer3d_vec3 start = slayer3d_vec3_make(0.0f, -cylinder_span * 0.5f, 0.0f);
        const slayer3d_vec3 end = slayer3d_vec3_make(0.0f, cylinder_span * 0.5f, 0.0f);
        return slayer3d_draw_capsule_wires(context->renderer, start, end, primitive->radius, primitive->slices,
                                           primitive->rings, primitive->wire_color);
    }
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
        return slayer3d_draw_cylinder_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                            primitive->radius_top, primitive->radius_bottom, primitive->height,
                                            primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        return slayer3d_draw_cylinder_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                            primitive->radius_top, primitive->radius_bottom, primitive->height,
                                            primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        return slayer3d_draw_torus_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                         primitive->major_radius, primitive->minor_radius, primitive->slices,
                                         primitive->tube_segments, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        return slayer3d_draw_pyramid_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                           primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        return slayer3d_draw_wedge_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                         primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
        return slayer3d_draw_quad_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                        (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        return slayer3d_draw_disc_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                        primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        return slayer3d_draw_hemisphere_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                              primitive->radius, primitive->rings, primitive->slices,
                                              primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        return slayer3d_draw_rounded_box_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                               primitive->bevel_radius, primitive->rings, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return slayer3d_draw_tube_segment_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                primitive->major_radius, primitive->minor_radius, primitive->arc_angle,
                                                primitive->slices, primitive->tube_segments, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
        return slayer3d_draw_arrow_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                         primitive->height, primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
        return slayer3d_draw_quad_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                        (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return false;
    }
}

static bool draw_mesh_primitive(primitive_draw_context *context, const slayer3d_game_data_render_primitive *primitive)
{
    if (!slayer3d_push_matrix(context->renderer))
        return false;
    bool ok =
        slayer3d_translate(context->renderer, primitive->position.x, primitive->position.y, primitive->position.z);
    const float axis_length_sq = primitive->rotation_axis.x * primitive->rotation_axis.x +
                                 primitive->rotation_axis.y * primitive->rotation_axis.y +
                                 primitive->rotation_axis.z * primitive->rotation_axis.z;
    if (ok && axis_length_sq > 0.000001f && SDL_fabsf(primitive->rotation_angle) > 0.000001f)
        ok = slayer3d_rotate(context->renderer, primitive->rotation_axis, primitive->rotation_angle);
    if (ok && primitive->draw_mode != SLAYER3D_GAME_DATA_RENDER_DRAW_WIRE)
        ok = draw_mesh_primitive_solid(context, primitive);
    if (ok && primitive->draw_mode != SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID)
        ok = draw_mesh_primitive_wires(context, primitive);
    const bool pop_ok = slayer3d_pop_matrix(context->renderer);
    return ok && pop_ok;
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
    apply_primitive_lod(context, &resolved);
    primitive = &resolved;
    if (primitive_sphere_can_batch(primitive))
        return append_sphere_draw_batch(context, primitive);
    if (!flush_sphere_draw_batch(context))
        return false;

    const bool restore_lighting = slayer3d_is_lighting_enabled(context->renderer);
    if (!primitive->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, false);
    slayer3d_set_emissive(context->renderer, primitive->emissive_color.x, primitive->emissive_color.y,
                          primitive->emissive_color.z);
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
    {
        const slayer3d_texture2d *texture = primitive_texture(context, primitive);
        if (!slayer3d_draw_cube_textured(context->renderer, primitive->position, primitive->size,
                                         primitive->rotation_axis, primitive->rotation_angle, texture,
                                         primitive->color))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE)
    {
        const slayer3d_texture2d *texture = primitive_texture(context, primitive);
        if (!slayer3d_draw_sphere_textured(context->renderer, primitive->position, primitive->radius, primitive->rings,
                                           primitive->slices, primitive->rotation_axis, primitive->rotation_angle,
                                           texture, primitive->color))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
    {
        if (!draw_mesh_primitive(context, primitive))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        if (!draw_sphere_batch(context->renderer, primitive))
            return false;
    }
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPRITE)
    {
        if (context->sprite_cache == NULL)
            return true;
        slayer3d_game_data_sprite_cache_entry *entry =
            find_or_load_sprite_entry(context->runtime, context->sprite_cache, primitive->sprite_asset);
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
            find_or_load_model_entry(context->runtime, context->model_cache, primitive->model_asset);
        if (entry == NULL)
            return false;
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

static bool draw_ui_text(void *userdata, const slayer3d_game_data_ui_text *text)
{
    ui_draw_context *draw = (ui_draw_context *)userdata;
    if (draw == NULL || text == NULL)
        return false;

    slayer3d_game_data_ui_text resolved;
    bool visible = false;
    if (!slayer3d_game_data_resolve_ui_text(draw->runtime, text, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    char content[128];
    if (!slayer3d_game_data_format_ui_text(draw->runtime, &resolved, draw->metrics, content, sizeof(content)))
        return true;

    slayer3d_font *font = find_or_load_font(draw->runtime, draw->font_cache, resolved.font);
    if (font == NULL)
    {
        draw->ok = false;
        return true;
    }

    slayer3d_color color = resolved.color;
    if (resolved.pulse_alpha)
    {
        const float pulse = 0.5f + 0.5f * SDL_sinf(draw->pulse_phase * SDL_PI_F * 2.0f);
        const float alpha = (120.0f + pulse * 135.0f) / 255.0f;
        color.a = (Uint8)SDL_clamp((int)((float)color.a * alpha + 0.5f), 0, 255);
    }

    const int width = slayer3d_get_render_context_width(draw->renderer);
    const int height = slayer3d_get_render_context_height(draw->renderer);
    const float scale = resolved.scale > 0.0f ? resolved.scale : 1.0f;
    float x = resolved.normalized ? resolved.x * (float)width : resolved.x;
    const float y = resolved.normalized ? resolved.y * (float)height : resolved.y;
    if (resolved.align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER || resolved.centered)
    {
        float text_w = 0.0f;
        float text_h = 0.0f;
        slayer3d_measure_text(font, content, &text_w, &text_h);
        x -= text_w * scale * 0.5f;
    }
    else if (resolved.align == SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT)
    {
        float text_w = 0.0f;
        float text_h = 0.0f;
        slayer3d_measure_text(font, content, &text_w, &text_h);
        x -= text_w * scale;
    }

    if (!slayer3d_draw_text_overlay_scaled(draw->renderer, font, content, x, y, scale, color))
        draw->ok = false;
    return true;
}

static void resolve_ui_image_rect(const slayer3d_game_data_ui_image *image, const slayer3d_texture2d *texture,
                                  int width, int height, float *out_x, float *out_y, float *out_w, float *out_h)
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
    if (image->align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER)
        x -= w * 0.5f;
    else if (image->align == SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT)
        x -= w;
    if (image->valign == SLAYER3D_GAME_DATA_UI_VALIGN_CENTER)
        y -= h * 0.5f;
    else if (image->valign == SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM)
        y -= h;

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

static bool draw_ui_image(void *userdata, const slayer3d_game_data_ui_image *image)
{
    ui_image_draw_context *draw = (ui_image_draw_context *)userdata;
    if (draw == NULL || image == NULL)
        return false;

    slayer3d_game_data_ui_image resolved;
    bool visible = false;
    if (!slayer3d_game_data_resolve_ui_image(draw->runtime, image, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    slayer3d_game_data_image_cache_entry *entry =
        find_or_load_image_entry(draw->runtime, draw->image_cache, resolved.image);
    if (entry == NULL)
    {
        draw->ok = false;
        return true;
    }
    slayer3d_texture2d *texture = &entry->texture;

    const int width = slayer3d_get_render_context_width(draw->renderer);
    const int height = slayer3d_get_render_context_height(draw->renderer);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    resolve_ui_image_rect(&resolved, texture, width, height, &x, &y, &w, &h);
    const char *effect_name = resolved.effect != NULL ? resolved.effect : entry->effect;
    const slayer3d_overlay_effect effect = ui_image_effect_from_name(effect_name);
    const float effect_progress =
        effect != SLAYER3D_OVERLAY_EFFECT_NONE && draw->render_eval != NULL
            ? SDL_clamp((draw->render_eval->time - entry->effect_delay) / SDL_max(entry->effect_duration, 0.0001f),
                        0.0f, 1.0f)
            : 0.0f;
    const Uint32 effect_seed = ui_image_hash_string(resolved.name);
    const bool has_custom_shader = (entry->shader_vertex_source != NULL && entry->shader_vertex_source[0] != '\0') ||
                                   (entry->shader_fragment_source != NULL && entry->shader_fragment_source[0] != '\0');
    const bool drawn = has_custom_shader
                           ? slayer3d_draw_texture_overlay_shader(
                                 draw->renderer, texture, x, y, w, h, resolved.color, effect, effect_progress,
                                 effect_seed, entry->shader_vertex_source, entry->shader_fragment_source)
                           : slayer3d_draw_texture_overlay(draw->renderer, texture, x, y, w, h, resolved.color, effect,
                                                           effect_progress, effect_seed);
    if (!drawn)
        draw->ok = false;
    return true;
}

static void resolve_ui_rect_rect(const slayer3d_game_data_ui_rect *rect, int width, int height, float *out_x,
                                 float *out_y, float *out_w, float *out_h)
{
    float w = rect->normalized ? rect->w * (float)width : rect->w;
    float h = rect->normalized ? rect->h * (float)height : rect->h;
    const float scale = rect->scale > 0.0f ? rect->scale : 1.0f;
    w *= scale;
    h *= scale;

    float x = rect->normalized ? rect->x * (float)width : rect->x;
    float y = rect->normalized ? rect->y * (float)height : rect->y;
    if (rect->align == SLAYER3D_GAME_DATA_UI_ALIGN_CENTER)
        x -= w * 0.5f;
    else if (rect->align == SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT)
        x -= w;
    if (rect->valign == SLAYER3D_GAME_DATA_UI_VALIGN_CENTER)
        y -= h * 0.5f;
    else if (rect->valign == SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM)
        y -= h;

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

static bool draw_ui_rect(void *userdata, const slayer3d_game_data_ui_rect *rect)
{
    ui_rect_draw_context *draw = (ui_rect_draw_context *)userdata;
    if (draw == NULL || rect == NULL)
        return false;

    slayer3d_game_data_ui_rect resolved;
    bool visible = false;
    if (!slayer3d_game_data_resolve_ui_rect(draw->runtime, rect, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    slayer3d_color color = resolved.color;
    if (resolved.pulse_alpha)
    {
        const float time = draw->render_eval != NULL ? draw->render_eval->time : 0.0f;
        const float pulse = 0.5f + 0.5f * SDL_sinf(time * SDL_max(resolved.pulse_rate, 0.0f) * SDL_PI_F * 2.0f);
        const float alpha = resolved.pulse_min + (resolved.pulse_max - resolved.pulse_min) * pulse;
        color.a = (Uint8)SDL_clamp((int)((float)color.a * SDL_clamp(alpha, 0.0f, 1.0f) + 0.5f), 0, 255);
    }
    if (color.a == 0)
        return true;

    const int width = slayer3d_get_render_context_width(draw->renderer);
    const int height = slayer3d_get_render_context_height(draw->renderer);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    resolve_ui_rect_rect(&resolved, width, height, &x, &y, &w, &h);
    if (!slayer3d_draw_rect_overlay(draw->renderer, x, y, w, h, color))
        draw->ok = false;
    return true;
}

static bool update_particle(void *userdata, const slayer3d_game_data_particle_emitter *emitter)
{
    particle_update_context *context = (particle_update_context *)userdata;
    if (context == NULL || context->cache == NULL || emitter == NULL)
        return false;

    slayer3d_game_data_particle_cache_entry *entry = find_or_create_particle_entry(context->cache, emitter);
    if (entry == NULL)
    {
        context->ok = false;
        return true;
    }

    if (!slayer3d_particle_emitter_set_config(entry->emitter, &emitter->config))
    {
        context->ok = false;
        return true;
    }
    entry->view_space = emitter->view_space;
    entry->draw_emissive = emitter->draw_emissive;
    entry->visible = true;
    slayer3d_particle_emitter_update(entry->emitter, context->dt);
    return true;
}

void slayer3d_game_data_font_cache_init(slayer3d_game_data_font_cache *cache, const char *media_dir)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->media_dir = media_dir;
}

void slayer3d_game_data_font_cache_free(slayer3d_game_data_font_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
        slayer3d_free_font(&cache->fonts[i]);
    SDL_free(cache->fonts);
    SDL_free(cache->font_ids);
    SDL_zero(*cache);
}

void slayer3d_game_data_image_cache_init(slayer3d_game_data_image_cache *cache, slayer3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void slayer3d_game_data_image_cache_free(slayer3d_game_data_image_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            slayer3d_free_texture(&cache->entries[i].texture);
        SDL_free(cache->entries[i].shader_vertex_source);
        SDL_free(cache->entries[i].shader_fragment_source);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

void slayer3d_game_data_sprite_cache_init(slayer3d_game_data_sprite_cache *cache, slayer3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void slayer3d_game_data_sprite_cache_free(slayer3d_game_data_sprite_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            slayer3d_sprite_asset_free(&cache->entries[i].sprite);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

void slayer3d_game_data_model_cache_init(slayer3d_game_data_model_cache *cache, slayer3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void slayer3d_game_data_model_cache_free(slayer3d_game_data_model_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            slayer3d_free_model(&cache->entries[i].model);
    }
    for (int i = 0; i < cache->pose_capacity; ++i)
        SDL_free(cache->pose_entries[i].joint_matrices);
    SDL_free(cache->pose_entries);
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

void slayer3d_game_data_mesh_primitive_cache_init(slayer3d_game_data_mesh_primitive_cache *cache)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
}

void slayer3d_game_data_mesh_primitive_cache_free(slayer3d_game_data_mesh_primitive_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
        mesh_primitive_free_mesh(&cache->entries[i].mesh);
    SDL_free(cache->entries);
    SDL_zero(*cache);
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
    ok = flush_sphere_draw_batch(&context) && ok;
    SDL_free(context.sphere_batch_positions);
    return ok;
}

static bool draw_sector_level_instance(void *userdata, const slayer3d_game_data_sector_level_instance *instance)
{
    sector_level_draw_context *context = (sector_level_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || instance == NULL || instance->level == NULL)
        return false;

    slayer3d_visibility_result vis;
    SDL_zero(vis);
    const slayer3d_visibility_result *vis_ptr = NULL;
    if (instance->portal_culling && context->camera != NULL && instance->sectors != NULL && instance->sector_count > 0)
    {
        if (context->sector_visible_capacity < instance->sector_count)
        {
            bool *visible =
                (bool *)SDL_realloc(context->sector_visible, (size_t)instance->sector_count * sizeof(*visible));
            if (visible == NULL)
            {
                context->ok = false;
                return false;
            }
            context->sector_visible = visible;
            context->sector_visible_capacity = instance->sector_count;
        }
        vis.sector_visible = context->sector_visible;
        slayer3d_camera3d local_camera = *context->camera;
        local_camera.position.x -= instance->position.x;
        local_camera.position.y -= instance->position.y;
        local_camera.position.z -= instance->position.z;
        local_camera.target.x -= instance->position.x;
        local_camera.target.y -= instance->position.y;
        local_camera.target.z -= instance->position.z;
        slayer3d_level_compute_visibility_from_camera(
            instance->level, instance->sectors, &local_camera, slayer3d_get_render_context_width(context->renderer),
            slayer3d_get_render_context_height(context->renderer), 0.01f, 1000.0f, &vis);
        vis_ptr = &vis;
    }

    bool pushed = false;
    if (instance->position.x != 0.0f || instance->position.y != 0.0f || instance->position.z != 0.0f)
    {
        if (!slayer3d_push_matrix(context->renderer))
        {
            context->ok = false;
            return false;
        }
        pushed = true;
        if (!slayer3d_translate(context->renderer, instance->position.x, instance->position.y, instance->position.z))
        {
            context->ok = false;
            if (pushed)
                (void)slayer3d_pop_matrix(context->renderer);
            return false;
        }
    }

    const bool drawn = slayer3d_draw_level_with_assets(context->renderer, context->assets, instance->level, vis_ptr,
                                                       (slayer3d_color){255, 255, 255, 255});
    if (pushed && !slayer3d_pop_matrix(context->renderer))
        context->ok = false;
    if (!drawn)
        context->ok = false;
    return context->ok;
}

bool slayer3d_game_data_draw_sector_levels(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                           const slayer3d_camera3d *camera)
{
    return slayer3d_game_data_draw_sector_levels_with_assets(runtime, renderer, NULL, camera);
}

bool slayer3d_game_data_draw_sector_levels_with_assets(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_render_context *renderer,
                                                       const slayer3d_asset_resolver *assets,
                                                       const slayer3d_camera3d *camera)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    sector_level_draw_context context;
    SDL_zero(context);
    context.renderer = renderer;
    context.assets = assets;
    context.camera = camera;
    context.ok = true;
    const bool iterated =
        slayer3d_game_data_for_each_sector_level_instance(runtime, draw_sector_level_instance, &context);
    SDL_free(context.sector_visible);
    return iterated && context.ok;
}

static bool draw_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    brush_world_draw_context *context = (brush_world_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || instance == NULL || instance->world == NULL)
        return false;
    if (instance->world->render_model == NULL || instance->world->render_model->mesh_count <= 0)
        return true;

    bool pushed = false;
    bool ok = true;
    if (instance->position.x != 0.0f || instance->position.y != 0.0f || instance->position.z != 0.0f)
    {
        if (!slayer3d_push_matrix(context->renderer))
        {
            context->ok = false;
            return false;
        }
        pushed = true;
        ok = slayer3d_translate(context->renderer, instance->position.x, instance->position.y, instance->position.z);
    }

    const bool restore_lighting = slayer3d_is_lighting_enabled(context->renderer);
    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, false);

    if (ok)
    {
        ok = slayer3d_draw_model_ex_with_assets(
            context->renderer, context->assets, instance->world->render_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
            slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
            (slayer3d_color){255, 255, 255, 255});
    }
    if (ok && instance->debug_wireframe && !slayer3d_is_wireframe_enabled(context->renderer))
    {
        if (slayer3d_set_wireframe_enabled(context->renderer, true))
        {
            ok = slayer3d_draw_model_ex_with_assets(
                context->renderer, context->assets, instance->world->render_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                (slayer3d_color){30, 35, 40, 220});
            if (!slayer3d_set_wireframe_enabled(context->renderer, false))
                ok = false;
        }
        else
        {
            ok = false;
        }
    }
    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, restore_lighting);
    if (pushed && !slayer3d_pop_matrix(context->renderer))
        ok = false;
    if (!ok)
        context->ok = false;
    return context->ok;
}

static Uint64 brush_model_triangle_count(const slayer3d_model *model)
{
    if (model == NULL || model->meshes == NULL || model->mesh_count <= 0)
        return 0u;
    Uint64 triangles = 0u;
    for (int mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index)
    {
        const slayer3d_mesh *mesh = &model->meshes[mesh_index];
        triangles += (Uint64)((mesh->index_count > 0 ? mesh->index_count : mesh->vertex_count) / 3);
    }
    return triangles;
}

static slayer3d_vec3 brush_bounds_sample(const slayer3d_bounding_box *bounds, int sample_index)
{
    if (bounds == NULL)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (sample_index <= 0)
    {
        return slayer3d_vec3_scale(slayer3d_vec3_add(bounds->min, bounds->max), 0.5f);
    }
    const int corner = sample_index - 1;
    return slayer3d_vec3_make((corner & 1) != 0 ? bounds->max.x : bounds->min.x,
                              (corner & 2) != 0 ? bounds->max.y : bounds->min.y,
                              (corner & 4) != 0 ? bounds->max.z : bounds->min.z);
}

static bool point_inside_bounds(slayer3d_vec3 point, const slayer3d_bounding_box *bounds)
{
    if (bounds == NULL)
        return false;
    return point.x >= bounds->min.x && point.x <= bounds->max.x && point.y >= bounds->min.y &&
           point.y <= bounds->max.y && point.z >= bounds->min.z && point.z <= bounds->max.z;
}

static bool brush_visibility_sample_occluded(const slayer3d_game_data_brush_world_instance *instance,
                                             const slayer3d_game_data_brush *brush, slayer3d_vec3 local_camera,
                                             slayer3d_vec3 sample)
{
    if (instance == NULL || instance->world == NULL || brush == NULL)
        return false;
    if (slayer3d_vec3_length_squared(slayer3d_vec3_sub(sample, local_camera)) <= 0.0001f)
        return false;

    slayer3d_game_data_brush_trace_desc trace;
    SDL_zero(trace);
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER |
                          SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA | SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY;
    trace.start = local_camera;
    trace.end = sample;

    slayer3d_game_data_brush_trace_result result;
    if (!slayer3d_game_data_brush_world_trace_local_with_diagnostics(instance->world, &trace,
                                                                     instance->acceleration_enabled, &result, NULL) ||
        !result.hit)
    {
        return false;
    }
    if (brush->name != NULL && result.brush_name != NULL && SDL_strcmp(brush->name, result.brush_name) == 0)
        return false;
    return result.fraction < 0.995f;
}

static bool brush_occluded_from_camera(const slayer3d_game_data_brush_world_instance *instance,
                                       const slayer3d_game_data_brush *brush, const slayer3d_camera3d *camera)
{
    if (instance == NULL || brush == NULL || camera == NULL || !brush->has_bounds)
        return false;
    const slayer3d_vec3 local_camera = slayer3d_vec3_sub(camera->position, instance->position);
    if (point_inside_bounds(local_camera, &brush->bounds))
        return false;

    for (int sample_index = 0; sample_index < 9; ++sample_index)
    {
        const slayer3d_vec3 sample = brush_bounds_sample(&brush->bounds, sample_index);
        if (!brush_visibility_sample_occluded(instance, brush, local_camera, sample))
            return false;
    }
    return true;
}

static bool brush_visibility_grid_cell(const brush_world_runtime *world_runtime, slayer3d_vec3 point, int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (world_runtime == NULL || world_runtime->visibility_grid_solid == NULL ||
        world_runtime->visibility_grid_cell_count <= 0 || world_runtime->visibility_cell_size <= 0.0f)
    {
        return false;
    }
    const float cell_size = world_runtime->visibility_cell_size;
    const int x = (int)SDL_floorf((point.x - world_runtime->visibility_grid_bounds.min.x) / cell_size);
    const int y = (int)SDL_floorf((point.y - world_runtime->visibility_grid_bounds.min.y) / cell_size);
    const int z = (int)SDL_floorf((point.z - world_runtime->visibility_grid_bounds.min.z) / cell_size);
    if (x < 0 || y < 0 || z < 0 || x >= world_runtime->visibility_grid_dim_x ||
        y >= world_runtime->visibility_grid_dim_y || z >= world_runtime->visibility_grid_dim_z)
    {
        return false;
    }
    const int index = x + y * world_runtime->visibility_grid_dim_x +
                      z * world_runtime->visibility_grid_dim_x * world_runtime->visibility_grid_dim_y;
    if (index < 0 || index >= world_runtime->visibility_grid_cell_count)
        return false;
    if (out_index != NULL)
        *out_index = index;
    return true;
}

static void brush_visibility_grid_cell_coords(const brush_world_runtime *world_runtime, int index, int *out_x,
                                              int *out_y, int *out_z)
{
    const int dim_x = world_runtime != NULL ? world_runtime->visibility_grid_dim_x : 1;
    const int dim_y = world_runtime != NULL ? world_runtime->visibility_grid_dim_y : 1;
    const int z = index / (dim_x * dim_y);
    const int rem = index - z * dim_x * dim_y;
    const int y = rem / dim_x;
    const int x = rem - y * dim_x;
    if (out_x != NULL)
        *out_x = x;
    if (out_y != NULL)
        *out_y = y;
    if (out_z != NULL)
        *out_z = z;
}

static slayer3d_vec3 brush_visibility_grid_cell_center(const brush_world_runtime *world_runtime, int x, int y, int z)
{
    const float cell_size = world_runtime->visibility_cell_size;
    return slayer3d_vec3_make(world_runtime->visibility_grid_bounds.min.x + ((float)x + 0.5f) * cell_size,
                              world_runtime->visibility_grid_bounds.min.y + ((float)y + 0.5f) * cell_size,
                              world_runtime->visibility_grid_bounds.min.z + ((float)z + 0.5f) * cell_size);
}

static bool brush_visibility_grid_ray_clear(const brush_world_runtime *world_runtime, int start_index,
                                            slayer3d_vec3 local_camera, slayer3d_vec3 target)
{
    if (world_runtime == NULL || world_runtime->visibility_grid_solid == NULL)
        return false;
    const slayer3d_vec3 delta = slayer3d_vec3_sub(target, local_camera);
    const float distance = slayer3d_vec3_length(delta);
    if (distance <= 0.0001f)
        return true;
    const float cell_size = world_runtime->visibility_cell_size;
    const int steps = SDL_max(1, SDL_min(4096, (int)SDL_ceilf(distance / SDL_max(cell_size * 0.35f, 0.05f))));
    for (int step = 1; step <= steps; ++step)
    {
        const float t = (float)step / (float)steps;
        const slayer3d_vec3 point = slayer3d_vec3_add(local_camera, slayer3d_vec3_scale(delta, t));
        int index = -1;
        if (!brush_visibility_grid_cell(world_runtime, point, &index))
            return false;
        if (index == start_index)
            continue;
        if (world_runtime->visibility_grid_solid[index])
            return false;
    }
    return true;
}

static void brush_visibility_grid_mark_neighborhood_visible(const brush_world_runtime *world_runtime, int start,
                                                            Uint8 *visible_cells)
{
    int sx = 0;
    int sy = 0;
    int sz = 0;
    brush_visibility_grid_cell_coords(world_runtime, start, &sx, &sy, &sz);
    for (int z = sz - 1; z <= sz + 1; ++z)
    {
        for (int y = sy - 1; y <= sy + 1; ++y)
        {
            for (int x = sx - 1; x <= sx + 1; ++x)
            {
                if (x < 0 || y < 0 || z < 0 || x >= world_runtime->visibility_grid_dim_x ||
                    y >= world_runtime->visibility_grid_dim_y || z >= world_runtime->visibility_grid_dim_z)
                {
                    continue;
                }
                const int index = x + y * world_runtime->visibility_grid_dim_x +
                                  z * world_runtime->visibility_grid_dim_x * world_runtime->visibility_grid_dim_y;
                if (index >= 0 && index < world_runtime->visibility_grid_cell_count &&
                    !world_runtime->visibility_grid_solid[index])
                {
                    visible_cells[index] = 1u;
                }
            }
        }
    }
}

static bool brush_visibility_grid_mark_visible_los(const brush_world_runtime *world_runtime, slayer3d_vec3 local_camera,
                                                   Uint8 *visible_cells)
{
    int start = -1;
    if (world_runtime == NULL || visible_cells == NULL ||
        !brush_visibility_grid_cell(world_runtime, local_camera, &start) || start < 0 ||
        world_runtime->visibility_grid_solid[start])
    {
        return false;
    }

    static const int max_los_cells = 131072;
    if (world_runtime->visibility_grid_cell_count > max_los_cells)
        return false;

    visible_cells[start] = 1u;
    brush_visibility_grid_mark_neighborhood_visible(world_runtime, start, visible_cells);
    const int dim_x = world_runtime->visibility_grid_dim_x;
    const int dim_y = world_runtime->visibility_grid_dim_y;
    const int dim_z = world_runtime->visibility_grid_dim_z;
    for (int z = 0; z < dim_z; ++z)
    {
        for (int y = 0; y < dim_y; ++y)
        {
            for (int x = 0; x < dim_x; ++x)
            {
                const int index = x + y * dim_x + z * dim_x * dim_y;
                if (visible_cells[index] || world_runtime->visibility_grid_solid[index])
                    continue;
                const slayer3d_vec3 target = brush_visibility_grid_cell_center(world_runtime, x, y, z);
                if (brush_visibility_grid_ray_clear(world_runtime, start, local_camera, target))
                    visible_cells[index] = 1u;
            }
        }
    }
    return true;
}

static bool brush_visibility_grid_mark_visible(const brush_world_runtime *world_runtime, slayer3d_vec3 local_camera,
                                               Uint8 *visible_cells)
{
    return brush_visibility_grid_mark_visible_los(world_runtime, local_camera, visible_cells);
}

static const Uint8 *brush_visibility_grid_visible_cells(brush_world_runtime *world_runtime, slayer3d_vec3 local_camera,
                                                        slayer3d_game_data_brush_diagnostics *diagnostics)
{
    int start = -1;
    if (world_runtime == NULL || world_runtime->visibility_grid_solid == NULL ||
        world_runtime->visibility_grid_cell_count <= 0 ||
        !brush_visibility_grid_cell(world_runtime, local_camera, &start) || start < 0 ||
        world_runtime->visibility_grid_solid[start])
    {
        return NULL;
    }

    if (world_runtime->visibility_grid_visible_cache != NULL &&
        world_runtime->visibility_grid_visible_cache_start == start)
    {
        if (diagnostics != NULL)
            ++diagnostics->visibility_grid_cache_hits;
        return world_runtime->visibility_grid_visible_cache;
    }

    if (diagnostics != NULL)
        ++diagnostics->visibility_grid_cache_misses;
    if (world_runtime->visibility_grid_visible_cache == NULL)
    {
        world_runtime->visibility_grid_visible_cache = (Uint8 *)SDL_calloc(
            (size_t)world_runtime->visibility_grid_cell_count, sizeof(*world_runtime->visibility_grid_visible_cache));
        if (world_runtime->visibility_grid_visible_cache == NULL)
            return NULL;
    }
    SDL_memset(world_runtime->visibility_grid_visible_cache, 0,
               (size_t)world_runtime->visibility_grid_cell_count *
                   sizeof(*world_runtime->visibility_grid_visible_cache));
    world_runtime->visibility_grid_visible_cache_start = -1;
    if (!brush_visibility_grid_mark_visible(world_runtime, local_camera, world_runtime->visibility_grid_visible_cache))
        return NULL;
    world_runtime->visibility_grid_visible_cache_start = start;
    return world_runtime->visibility_grid_visible_cache;
}

static bool brush_visibility_forced_visible(const slayer3d_game_data_brush *brush)
{
    return brush != NULL && brush->visibility == SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_ALWAYS;
}

static bool brush_visibility_trace_fallback_enabled(const slayer3d_game_data_brush *brush)
{
    return brush != NULL &&
           (brush->visibility == SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_TRACE || brush->visibility_cullable);
}

static bool brush_visible_from_visibility_grid(const brush_world_runtime *world_runtime,
                                               const slayer3d_game_data_brush *brush, const Uint8 *visible_cells)
{
    if (world_runtime == NULL || brush == NULL || visible_cells == NULL || !brush->has_bounds ||
        world_runtime->visibility_cell_size <= 0.0f)
    {
        return true;
    }
    const float cell_size = world_runtime->visibility_cell_size;
    const float expand = cell_size * 0.5f;
    const int min_x = SDL_max(
        0, (int)SDL_floorf((brush->bounds.min.x - expand - world_runtime->visibility_grid_bounds.min.x) / cell_size));
    const int min_y = SDL_max(
        0, (int)SDL_floorf((brush->bounds.min.y - expand - world_runtime->visibility_grid_bounds.min.y) / cell_size));
    const int min_z = SDL_max(
        0, (int)SDL_floorf((brush->bounds.min.z - expand - world_runtime->visibility_grid_bounds.min.z) / cell_size));
    const int max_x = SDL_min(
        world_runtime->visibility_grid_dim_x - 1,
        (int)SDL_floorf((brush->bounds.max.x + expand - world_runtime->visibility_grid_bounds.min.x) / cell_size));
    const int max_y = SDL_min(
        world_runtime->visibility_grid_dim_y - 1,
        (int)SDL_floorf((brush->bounds.max.y + expand - world_runtime->visibility_grid_bounds.min.y) / cell_size));
    const int max_z = SDL_min(
        world_runtime->visibility_grid_dim_z - 1,
        (int)SDL_floorf((brush->bounds.max.z + expand - world_runtime->visibility_grid_bounds.min.z) / cell_size));
    if (min_x > max_x || min_y > max_y || min_z > max_z)
        return true;

    const int dim_x = world_runtime->visibility_grid_dim_x;
    const int dim_y = world_runtime->visibility_grid_dim_y;
    for (int z = min_z; z <= max_z; ++z)
    {
        for (int y = min_y; y <= max_y; ++y)
        {
            for (int x = min_x; x <= max_x; ++x)
            {
                const int index = x + y * dim_x + z * dim_x * dim_y;
                if (index >= 0 && index < world_runtime->visibility_grid_cell_count && visible_cells[index])
                    return true;
            }
        }
    }
    return false;
}

static bool apply_brush_visibility_grid(brush_world_runtime *world_runtime,
                                        const slayer3d_game_data_brush_world_instance *instance,
                                        const slayer3d_camera3d *camera, bool *brush_visible, int brush_count,
                                        int *out_occluded_count, slayer3d_game_data_runtime *mutable_runtime)
{
    if (out_occluded_count != NULL)
        *out_occluded_count = 0;
    if (world_runtime == NULL || instance == NULL || camera == NULL || brush_visible == NULL ||
        mutable_runtime == NULL || world_runtime->visibility_grid_solid == NULL ||
        world_runtime->visibility_grid_cell_count <= 0)
    {
        return false;
    }
    const slayer3d_vec3 local_camera = slayer3d_vec3_sub(camera->position, instance->position);
    const Uint8 *visible_cells =
        brush_visibility_grid_visible_cells(world_runtime, local_camera, &mutable_runtime->brush_diagnostics);
    if (visible_cells == NULL)
        return false;

    int occluded_count = 0;
    for (int brush_index = 0; brush_index < brush_count; ++brush_index)
    {
        const slayer3d_model *brush_model = &world_runtime->brush_render_models[brush_index];
        const slayer3d_game_data_brush *brush = &instance->world->brushes[brush_index];
        const Uint64 triangles = brush_model_triangle_count(brush_model);
        if (triangles == 0u)
            continue;
        if (brush_visibility_forced_visible(brush))
            continue;

        ++mutable_runtime->brush_diagnostics.visibility_brush_candidates;
        if (brush_visible_from_visibility_grid(world_runtime, brush, visible_cells) ||
            !brush_occluded_from_camera(instance, brush, camera))
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_visible;
        }
        else
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_occluded;
            mutable_runtime->brush_diagnostics.visibility_triangles_culled += triangles;
            brush_visible[brush_index] = false;
            ++occluded_count;
        }
    }
    if (out_occluded_count != NULL)
        *out_occluded_count = occluded_count;
    return true;
}

static bool brush_render_chunk_all_visible(const slayer3d_game_data_brush_compile_chunk *chunk,
                                           const bool *brush_visible, int brush_count)
{
    if (chunk == NULL || brush_visible == NULL || chunk->brush_count <= 1)
        return false;
    for (int i = 0; i < chunk->brush_count; ++i)
    {
        const int brush_index = chunk->brush_indices[i];
        if (brush_index < 0 || brush_index >= brush_count || !brush_visible[brush_index])
            return false;
    }
    return true;
}

static void brush_render_chunk_mark_drawn(const slayer3d_game_data_brush_compile_chunk *chunk, bool *brush_visible,
                                          int brush_count)
{
    if (chunk == NULL || brush_visible == NULL)
        return;
    for (int i = 0; i < chunk->brush_count; ++i)
    {
        const int brush_index = chunk->brush_indices[i];
        if (brush_index >= 0 && brush_index < brush_count)
            brush_visible[brush_index] = false;
    }
}

static bool draw_visible_brush_chunks(brush_world_draw_context *context, brush_world_runtime *world_runtime,
                                      bool *brush_visible, int brush_count)
{
    if (context == NULL || context->renderer == NULL || context->runtime == NULL || world_runtime == NULL ||
        world_runtime->chunk_render_models == NULL || world_runtime->desc.compile_chunks == NULL)
    {
        return true;
    }

    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)context->runtime;
    const int chunk_count = SDL_min(world_runtime->desc.compile_chunk_count, world_runtime->chunk_render_model_count);
    for (int chunk_index = 0; chunk_index < chunk_count; ++chunk_index)
    {
        const slayer3d_game_data_brush_compile_chunk *chunk = &world_runtime->desc.compile_chunks[chunk_index];
        if (!brush_render_chunk_all_visible(chunk, brush_visible, brush_count))
            continue;
        const slayer3d_model *chunk_model = &world_runtime->chunk_render_models[chunk_index];
        if (brush_model_triangle_count(chunk_model) == 0u)
            continue;
        if (!slayer3d_draw_model_ex_with_assets(
                context->renderer, context->assets, chunk_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                (slayer3d_color){255, 255, 255, 255}))
        {
            return false;
        }
        ++mutable_runtime->brush_diagnostics.render_chunk_draws;
        mutable_runtime->brush_diagnostics.render_chunk_brushes_drawn += (Uint64)chunk->brush_count;
        brush_render_chunk_mark_drawn(chunk, brush_visible, brush_count);
    }
    return true;
}

static bool draw_brush_world_instance_with_visibility(void *userdata,
                                                      const slayer3d_game_data_brush_world_instance *instance)
{
    brush_world_draw_context *context = (brush_world_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || context->runtime == NULL || instance == NULL ||
        instance->world == NULL)
        return false;
    if (!instance->visibility_occlusion_enabled || context->camera == NULL)
        return draw_brush_world_instance(userdata, instance);
    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)context->runtime;
    brush_world_runtime *world_runtime =
        (brush_world_runtime *)find_brush_world_runtime(mutable_runtime, instance->world_name);
    if (world_runtime == NULL || world_runtime->brush_render_models == NULL || instance->world->brushes == NULL ||
        instance->world->brush_count <= 0)
    {
        return draw_brush_world_instance(userdata, instance);
    }
    const int brush_count = SDL_min(instance->world->brush_count, world_runtime->brush_render_model_count);
    bool *brush_visible = (bool *)SDL_calloc((size_t)brush_count, sizeof(*brush_visible));
    if (brush_visible == NULL)
    {
        context->ok = false;
        return false;
    }
    for (int brush_index = 0; brush_index < brush_count; ++brush_index)
        brush_visible[brush_index] = true;

    int occluded_count = 0;
    const bool used_visibility_grid = apply_brush_visibility_grid(
        world_runtime, instance, context->camera, brush_visible, brush_count, &occluded_count, mutable_runtime);
    for (int brush_index = 0; !used_visibility_grid && brush_index < brush_count; ++brush_index)
    {
        const slayer3d_model *brush_model = &world_runtime->brush_render_models[brush_index];
        const slayer3d_game_data_brush *brush = &instance->world->brushes[brush_index];
        const Uint64 triangles = brush_model_triangle_count(brush_model);
        if (triangles == 0u)
            continue;
        if (!brush_visibility_trace_fallback_enabled(brush))
            continue;

        ++mutable_runtime->brush_diagnostics.visibility_brush_candidates;
        if (brush_occluded_from_camera(instance, brush, context->camera))
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_occluded;
            mutable_runtime->brush_diagnostics.visibility_triangles_culled += triangles;
            brush_visible[brush_index] = false;
            ++occluded_count;
        }
        else
        {
            ++mutable_runtime->brush_diagnostics.visibility_brush_visible;
        }
    }
    if (occluded_count <= 0)
    {
        SDL_free(brush_visible);
        return draw_brush_world_instance(userdata, instance);
    }

    bool pushed = false;
    bool ok = true;
    if (instance->position.x != 0.0f || instance->position.y != 0.0f || instance->position.z != 0.0f)
    {
        if (!slayer3d_push_matrix(context->renderer))
        {
            context->ok = false;
            return false;
        }
        pushed = true;
        ok = slayer3d_translate(context->renderer, instance->position.x, instance->position.y, instance->position.z);
    }

    const bool restore_lighting = slayer3d_is_lighting_enabled(context->renderer);
    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, false);

    ok = draw_visible_brush_chunks(context, world_runtime, brush_visible, brush_count);
    for (int brush_index = 0; ok && brush_index < brush_count; ++brush_index)
    {
        if (!brush_visible[brush_index])
            continue;
        const slayer3d_model *brush_model = &world_runtime->brush_render_models[brush_index];
        const Uint64 triangles = brush_model_triangle_count(brush_model);
        if (triangles == 0u)
            continue;

        ok = slayer3d_draw_model_ex_with_assets(
            context->renderer, context->assets, brush_model, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
            slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
            (slayer3d_color){255, 255, 255, 255});
    }

    if (!instance->lighting_enabled)
        slayer3d_set_lighting_enabled(context->renderer, restore_lighting);
    if (pushed && !slayer3d_pop_matrix(context->renderer))
        ok = false;
    SDL_free(brush_visible);
    if (!ok)
        context->ok = false;
    return context->ok;
}

bool slayer3d_game_data_draw_brush_worlds(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer)
{
    return slayer3d_game_data_draw_brush_worlds_with_assets(runtime, renderer, NULL);
}

bool slayer3d_game_data_draw_brush_worlds_with_assets(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_render_context *renderer,
                                                      const slayer3d_asset_resolver *assets)
{
    return slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, renderer, assets, NULL);
}

bool slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(const slayer3d_game_data_runtime *runtime,
                                                                 slayer3d_render_context *renderer,
                                                                 const slayer3d_asset_resolver *assets,
                                                                 const slayer3d_camera3d *camera)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    slayer3d_render_stats before_stats;
    SDL_zero(before_stats);
    const bool have_before_stats = slayer3d_get_render_stats(renderer, &before_stats);
    brush_world_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.assets = assets;
    context.camera = camera;
    context.ok = true;
    const bool previous_depth_prepass_scope = renderer->depth_prepass_scope_enabled;
    renderer->depth_prepass_scope_enabled = true;
    const bool iterated = slayer3d_game_data_for_each_brush_world_instance(
        runtime, camera != NULL ? draw_brush_world_instance_with_visibility : draw_brush_world_instance, &context);
    renderer->depth_prepass_scope_enabled = previous_depth_prepass_scope;
    slayer3d_render_stats after_stats;
    SDL_zero(after_stats);
    if (have_before_stats && slayer3d_get_render_stats(renderer, &after_stats))
    {
        slayer3d_game_data_accumulate_brush_render_diagnostics((slayer3d_game_data_runtime *)runtime, &before_stats,
                                                               &after_stats);
    }
    return iterated && context.ok;
}

static bool draw_active_scene_skybox(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                     slayer3d_game_data_image_cache *image_cache)
{
    slayer3d_game_data_scene_skybox skybox_desc;
    if (runtime == NULL || renderer == NULL || image_cache == NULL)
        return true;
    if (!slayer3d_game_data_get_active_scene_skybox(runtime, &skybox_desc))
        return true;

    slayer3d_game_data_image_cache_entry *pos_x = find_or_load_image_entry(runtime, image_cache, skybox_desc.pos_x);
    slayer3d_game_data_image_cache_entry *neg_x = find_or_load_image_entry(runtime, image_cache, skybox_desc.neg_x);
    slayer3d_game_data_image_cache_entry *pos_y = find_or_load_image_entry(runtime, image_cache, skybox_desc.pos_y);
    slayer3d_game_data_image_cache_entry *neg_y = find_or_load_image_entry(runtime, image_cache, skybox_desc.neg_y);
    slayer3d_game_data_image_cache_entry *pos_z = find_or_load_image_entry(runtime, image_cache, skybox_desc.pos_z);
    slayer3d_game_data_image_cache_entry *neg_z = find_or_load_image_entry(runtime, image_cache, skybox_desc.neg_z);
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

bool slayer3d_game_data_draw_ui_text(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                     slayer3d_game_data_font_cache *font_cache,
                                     const slayer3d_game_data_ui_metrics *metrics, float pulse_phase)
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

    return slayer3d_game_data_for_each_ui_text_for_metrics(runtime, metrics, draw_ui_text, &context) && context.ok;
}

bool slayer3d_game_data_draw_ui_images(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                       slayer3d_game_data_image_cache *image_cache,
                                       const slayer3d_game_data_ui_metrics *metrics,
                                       const slayer3d_game_data_render_eval *render_eval)
{
    if (runtime == NULL || renderer == NULL || image_cache == NULL)
        return false;

    ui_image_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.image_cache = image_cache;
    context.metrics = metrics;
    context.render_eval = render_eval;
    context.ok = true;

    return slayer3d_game_data_for_each_ui_image(runtime, draw_ui_image, &context) && context.ok;
}

bool slayer3d_game_data_draw_ui_rects(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                      const slayer3d_game_data_ui_metrics *metrics,
                                      const slayer3d_game_data_render_eval *render_eval)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    ui_rect_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.metrics = metrics;
    context.render_eval = render_eval;
    context.ok = true;

    return slayer3d_game_data_for_each_ui_rect(runtime, draw_ui_rect, &context) && context.ok;
}

void slayer3d_game_data_particle_cache_init(slayer3d_game_data_particle_cache *cache)
{
    if (cache != NULL)
        SDL_zero(*cache);
}

void slayer3d_game_data_particle_cache_free(slayer3d_game_data_particle_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        slayer3d_destroy_particle_emitter(cache->entries[i].emitter);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

bool slayer3d_game_data_update_particles(const slayer3d_game_data_runtime *runtime,
                                         slayer3d_game_data_particle_cache *cache, float dt)
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

    return slayer3d_game_data_for_each_particle_emitter(runtime, update_particle, &context) && context.ok;
}

static bool draw_particles_filtered(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                    slayer3d_game_data_particle_cache *cache, bool draw_world_space,
                                    bool draw_view_space)
{
    if (runtime == NULL || renderer == NULL || cache == NULL)
        return false;

    for (int i = 0; i < cache->count; ++i)
    {
        slayer3d_game_data_particle_cache_entry *entry = &cache->entries[i];
        if (!entry->visible || entry->emitter == NULL || (entry->view_space && !draw_view_space) ||
            (!entry->view_space && !draw_world_space) ||
            !slayer3d_game_data_active_scene_has_entity(runtime, entry->entity_name))
        {
            continue;
        }

        slayer3d_set_emissive(renderer, entry->draw_emissive.x, entry->draw_emissive.y, entry->draw_emissive.z);
        slayer3d_draw_particles(renderer, entry->emitter);
        slayer3d_set_emissive(renderer, 0.0f, 0.0f, 0.0f);
    }
    return true;
}

bool slayer3d_game_data_draw_particles(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                       slayer3d_game_data_particle_cache *cache)
{
    return draw_particles_filtered(runtime, renderer, cache, true, true);
}

static bool menu_input_is_idle(const slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                               const slayer3d_game_data_menu *menu)
{
    if (menu == NULL)
        return true;
    if (input == NULL)
        return false;

    return !menu_action_held(runtime, input, menu->up_action_id, "ui_up") &&
           !menu_action_held(runtime, input, menu->down_action_id, "ui_down") &&
           !menu_action_held(runtime, input, menu->left_action_id, "ui_left") &&
           !menu_action_held(runtime, input, menu->right_action_id, "ui_right") &&
           !menu_action_held(runtime, input, menu->select_action_id, "ui_accept") &&
           !menu_action_held(runtime, input, menu->back_action_id, "ui_back");
}

static void menu_result_set_scene_state(slayer3d_game_data_menu_update_result *result,
                                        const slayer3d_game_data_menu_item *item)
{
    if (result == NULL || item == NULL)
        return;
    result->scene_state_key = item->scene_state_key;
    result->scene_state_value = item->scene_state_value;
    if (item->dynamic_list_item && item->scene_state_value != NULL)
    {
        SDL_strlcpy(result->scene_state_value_storage, item->scene_state_value,
                    sizeof(result->scene_state_value_storage));
        result->scene_state_value = result->scene_state_value_storage;
    }
}

bool slayer3d_game_data_update_menus_for_metrics(slayer3d_game_data_runtime *runtime,
                                                 const slayer3d_input_manager *input, bool *input_armed,
                                                 const slayer3d_game_data_ui_metrics *metrics,
                                                 slayer3d_game_data_menu_update_result *out_result)
{
    if (out_result != NULL)
    {
        SDL_zero(*out_result);
        out_result->selected_index = -1;
        out_result->signal_id = -1;
        out_result->move_signal_id = -1;
        out_result->select_signal_id = -1;
    }
    if (runtime == NULL || input_armed == NULL)
        return false;

    slayer3d_game_data_menu menu;
    if (!slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &menu))
        return true;

    if (out_result != NULL)
    {
        out_result->menu = menu.name;
        out_result->selected_index = menu.selected_index;
    }

    if (slayer3d_game_data_menu_input_binding_capture_active(runtime))
    {
        const slayer3d_game_data_input_binding_capture_status capture_status =
            slayer3d_game_data_update_menu_input_binding_capture(runtime, input);
        if (out_result != NULL)
        {
            out_result->handled_input = true;
            out_result->input_binding_changed = capture_status == SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
            out_result->input_binding_conflict = capture_status == SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
            out_result->selected = out_result->input_binding_changed;
            out_result->select_signal_id = out_result->input_binding_changed ? menu.select_signal_id : -1;
        }
        return true;
    }
    if (slayer3d_game_data_menu_text_entry_capture_active(runtime))
    {
        const slayer3d_game_data_text_entry_capture_status capture_status =
            slayer3d_game_data_update_menu_text_entry_capture(runtime, input);
        if (out_result != NULL)
        {
            out_result->handled_input = true;
            out_result->text_entry_changed = capture_status == SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CHANGED;
            out_result->text_entry_submitted = capture_status == SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_SUBMITTED;
            out_result->text_entry_canceled = capture_status == SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED;
            out_result->control_changed = out_result->text_entry_changed;
            out_result->selected = out_result->text_entry_submitted;
            out_result->select_signal_id = out_result->text_entry_submitted ? menu.select_signal_id : -1;
        }
        return true;
    }

    if (!*input_armed)
    {
        if (menu_input_is_idle(runtime, input, &menu))
            *input_armed = true;
        return true;
    }

    bool handled = false;
    if (menu_action_pressed(runtime, input, menu.up_action_id, "ui_up"))
    {
        const bool moved = slayer3d_game_data_menu_move(runtime, menu.name, -1);
        handled = moved || handled;
        if (moved && out_result != NULL)
            out_result->move_signal_id = menu.move_signal_id;
    }
    if (menu_action_pressed(runtime, input, menu.down_action_id, "ui_down"))
    {
        const bool moved = slayer3d_game_data_menu_move(runtime, menu.name, 1);
        handled = moved || handled;
        if (moved && out_result != NULL)
            out_result->move_signal_id = menu.move_signal_id;
    }
    int control_direction = 0;
    bool control_adjust_input = false;
    if (menu_action_pressed(runtime, input, menu.left_action_id, "ui_left"))
    {
        handled = true;
        control_direction = -1;
        control_adjust_input = true;
    }
    if (menu_action_pressed(runtime, input, menu.right_action_id, "ui_right"))
    {
        handled = true;
        control_direction = 1;
        control_adjust_input = true;
    }
    if (menu_action_pressed(runtime, input, menu.select_action_id, "ui_accept"))
    {
        handled = true;
        control_direction = 1;
        control_adjust_input = false;
    }
    if (menu_action_pressed(runtime, input, menu.back_action_id, "ui_back"))
    {
        handled = true;
        slayer3d_game_data_menu refreshed;
        if (!slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            return true;

        slayer3d_game_data_menu_item item;
        bool found_back_item = false;
        for (int i = 0; i < refreshed.item_count; ++i)
        {
            if (!slayer3d_game_data_get_menu_item(runtime, refreshed.name, i, &item))
                continue;
            if (!item.return_scene && !(item.label != NULL && SDL_strcasecmp(item.label, "Back") == 0))
                continue;
            found_back_item = true;
            if (out_result != NULL)
            {
                out_result->menu = refreshed.name;
                out_result->selected_index = i;
                out_result->control_changed = false;
                out_result->selected = true;
                out_result->select_signal_id = refreshed.select_signal_id;
                out_result->quit = item.quit;
                out_result->scene = item.scene;
                out_result->return_to = item.return_to;
                menu_result_set_scene_state(out_result, &item);
                out_result->return_scene = item.return_scene;
                out_result->signal_id = item.signal_id;
                out_result->pause_command = item.pause_command;
                out_result->has_return_paused = item.has_return_paused;
                out_result->return_paused = item.return_paused;
                out_result->handled_input = true;
            }
            break;
        }
        if (!found_back_item)
        {
            if (out_result != NULL)
                out_result->handled_input = true;
        }
        return true;
    }

    if (control_direction != 0)
    {
        slayer3d_game_data_menu refreshed;
        if (!slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            return true;

        slayer3d_game_data_menu_item item;
        /* Selecting the initially highlighted dynamic-list row should publish its selected outputs. */
        (void)slayer3d_game_data_publish_menu_selection(runtime, refreshed.name);
        if (!slayer3d_game_data_get_menu_item(runtime, refreshed.name, refreshed.selected_index, &item))
            return true;

        if (out_result != NULL)
        {
            out_result->menu = refreshed.name;
            out_result->selected_index = refreshed.selected_index;
            if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            {
                out_result->input_binding_capture_started = slayer3d_game_data_start_menu_input_binding_capture(
                    runtime, refreshed.name, refreshed.selected_index);
                out_result->selected = out_result->input_binding_capture_started;
                out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
                out_result->signal_id = -1;
                out_result->quit = false;
                out_result->scene = NULL;
                out_result->return_to = NULL;
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
                out_result->return_scene = false;
                out_result->pause_command = SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
                out_result->has_return_paused = false;
                out_result->return_paused = false;
                out_result->control_changed = false;
                out_result->handled_input = true;
                return true;
            }
            if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
            {
                out_result->text_entry_capture_started =
                    slayer3d_game_data_start_menu_text_entry_capture(runtime, refreshed.name, refreshed.selected_index);
                out_result->selected = out_result->text_entry_capture_started;
                out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
                out_result->signal_id = -1;
                out_result->quit = false;
                out_result->scene = NULL;
                out_result->return_to = NULL;
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
                out_result->return_scene = false;
                out_result->pause_command = SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
                out_result->has_return_paused = false;
                out_result->return_paused = false;
                out_result->control_changed = false;
                out_result->handled_input = true;
                return true;
            }
            out_result->control_changed =
                control_adjust_input && (item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING ||
                                         item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
                    ? false
                    : slayer3d_game_data_adjust_menu_item_control(runtime, &item, control_direction);
            out_result->selected = !control_adjust_input || out_result->control_changed;
            out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
            out_result->quit = !control_adjust_input && !out_result->control_changed && item.quit;
            out_result->scene = !control_adjust_input && !out_result->control_changed ? item.scene : NULL;
            out_result->return_to = !control_adjust_input && !out_result->control_changed ? item.return_to : NULL;
            if (!control_adjust_input && !out_result->control_changed)
                menu_result_set_scene_state(out_result, &item);
            else
            {
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
            }
            out_result->return_scene = !control_adjust_input && !out_result->control_changed && item.return_scene;
            out_result->signal_id = out_result->selected ? item.signal_id : -1;
            out_result->pause_command = !control_adjust_input && !out_result->control_changed
                                            ? item.pause_command
                                            : SLAYER3D_GAME_DATA_MENU_PAUSE_NONE;
            out_result->has_return_paused =
                !control_adjust_input && !out_result->control_changed && item.has_return_paused;
            out_result->return_paused = item.return_paused;
        }
        else
        {
            if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
                (void)slayer3d_game_data_start_menu_input_binding_capture(runtime, refreshed.name,
                                                                          refreshed.selected_index);
            else if (!control_adjust_input && item.control_type == SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
                (void)slayer3d_game_data_start_menu_text_entry_capture(runtime, refreshed.name,
                                                                       refreshed.selected_index);
            else if (item.control_type != SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING &&
                     item.control_type != SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT)
                (void)slayer3d_game_data_adjust_menu_item_control(runtime, &item, control_direction);
        }
    }
    else if (handled && out_result != NULL)
    {
        slayer3d_game_data_menu refreshed;
        if (slayer3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            out_result->selected_index = refreshed.selected_index;
    }

    if (out_result != NULL)
        out_result->handled_input = handled;
    return true;
}

bool slayer3d_game_data_update_menus(slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input,
                                     bool *input_armed, slayer3d_game_data_menu_update_result *out_result)
{
    return slayer3d_game_data_update_menus_for_metrics(runtime, input, input_armed, NULL, out_result);
}

void slayer3d_game_data_frame_state_init(slayer3d_game_data_frame_state *state)
{
    if (state == NULL)
        return;
    SDL_zero(*state);
}

void slayer3d_game_data_frame_state_record_update_cpu_time(slayer3d_game_data_frame_state *state, float seconds)
{
    if (state == NULL || seconds < 0.0f)
        return;
    state->update_cpu_ms_sample_sum += seconds * 1000.0f;
}

void slayer3d_game_data_frame_state_record_render_cpu_time(slayer3d_game_data_frame_state *state, float seconds)
{
    if (state == NULL || seconds < 0.0f)
        return;
    state->render_cpu_ms_sample_sum += seconds * 1000.0f;
}

static float render_stat_delta_u64(Uint64 before, Uint64 after)
{
    return (float)(after >= before ? after - before : 0u);
}

bool slayer3d_game_data_update_frame(slayer3d_game_data_frame_state *state,
                                     const slayer3d_game_data_update_frame_desc *desc)
{
    if (state == NULL || desc == NULL || desc->ctx == NULL || desc->runtime == NULL)
        return false;

    slayer3d_game_context *ctx = desc->ctx;
    slayer3d_game_data_runtime *runtime = desc->runtime;
    const bool paused_at_start = ctx->paused;
    if (slayer3d_game_data_active_scene_update_phase(runtime, "app_flow", ctx->paused) && desc->app_flow != NULL)
    {
        if (!slayer3d_game_data_app_flow_update(desc->app_flow, ctx, runtime, desc->dt))
            return false;
    }

    if (paused_at_start && !ctx->paused)
    {
        state->was_paused = false;
        return true;
    }

    const bool pause_entered = !state->was_paused && ctx->paused;
    if (slayer3d_game_data_active_scene_update_phase(runtime, "scene_activity", ctx->paused) &&
        !slayer3d_game_data_update_scene_activity(runtime, slayer3d_game_session_get_input(ctx->session), desc->dt))
        return false;

    if (slayer3d_game_data_active_scene_update_phase(runtime, "presentation", ctx->paused))
    {
        state->time += desc->dt;
        if (!slayer3d_game_data_update_active_editor_tooling(runtime))
            return false;
        if (!slayer3d_game_data_update_presentation_clocks(runtime, desc->dt, ctx->paused, pause_entered))
            return false;
        if (!slayer3d_game_data_update_animations(runtime, desc->dt))
            return false;
        state->ui_pulse_phase = slayer3d_game_data_ui_pulse_phase(runtime, state->ui_pulse_phase);
    }
    if (slayer3d_game_data_active_scene_update_phase(runtime, "property_effects", ctx->paused) &&
        !slayer3d_game_data_update_property_effects(runtime, desc->dt))
        return false;
    if (desc->particle_cache != NULL &&
        slayer3d_game_data_active_scene_update_phase(runtime, "particles", ctx->paused) &&
        !slayer3d_game_data_update_particles(runtime, desc->particle_cache, desc->dt))
        return false;
    if (!paused_at_start && slayer3d_game_data_active_scene_update_phase(runtime, "simulation", ctx->paused) &&
        (desc->app_flow == NULL || !slayer3d_game_data_app_flow_quit_pending(desc->app_flow)))
    {
        if (!slayer3d_game_data_update(runtime, desc->dt))
            return false;
    }

    state->was_paused = ctx->paused;
    return true;
}

void slayer3d_game_data_frame_state_record_render(slayer3d_game_data_frame_state *state,
                                                  const slayer3d_game_context *ctx,
                                                  const slayer3d_game_data_runtime *runtime)
{
    if (state == NULL || ctx == NULL)
        return;

    if (state->rendered_frames > 0)
    {
        const float frame_dt = ctx->real_time - state->last_render_time;
        if (frame_dt > 0.0f)
        {
            state->fps_sample_time += frame_dt;
            state->frame_ms_sample_sum += frame_dt * 1000.0f;
            ++state->fps_sample_frames;
            if (ctx->renderer != NULL)
            {
                slayer3d_render_stats stats;
                if (slayer3d_get_render_stats(ctx->renderer, &stats))
                {
                    if (state->have_last_render_stats)
                    {
                        state->render_mesh_submissions_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.model_mesh_submissions, stats.model_mesh_submissions);
                        state->render_mesh_draws_sample_sum +=
                            render_stat_delta_u64(state->last_render_stats.model_mesh_draws, stats.model_mesh_draws);
                        state->render_triangles_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.model_triangles_submitted, stats.model_triangles_submitted);
                        state->geometry_draw_calls_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.geometry_draw_calls, stats.geometry_draw_calls);
                        state->static_mesh_instanced_draw_sample_sum +=
                            render_stat_delta_u64(state->last_render_stats.static_mesh_instanced_draw_calls,
                                                  stats.static_mesh_instanced_draw_calls);
                        state->static_mesh_instances_batched_sum +=
                            render_stat_delta_u64(state->last_render_stats.static_mesh_instances_batched,
                                                  stats.static_mesh_instances_batched);
                        state->static_mesh_draw_calls_saved_sum += render_stat_delta_u64(
                            state->last_render_stats.static_mesh_draw_calls_saved, stats.static_mesh_draw_calls_saved);
                        state->procedural_lod_candidates_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.procedural_lod_candidates, stats.procedural_lod_candidates);
                        state->procedural_lod_reduced_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.procedural_lod_reduced, stats.procedural_lod_reduced);
                        state->procedural_lod_authored_triangles_sum +=
                            render_stat_delta_u64(state->last_render_stats.procedural_lod_authored_triangles,
                                                  stats.procedural_lod_authored_triangles);
                        state->procedural_lod_resolved_triangles_sum +=
                            render_stat_delta_u64(state->last_render_stats.procedural_lod_resolved_triangles,
                                                  stats.procedural_lod_resolved_triangles);
                        state->procedural_lod_triangles_saved_sum +=
                            render_stat_delta_u64(state->last_render_stats.procedural_lod_triangles_saved,
                                                  stats.procedural_lod_triangles_saved);
                        state->depth_prepass_draws_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.depth_prepass_draws, stats.depth_prepass_draws);
                        state->depth_prepass_triangles_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.depth_prepass_triangles, stats.depth_prepass_triangles);
                        state->depth_prepass_samples_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.depth_prepass_samples_passed, stats.depth_prepass_samples_passed);
                        state->geometry_samples_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.geometry_samples_passed, stats.geometry_samples_passed);
                        state->light_candidates_sample_sum +=
                            render_stat_delta_u64(state->last_render_stats.light_candidates, stats.light_candidates);
                        state->lights_selected_sample_sum +=
                            render_stat_delta_u64(state->last_render_stats.lights_selected, stats.lights_selected);
                        state->light_selection_draws_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.light_selection_draws, stats.light_selection_draws);
                    }
                    state->last_render_stats = stats;
                    state->have_last_render_stats = true;
                }
            }
            const float sample_seconds = slayer3d_game_data_fps_sample_seconds(runtime, 0.25f);
            if (state->fps_sample_time >= sample_seconds)
            {
                const float sample_frames = (float)SDL_max(state->fps_sample_frames, 1);
                state->displayed_fps = (float)state->fps_sample_frames / state->fps_sample_time;
                state->metrics.frame_ms = state->frame_ms_sample_sum / sample_frames;
                state->metrics.update_cpu_ms = state->update_cpu_ms_sample_sum / sample_frames;
                state->metrics.render_cpu_ms = state->render_cpu_ms_sample_sum / sample_frames;
                state->metrics.render_model_mesh_submissions_per_frame =
                    state->render_mesh_submissions_sample_sum / sample_frames;
                state->metrics.render_model_mesh_draws_per_frame = state->render_mesh_draws_sample_sum / sample_frames;
                state->metrics.render_model_triangles_per_frame = state->render_triangles_sample_sum / sample_frames;
                state->metrics.render_geometry_draw_calls_per_frame =
                    state->geometry_draw_calls_sample_sum / sample_frames;
                state->metrics.render_static_mesh_instanced_draw_calls_per_frame =
                    state->static_mesh_instanced_draw_sample_sum / sample_frames;
                state->metrics.render_static_mesh_instances_batched_per_frame =
                    state->static_mesh_instances_batched_sum / sample_frames;
                state->metrics.render_static_mesh_draw_calls_saved_per_frame =
                    state->static_mesh_draw_calls_saved_sum / sample_frames;
                state->metrics.render_procedural_lod_candidates_per_frame =
                    state->procedural_lod_candidates_sample_sum / sample_frames;
                state->metrics.render_procedural_lod_reduced_per_frame =
                    state->procedural_lod_reduced_sample_sum / sample_frames;
                state->metrics.render_procedural_lod_authored_triangles_per_frame =
                    state->procedural_lod_authored_triangles_sum / sample_frames;
                state->metrics.render_procedural_lod_resolved_triangles_per_frame =
                    state->procedural_lod_resolved_triangles_sum / sample_frames;
                state->metrics.render_procedural_lod_triangles_saved_per_frame =
                    state->procedural_lod_triangles_saved_sum / sample_frames;
                state->metrics.render_depth_prepass_draws_per_frame =
                    state->depth_prepass_draws_sample_sum / sample_frames;
                state->metrics.render_depth_prepass_triangles_per_frame =
                    state->depth_prepass_triangles_sample_sum / sample_frames;
                state->metrics.render_depth_prepass_samples_per_frame =
                    state->depth_prepass_samples_sample_sum / sample_frames;
                state->metrics.render_geometry_samples_per_frame = state->geometry_samples_sample_sum / sample_frames;
                state->metrics.render_light_candidates_per_frame = state->light_candidates_sample_sum / sample_frames;
                state->metrics.render_lights_selected_per_frame = state->lights_selected_sample_sum / sample_frames;
                state->metrics.render_light_selection_draws_per_frame =
                    state->light_selection_draws_sample_sum / sample_frames;
                state->metrics.render_light_selection_ratio =
                    state->light_candidates_sample_sum > 0.0f
                        ? state->lights_selected_sample_sum / state->light_candidates_sample_sum
                        : 0.0f;
                state->fps_sample_time = 0.0f;
                state->frame_ms_sample_sum = 0.0f;
                state->update_cpu_ms_sample_sum = 0.0f;
                state->render_cpu_ms_sample_sum = 0.0f;
                state->render_mesh_submissions_sample_sum = 0.0f;
                state->render_mesh_draws_sample_sum = 0.0f;
                state->render_triangles_sample_sum = 0.0f;
                state->geometry_draw_calls_sample_sum = 0.0f;
                state->static_mesh_instanced_draw_sample_sum = 0.0f;
                state->static_mesh_instances_batched_sum = 0.0f;
                state->static_mesh_draw_calls_saved_sum = 0.0f;
                state->procedural_lod_candidates_sample_sum = 0.0f;
                state->procedural_lod_reduced_sample_sum = 0.0f;
                state->procedural_lod_authored_triangles_sum = 0.0f;
                state->procedural_lod_resolved_triangles_sum = 0.0f;
                state->procedural_lod_triangles_saved_sum = 0.0f;
                state->depth_prepass_draws_sample_sum = 0.0f;
                state->depth_prepass_triangles_sample_sum = 0.0f;
                state->depth_prepass_samples_sample_sum = 0.0f;
                state->geometry_samples_sample_sum = 0.0f;
                state->light_candidates_sample_sum = 0.0f;
                state->lights_selected_sample_sum = 0.0f;
                state->light_selection_draws_sample_sum = 0.0f;
                state->fps_sample_frames = 0;
            }
        }
    }
    if (ctx->renderer != NULL)
    {
        int world_width = 0;
        int world_height = 0;
        if (slayer3d_get_world_render_size(ctx->renderer, &world_width, &world_height))
        {
            state->metrics.render_world_scale = slayer3d_get_world_render_scale(ctx->renderer);
            state->metrics.render_world_width = (float)world_width;
            state->metrics.render_world_height = (float)world_height;
        }
    }
    state->last_render_time = ctx->real_time;
    ++state->rendered_frames;
    state->metrics.paused = ctx->paused;
    state->metrics.fps = state->displayed_fps;
    state->metrics.frame = state->rendered_frames;
    state->render_eval.time = state->time;
    state->ui_pulse_phase = slayer3d_game_data_ui_pulse_phase(runtime, state->ui_pulse_phase);
}

void slayer3d_game_data_scene_flow_init(slayer3d_game_data_scene_flow *flow)
{
    if (flow == NULL)
        return;
    SDL_zero(*flow);
    slayer3d_transition_reset(&flow->transition);
}

bool slayer3d_game_data_scene_flow_is_transitioning(const slayer3d_game_data_scene_flow *flow)
{
    return flow != NULL && (flow->fading_out || flow->fading_in || flow->transition.active);
}

bool slayer3d_game_data_scene_flow_request(slayer3d_game_data_scene_flow *flow, slayer3d_game_data_runtime *runtime,
                                           const char *scene_name)
{
    if (flow == NULL || runtime == NULL || scene_name == NULL)
        return false;

    slayer3d_game_data_scene_transition_policy policy;
    (void)slayer3d_game_data_get_scene_transition_policy(runtime, &policy);
    if (slayer3d_game_data_scene_flow_is_transitioning(flow) && !policy.allow_interrupt)
        return false;

    const char *active = slayer3d_game_data_active_scene(runtime);
    if (active != NULL && SDL_strcmp(active, scene_name) == 0 && !policy.allow_same_scene)
        return false;

    bool known_scene = false;
    const int scene_count = slayer3d_game_data_scene_count(runtime);
    for (int i = 0; i < scene_count; ++i)
    {
        const char *name = slayer3d_game_data_scene_name_at(runtime, i);
        if (name != NULL && SDL_strcmp(name, scene_name) == 0)
        {
            known_scene = true;
            break;
        }
    }
    if (!known_scene)
        return false;

    if (slayer3d_game_data_scene_flow_is_transitioning(flow))
        slayer3d_transition_reset(&flow->transition);
    flow->pending_scene = scene_name;
    flow->fading_out = true;
    flow->fading_in = false;

    slayer3d_game_data_transition_desc transition;
    if (active != NULL && slayer3d_game_data_get_scene_transition(runtime, active, "exit", &transition))
    {
        slayer3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                                  transition.duration, transition.done_signal_id);
    }
    else
    {
        slayer3d_transition_reset(&flow->transition);
    }
    return true;
}

void slayer3d_game_data_scene_flow_update(slayer3d_game_data_scene_flow *flow, slayer3d_game_data_runtime *runtime,
                                          slayer3d_signal_bus *bus, float dt)
{
    if (flow == NULL || runtime == NULL)
        return;

    slayer3d_transition_update(&flow->transition, bus, dt);
    if (flow->fading_out && !flow->transition.active)
    {
        const char *next_scene = flow->pending_scene;
        flow->pending_scene = NULL;
        flow->fading_out = false;
        if (next_scene != NULL && slayer3d_game_data_set_active_scene(runtime, next_scene))
        {
            slayer3d_game_data_transition_desc transition;
            if (slayer3d_game_data_get_scene_transition(runtime, next_scene, "enter", &transition))
            {
                flow->fading_in = true;
                slayer3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                                          transition.duration, transition.done_signal_id);
            }
            else
            {
                slayer3d_transition_reset(&flow->transition);
            }
        }
        else
        {
            slayer3d_transition_reset(&flow->transition);
        }
    }
    else if (flow->fading_in && !flow->transition.active)
    {
        flow->fading_in = false;
    }
}

void slayer3d_game_data_scene_flow_draw(const slayer3d_game_data_scene_flow *flow, slayer3d_render_context *renderer)
{
    if (flow != NULL)
        slayer3d_transition_draw(&flow->transition, renderer);
}

static void app_flow_request_quit(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                  slayer3d_game_data_runtime *runtime)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || flow->quit_pending)
        return;

    flow->quit_pending = true;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D app quit requested");
    slayer3d_game_data_transition_desc transition;
    if (flow->app.quit_transition == NULL ||
        !slayer3d_game_data_get_transition(runtime, flow->app.quit_transition, &transition))
    {
        ctx->quit_requested = true;
        return;
    }

    slayer3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                              transition.duration, transition.done_signal_id);
}

static bool app_flow_request_scene(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                   const char *scene_name)
{
    if (slayer3d_game_data_scene_flow_request(&flow->scene_flow, runtime, scene_name))
    {
        slayer3d_game_data_scene_transition_policy policy;
        (void)slayer3d_game_data_get_scene_transition_policy(runtime, &policy);
        if (policy.reset_menu_input_on_request)
            flow->scene_input_armed = false;
        return true;
    }
    return false;
}

static void app_flow_apply_pause_command(slayer3d_game_context *ctx, slayer3d_game_data_menu_pause_command command)
{
    if (ctx == NULL)
        return;

    if (command == SLAYER3D_GAME_DATA_MENU_PAUSE_PAUSE)
        ctx->paused = true;
    else if (command == SLAYER3D_GAME_DATA_MENU_PAUSE_RESUME)
        ctx->paused = false;
    else if (command == SLAYER3D_GAME_DATA_MENU_PAUSE_TOGGLE)
        ctx->paused = !ctx->paused;
}

static bool app_flow_apply_window_settings(const slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                           slayer3d_game_data_runtime *runtime)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || ctx->window == NULL || ctx->renderer == NULL)
        return false;

    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, flow->app.window_settings_target);
    if (settings == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D window settings skipped: settings actor '%s' was not found",
                    flow->app.window_settings_target != NULL ? flow->app.window_settings_target : "");
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(ctx->window, &width, &height);

    slayer3d_window_config config;
    slayer3d_init_window_config(&config);
    config.width = width;
    config.height = height;
    config.logical_width = slayer3d_get_render_context_width(ctx->renderer);
    config.logical_height = slayer3d_get_render_context_height(ctx->renderer);
    config.title = SDL_GetWindowTitle(ctx->window);
    config.display_mode = parse_window_mode_setting(
        slayer3d_properties_get_string(settings->props, flow->app.window_display_mode_key, "windowed"),
        SLAYER3D_WINDOW_MODE_WINDOWED);
    config.backend =
        parse_backend_setting(slayer3d_properties_get_string(settings->props, flow->app.window_renderer_key, "opengl"),
                              slayer3d_get_render_context_backend(ctx->renderer));
    config.vsync = slayer3d_properties_get_bool(settings->props, flow->app.window_vsync_key, true);
    config.maximized = true;
    config.resizable = true;
    config.allow_backend_fallback = false;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D authored window apply requested: mode=%s renderer=%s vsync=%s",
                window_mode_setting_name(config.display_mode), slayer3d_get_backend_name(config.backend),
                config.vsync ? "on" : "off");
    return slayer3d_apply_window_config(&ctx->window, &ctx->renderer, &config);
}

static bool app_flow_consume_menu(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                  slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                  slayer3d_signal_bus *bus)
{
    if (flow->quit_pending || slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        return false;

    slayer3d_game_data_ui_metrics metrics;
    SDL_zero(metrics);
    metrics.paused = ctx != NULL && ctx->paused;

    slayer3d_game_data_menu_update_result result;
    if (!slayer3d_game_data_update_menus_for_metrics(runtime, input, &flow->scene_input_armed, &metrics, &result) ||
        !result.handled_input)
        return false;

    if (result.move_signal_id >= 0)
        slayer3d_signal_emit(bus, result.move_signal_id, NULL);
    if (result.select_signal_id >= 0)
        slayer3d_signal_emit(bus, result.select_signal_id, NULL);
    if (!result.selected)
        return true;

    if (result.signal_id >= 0)
        slayer3d_signal_emit(bus, result.signal_id, NULL);
    if (slayer3d_game_data_app_signal_applies_window_settings(runtime, result.signal_id))
        (void)app_flow_apply_window_settings(flow, ctx, runtime);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    if (result.return_to != NULL && scene_state != NULL)
        slayer3d_properties_set_string(scene_state, "return_scene", result.return_to);
    if (result.scene_state_key != NULL && result.scene_state_value != NULL && scene_state != NULL)
        slayer3d_properties_set_string(scene_state, result.scene_state_key, result.scene_state_value);
    if (result.has_return_paused && scene_state != NULL)
        slayer3d_properties_set_bool(scene_state, "return_paused", result.return_paused);

    const char *scene = result.scene;
    if (result.return_scene && scene_state != NULL)
    {
        scene = slayer3d_properties_get_string(scene_state, "return_scene", scene);
        if (ctx != NULL)
            ctx->paused = slayer3d_properties_get_bool(scene_state, "return_paused", ctx->paused);
    }
    app_flow_apply_pause_command(ctx, result.pause_command);

    if (result.quit)
        app_flow_request_quit(flow, ctx, runtime);
    else if (scene != NULL)
        (void)app_flow_request_scene(flow, runtime, scene);

    return true;
}

static void app_flow_consume_scene_shortcuts(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                             slayer3d_input_manager *input)
{
    if (flow->quit_pending || slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        return;

    const int count = slayer3d_game_data_scene_shortcut_count(runtime);
    for (int i = 0; i < count; ++i)
    {
        slayer3d_game_data_scene_shortcut shortcut;
        if (slayer3d_game_data_scene_shortcut_at(runtime, i, &shortcut) && shortcut.action_id >= 0 &&
            slayer3d_game_data_active_scene_allows_action(runtime, shortcut.action_id) &&
            slayer3d_input_is_pressed(input, shortcut.action_id))
        {
            (void)app_flow_request_scene(flow, runtime, shortcut.scene);
            return;
        }
    }
}

static bool app_flow_set_scene_without_transition(slayer3d_game_data_app_flow *flow,
                                                  slayer3d_game_data_runtime *runtime, const char *scene_name)
{
    if (flow == NULL || runtime == NULL || scene_name == NULL)
        return false;

    if (!slayer3d_game_data_set_active_scene(runtime, scene_name))
        return false;

    slayer3d_game_data_scene_flow_init(&flow->scene_flow);
    flow->scene_input_armed = false;
    slayer3d_game_data_timeline_state_init(&flow->timeline);
    return true;
}

static bool app_flow_apply_skip_policy(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                       const slayer3d_input_manager *input, bool capture_input, bool *out_applied,
                                       bool *out_block_menus, bool *out_block_scene_shortcuts)
{
    if (out_applied != NULL)
        *out_applied = false;
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;
    if (flow == NULL || runtime == NULL)
        return false;

    slayer3d_game_data_skip_policy policy;
    if (!slayer3d_game_data_get_active_skip_policy(runtime, &policy))
    {
        flow->skip_scene = NULL;
        flow->skip_requested = false;
        return false;
    }

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (flow->skip_scene != active_scene)
    {
        flow->skip_scene = active_scene;
        flow->skip_requested = false;
    }

    bool consumed = false;
    if (capture_input && input != NULL)
    {
        bool matched = false;
        if (policy.input == SLAYER3D_GAME_DATA_SKIP_INPUT_ANY)
        {
            matched = slayer3d_input_any_pressed(input);
        }
        else if (policy.input == SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION)
        {
            matched = policy.action_id >= 0 &&
                      slayer3d_game_data_active_scene_allows_action(runtime, policy.action_id) &&
                      slayer3d_input_is_pressed(input, policy.action_id);
        }

        if (matched)
        {
            flow->skip_requested = true;
            consumed = policy.consume_input;
            if (out_block_menus != NULL)
                *out_block_menus = policy.block_menus || policy.consume_input;
            if (out_block_scene_shortcuts != NULL)
                *out_block_scene_shortcuts = policy.block_scene_shortcuts || policy.consume_input;
        }
    }

    if (flow->skip_requested && !flow->quit_pending && !flow->transition.active &&
        !slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
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

static bool app_flow_timeline_is_pending(const slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime)
{
    slayer3d_game_data_timeline_policy policy;
    if (flow == NULL || runtime == NULL || flow->timeline.complete ||
        !slayer3d_game_data_get_active_timeline_policy(runtime, &policy))
    {
        return false;
    }
    return true;
}

static void app_flow_timeline_blocks(const slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime,
                                     bool *out_block_menus, bool *out_block_scene_shortcuts)
{
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;

    if (!app_flow_timeline_is_pending(flow, runtime))
        return;

    slayer3d_game_data_timeline_policy policy;
    if (!slayer3d_game_data_get_active_timeline_policy(runtime, &policy))
        return;

    if (out_block_menus != NULL)
        *out_block_menus = policy.block_menus;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = policy.block_scene_shortcuts;
}

static bool app_flow_update_timeline(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime, float dt)
{
    if (flow == NULL || runtime == NULL || flow->quit_pending || flow->transition.active ||
        slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
    {
        return true;
    }

    slayer3d_game_data_timeline_update_result result;
    if (!slayer3d_game_data_update_timeline(runtime, &flow->timeline, dt, &result))
        return false;

    if (result.scene_request != NULL)
        (void)app_flow_request_scene(flow, runtime, result.scene_request);
    return true;
}

static void app_flow_update_transition(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                       slayer3d_signal_bus *bus, float dt)
{
    if (flow->transition.active)
        slayer3d_transition_update(&flow->transition, bus, dt);
    if (flow->quit_pending && flow->transition.finished && !flow->transition.active)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D app quit transition finished");
        ctx->quit_requested = true;
    }
}

void slayer3d_game_data_app_flow_init(slayer3d_game_data_app_flow *flow)
{
    if (flow == NULL)
        return;
    SDL_zero(*flow);
    slayer3d_game_data_scene_flow_init(&flow->scene_flow);
    slayer3d_transition_reset(&flow->transition);
    flow->app.start_signal_id = -1;
    flow->app.quit_action_id = -1;
    flow->app.pause_action_id = -1;
    flow->app.quit_signal_id = -1;
}

bool slayer3d_game_data_app_flow_start(slayer3d_game_data_app_flow *flow, slayer3d_game_data_runtime *runtime)
{
    if (flow == NULL || runtime == NULL)
        return false;

    slayer3d_game_data_scene_flow_init(&flow->scene_flow);
    slayer3d_transition_reset(&flow->transition);
    flow->quit_pending = false;
    flow->scene_input_armed = false;
    flow->skip_scene = NULL;
    flow->skip_requested = false;
    slayer3d_game_data_timeline_state_init(&flow->timeline);
    if (!slayer3d_game_data_get_app_control(runtime, &flow->app))
        return false;

    slayer3d_game_data_transition_desc transition;
    if (flow->app.startup_transition != NULL &&
        slayer3d_game_data_get_transition(runtime, flow->app.startup_transition, &transition))
    {
        slayer3d_transition_start(&flow->transition, transition.type, transition.direction, transition.color,
                                  transition.duration, transition.done_signal_id);
    }
    return true;
}

bool slayer3d_game_data_app_flow_quit_pending(const slayer3d_game_data_app_flow *flow)
{
    return flow != NULL && flow->quit_pending;
}

bool slayer3d_game_data_app_flow_is_transitioning(const slayer3d_game_data_app_flow *flow)
{
    return flow != NULL &&
           (flow->transition.active || slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow));
}

bool slayer3d_game_data_app_flow_update(slayer3d_game_data_app_flow *flow, slayer3d_game_context *ctx,
                                        slayer3d_game_data_runtime *runtime, float dt)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || ctx->session == NULL)
        return false;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(ctx->session);
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(ctx->session);
    bool skip_applied = false;
    bool skip_blocks_menus = false;
    bool skip_blocks_scene_shortcuts = false;
    const bool skip_consumed = app_flow_apply_skip_policy(flow, runtime, input, true, &skip_applied, &skip_blocks_menus,
                                                          &skip_blocks_scene_shortcuts);
    bool timeline_blocks_menus = false;
    bool timeline_blocks_scene_shortcuts = false;
    app_flow_timeline_blocks(flow, runtime, &timeline_blocks_menus, &timeline_blocks_scene_shortcuts);

    bool activity_blocks_menus = false;
    bool activity_blocks_scene_shortcuts = false;
    const bool activity_wake_consumed = slayer3d_game_data_scene_activity_consumes_wake_input(
        runtime, input, &activity_blocks_menus, &activity_blocks_scene_shortcuts);

    if (!skip_consumed && !activity_wake_consumed)
    {
        if (flow->app.quit_action_id >= 0 &&
            slayer3d_game_data_active_scene_allows_action(runtime, flow->app.quit_action_id) &&
            slayer3d_input_is_pressed(input, flow->app.quit_action_id))
            app_flow_request_quit(flow, ctx, runtime);

        if (!skip_blocks_scene_shortcuts && !timeline_blocks_scene_shortcuts && !activity_blocks_scene_shortcuts)
            app_flow_consume_scene_shortcuts(flow, runtime, input);
        bool menu_consumed = false;
        if (!skip_blocks_menus && !timeline_blocks_menus && !activity_blocks_menus)
            menu_consumed = app_flow_consume_menu(flow, ctx, runtime, input, bus);

        if (!menu_consumed && flow->app.pause_action_id >= 0 &&
            slayer3d_input_is_pressed(input, flow->app.pause_action_id) &&
            slayer3d_game_data_active_scene_allows_action(runtime, flow->app.pause_action_id) && !flow->quit_pending &&
            !slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        {
            if (ctx->paused)
                ctx->paused = false;
            else
            {
                slayer3d_game_data_ui_metrics metrics;
                SDL_zero(metrics);
                metrics.paused = ctx->paused;
                if (slayer3d_game_data_app_pause_allowed(runtime, &metrics) &&
                    slayer3d_game_data_active_scene_updates_game(runtime))
                    ctx->paused = true;
            }
        }
    }

    const bool scene_transitioning_before = slayer3d_game_data_scene_flow_is_transitioning(&flow->scene_flow);
    app_flow_update_transition(flow, ctx, bus, dt);
    slayer3d_game_data_scene_flow_update(&flow->scene_flow, runtime, bus, dt);
    bool deferred_skip_applied = false;
    (void)app_flow_apply_skip_policy(flow, runtime, input, false, &deferred_skip_applied, NULL, NULL);
    skip_applied = skip_applied || deferred_skip_applied;
    if (!scene_transitioning_before && !skip_applied && !app_flow_update_timeline(flow, runtime, dt))
        return false;
    return true;
}

void slayer3d_game_data_app_flow_draw(const slayer3d_game_data_app_flow *flow, slayer3d_render_context *renderer)
{
    if (flow == NULL)
        return;
    slayer3d_game_data_scene_flow_draw(&flow->scene_flow, renderer);
    slayer3d_transition_draw(&flow->transition, renderer);
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
        const slayer3d_camera3d camera = active_camera_or_fallback(frame->runtime, frame->fallback_camera);
        if (slayer3d_begin_mode_3d(frame->renderer, camera))
        {
            ok = run_frame_hook(frame, frame->before_world_3d) && ok;
            ok = draw_active_scene_skybox(frame->runtime, frame->renderer, frame->image_cache) && ok;
            ok = slayer3d_game_data_draw_sector_levels_with_assets(
                     frame->runtime, frame->renderer, frame->image_cache != NULL ? frame->image_cache->assets : NULL,
                     &camera) &&
                 ok;
            ok = slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(
                     frame->runtime, frame->renderer, frame->image_cache != NULL ? frame->image_cache->assets : NULL,
                     &camera) &&
                 ok;
            if (frame->particle_cache != NULL)
                ok = draw_particles_filtered(frame->runtime, frame->renderer, frame->particle_cache, true, false) && ok;
            ok = draw_render_primitives_evaluated_with_cache(
                     frame->runtime, frame->renderer, frame->render_eval, frame->image_cache, frame->sprite_cache,
                     frame->model_cache, frame->mesh_primitive_cache, &camera, true, false) &&
                 ok;
            ok = slayer3d_game_data_draw_active_editor_debug_primitives(frame->runtime, frame->renderer) && ok;
            ok = run_frame_hook(frame, frame->after_world_3d) && ok;
            slayer3d_end_mode_3d(frame->renderer);
            const slayer3d_camera3d viewmodel_camera = game_data_viewmodel_camera(&camera);
            if (slayer3d_begin_mode_3d(frame->renderer, viewmodel_camera))
            {
                ok = draw_render_primitives_evaluated_with_cache(
                         frame->runtime, frame->renderer, frame->render_eval, frame->image_cache, frame->sprite_cache,
                         frame->model_cache, frame->mesh_primitive_cache, &viewmodel_camera, false, true) &&
                     ok;
                if (frame->particle_cache != NULL)
                    ok = draw_particles_filtered(frame->runtime, frame->renderer, frame->particle_cache, false, true) &&
                         ok;
                slayer3d_end_mode_3d(frame->renderer);
            }
            else
            {
                ok = false;
            }
        }
        else
        {
            ok = false;
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
