#ifndef SLAYER3D_SCENE_H
#define SLAYER3D_SCENE_H

#include <stdbool.h>

#include "slayer3d/level.h"
#include "slayer3d/model.h"
#include "slayer3d/render_context.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_actor slayer3d_actor;
    typedef struct slayer3d_scene slayer3d_scene;

    /* ============================================================== */
    /* Scene lifecycle                                                 */
    /* ============================================================== */

    slayer3d_scene *slayer3d_create_scene(void);
    void slayer3d_destroy_scene(slayer3d_scene *scene);

    /* ============================================================== */
    /* Actor management                                               */
    /* ============================================================== */

    /*
     * Add an actor to the scene referencing the given model. The model
     * must outlive the actor. Returns NULL on failure.
     */
    slayer3d_actor *slayer3d_scene_add_actor(slayer3d_scene *scene, const slayer3d_model *model);

    /*
     * Remove and free an actor from the scene. Safe with NULL.
     */
    void slayer3d_scene_remove_actor(slayer3d_scene *scene, slayer3d_actor *actor);

    int slayer3d_scene_get_actor_count(const slayer3d_scene *scene);

    /*
     * Get actor by index (0-based). Returns NULL if out of range.
     */
    slayer3d_actor *slayer3d_scene_get_actor_at(const slayer3d_scene *scene, int index);

    /* ============================================================== */
    /* Actor properties                                               */
    /* ============================================================== */

    void slayer3d_actor_set_position(slayer3d_actor *actor, slayer3d_vec3 position);
    slayer3d_vec3 slayer3d_actor_get_position(const slayer3d_actor *actor);

    void slayer3d_actor_set_rotation(slayer3d_actor *actor, slayer3d_vec3 axis, float angle_radians);

    void slayer3d_actor_set_scale(slayer3d_actor *actor, slayer3d_vec3 scale);
    slayer3d_vec3 slayer3d_actor_get_scale(const slayer3d_actor *actor);

    void slayer3d_actor_set_visible(slayer3d_actor *actor, bool visible);
    bool slayer3d_actor_is_visible(const slayer3d_actor *actor);

    void slayer3d_actor_set_tint(slayer3d_actor *actor, slayer3d_color tint);

    /*
     * Optional sector ownership for portal-based visibility culling.
     * Defaults to -1 (no sector). When set and a slayer3d_visibility_result
     * is supplied to slayer3d_draw_scene_with_visibility, actors whose
     * sector is not visible are skipped before any frustum work.
     *
     * Callers typically derive the id with slayer3d_level_find_sector
     * after positioning the actor.
     */
    void slayer3d_actor_set_sector(slayer3d_actor *actor, int sector_id);
    int slayer3d_actor_get_sector(const slayer3d_actor *actor);

    const slayer3d_model *slayer3d_actor_get_model(const slayer3d_actor *actor);

    /* ============================================================== */
    /* Drawing                                                        */
    /* ============================================================== */

    /*
     * Draw all visible actors in the scene. Must be called between
     * slayer3d_begin_mode_3d and slayer3d_end_mode_3d. The caller owns the
     * camera, lighting, and render profile — this function simply
     * iterates actors and calls slayer3d_draw_model_ex for each.
     */
    bool slayer3d_draw_scene(slayer3d_render_context *context, const slayer3d_scene *scene);

    /*
     * Same as slayer3d_draw_scene, but additionally rejects actors whose
     * assigned sector (see slayer3d_actor_set_sector) is not present in the
     * supplied visibility result. Pass vis = NULL for behavior identical
     * to slayer3d_draw_scene. Actors with sector_id < 0 are unaffected by
     * the visibility test and fall through to the frustum check.
     */
    bool slayer3d_draw_scene_with_visibility(slayer3d_render_context *context, const slayer3d_scene *scene,
                                             const slayer3d_visibility_result *vis);

#ifdef __cplusplus
}
#endif

#endif
