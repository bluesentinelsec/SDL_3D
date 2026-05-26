/**
 * @file game_presentation_sector_levels.c
 * @brief Sector-level drawing for game presentation.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/collision.h"
#include "slayer3d/drawing3d.h"
#include "slayer3d/lighting.h"

#include "game_data_internal.h"

typedef struct sector_level_draw_context
{
    slayer3d_render_context *renderer;
    const slayer3d_asset_resolver *assets;
    const slayer3d_camera3d *camera;
    bool *sector_visible;
    int sector_visible_capacity;
    bool ok;
} sector_level_draw_context;

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
