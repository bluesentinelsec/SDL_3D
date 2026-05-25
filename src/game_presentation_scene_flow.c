/**
 * @file game_presentation_scene_flow.c
 * @brief Data-authored scene transition flow helpers.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_stdinc.h>

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
