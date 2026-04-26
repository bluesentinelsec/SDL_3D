/**
 * @file transition.h
 * @brief Screen transition state and rendering API.
 *
 * Transitions are time-based full-screen effects used for scene changes,
 * level loads, menu handoffs, and polished quits. The transition state
 * machine is renderer-independent; sdl3d_transition_draw dispatches to the
 * active backend.
 */

#ifndef SDL3D_TRANSITION_H
#define SDL3D_TRANSITION_H

#include <stdbool.h>

#include "sdl3d/render_context.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_TRANSITION_MELT_COLUMNS 320

    /**
     * @brief Visual effect used for a screen transition.
     */
    typedef enum sdl3d_transition_type
    {
        SDL3D_TRANSITION_FADE = 0,     /**< Linear blend to or from a solid color. */
        SDL3D_TRANSITION_CIRCLE = 1,   /**< Centered circular reveal or cover. */
        SDL3D_TRANSITION_MELT = 2,     /**< Doom-style staggered column melt. */
        SDL3D_TRANSITION_PIXELATE = 3, /**< Pixel block dissolve approximation. */
    } sdl3d_transition_type;

    /**
     * @brief Whether a transition reveals the scene or covers it.
     */
    typedef enum sdl3d_transition_direction
    {
        SDL3D_TRANSITION_IN = 0,  /**< Effect recedes and reveals the scene. */
        SDL3D_TRANSITION_OUT = 1, /**< Effect grows and covers the scene. */
    } sdl3d_transition_direction;

    /**
     * @brief Complete state for one active or completed transition.
     *
     * The struct is caller-owned and may be stack, static, or heap allocated.
     * Use sdl3d_transition_start to initialize and begin an effect, then call
     * sdl3d_transition_update once per simulation tick and
     * sdl3d_transition_draw once per rendered frame.
     */
    typedef struct sdl3d_transition
    {
        sdl3d_transition_type type;                        /**< Transition visual effect. */
        sdl3d_transition_direction direction;              /**< Reveal or cover direction. */
        sdl3d_color color;                                 /**< Solid transition color. */
        float duration;                                    /**< Transition length in seconds. */
        float elapsed;                                     /**< Elapsed transition time in seconds. */
        bool active;                                       /**< True while the transition is still running. */
        bool finished;                                     /**< True after a transition reaches duration. */
        int done_signal_id;                                /**< Signal emitted on completion, or -1 for none. */
        float melt_offsets[SDL3D_TRANSITION_MELT_COLUMNS]; /**< Deterministic per-column melt offsets. */
    } sdl3d_transition;

    /**
     * @brief Start or restart a transition.
     *
     * @param transition     Transition state to initialize.
     * @param type           Visual effect.
     * @param direction      Whether the effect reveals or covers the scene.
     * @param color          Transition color.
     * @param duration       Length in seconds. Values <= 0 use a tiny positive duration.
     * @param done_signal_id Signal emitted on completion, or -1 to emit nothing.
     */
    void sdl3d_transition_start(sdl3d_transition *transition, sdl3d_transition_type type,
                                sdl3d_transition_direction direction, sdl3d_color color, float duration,
                                int done_signal_id);

    /**
     * @brief Advance a transition and emit its completion signal if needed.
     *
     * Completion is emitted at most once. Negative dt values are ignored.
     *
     * @param transition Transition to update.
     * @param bus        Optional signal bus used for completion notification.
     * @param dt         Fixed or variable timestep in seconds.
     */
    void sdl3d_transition_update(sdl3d_transition *transition, sdl3d_signal_bus *bus, float dt);

    /**
     * @brief Draw or queue a transition on the active render backend.
     *
     * The software backend draws immediately into the color buffer. The GL
     * backend queues the transition so it can be applied during present after
     * post-processing and before the final window blit.
     *
     * @param transition Transition to draw. Inactive transitions are no-ops.
     * @param context    Render context receiving the transition.
     */
    void sdl3d_transition_draw(const sdl3d_transition *transition, sdl3d_render_context *context);

    /**
     * @brief Reset transition state to an inactive default.
     *
     * The reset state does not emit a signal; done_signal_id is set to -1.
     */
    void sdl3d_transition_reset(sdl3d_transition *transition);

    /**
     * @brief Return normalized progress in the range [0, 1].
     *
     * Returns 0 for NULL transitions or non-positive durations.
     */
    float sdl3d_transition_progress(const sdl3d_transition *transition);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_TRANSITION_H */
