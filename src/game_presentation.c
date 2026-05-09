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
    const sdl3d_game_data_runtime *runtime;
    sdl3d_render_context *renderer;
    sdl3d_game_data_image_cache *image_cache;
    sdl3d_game_data_sprite_cache *sprite_cache;
    const sdl3d_camera3d *camera;
    const sdl3d_game_data_render_eval *eval;
    sdl3d_game_data_render_primitive sphere_batch;
    sdl3d_vec3 *sphere_batch_positions;
    int sphere_batch_count;
    int sphere_batch_capacity;
    bool sphere_batch_active;
} primitive_draw_context;

typedef struct sector_level_draw_context
{
    sdl3d_render_context *renderer;
    const sdl3d_camera3d *camera;
    bool *sector_visible;
    int sector_visible_capacity;
    bool ok;
} sector_level_draw_context;

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
    const sdl3d_game_data_render_eval *render_eval;
    bool ok;
} ui_image_draw_context;

typedef struct ui_rect_draw_context
{
    const sdl3d_game_data_runtime *runtime;
    sdl3d_render_context *renderer;
    const sdl3d_game_data_ui_metrics *metrics;
    const sdl3d_game_data_render_eval *render_eval;
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

static sdl3d_overlay_effect ui_image_effect_from_name(const char *effect)
{
    if (effect == NULL)
        return SDL3D_OVERLAY_EFFECT_NONE;
    if (SDL_strcasecmp(effect, "melt") == 0)
        return SDL3D_OVERLAY_EFFECT_MELT;
    return SDL3D_OVERLAY_EFFECT_NONE;
}

typedef struct particle_update_context
{
    sdl3d_game_data_particle_cache *cache;
    float dt;
    bool ok;
} particle_update_context;

static sdl3d_window_mode parse_window_mode_setting(const char *value, sdl3d_window_mode fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;
    if (SDL_strcasecmp(value, "windowed") == 0 || SDL_strcasecmp(value, "window") == 0)
        return SDL3D_WINDOW_MODE_WINDOWED;
    if (SDL_strcasecmp(value, "fullscreen_exclusive") == 0 || SDL_strcasecmp(value, "exclusive") == 0)
        return SDL3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE;
    if (SDL_strcasecmp(value, "fullscreen_borderless") == 0 || SDL_strcasecmp(value, "borderless") == 0 ||
        SDL_strcasecmp(value, "desktop_fullscreen") == 0)
        return SDL3D_WINDOW_MODE_FULLSCREEN_BORDERLESS;
    return fallback;
}

static const char *window_mode_setting_name(sdl3d_window_mode mode)
{
    switch (mode)
    {
    case SDL3D_WINDOW_MODE_WINDOWED:
        return "windowed";
    case SDL3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE:
        return "fullscreen_exclusive";
    case SDL3D_WINDOW_MODE_FULLSCREEN_BORDERLESS:
        return "fullscreen_borderless";
    case SDL3D_WINDOW_MODE_DEFAULT:
    default:
        return "default";
    }
}

static sdl3d_backend parse_backend_setting(const char *value, sdl3d_backend fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;
    if (SDL_strcasecmp(value, "software") == 0 || SDL_strcasecmp(value, "sw") == 0)
        return SDL3D_BACKEND_SOFTWARE;
    if (SDL_strcasecmp(value, "opengl") == 0 || SDL_strcasecmp(value, "gl") == 0)
        return SDL3D_BACKEND_OPENGL;
    if (SDL_strcasecmp(value, "auto") == 0)
        return SDL3D_BACKEND_AUTO;
    return fallback;
}

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

    sdl3d_light selected[SDL3D_MAX_LIGHTS];
    float scores[SDL3D_MAX_LIGHTS];
    int selected_count = 0;
    const int light_count = sdl3d_game_data_world_light_count(runtime);
    for (int i = 0; i < light_count; ++i)
    {
        sdl3d_light light;
        if (!sdl3d_game_data_get_world_light_evaluated(runtime, i, eval, &light))
            continue;
        const float score =
            light.type == SDL3D_LIGHT_DIRECTIONAL ? 1000000.0f + light.intensity : light.intensity * light.range;
        int insert = selected_count;
        for (int candidate = 0; candidate < selected_count; ++candidate)
        {
            if (score > scores[candidate])
            {
                insert = candidate;
                break;
            }
        }
        if (insert >= SDL3D_MAX_LIGHTS)
            continue;
        if (selected_count < SDL3D_MAX_LIGHTS)
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
        ok = sdl3d_add_light(renderer, &selected[i]) && ok;
    return ok;
}

static bool run_frame_hook(const sdl3d_game_data_frame_desc *frame, sdl3d_game_data_frame_hook hook)
{
    return hook == NULL || hook(frame->userdata, frame);
}

static bool menu_action_pressed(const sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input, int action_id,
                                const char *fallback_name)
{
    if (action_id >= 0 && sdl3d_game_data_active_scene_allows_action(runtime, action_id) &&
        sdl3d_input_is_pressed(input, action_id))
    {
        return true;
    }

    if (fallback_name != NULL && input != NULL)
    {
        const int fallback_id = sdl3d_input_find_action(input, fallback_name);
        if (fallback_id >= 0 && sdl3d_input_is_pressed(input, fallback_id))
        {
            return true;
        }
    }

    return false;
}

static bool menu_action_held(const sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input, int action_id,
                             const char *fallback_name)
{
    if (action_id >= 0 && sdl3d_game_data_active_scene_allows_action(runtime, action_id) &&
        sdl3d_input_is_held(input, action_id))
    {
        return true;
    }

    if (fallback_name != NULL && input != NULL)
    {
        const int fallback_id = sdl3d_input_find_action(input, fallback_name);
        if (fallback_id >= 0 && sdl3d_input_is_held(input, fallback_id))
        {
            return true;
        }
    }

    return false;
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

static bool ensure_sprite_cache_capacity(sdl3d_game_data_sprite_cache *cache, int required)
{
    if (cache == NULL || required <= cache->capacity)
        return cache != NULL;

    int next_capacity = cache->capacity < 4 ? 4 : cache->capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    sdl3d_game_data_sprite_cache_entry *entries =
        (sdl3d_game_data_sprite_cache_entry *)SDL_realloc(cache->entries, (size_t)next_capacity * sizeof(*entries));
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

static sdl3d_game_data_image_cache_entry *find_or_load_image_entry(const sdl3d_game_data_runtime *runtime,
                                                                   sdl3d_game_data_image_cache *cache,
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

    sdl3d_game_data_image_asset asset;
    if (!sdl3d_game_data_get_image_asset(runtime, image_id, &asset))
        return NULL;

    if (asset.sprite != NULL)
    {
        sdl3d_sprite_asset_runtime sprite;
        SDL_zero(sprite);
        char error[256];
        if (!sdl3d_game_data_load_sprite_asset(runtime, asset.sprite, &sprite, error, (int)sizeof(error)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load sprite-backed UI image %s: %s", asset.sprite,
                         error);
            return NULL;
        }

        if (sprite.base_texture_count <= 0 || sprite.base_textures == NULL || sprite.base_textures[0].pixels == NULL ||
            sprite.base_textures[0].width <= 0 || sprite.base_textures[0].height <= 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sprite-backed UI image %s has no base texture", asset.sprite);
            sdl3d_sprite_asset_free(&sprite);
            return NULL;
        }

        sdl3d_image image;
        SDL_zero(image);
        image.pixels = sprite.base_textures[0].pixels;
        image.width = sprite.base_textures[0].width;
        image.height = sprite.base_textures[0].height;

        sdl3d_game_data_image_cache_entry *entry = &cache->entries[cache->count];
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
            sdl3d_sprite_asset_free(&sprite);
            return NULL;
        }
        entry->loaded = sdl3d_create_texture_from_image(&image, &entry->texture);
        sdl3d_sprite_asset_free(&sprite);
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
    entry->effect = NULL;
    entry->effect_delay = 0.0f;
    entry->effect_duration = 1.0f;
    entry->shader_vertex_source = NULL;
    entry->shader_fragment_source = NULL;
    entry->loaded = sdl3d_create_texture_from_image(&image, &entry->texture);
    sdl3d_free_image(&image);
    if (!entry->loaded)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture for UI image asset %s", asset.path);
        return NULL;
    }

    ++cache->count;
    return entry;
}

static sdl3d_game_data_sprite_cache_entry *find_or_load_sprite_entry(const sdl3d_game_data_runtime *runtime,
                                                                     sdl3d_game_data_sprite_cache *cache,
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

    sdl3d_game_data_sprite_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    entry->sprite_id = sprite_id;

    char error[256];
    if (!sdl3d_game_data_load_sprite_asset(runtime, sprite_id, &entry->sprite, error, (int)sizeof(error)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load world sprite asset %s: %s", sprite_id, error);
        SDL_zero(entry);
        return NULL;
    }

    entry->loaded = true;
    ++cache->count;
    return entry;
}

static bool draw_sphere_batch(sdl3d_render_context *renderer, const sdl3d_game_data_render_primitive *primitive)
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
        const sdl3d_vec3 center = primitive->instances[instance];
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

    sdl3d_mesh mesh;
    SDL_zero(mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.indices = indices;
    mesh.vertex_count = vertex_count;
    mesh.index_count = index_count;
    const bool ok = sdl3d_draw_mesh(renderer, &mesh, NULL, primitive->color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(uvs);
    SDL_free(indices);
    return ok;
}

static bool primitive_sphere_can_batch(const sdl3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL || primitive->type != SDL3D_GAME_DATA_RENDER_SPHERE || primitive->texture_image != NULL)
        return false;
    return SDL_fabsf(primitive->rotation_angle) <= 0.0001f && SDL_fabsf(primitive->rotation_axis.x) <= 0.0001f &&
           SDL_fabsf(primitive->rotation_axis.y) <= 0.0001f && SDL_fabsf(primitive->rotation_axis.z) <= 0.0001f;
}

static bool primitive_sphere_batch_matches(const sdl3d_game_data_render_primitive *batch,
                                           const sdl3d_game_data_render_primitive *primitive)
{
    if (batch == NULL || primitive == NULL)
        return false;
    return batch->type == SDL3D_GAME_DATA_RENDER_SPHERE && primitive_sphere_can_batch(primitive) &&
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
    sdl3d_game_data_render_primitive primitive = context->sphere_batch;
    primitive.type = SDL3D_GAME_DATA_RENDER_SPHERE_BATCH;
    primitive.instances = context->sphere_batch_positions;
    primitive.instance_count = context->sphere_batch_count;
    const bool ok = draw_sphere_batch(context->renderer, &primitive);
    context->sphere_batch_active = false;
    context->sphere_batch_count = 0;
    return ok;
}

static bool append_sphere_draw_batch(primitive_draw_context *context, const sdl3d_game_data_render_primitive *primitive)
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
        sdl3d_vec3 *positions =
            (sdl3d_vec3 *)SDL_realloc(context->sphere_batch_positions, (size_t)next_capacity * sizeof(*positions));
        if (positions == NULL)
            return false;
        context->sphere_batch_positions = positions;
        context->sphere_batch_capacity = next_capacity;
    }
    context->sphere_batch_positions[context->sphere_batch_count++] = primitive->position;
    return true;
}

static bool draw_primitive(void *userdata, const sdl3d_game_data_render_primitive *primitive)
{
    primitive_draw_context *context = (primitive_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || primitive == NULL)
        return false;
    if (primitive_sphere_can_batch(primitive))
        return append_sphere_draw_batch(context, primitive);
    if (!flush_sphere_draw_batch(context))
        return false;

    const bool restore_lighting = sdl3d_is_lighting_enabled(context->renderer);
    if (!primitive->lighting_enabled)
        sdl3d_set_lighting_enabled(context->renderer, false);
    sdl3d_set_emissive(context->renderer, primitive->emissive_color.x, primitive->emissive_color.y,
                       primitive->emissive_color.z);
    if (primitive->type == SDL3D_GAME_DATA_RENDER_CUBE)
    {
        sdl3d_draw_cube(context->renderer, primitive->position, primitive->size, primitive->color);
    }
    else if (primitive->type == SDL3D_GAME_DATA_RENDER_SPHERE)
    {
        const sdl3d_texture2d *texture = NULL;
        if (primitive->texture_image != NULL && context->image_cache != NULL)
        {
            sdl3d_game_data_image_cache_entry *entry =
                find_or_load_image_entry(context->runtime, context->image_cache, primitive->texture_image);
            texture = entry != NULL ? &entry->texture : NULL;
        }
        sdl3d_draw_sphere_textured(context->renderer, primitive->position, primitive->radius, primitive->rings,
                                   primitive->slices, primitive->rotation_axis, primitive->rotation_angle, texture,
                                   primitive->color);
    }
    else if (primitive->type == SDL3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        if (!draw_sphere_batch(context->renderer, primitive))
            return false;
    }
    else if (primitive->type == SDL3D_GAME_DATA_RENDER_SPRITE)
    {
        if (context->sprite_cache == NULL)
            return true;
        sdl3d_game_data_sprite_cache_entry *entry =
            find_or_load_sprite_entry(context->runtime, context->sprite_cache, primitive->sprite_asset);
        if (entry == NULL)
            return false;

        sdl3d_sprite_actor actor;
        SDL_zero(actor);
        sdl3d_sprite_asset_apply_actor(&actor, &entry->sprite);
        actor.position = primitive->position;
        actor.size = primitive->sprite_size.x > 0.0f && primitive->sprite_size.y > 0.0f ? primitive->sprite_size
                                                                                        : (sdl3d_vec2){1.0f, 1.0f};
        actor.tint = primitive->color;
        actor.visible = true;
        actor.sector_id = -1;
        sdl3d_sprite_actor_set_facing_yaw(&actor, primitive->sprite_facing_yaw);
        if (context->eval != NULL)
            actor.animation_time = context->eval->time;

        sdl3d_sprite_scene scene;
        SDL_zero(scene);
        scene.actors = &actor;
        scene.count = 1;
        scene.capacity = 1;
        const sdl3d_vec3 camera_position =
            context->camera != NULL ? context->camera->position : sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
        sdl3d_sprite_scene_draw(&scene, context->renderer, camera_position, NULL);
    }
    sdl3d_set_emissive(context->renderer, 0.0f, 0.0f, 0.0f);
    if (!primitive->lighting_enabled)
        sdl3d_set_lighting_enabled(context->renderer, restore_lighting);
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

    sdl3d_game_data_image_cache_entry *entry =
        find_or_load_image_entry(draw->runtime, draw->image_cache, resolved.image);
    if (entry == NULL)
    {
        draw->ok = false;
        return true;
    }
    sdl3d_texture2d *texture = &entry->texture;

    const int width = sdl3d_get_render_context_width(draw->renderer);
    const int height = sdl3d_get_render_context_height(draw->renderer);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    resolve_ui_image_rect(&resolved, texture, width, height, &x, &y, &w, &h);
    const char *effect_name = resolved.effect != NULL ? resolved.effect : entry->effect;
    const sdl3d_overlay_effect effect = ui_image_effect_from_name(effect_name);
    const float effect_progress =
        effect != SDL3D_OVERLAY_EFFECT_NONE && draw->render_eval != NULL
            ? SDL_clamp((draw->render_eval->time - entry->effect_delay) / SDL_max(entry->effect_duration, 0.0001f),
                        0.0f, 1.0f)
            : 0.0f;
    const Uint32 effect_seed = ui_image_hash_string(resolved.name);
    const bool has_custom_shader = (entry->shader_vertex_source != NULL && entry->shader_vertex_source[0] != '\0') ||
                                   (entry->shader_fragment_source != NULL && entry->shader_fragment_source[0] != '\0');
    const bool drawn = has_custom_shader
                           ? sdl3d_draw_texture_overlay_shader(
                                 draw->renderer, texture, x, y, w, h, resolved.color, effect, effect_progress,
                                 effect_seed, entry->shader_vertex_source, entry->shader_fragment_source)
                           : sdl3d_draw_texture_overlay(draw->renderer, texture, x, y, w, h, resolved.color, effect,
                                                        effect_progress, effect_seed);
    if (!drawn)
        draw->ok = false;
    return true;
}

static void resolve_ui_rect_rect(const sdl3d_game_data_ui_rect *rect, int width, int height, float *out_x, float *out_y,
                                 float *out_w, float *out_h)
{
    float w = rect->normalized ? rect->w * (float)width : rect->w;
    float h = rect->normalized ? rect->h * (float)height : rect->h;
    const float scale = rect->scale > 0.0f ? rect->scale : 1.0f;
    w *= scale;
    h *= scale;

    float x = rect->normalized ? rect->x * (float)width : rect->x;
    float y = rect->normalized ? rect->y * (float)height : rect->y;
    if (rect->align == SDL3D_GAME_DATA_UI_ALIGN_CENTER)
        x -= w * 0.5f;
    else if (rect->align == SDL3D_GAME_DATA_UI_ALIGN_RIGHT)
        x -= w;
    if (rect->valign == SDL3D_GAME_DATA_UI_VALIGN_CENTER)
        y -= h * 0.5f;
    else if (rect->valign == SDL3D_GAME_DATA_UI_VALIGN_BOTTOM)
        y -= h;

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

static bool draw_ui_rect(void *userdata, const sdl3d_game_data_ui_rect *rect)
{
    ui_rect_draw_context *draw = (ui_rect_draw_context *)userdata;
    if (draw == NULL || rect == NULL)
        return false;

    sdl3d_game_data_ui_rect resolved;
    bool visible = false;
    if (!sdl3d_game_data_resolve_ui_rect(draw->runtime, rect, draw->metrics, &resolved, &visible))
    {
        draw->ok = false;
        return true;
    }
    if (!visible)
        return true;

    sdl3d_color color = resolved.color;
    if (resolved.pulse_alpha)
    {
        const float time = draw->render_eval != NULL ? draw->render_eval->time : 0.0f;
        const float pulse = 0.5f + 0.5f * SDL_sinf(time * SDL_max(resolved.pulse_rate, 0.0f) * SDL_PI_F * 2.0f);
        const float alpha = resolved.pulse_min + (resolved.pulse_max - resolved.pulse_min) * pulse;
        color.a = (Uint8)SDL_clamp((int)((float)color.a * SDL_clamp(alpha, 0.0f, 1.0f) + 0.5f), 0, 255);
    }
    if (color.a == 0)
        return true;

    const int width = sdl3d_get_render_context_width(draw->renderer);
    const int height = sdl3d_get_render_context_height(draw->renderer);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    resolve_ui_rect_rect(&resolved, width, height, &x, &y, &w, &h);
    if (!sdl3d_draw_rect_overlay(draw->renderer, x, y, w, h, color))
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
        SDL_free(cache->entries[i].shader_vertex_source);
        SDL_free(cache->entries[i].shader_fragment_source);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

void sdl3d_game_data_sprite_cache_init(sdl3d_game_data_sprite_cache *cache, sdl3d_asset_resolver *assets)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
    cache->assets = assets;
}

void sdl3d_game_data_sprite_cache_free(sdl3d_game_data_sprite_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
    {
        if (cache->entries[i].loaded)
            sdl3d_sprite_asset_free(&cache->entries[i].sprite);
    }
    SDL_free(cache->entries);
    SDL_zero(*cache);
}

static bool draw_render_primitives_evaluated_with_cache(
    const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer, const sdl3d_game_data_render_eval *eval,
    sdl3d_game_data_image_cache *image_cache, sdl3d_game_data_sprite_cache *sprite_cache, const sdl3d_camera3d *camera)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    primitive_draw_context context;
    SDL_zero(context);
    context.runtime = runtime;
    context.renderer = renderer;
    context.image_cache = image_cache;
    context.sprite_cache = sprite_cache;
    context.camera = camera;
    context.eval = eval;
    bool ok = sdl3d_game_data_for_each_render_primitive_evaluated(runtime, eval, draw_primitive, &context);
    ok = flush_sphere_draw_batch(&context) && ok;
    SDL_free(context.sphere_batch_positions);
    return ok;
}

static bool draw_sector_level_instance(void *userdata, const sdl3d_game_data_sector_level_instance *instance)
{
    sector_level_draw_context *context = (sector_level_draw_context *)userdata;
    if (context == NULL || context->renderer == NULL || instance == NULL || instance->level == NULL)
        return false;

    sdl3d_visibility_result vis;
    SDL_zero(vis);
    const sdl3d_visibility_result *vis_ptr = NULL;
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
        sdl3d_camera3d local_camera = *context->camera;
        local_camera.position.x -= instance->position.x;
        local_camera.position.y -= instance->position.y;
        local_camera.position.z -= instance->position.z;
        local_camera.target.x -= instance->position.x;
        local_camera.target.y -= instance->position.y;
        local_camera.target.z -= instance->position.z;
        sdl3d_level_compute_visibility_from_camera(
            instance->level, instance->sectors, &local_camera, sdl3d_get_render_context_width(context->renderer),
            sdl3d_get_render_context_height(context->renderer), 0.01f, 1000.0f, &vis);
        vis_ptr = &vis;
    }

    bool pushed = false;
    if (instance->position.x != 0.0f || instance->position.y != 0.0f || instance->position.z != 0.0f)
    {
        if (!sdl3d_push_matrix(context->renderer))
        {
            context->ok = false;
            return false;
        }
        pushed = true;
        if (!sdl3d_translate(context->renderer, instance->position.x, instance->position.y, instance->position.z))
        {
            context->ok = false;
            if (pushed)
                (void)sdl3d_pop_matrix(context->renderer);
            return false;
        }
    }

    const bool drawn = sdl3d_draw_level(context->renderer, instance->level, vis_ptr, (sdl3d_color){255, 255, 255, 255});
    if (pushed && !sdl3d_pop_matrix(context->renderer))
        context->ok = false;
    if (!drawn)
        context->ok = false;
    return context->ok;
}

bool sdl3d_game_data_draw_sector_levels(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                        const sdl3d_camera3d *camera)
{
    if (runtime == NULL || renderer == NULL)
        return false;

    sector_level_draw_context context;
    SDL_zero(context);
    context.renderer = renderer;
    context.camera = camera;
    context.ok = true;
    const bool iterated = sdl3d_game_data_for_each_sector_level_instance(runtime, draw_sector_level_instance, &context);
    SDL_free(context.sector_visible);
    return iterated && context.ok;
}

bool sdl3d_game_data_draw_render_primitives(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer)
{
    return draw_render_primitives_evaluated_with_cache(runtime, renderer, NULL, NULL, NULL, NULL);
}

bool sdl3d_game_data_draw_render_primitives_evaluated(const sdl3d_game_data_runtime *runtime,
                                                      sdl3d_render_context *renderer,
                                                      const sdl3d_game_data_render_eval *eval)
{
    return draw_render_primitives_evaluated_with_cache(runtime, renderer, eval, NULL, NULL, NULL);
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

    return sdl3d_game_data_for_each_ui_text_for_metrics(runtime, metrics, draw_ui_text, &context) && context.ok;
}

bool sdl3d_game_data_draw_ui_images(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                    sdl3d_game_data_image_cache *image_cache, const sdl3d_game_data_ui_metrics *metrics,
                                    const sdl3d_game_data_render_eval *render_eval)
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

    return sdl3d_game_data_for_each_ui_image(runtime, draw_ui_image, &context) && context.ok;
}

bool sdl3d_game_data_draw_ui_rects(const sdl3d_game_data_runtime *runtime, sdl3d_render_context *renderer,
                                   const sdl3d_game_data_ui_metrics *metrics,
                                   const sdl3d_game_data_render_eval *render_eval)
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

    return sdl3d_game_data_for_each_ui_rect(runtime, draw_ui_rect, &context) && context.ok;
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

static bool menu_input_is_idle(const sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input,
                               const sdl3d_game_data_menu *menu)
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

static void menu_result_set_scene_state(sdl3d_game_data_menu_update_result *result,
                                        const sdl3d_game_data_menu_item *item)
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

bool sdl3d_game_data_update_menus_for_metrics(sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input,
                                              bool *input_armed, const sdl3d_game_data_ui_metrics *metrics,
                                              sdl3d_game_data_menu_update_result *out_result)
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

    sdl3d_game_data_menu menu;
    if (!sdl3d_game_data_get_active_menu_for_metrics(runtime, metrics, &menu))
        return true;

    if (out_result != NULL)
    {
        out_result->menu = menu.name;
        out_result->selected_index = menu.selected_index;
    }

    if (sdl3d_game_data_menu_input_binding_capture_active(runtime))
    {
        const sdl3d_game_data_input_binding_capture_status capture_status =
            sdl3d_game_data_update_menu_input_binding_capture(runtime, input);
        if (out_result != NULL)
        {
            out_result->handled_input = true;
            out_result->input_binding_changed = capture_status == SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED;
            out_result->input_binding_conflict = capture_status == SDL3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT;
            out_result->selected = out_result->input_binding_changed;
            out_result->select_signal_id = out_result->input_binding_changed ? menu.select_signal_id : -1;
        }
        return true;
    }
    if (sdl3d_game_data_menu_text_entry_capture_active(runtime))
    {
        const sdl3d_game_data_text_entry_capture_status capture_status =
            sdl3d_game_data_update_menu_text_entry_capture(runtime, input);
        if (out_result != NULL)
        {
            out_result->handled_input = true;
            out_result->text_entry_changed = capture_status == SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CHANGED;
            out_result->text_entry_submitted = capture_status == SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_SUBMITTED;
            out_result->text_entry_canceled = capture_status == SDL3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED;
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
        const bool moved = sdl3d_game_data_menu_move(runtime, menu.name, -1);
        handled = moved || handled;
        if (moved && out_result != NULL)
            out_result->move_signal_id = menu.move_signal_id;
    }
    if (menu_action_pressed(runtime, input, menu.down_action_id, "ui_down"))
    {
        const bool moved = sdl3d_game_data_menu_move(runtime, menu.name, 1);
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
        sdl3d_game_data_menu refreshed;
        if (!sdl3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            return true;

        sdl3d_game_data_menu_item item;
        bool found_back_item = false;
        for (int i = 0; i < refreshed.item_count; ++i)
        {
            if (!sdl3d_game_data_get_menu_item(runtime, refreshed.name, i, &item))
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
        sdl3d_game_data_menu refreshed;
        if (!sdl3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            return true;

        sdl3d_game_data_menu_item item;
        /* Selecting the initially highlighted dynamic-list row should publish its selected outputs. */
        (void)sdl3d_game_data_publish_menu_selection(runtime, refreshed.name);
        if (!sdl3d_game_data_get_menu_item(runtime, refreshed.name, refreshed.selected_index, &item))
            return true;

        if (out_result != NULL)
        {
            out_result->menu = refreshed.name;
            out_result->selected_index = refreshed.selected_index;
            if (!control_adjust_input && item.control_type == SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
            {
                out_result->input_binding_capture_started =
                    sdl3d_game_data_start_menu_input_binding_capture(runtime, refreshed.name, refreshed.selected_index);
                out_result->selected = out_result->input_binding_capture_started;
                out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
                out_result->signal_id = -1;
                out_result->quit = false;
                out_result->scene = NULL;
                out_result->return_to = NULL;
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
                out_result->return_scene = false;
                out_result->pause_command = SDL3D_GAME_DATA_MENU_PAUSE_NONE;
                out_result->has_return_paused = false;
                out_result->return_paused = false;
                out_result->control_changed = false;
                out_result->handled_input = true;
                return true;
            }
            if (!control_adjust_input && item.control_type == SDL3D_GAME_DATA_MENU_CONTROL_TEXT)
            {
                out_result->text_entry_capture_started =
                    sdl3d_game_data_start_menu_text_entry_capture(runtime, refreshed.name, refreshed.selected_index);
                out_result->selected = out_result->text_entry_capture_started;
                out_result->select_signal_id = out_result->selected ? refreshed.select_signal_id : -1;
                out_result->signal_id = -1;
                out_result->quit = false;
                out_result->scene = NULL;
                out_result->return_to = NULL;
                out_result->scene_state_key = NULL;
                out_result->scene_state_value = NULL;
                out_result->return_scene = false;
                out_result->pause_command = SDL3D_GAME_DATA_MENU_PAUSE_NONE;
                out_result->has_return_paused = false;
                out_result->return_paused = false;
                out_result->control_changed = false;
                out_result->handled_input = true;
                return true;
            }
            out_result->control_changed =
                control_adjust_input && (item.control_type == SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING ||
                                         item.control_type == SDL3D_GAME_DATA_MENU_CONTROL_TEXT)
                    ? false
                    : sdl3d_game_data_adjust_menu_item_control(runtime, &item, control_direction);
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
                                            : SDL3D_GAME_DATA_MENU_PAUSE_NONE;
            out_result->has_return_paused =
                !control_adjust_input && !out_result->control_changed && item.has_return_paused;
            out_result->return_paused = item.return_paused;
        }
        else
        {
            if (!control_adjust_input && item.control_type == SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING)
                (void)sdl3d_game_data_start_menu_input_binding_capture(runtime, refreshed.name,
                                                                       refreshed.selected_index);
            else if (!control_adjust_input && item.control_type == SDL3D_GAME_DATA_MENU_CONTROL_TEXT)
                (void)sdl3d_game_data_start_menu_text_entry_capture(runtime, refreshed.name, refreshed.selected_index);
            else if (item.control_type != SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING &&
                     item.control_type != SDL3D_GAME_DATA_MENU_CONTROL_TEXT)
                (void)sdl3d_game_data_adjust_menu_item_control(runtime, &item, control_direction);
        }
    }
    else if (handled && out_result != NULL)
    {
        sdl3d_game_data_menu refreshed;
        if (sdl3d_game_data_get_active_menu_for_metrics(runtime, metrics, &refreshed))
            out_result->selected_index = refreshed.selected_index;
    }

    if (out_result != NULL)
        out_result->handled_input = handled;
    return true;
}

bool sdl3d_game_data_update_menus(sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input, bool *input_armed,
                                  sdl3d_game_data_menu_update_result *out_result)
{
    return sdl3d_game_data_update_menus_for_metrics(runtime, input, input_armed, NULL, out_result);
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
    if (sdl3d_game_data_active_scene_update_phase(runtime, "scene_activity", ctx->paused) &&
        !sdl3d_game_data_update_scene_activity(runtime, sdl3d_game_session_get_input(ctx->session), desc->dt))
        return false;

    if (sdl3d_game_data_active_scene_update_phase(runtime, "presentation", ctx->paused))
    {
        state->time += desc->dt;
        if (!sdl3d_game_data_update_presentation_clocks(runtime, desc->dt, ctx->paused, pause_entered))
            return false;
        if (!sdl3d_game_data_update_animations(runtime, desc->dt))
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
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D app quit requested");
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

static void app_flow_apply_pause_command(sdl3d_game_context *ctx, sdl3d_game_data_menu_pause_command command)
{
    if (ctx == NULL)
        return;

    if (command == SDL3D_GAME_DATA_MENU_PAUSE_PAUSE)
        ctx->paused = true;
    else if (command == SDL3D_GAME_DATA_MENU_PAUSE_RESUME)
        ctx->paused = false;
    else if (command == SDL3D_GAME_DATA_MENU_PAUSE_TOGGLE)
        ctx->paused = !ctx->paused;
}

static bool app_flow_apply_window_settings(const sdl3d_game_data_app_flow *flow, sdl3d_game_context *ctx,
                                           sdl3d_game_data_runtime *runtime)
{
    if (flow == NULL || ctx == NULL || runtime == NULL || ctx->window == NULL || ctx->renderer == NULL)
        return false;

    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, flow->app.window_settings_target);
    if (settings == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D window settings skipped: settings actor '%s' was not found",
                    flow->app.window_settings_target != NULL ? flow->app.window_settings_target : "");
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(ctx->window, &width, &height);

    sdl3d_window_config config;
    sdl3d_init_window_config(&config);
    config.width = width;
    config.height = height;
    config.logical_width = sdl3d_get_render_context_width(ctx->renderer);
    config.logical_height = sdl3d_get_render_context_height(ctx->renderer);
    config.title = SDL_GetWindowTitle(ctx->window);
    config.display_mode = parse_window_mode_setting(
        sdl3d_properties_get_string(settings->props, flow->app.window_display_mode_key, "windowed"),
        SDL3D_WINDOW_MODE_WINDOWED);
    config.backend =
        parse_backend_setting(sdl3d_properties_get_string(settings->props, flow->app.window_renderer_key, "opengl"),
                              sdl3d_get_render_context_backend(ctx->renderer));
    config.vsync = sdl3d_properties_get_bool(settings->props, flow->app.window_vsync_key, true);
    config.maximized = true;
    config.resizable = true;
    config.allow_backend_fallback = false;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D authored window apply requested: mode=%s renderer=%s vsync=%s",
                window_mode_setting_name(config.display_mode), sdl3d_get_backend_name(config.backend),
                config.vsync ? "on" : "off");
    return sdl3d_apply_window_config(&ctx->window, &ctx->renderer, &config);
}

static bool app_flow_consume_menu(sdl3d_game_data_app_flow *flow, sdl3d_game_context *ctx,
                                  sdl3d_game_data_runtime *runtime, sdl3d_input_manager *input, sdl3d_signal_bus *bus)
{
    if (flow->quit_pending || sdl3d_game_data_scene_flow_is_transitioning(&flow->scene_flow))
        return false;

    sdl3d_game_data_ui_metrics metrics;
    SDL_zero(metrics);
    metrics.paused = ctx != NULL && ctx->paused;

    sdl3d_game_data_menu_update_result result;
    if (!sdl3d_game_data_update_menus_for_metrics(runtime, input, &flow->scene_input_armed, &metrics, &result) ||
        !result.handled_input)
        return false;

    if (result.move_signal_id >= 0)
        sdl3d_signal_emit(bus, result.move_signal_id, NULL);
    if (result.select_signal_id >= 0)
        sdl3d_signal_emit(bus, result.select_signal_id, NULL);
    if (!result.selected)
        return true;

    if (result.signal_id >= 0)
        sdl3d_signal_emit(bus, result.signal_id, NULL);
    if (sdl3d_game_data_app_signal_applies_window_settings(runtime, result.signal_id))
        (void)app_flow_apply_window_settings(flow, ctx, runtime);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    if (result.return_to != NULL && scene_state != NULL)
        sdl3d_properties_set_string(scene_state, "return_scene", result.return_to);
    if (result.scene_state_key != NULL && result.scene_state_value != NULL && scene_state != NULL)
        sdl3d_properties_set_string(scene_state, result.scene_state_key, result.scene_state_value);
    if (result.has_return_paused && scene_state != NULL)
        sdl3d_properties_set_bool(scene_state, "return_paused", result.return_paused);

    const char *scene = result.scene;
    if (result.return_scene && scene_state != NULL)
    {
        scene = sdl3d_properties_get_string(scene_state, "return_scene", scene);
        if (ctx != NULL)
            ctx->paused = sdl3d_properties_get_bool(scene_state, "return_paused", ctx->paused);
    }
    app_flow_apply_pause_command(ctx, result.pause_command);

    if (result.quit)
        app_flow_request_quit(flow, ctx, runtime);
    else if (scene != NULL)
        (void)app_flow_request_scene(flow, runtime, scene);

    return true;
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
                                       const sdl3d_input_manager *input, bool capture_input, bool *out_applied,
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
            if (out_block_menus != NULL)
                *out_block_menus = policy.block_menus || policy.consume_input;
            if (out_block_scene_shortcuts != NULL)
                *out_block_scene_shortcuts = policy.block_scene_shortcuts || policy.consume_input;
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

static bool app_flow_timeline_is_pending(const sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime)
{
    sdl3d_game_data_timeline_policy policy;
    if (flow == NULL || runtime == NULL || flow->timeline.complete ||
        !sdl3d_game_data_get_active_timeline_policy(runtime, &policy))
    {
        return false;
    }
    return true;
}

static void app_flow_timeline_blocks(const sdl3d_game_data_app_flow *flow, sdl3d_game_data_runtime *runtime,
                                     bool *out_block_menus, bool *out_block_scene_shortcuts)
{
    if (out_block_menus != NULL)
        *out_block_menus = false;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = false;

    if (!app_flow_timeline_is_pending(flow, runtime))
        return;

    sdl3d_game_data_timeline_policy policy;
    if (!sdl3d_game_data_get_active_timeline_policy(runtime, &policy))
        return;

    if (out_block_menus != NULL)
        *out_block_menus = policy.block_menus;
    if (out_block_scene_shortcuts != NULL)
        *out_block_scene_shortcuts = policy.block_scene_shortcuts;
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
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D app quit transition finished");
        ctx->quit_requested = true;
    }
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
    bool skip_blocks_menus = false;
    bool skip_blocks_scene_shortcuts = false;
    const bool skip_consumed = app_flow_apply_skip_policy(flow, runtime, input, true, &skip_applied, &skip_blocks_menus,
                                                          &skip_blocks_scene_shortcuts);
    bool timeline_blocks_menus = false;
    bool timeline_blocks_scene_shortcuts = false;
    app_flow_timeline_blocks(flow, runtime, &timeline_blocks_menus, &timeline_blocks_scene_shortcuts);

    bool activity_blocks_menus = false;
    bool activity_blocks_scene_shortcuts = false;
    const bool activity_wake_consumed = sdl3d_game_data_scene_activity_consumes_wake_input(
        runtime, input, &activity_blocks_menus, &activity_blocks_scene_shortcuts);

    if (!skip_consumed && !activity_wake_consumed)
    {
        if (flow->app.quit_action_id >= 0 &&
            sdl3d_game_data_active_scene_allows_action(runtime, flow->app.quit_action_id) &&
            sdl3d_input_is_pressed(input, flow->app.quit_action_id))
            app_flow_request_quit(flow, ctx, runtime);

        if (!skip_blocks_scene_shortcuts && !timeline_blocks_scene_shortcuts && !activity_blocks_scene_shortcuts)
            app_flow_consume_scene_shortcuts(flow, runtime, input);
        bool menu_consumed = false;
        if (!skip_blocks_menus && !timeline_blocks_menus && !activity_blocks_menus)
            menu_consumed = app_flow_consume_menu(flow, ctx, runtime, input, bus);

        if (!menu_consumed && flow->app.pause_action_id >= 0 &&
            sdl3d_input_is_pressed(input, flow->app.pause_action_id) &&
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
    (void)app_flow_apply_skip_policy(flow, runtime, input, false, &deferred_skip_applied, NULL, NULL);
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
            ok = sdl3d_game_data_draw_sector_levels(frame->runtime, frame->renderer, &camera) && ok;
            if (frame->particle_cache != NULL)
                ok = sdl3d_game_data_draw_particles(frame->runtime, frame->renderer, frame->particle_cache) && ok;
            ok = draw_render_primitives_evaluated_with_cache(frame->runtime, frame->renderer, frame->render_eval,
                                                             frame->image_cache, frame->sprite_cache, &camera) &&
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
    ok = sdl3d_game_data_draw_ui_rects(frame->runtime, frame->renderer, frame->metrics, frame->render_eval) && ok;
    if (frame->image_cache != NULL)
        ok = sdl3d_game_data_draw_ui_images(frame->runtime, frame->renderer, frame->image_cache, frame->metrics,
                                            frame->render_eval) &&
             ok;
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
