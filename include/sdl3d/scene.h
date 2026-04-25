#ifndef SDL3D_SCENE_H
#define SDL3D_SCENE_H

#include <stdbool.h>

#include "sdl3d/level.h"
#include "sdl3d/model.h"
#include "sdl3d/render_context.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sdl3d_actor sdl3d_actor;
    typedef struct sdl3d_scene sdl3d_scene;

    /* ============================================================== */
    /* Scene lifecycle                                                 */
    /* ============================================================== */

    sdl3d_scene *sdl3d_create_scene(void);
    void sdl3d_destroy_scene(sdl3d_scene *scene);

    /* ============================================================== */
    /* Actor management                                               */
    /* ============================================================== */

    /*
     * Add an actor to the scene referencing the given model. The model
     * must outlive the actor. Returns NULL on failure.
     */
    sdl3d_actor *sdl3d_scene_add_actor(sdl3d_scene *scene, const sdl3d_model *model);

    /*
     * Remove and free an actor from the scene. Safe with NULL.
     */
    void sdl3d_scene_remove_actor(sdl3d_scene *scene, sdl3d_actor *actor);

    int sdl3d_scene_get_actor_count(const sdl3d_scene *scene);

    /*
     * Get actor by index (0-based). Returns NULL if out of range.
     */
    sdl3d_actor *sdl3d_scene_get_actor_at(const sdl3d_scene *scene, int index);

    /* ============================================================== */
    /* Actor properties                                               */
    /* ============================================================== */

    void sdl3d_actor_set_position(sdl3d_actor *actor, sdl3d_vec3 position);
    sdl3d_vec3 sdl3d_actor_get_position(const sdl3d_actor *actor);

    void sdl3d_actor_set_rotation(sdl3d_actor *actor, sdl3d_vec3 axis, float angle_radians);

    void sdl3d_actor_set_scale(sdl3d_actor *actor, sdl3d_vec3 scale);
    sdl3d_vec3 sdl3d_actor_get_scale(const sdl3d_actor *actor);

    void sdl3d_actor_set_visible(sdl3d_actor *actor, bool visible);
    bool sdl3d_actor_is_visible(const sdl3d_actor *actor);

    void sdl3d_actor_set_tint(sdl3d_actor *actor, sdl3d_color tint);

    /*
     * Optional sector ownership for portal-based visibility culling.
     * Defaults to -1 (no sector). When set and a sdl3d_visibility_result
     * is supplied to sdl3d_draw_scene_with_visibility, actors whose
     * sector is not visible are skipped before any frustum work.
     *
     * Callers typically derive the id with sdl3d_level_find_sector
     * after positioning the actor.
     */
    void sdl3d_actor_set_sector(sdl3d_actor *actor, int sector_id);
    int sdl3d_actor_get_sector(const sdl3d_actor *actor);

    const sdl3d_model *sdl3d_actor_get_model(const sdl3d_actor *actor);

    /* ============================================================== */
    /* Drawing                                                        */
    /* ============================================================== */

    /*
     * Draw all visible actors in the scene. Must be called between
     * sdl3d_begin_mode_3d and sdl3d_end_mode_3d. The caller owns the
     * camera, lighting, and render profile — this function simply
     * iterates actors and calls sdl3d_draw_model_ex for each.
     */
    bool sdl3d_draw_scene(sdl3d_render_context *context, const sdl3d_scene *scene);

    /*
     * Same as sdl3d_draw_scene, but additionally rejects actors whose
     * assigned sector (see sdl3d_actor_set_sector) is not present in the
     * supplied visibility result. Pass vis = NULL for behavior identical
     * to sdl3d_draw_scene. Actors with sector_id < 0 are unaffected by
     * the visibility test and fall through to the frustum check.
     */
    bool sdl3d_draw_scene_with_visibility(sdl3d_render_context *context, const sdl3d_scene *scene,
                                          const sdl3d_visibility_result *vis);

#ifdef __cplusplus
}
#endif

#endif
