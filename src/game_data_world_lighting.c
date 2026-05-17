/**
 * @file game_data_world_lighting.c
 * @brief Sector lighting and sector-level variant helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static int sector_level_resolve_sector_index(const sector_level_runtime *level, const char *sector)
{
    if (level == NULL || sector == NULL || sector[0] == '\0')
        return -1;
    const int named_index = sector_level_find_sector_name(level, sector);
    if (named_index >= 0)
        return named_index;

    char *end = NULL;
    const long parsed = SDL_strtol(sector, &end, 10);
    if (end != NULL && *end == '\0' && parsed >= 0 && parsed < level->sector_count)
        return (int)parsed;
    return -1;
}

static float clamp01(float value)
{
    if (value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value;
}

static void sector_lighting_rgb(const slayer3d_sector *sector, float out_rgb[3])
{
    if (out_rgb == NULL)
        return;
    out_rgb[0] = 1.0f;
    out_rgb[1] = 1.0f;
    out_rgb[2] = 1.0f;
    if (sector == NULL || !sector->has_lighting)
        return;

    const float level = SDL_clamp(sector->lighting_level, 0.0f, 255.0f) / 255.0f;
    const float influence = clamp01(sector->lighting_color[3]);
    for (int i = 0; i < 3; ++i)
    {
        const float tint = 1.0f + (clamp01(sector->lighting_color[i]) - 1.0f) * influence;
        out_rgb[i] = level * tint;
    }
}

static Uint8 color_channel_from_float(float value)
{
    value = SDL_clamp(value, 0.0f, 255.0f);
    return (Uint8)(value + 0.5f);
}

void modulate_color_by_sector_lighting(slayer3d_color *color, const slayer3d_sector *sector)
{
    if (color == NULL || sector == NULL || !sector->has_lighting)
        return;
    float lighting[3];
    sector_lighting_rgb(sector, lighting);
    color->r = color_channel_from_float((float)color->r * lighting[0]);
    color->g = color_channel_from_float((float)color->g * lighting[1]);
    color->b = color_channel_from_float((float)color->b * lighting[2]);
}

static bool rebuild_sector_level_variants_atomic(sector_level_runtime *level, char *error_buffer, int error_buffer_size)
{
    if (level == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector level is invalid");
        return false;
    }

    slayer3d_level lightmapped;
    slayer3d_level vertex_baked;
    slayer3d_level unlit;
    slayer3d_level lightmapped_without_sector_lighting;
    slayer3d_level vertex_baked_without_sector_lighting;
    slayer3d_level unlit_without_sector_lighting;
    SDL_zero(lightmapped);
    SDL_zero(vertex_baked);
    SDL_zero(unlit);
    SDL_zero(lightmapped_without_sector_lighting);
    SDL_zero(vertex_baked_without_sector_lighting);
    SDL_zero(unlit_without_sector_lighting);

    bool ok = true;
    if (!build_sector_level_variant_set(level, level->sectors, &lightmapped, &vertex_baked, &unlit, "sector-lit",
                                        error_buffer, error_buffer_size))
    {
        ok = false;
    }
    slayer3d_sector *unlit_sectors = NULL;
    if (ok)
    {
        unlit_sectors = copy_sectors_without_sector_lighting(level);
        if (unlit_sectors == NULL && level->sector_count > 0)
        {
            set_error(error_buffer, error_buffer_size, "failed to allocate sector lighting toggle rebuild variants");
            ok = false;
        }
    }
    if (ok && !build_sector_level_variant_set(level, unlit_sectors != NULL ? unlit_sectors : level->sectors,
                                              &lightmapped_without_sector_lighting,
                                              &vertex_baked_without_sector_lighting, &unlit_without_sector_lighting,
                                              "sector-neutral", error_buffer, error_buffer_size))
    {
        ok = false;
    }
    SDL_free(unlit_sectors);

    if (!ok)
    {
        slayer3d_free_level(&lightmapped);
        slayer3d_free_level(&vertex_baked);
        slayer3d_free_level(&unlit);
        slayer3d_free_level(&lightmapped_without_sector_lighting);
        slayer3d_free_level(&vertex_baked_without_sector_lighting);
        slayer3d_free_level(&unlit_without_sector_lighting);
        return false;
    }

    slayer3d_free_level(&level->lightmapped);
    slayer3d_free_level(&level->vertex_baked);
    slayer3d_free_level(&level->unlit);
    slayer3d_free_level(&level->lightmapped_without_sector_lighting);
    slayer3d_free_level(&level->vertex_baked_without_sector_lighting);
    slayer3d_free_level(&level->unlit_without_sector_lighting);
    level->lightmapped = lightmapped;
    level->vertex_baked = vertex_baked;
    level->unlit = unlit;
    level->lightmapped_without_sector_lighting = lightmapped_without_sector_lighting;
    level->vertex_baked_without_sector_lighting = vertex_baked_without_sector_lighting;
    level->unlit_without_sector_lighting = unlit_without_sector_lighting;
    return true;
}

bool slayer3d_game_data_get_sector_lighting(const slayer3d_game_data_runtime *runtime, const char *sector_level,
                                            const char *sector, float *out_level, float out_color[4],
                                            char *error_buffer, int error_buffer_size)
{
    if (out_level != NULL)
        *out_level = 0.0f;
    if (out_color != NULL)
        SDL_memset(out_color, 0, sizeof(float) * 4U);
    const sector_level_runtime *level = find_sector_level_runtime(runtime, sector_level);
    const int sector_index = sector_level_resolve_sector_index(level, sector);
    if (level == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' not found",
                   sector_level != NULL ? sector_level : "<null>");
        return false;
    }
    if (sector_index < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' not found", sector != NULL ? sector : "<null>");
        return false;
    }

    const slayer3d_sector *resolved = &level->sectors[sector_index];
    if (!resolved->has_lighting)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' has no authored lighting", sector);
        return false;
    }
    if (out_level == NULL || out_color == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector lighting output pointers are required");
        return false;
    }
    *out_level = resolved->lighting_level;
    SDL_memcpy(out_color, resolved->lighting_color, sizeof(resolved->lighting_color));
    return true;
}

bool slayer3d_game_data_set_sector_lighting(slayer3d_game_data_runtime *runtime, const char *sector_level,
                                            const char *sector, float level, const float color[4], char *error_buffer,
                                            int error_buffer_size)
{
    sector_level_runtime *resolved_level = find_sector_level_runtime_mutable(runtime, sector_level);
    const int sector_index = sector_level_resolve_sector_index(resolved_level, sector);
    if (resolved_level == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "sector level '%s' not found",
                   sector_level != NULL ? sector_level : "<null>");
        return false;
    }
    if (sector_index < 0)
    {
        set_errorf(error_buffer, error_buffer_size, "sector '%s' not found", sector != NULL ? sector : "<null>");
        return false;
    }
    if (color == NULL)
    {
        set_error(error_buffer, error_buffer_size, "sector lighting color is required");
        return false;
    }

    slayer3d_sector *target = &resolved_level->sectors[sector_index];
    const bool old_has_lighting = target->has_lighting;
    const float old_level = target->lighting_level;
    float old_color[4];
    SDL_memcpy(old_color, target->lighting_color, sizeof(old_color));

    target->has_lighting = true;
    target->lighting_level = SDL_clamp(level, 0.0f, 255.0f);
    for (int i = 0; i < 4; ++i)
        target->lighting_color[i] = clamp01(color[i]);

    if (!rebuild_sector_level_variants_atomic(resolved_level, error_buffer, error_buffer_size))
    {
        target->has_lighting = old_has_lighting;
        target->lighting_level = old_level;
        SDL_memcpy(target->lighting_color, old_color, sizeof(target->lighting_color));
        (void)rebuild_sector_level_variants_atomic(resolved_level, NULL, 0);
        return false;
    }
    return true;
}

slayer3d_game_data_sector_level_variant sector_level_variant_from_string(const char *variant,
                                                                         const slayer3d_level **out_level,
                                                                         const sector_level_runtime *level,
                                                                         bool sector_lighting_enabled)
{
    if (out_level != NULL)
        *out_level = NULL;
    if (level == NULL)
        return 0;

    const char *name = variant != NULL ? variant : "lightmapped";
    if (SDL_strcmp(name, "lightmapped") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->lightmapped : &level->lightmapped_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_LIGHTMAPPED;
    }
    if (SDL_strcmp(name, "vertex_baked") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->vertex_baked : &level->vertex_baked_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_VERTEX_BAKED;
    }
    if (SDL_strcmp(name, "unlit") == 0)
    {
        if (out_level != NULL)
            *out_level = sector_lighting_enabled ? &level->unlit : &level->unlit_without_sector_lighting;
        return SLAYER3D_GAME_DATA_SECTOR_LEVEL_UNLIT;
    }
    return 0;
}
