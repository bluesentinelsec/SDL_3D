/**
 * @file pong_rules.h
 * @brief Pure gameplay rules for the SDL3D Pong demo.
 *
 * The Pong demo uses SDL3D's generic runtime services for input, timing,
 * signaling, actors, rendering, particles, and transitions. This module owns
 * only Pong-specific deterministic simulation: paddle movement, CPU tracking,
 * ball serving, collision, scoring, and match end state.
 */

#ifndef SDL3D_DEMOS_PONG_RULES_H
#define SDL3D_DEMOS_PONG_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Winner of a Pong match. */
    typedef enum pong_winner
    {
        PONG_WINNER_NONE = 0, /**< No player has won yet. */
        PONG_WINNER_PLAYER,   /**< The human player reached the score limit. */
        PONG_WINNER_CPU       /**< The CPU reached the score limit. */
    } pong_winner;

    /** @brief Events emitted by one rules step. */
    typedef enum pong_event
    {
        PONG_EVENT_NONE = 0,               /**< No notable event occurred. */
        PONG_EVENT_WALL_BOUNCE = 1 << 0,   /**< Ball reflected off the upper/lower playfield bound. */
        PONG_EVENT_PADDLE_BOUNCE = 1 << 1, /**< Ball reflected off a paddle. */
        PONG_EVENT_PLAYER_SCORED = 1 << 2, /**< Human player scored this step. */
        PONG_EVENT_CPU_SCORED = 1 << 3,    /**< CPU scored this step. */
        PONG_EVENT_MATCH_FINISHED = 1 << 4 /**< A score event ended the match. */
    } pong_event;

    /** @brief Runtime-tunable constants for Pong simulation. */
    typedef struct pong_rules_config
    {
        float half_width;           /**< Half-width of the playable field. */
        float half_height;          /**< Half-height of the playable field. */
        float paddle_x;             /**< Absolute paddle X position from center. */
        float paddle_half_width;    /**< Paddle half-width used for collision. */
        float paddle_half_height;   /**< Paddle half-height used for collision. */
        float paddle_speed;         /**< Human paddle speed in world units per second. */
        float cpu_speed;            /**< CPU paddle speed in world units per second. */
        float ball_radius;          /**< Collision radius for the ball. */
        float ball_speed;           /**< Initial serve speed. */
        float ball_speedup_per_hit; /**< Speed gained on each paddle hit. */
        float max_ball_speed;       /**< Maximum ball speed after repeated hits. */
        int winning_score;          /**< Match score limit. */
    } pong_rules_config;

    /** @brief Complete Pong match state. */
    typedef struct pong_rules_state
    {
        pong_rules_config config; /**< Copied rules config. */
        int player_score;         /**< Human score. */
        int cpu_score;            /**< CPU score. */
        float player_y;           /**< Human paddle center Y. */
        float cpu_y;              /**< CPU paddle center Y. */
        sdl3d_vec2 ball_position; /**< Ball center in playfield coordinates. */
        sdl3d_vec2 ball_velocity; /**< Ball velocity in world units per second. */
        bool ball_active;         /**< False while waiting for a delayed serve. */
        bool match_finished;      /**< True once either side reaches winning_score. */
        pong_winner winner;       /**< Winner when match_finished is true. */
        uint32_t rng_state;       /**< Deterministic local random state for serves. */
    } pong_rules_state;

    /** @brief Inputs consumed by one Pong simulation step. */
    typedef struct pong_rules_input
    {
        float player_axis; /**< Desired human paddle movement in [-1, 1], positive up. */
    } pong_rules_input;

    /** @brief Result produced by one Pong simulation step. */
    typedef struct pong_rules_step_result
    {
        int events;         /**< Bitmask of pong_event values. */
        pong_winner winner; /**< Winner after this step, or PONG_WINNER_NONE. */
        int player_score;   /**< Human score after this step. */
        int cpu_score;      /**< CPU score after this step. */
    } pong_rules_step_result;

    /** @brief Return conservative default simulation constants for the demo. */
    pong_rules_config pong_rules_default_config(void);

    /**
     * @brief Initialize a rules state for a new match.
     *
     * @param state Rules state to initialize.
     * @param config Optional config. NULL uses pong_rules_default_config().
     * @param seed Nonzero deterministic random seed. Zero selects a stable default.
     */
    void pong_rules_init(pong_rules_state *state, const pong_rules_config *config, uint32_t seed);

    /**
     * @brief Reset scores and positions for a fresh match.
     *
     * The ball is centered and inactive. Call pong_rules_serve_random() after
     * the desired delay to put it in motion.
     */
    void pong_rules_reset_match(pong_rules_state *state);

    /**
     * @brief Center the ball and make it inactive between points.
     */
    void pong_rules_reset_round(pong_rules_state *state);

    /**
     * @brief Serve the ball in a randomized, playable direction.
     *
     * @param state Rules state.
     * @param direction Positive serves right, negative serves left, zero picks randomly.
     */
    void pong_rules_serve_random(pong_rules_state *state, int direction);

    /**
     * @brief Advance Pong simulation by one fixed step.
     *
     * @param state Rules state.
     * @param input Optional inputs. NULL means no human paddle movement.
     * @param dt Elapsed seconds. Negative values are ignored and large values are clamped.
     * @return Step events and current score summary.
     */
    pong_rules_step_result pong_rules_step(pong_rules_state *state, const pong_rules_input *input, float dt);

    /** @brief Return true when the ball is waiting to be served. */
    bool pong_rules_is_waiting_for_serve(const pong_rules_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_DEMOS_PONG_RULES_H */
