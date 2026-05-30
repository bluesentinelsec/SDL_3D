/**
 * @file game_data_ui_bindings.c
 * @brief Shared string formatting for data-authored UI bindings.
 */

#include "game_data_internal.h"

static bool game_data_ui_value_to_string(const slayer3d_value *value, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (value == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        SDL_snprintf(buffer, buffer_size, "%d", value->as_int);
        return true;
    case SLAYER3D_VALUE_FLOAT:
        SDL_snprintf(buffer, buffer_size, "%.3f", value->as_float);
        return true;
    case SLAYER3D_VALUE_BOOL:
        SDL_snprintf(buffer, buffer_size, "%s", value->as_bool ? "true" : "false");
        return true;
    case SLAYER3D_VALUE_VEC3:
        SDL_snprintf(buffer, buffer_size, "%.3f, %.3f, %.3f", value->as_vec3.x, value->as_vec3.y, value->as_vec3.z);
        return true;
    case SLAYER3D_VALUE_STRING:
        SDL_snprintf(buffer, buffer_size, "%s", value->as_string != NULL ? value->as_string : "");
        return true;
    case SLAYER3D_VALUE_COLOR:
        SDL_snprintf(buffer, buffer_size, "%u, %u, %u, %u", (unsigned)value->as_color.r, (unsigned)value->as_color.g,
                     (unsigned)value->as_color.b, (unsigned)value->as_color.a);
        return true;
    }
    return false;
}

bool slayer3d_game_data_ui_json_scalar_to_string(yyjson_val *value, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (yyjson_is_str(value))
        SDL_snprintf(buffer, buffer_size, "%s", yyjson_get_str(value));
    else if (yyjson_is_int(value))
        SDL_snprintf(buffer, buffer_size, "%lld", (long long)yyjson_get_sint(value));
    else if (yyjson_is_real(value))
        SDL_snprintf(buffer, buffer_size, "%.3f", yyjson_get_real(value));
    else if (yyjson_is_bool(value))
        SDL_snprintf(buffer, buffer_size, "%s", yyjson_get_bool(value) ? "true" : "false");
    else
        return false;
    return true;
}

static bool game_data_ui_metric_to_string(const slayer3d_game_data_ui_metrics *metrics, const char *metric,
                                          char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (metrics == NULL || metric == NULL)
        return false;
    if (SDL_strcmp(metric, "fps") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->fps);
    else if (SDL_strcmp(metric, "frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%llu", (unsigned long long)metrics->frame);
    else if (SDL_strcmp(metric, "paused") == 0)
        SDL_snprintf(buffer, buffer_size, "%s", metrics->paused ? "true" : "false");
    else if (SDL_strcmp(metric, "perf.frame_ms") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->frame_ms);
    else if (SDL_strcmp(metric, "perf.update_cpu_ms") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->update_cpu_ms);
    else if (SDL_strcmp(metric, "perf.render_cpu_ms") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_cpu_ms);
    else if (SDL_strcmp(metric, "render.model_mesh_submissions_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_model_mesh_submissions_per_frame);
    else if (SDL_strcmp(metric, "render.model_mesh_draws_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_model_mesh_draws_per_frame);
    else if (SDL_strcmp(metric, "render.model_triangles_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_model_triangles_per_frame);
    else if (SDL_strcmp(metric, "render.geometry_draw_calls_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_geometry_draw_calls_per_frame);
    else if (SDL_strcmp(metric, "render.static_mesh_instanced_draw_calls_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_static_mesh_instanced_draw_calls_per_frame);
    else if (SDL_strcmp(metric, "render.static_mesh_instances_batched_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_static_mesh_instances_batched_per_frame);
    else if (SDL_strcmp(metric, "render.static_mesh_draw_calls_saved_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_static_mesh_draw_calls_saved_per_frame);
    else if (SDL_strcmp(metric, "render.procedural_lod_candidates_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_procedural_lod_candidates_per_frame);
    else if (SDL_strcmp(metric, "render.procedural_lod_reduced_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_procedural_lod_reduced_per_frame);
    else if (SDL_strcmp(metric, "render.procedural_lod_authored_triangles_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_procedural_lod_authored_triangles_per_frame);
    else if (SDL_strcmp(metric, "render.procedural_lod_resolved_triangles_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_procedural_lod_resolved_triangles_per_frame);
    else if (SDL_strcmp(metric, "render.procedural_lod_triangles_saved_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_procedural_lod_triangles_saved_per_frame);
    else if (SDL_strcmp(metric, "render.model_lod_candidates_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_model_lod_candidates_per_frame);
    else if (SDL_strcmp(metric, "render.model_lod_culled_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_model_lod_culled_per_frame);
    else if (SDL_strcmp(metric, "render.model_lod_triangles_saved_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_model_lod_triangles_saved_per_frame);
    else if (SDL_strcmp(metric, "render.depth_prepass_draws_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_depth_prepass_draws_per_frame);
    else if (SDL_strcmp(metric, "render.depth_prepass_triangles_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_depth_prepass_triangles_per_frame);
    else if (SDL_strcmp(metric, "render.depth_prepass_samples_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_depth_prepass_samples_per_frame);
    else if (SDL_strcmp(metric, "render.geometry_samples_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_geometry_samples_per_frame);
    else if (SDL_strcmp(metric, "render.light_candidates_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_light_candidates_per_frame);
    else if (SDL_strcmp(metric, "render.lights_selected_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_lights_selected_per_frame);
    else if (SDL_strcmp(metric, "render.light_selection_draws_per_frame") == 0)
        SDL_snprintf(buffer, buffer_size, "%.1f", metrics->render_light_selection_draws_per_frame);
    else if (SDL_strcmp(metric, "render.light_selection_ratio") == 0)
        SDL_snprintf(buffer, buffer_size, "%.3f", metrics->render_light_selection_ratio);
    else
        return false;
    return true;
}

bool slayer3d_game_data_ui_binding_to_string(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_ui_metrics *metrics, yyjson_val *binding,
                                             char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (!yyjson_is_obj(binding))
        return false;

    const char *type = json_string(binding, "type", "");
    bool formatted = false;
    if (SDL_strcmp(type, "scene_state") == 0)
    {
        const slayer3d_properties *scene_state = slayer3d_game_data_scene_state(runtime);
        const slayer3d_value *value =
            scene_state != NULL ? slayer3d_properties_get_value(scene_state, json_string(binding, "key", NULL)) : NULL;
        formatted = game_data_ui_value_to_string(value, buffer, buffer_size);
    }
    else if (SDL_strcmp(type, "property") == 0)
    {
        const slayer3d_registered_actor *actor =
            slayer3d_game_data_find_actor(runtime, json_string(binding, "entity", NULL));
        const slayer3d_value *value =
            actor != NULL ? slayer3d_properties_get_value(actor->props, json_string(binding, "key", NULL)) : NULL;
        formatted = game_data_ui_value_to_string(value, buffer, buffer_size);
    }
    else if (SDL_strcmp(type, "metric") == 0)
    {
        formatted = game_data_ui_metric_to_string(metrics, json_string(binding, "metric", NULL), buffer, buffer_size);
    }
    if (!formatted)
        formatted = slayer3d_game_data_ui_json_scalar_to_string(obj_get(binding, "default"), buffer, buffer_size);
    return formatted;
}
