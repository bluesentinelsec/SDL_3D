/**
 * @file game_presentation_frame_state.c
 * @brief Frame update and render metrics sampling for data-authored games.
 */

#include "slayer3d/game_presentation.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include "game_data_internal.h"
#include "game_presentation_internal.h"
#include "render_context_internal.h"

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
                        state->model_lod_candidates_sample_sum += render_stat_delta_u64(
                            state->last_render_stats.model_lod_candidates, stats.model_lod_candidates);
                        state->model_lod_culled_sample_sum +=
                            render_stat_delta_u64(state->last_render_stats.model_lod_culled, stats.model_lod_culled);
                        state->model_lod_triangles_saved_sum += render_stat_delta_u64(
                            state->last_render_stats.model_lod_triangles_saved, stats.model_lod_triangles_saved);
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
                state->metrics.render_model_lod_candidates_per_frame =
                    state->model_lod_candidates_sample_sum / sample_frames;
                state->metrics.render_model_lod_culled_per_frame = state->model_lod_culled_sample_sum / sample_frames;
                state->metrics.render_model_lod_triangles_saved_per_frame =
                    state->model_lod_triangles_saved_sum / sample_frames;
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
                state->model_lod_candidates_sample_sum = 0.0f;
                state->model_lod_culled_sample_sum = 0.0f;
                state->model_lod_triangles_saved_sum = 0.0f;
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
    if (ctx->window != NULL)
    {
        int pixel_width = 0;
        int pixel_height = 0;
        if (SDL_GetWindowSizeInPixels(ctx->window, &pixel_width, &pixel_height))
        {
            state->metrics.render_window_pixel_width = (float)pixel_width;
            state->metrics.render_window_pixel_height = (float)pixel_height;
        }
        state->metrics.render_window_pixel_density = SDL_GetWindowPixelDensity(ctx->window);
    }
    state->last_render_time = ctx->real_time;
    ++state->rendered_frames;
    state->metrics.paused = ctx->paused;
    state->metrics.fps = state->displayed_fps;
    state->metrics.frame = state->rendered_frames;
    state->render_eval.time = state->time;
    state->ui_pulse_phase = slayer3d_game_data_ui_pulse_phase(runtime, state->ui_pulse_phase);
}
