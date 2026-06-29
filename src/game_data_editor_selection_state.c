/**
 * @file game_data_editor_selection_state.c
 * @brief Editor selection-state bookkeeping and published diagnostics.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/math.h"

static void inspector_format_vec3(slayer3d_vec3 value, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    SDL_snprintf(buffer, buffer_size, "%.3f, %.3f, %.3f", value.x, value.y, value.z);
}

static void inspector_format_bounds(slayer3d_bounding_box bounds, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    SDL_snprintf(buffer, buffer_size, "%.3f, %.3f, %.3f -> %.3f, %.3f, %.3f", bounds.min.x, bounds.min.y, bounds.min.z,
                 bounds.max.x, bounds.max.y, bounds.max.z);
}

static void inspector_format_color(slayer3d_color color, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    SDL_snprintf(buffer, buffer_size, "%u, %u, %u, %u", color.r, color.g, color.b, color.a);
}

static const char *inspector_properties_string(const slayer3d_properties *properties, const char *key,
                                               const char *fallback)
{
    return properties != NULL ? slayer3d_properties_get_string(properties, key, fallback) : fallback;
}

static float inspector_properties_float(const slayer3d_properties *properties, const char *key, float fallback)
{
    return properties != NULL ? slayer3d_properties_get_float(properties, key, fallback) : fallback;
}

static bool inspector_actor_is_light(const editor_actor_runtime *actor)
{
    const char *role = actor != NULL ? inspector_properties_string(actor->properties, "role", "") : "";
    const char *light_type = actor != NULL ? inspector_properties_string(actor->properties, "light_type", "") : "";
    return (role != NULL && SDL_strcmp(role, "light") == 0) || (light_type != NULL && light_type[0] != '\0');
}

static const char *inspector_light_animation_summary(const slayer3d_properties *properties)
{
    const bool enabled =
        properties != NULL ? slayer3d_properties_get_bool(properties, "light_animation_enabled", false) : false;
    const char *type = inspector_properties_string(properties, "light_animation_type", "");
    const char *preset = inspector_properties_string(properties, "light_animation_preset", "");
    if (!enabled && (type == NULL || type[0] == '\0' || SDL_strcmp(type, "none") == 0))
        return "steady";
    if (preset != NULL && preset[0] != '\0')
        return preset;
    return type != NULL && type[0] != '\0' ? type : "custom";
}

static void inspector_publish_light_state(slayer3d_properties *scene_state, const editor_actor_runtime *actor)
{
    if (scene_state == NULL)
        return;
    const bool visible = inspector_actor_is_light(actor);
    slayer3d_properties_set_bool(scene_state, "editor.inspector.light.visible", visible);
    if (!visible)
    {
        slayer3d_properties_set_string(scene_state, "editor.inspector.light.type", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.light.kind", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.light.intensity_range", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.light.shadow_falloff", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.light.geometry", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.light.cone", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.light.animation", "");
        return;
    }

    const slayer3d_properties *properties = actor->properties;
    char value[128];
    const char *type = inspector_properties_string(properties, "light_type", "point");
    const char *kind = inspector_properties_string(properties, "light_kind", "dynamic");
    const char *shadow = inspector_properties_string(properties, "shadow_mode", "none");
    const char *falloff = inspector_properties_string(properties, "falloff", "inverse_square");
    const float intensity = inspector_properties_float(properties, "light_intensity", 1.0f);
    const float range = inspector_properties_float(properties, "light_range", 0.0f);
    const float width = inspector_properties_float(properties, "width", 0.0f);
    const float height = inspector_properties_float(properties, "height", 0.0f);
    const float radius = inspector_properties_float(properties, "radius", 0.0f);
    const float inner = inspector_properties_float(properties, "inner_angle_degrees", 0.0f);
    const float outer = inspector_properties_float(properties, "outer_angle_degrees", 0.0f);

    slayer3d_properties_set_string(scene_state, "editor.inspector.light.type", type);
    slayer3d_properties_set_string(scene_state, "editor.inspector.light.kind", kind);
    SDL_snprintf(value, sizeof(value), "%.2f / %.2f", intensity, range);
    slayer3d_properties_set_string(scene_state, "editor.inspector.light.intensity_range", value);
    SDL_snprintf(value, sizeof(value), "%s / %s", shadow, falloff);
    slayer3d_properties_set_string(scene_state, "editor.inspector.light.shadow_falloff", value);
    if (width > 0.0f || height > 0.0f)
        SDL_snprintf(value, sizeof(value), "%.2f x %.2f", width, height);
    else if (radius > 0.0f)
        SDL_snprintf(value, sizeof(value), "r %.2f", radius);
    else
        SDL_strlcpy(value, "n/a", sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.light.geometry", value);
    if (inner > 0.0f || outer > 0.0f)
        SDL_snprintf(value, sizeof(value), "%.1f / %.1f", inner, outer);
    else
        SDL_strlcpy(value, "n/a", sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.light.cone", value);
    slayer3d_properties_set_string(scene_state, "editor.inspector.light.animation",
                                   inspector_light_animation_summary(properties));
}

static bool inspector_color_equal(slayer3d_color lhs, slayer3d_color rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

static void inspector_publish_brush_color_draft(slayer3d_properties *scene_state, slayer3d_color color, bool editable,
                                                const char *selection_token)
{
    char value[64];
    if (scene_state == NULL)
        return;
    slayer3d_properties_set_bool(scene_state, "editor.inspector.brush.editable", editable);
    const char *token = selection_token != NULL ? selection_token : "";
    const bool dirty = slayer3d_properties_get_bool(scene_state, "editor.inspector.brush.color.dirty", false);
    const char *active_token =
        slayer3d_properties_get_string(scene_state, "editor.inspector.brush.color.selection_token", "");
    if (editable && dirty && SDL_strcmp(active_token, token) == 0)
        return;

    slayer3d_properties_set_string(scene_state, "editor.inspector.brush.color.selection_token", token);
    slayer3d_properties_set_bool(scene_state, "editor.inspector.brush.color.dirty", false);
    slayer3d_properties_set_color(scene_state, "editor.inspector.brush.color", color);
    slayer3d_properties_set_int(scene_state, "editor.inspector.brush.color.r", (int)color.r);
    slayer3d_properties_set_int(scene_state, "editor.inspector.brush.color.g", (int)color.g);
    slayer3d_properties_set_int(scene_state, "editor.inspector.brush.color.b", (int)color.b);
    slayer3d_properties_set_int(scene_state, "editor.inspector.brush.color.a", (int)color.a);
    inspector_format_color(color, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.brush.color.label", value);
}

static void inspector_publish_actor_color_draft(slayer3d_properties *scene_state, slayer3d_color color, bool editable,
                                                const char *selection_token)
{
    char value[64];
    if (scene_state == NULL)
        return;
    slayer3d_properties_set_bool(scene_state, "editor.inspector.actor.editable", editable);
    const char *token = selection_token != NULL ? selection_token : "";
    const bool dirty = slayer3d_properties_get_bool(scene_state, "editor.inspector.actor.color.dirty", false);
    const char *active_token =
        slayer3d_properties_get_string(scene_state, "editor.inspector.actor.color.selection_token", "");
    if (editable && dirty && SDL_strcmp(active_token, token) == 0)
        return;

    slayer3d_properties_set_string(scene_state, "editor.inspector.actor.color.selection_token", token);
    slayer3d_properties_set_bool(scene_state, "editor.inspector.actor.color.dirty", false);
    slayer3d_properties_set_color(scene_state, "editor.inspector.actor.color", color);
    slayer3d_properties_set_int(scene_state, "editor.inspector.actor.color.r", (int)color.r);
    slayer3d_properties_set_int(scene_state, "editor.inspector.actor.color.g", (int)color.g);
    slayer3d_properties_set_int(scene_state, "editor.inspector.actor.color.b", (int)color.b);
    slayer3d_properties_set_int(scene_state, "editor.inspector.actor.color.a", (int)color.a);
    inspector_format_color(color, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.actor.color.label", value);
}

static bool inspector_brush_uniform_color(const slayer3d_game_data_brush *brush, slayer3d_color *out_color)
{
    if (brush == NULL || brush->faces == NULL || brush->face_count <= 0 || out_color == NULL)
        return false;
    bool found = false;
    slayer3d_color color = {180, 184, 192, 255};
    for (int i = 0; i < brush->face_count; ++i)
    {
        const slayer3d_game_data_brush_face *face = &brush->faces[i];
        if (!face->has_color)
            return false;
        if (!found)
        {
            color = face->color;
            found = true;
            continue;
        }
        if (!inspector_color_equal(color, face->color))
            return false;
    }
    if (!found)
        return false;
    *out_color = color;
    return true;
}

static void inspector_format_brush_contents(unsigned int contents, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    buffer[0] = '\0';
    if (contents == 0U)
    {
        SDL_strlcpy(buffer, "solid", buffer_size);
        return;
    }
    struct content_label
    {
        unsigned int bit;
        const char *label;
    };
    static const struct content_label labels[] = {
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID, "solid"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP, "player_clip"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP, "projectile_clip"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER, "trigger"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER, "water"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA, "lava"},
        {SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY, "sky"},
    };
    for (size_t i = 0; i < SDL_arraysize(labels); ++i)
    {
        if ((contents & labels[i].bit) == 0U)
            continue;
        if (buffer[0] != '\0')
            SDL_strlcat(buffer, "|", buffer_size);
        SDL_strlcat(buffer, labels[i].label, buffer_size);
    }
    if (buffer[0] == '\0')
        SDL_snprintf(buffer, buffer_size, "0x%08x", contents);
}

static void inspector_set_empty_selection(slayer3d_properties *scene_state)
{
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.kind", "none");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.title", "No selection");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.count", "0 selected");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.id", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.position", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.size", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.bounds", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.material", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.face", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.contents", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.color", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tint", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.prefab", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tags", "");
    inspector_publish_light_state(scene_state, NULL);
    inspector_publish_brush_color_draft(scene_state, (slayer3d_color){180, 184, 192, 255}, false, "none");
    inspector_publish_actor_color_draft(scene_state, (slayer3d_color){120, 200, 255, 210}, false, "none");
}

static const slayer3d_game_data_brush *inspector_brush_for_selection(
    const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || selection == NULL || !selection->hit ||
        selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD || selection->world_name == NULL ||
        selection->element_index < 0)
    {
        return NULL;
    }
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    if (world_runtime == NULL || selection->element_index >= world_runtime->desc.brush_count)
        return NULL;
    return &world_runtime->desc.brushes[selection->element_index];
}

static void inspector_join_brush_tags(const slayer3d_game_data_brush *brush, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    buffer[0] = '\0';
    if (brush == NULL || brush->tags == NULL || brush->tag_count <= 0)
        return;
    for (int i = 0; i < brush->tag_count; ++i)
    {
        if (brush->tags[i] == NULL || brush->tags[i][0] == '\0')
            continue;
        if (buffer[0] != '\0')
            SDL_strlcat(buffer, ", ", buffer_size);
        SDL_strlcat(buffer, brush->tags[i], buffer_size);
    }
}

static void publish_single_brush_inspector_state(slayer3d_game_data_runtime *runtime,
                                                 const slayer3d_game_data_editor_selection *selection)
{
    slayer3d_properties *scene_state = runtime->scene_state;
    const slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, selection);
    const slayer3d_game_data_brush *brush = inspector_brush_for_selection(runtime, &resolved);
    char value[256];
    char title[256];
    const char *stable_id = resolved.element_editor != NULL && resolved.element_editor->stable_id != NULL
                                ? resolved.element_editor->stable_id
                                : "";
    SDL_snprintf(title, sizeof(title), "%s", resolved.element_name != NULL ? resolved.element_name : "Brush");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.kind", "Brush");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.title", title);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.count", "1 selected");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.id", stable_id);
    if (resolved.has_bounds)
    {
        const slayer3d_vec3 dimensions = slayer3d_vec3_sub(resolved.bounds.max, resolved.bounds.min);
        const slayer3d_vec3 center =
            slayer3d_vec3_scale(slayer3d_vec3_add(resolved.bounds.min, resolved.bounds.max), 0.5f);
        inspector_format_vec3(center, value, sizeof(value));
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.position", value);
        inspector_format_vec3(dimensions, value, sizeof(value));
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.size", value);
        inspector_format_bounds(resolved.bounds, value, sizeof(value));
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.bounds", value);
    }
    else
    {
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.position", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.size", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.bounds", "");
    }
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.material",
                                   resolved.material_name != NULL ? resolved.material_name : "");
    if (resolved.face_index >= 0)
        SDL_snprintf(value, sizeof(value), "%d", resolved.face_index);
    else
        SDL_strlcpy(value, "all", sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.face", value);
    inspector_format_brush_contents(brush != NULL ? brush->contents : 0U, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.contents", value);
    const slayer3d_game_data_brush_face *face =
        brush != NULL && resolved.face_index >= 0 && resolved.face_index < brush->face_count
            ? &brush->faces[resolved.face_index]
            : NULL;
    slayer3d_color brush_color = {180, 184, 192, 255};
    const bool has_uniform_color = inspector_brush_uniform_color(brush, &brush_color);
    if (face != NULL && face->has_color)
        inspector_format_color(face->color, value, sizeof(value));
    else if (has_uniform_color)
        inspector_format_color(brush_color, value, sizeof(value));
    else
        SDL_strlcpy(value, "default", sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.color", value);
    inspector_publish_brush_color_draft(
        scene_state, brush_color, true,
        stable_id[0] != '\0' ? stable_id : (resolved.element_name != NULL ? resolved.element_name : "brush"));
    inspector_publish_actor_color_draft(scene_state, (slayer3d_color){120, 200, 255, 210}, false,
                                        stable_id[0] != '\0' ? stable_id : "brush");
    if (face != NULL && face->tint_enabled)
        inspector_format_color(face->tint, value, sizeof(value));
    else
        SDL_strlcpy(value, "", sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tint", value);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.prefab",
                                   resolved.element_editor != NULL && resolved.element_editor->prefab != NULL
                                       ? resolved.element_editor->prefab
                                       : "");
    inspector_join_brush_tags(brush, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tags", value);
}

static void publish_multi_brush_inspector_state(slayer3d_game_data_runtime *runtime, int count)
{
    slayer3d_properties *scene_state = runtime->scene_state;
    char value[256];
    char title[64];
    bool has_bounds = false;
    slayer3d_bounding_box bounds;
    SDL_zero(bounds);
    for (int i = 0; i < count; ++i)
    {
        const slayer3d_game_data_editor_selection resolved =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!resolved.hit || !resolved.has_bounds)
            continue;
        if (!has_bounds)
        {
            bounds = resolved.bounds;
            has_bounds = true;
            continue;
        }
        bounds.min.x = SDL_min(bounds.min.x, resolved.bounds.min.x);
        bounds.min.y = SDL_min(bounds.min.y, resolved.bounds.min.y);
        bounds.min.z = SDL_min(bounds.min.z, resolved.bounds.min.z);
        bounds.max.x = SDL_max(bounds.max.x, resolved.bounds.max.x);
        bounds.max.y = SDL_max(bounds.max.y, resolved.bounds.max.y);
        bounds.max.z = SDL_max(bounds.max.z, resolved.bounds.max.z);
    }
    SDL_snprintf(title, sizeof(title), "%d Brushes", count);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.kind", "Brushes");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.title", title);
    SDL_snprintf(value, sizeof(value), "%d selected", count);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.count", value);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.id", "mixed");
    if (has_bounds)
    {
        const slayer3d_vec3 dimensions = slayer3d_vec3_sub(bounds.max, bounds.min);
        const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
        inspector_format_vec3(center, value, sizeof(value));
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.position", value);
        inspector_format_vec3(dimensions, value, sizeof(value));
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.size", value);
        inspector_format_bounds(bounds, value, sizeof(value));
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.bounds", value);
    }
    else
    {
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.position", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.size", "");
        slayer3d_properties_set_string(scene_state, "editor.inspector.selection.bounds", "");
    }
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.material", "mixed");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.face", "mixed");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.contents", "mixed");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.color", "mixed");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tint", "mixed");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.prefab", "mixed");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tags", "mixed");
    inspector_publish_brush_color_draft(scene_state, (slayer3d_color){180, 184, 192, 255}, true, "multi-brush");
    inspector_publish_actor_color_draft(scene_state, (slayer3d_color){120, 200, 255, 210}, false, "multi-brush");
}

static void publish_actor_inspector_state(slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_editor_selection *selection)
{
    slayer3d_properties *scene_state = runtime->scene_state;
    const editor_actor_runtime *actor = selection != NULL && selection->element_name != NULL
                                            ? find_editor_actor(runtime, selection->element_name)
                                            : NULL;
    if (actor == NULL)
    {
        inspector_set_empty_selection(scene_state);
        return;
    }
    char value[256];
    const bool is_light = inspector_actor_is_light(actor);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.kind", is_light ? "Light" : "Actor");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.title",
                                   actor->display_name != NULL && actor->display_name[0] != '\0' ? actor->display_name
                                                                                                 : actor->name);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.count", "1 selected");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.id",
                                   actor->name != NULL ? actor->name : "");
    inspector_format_vec3(actor->position, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.position", value);
    inspector_format_vec3(actor->scale, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.size", value);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.bounds", "");
    slayer3d_properties_set_string(
        scene_state, "editor.inspector.selection.material",
        actor->mesh != NULL && actor->mesh[0] != '\0' ? actor->mesh : (actor->model != NULL ? actor->model : ""));
    inspector_format_vec3(actor->rotation, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.face", value);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.contents",
                                   actor->archetype != NULL ? actor->archetype : "");
    inspector_format_color(actor->color, value, sizeof(value));
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.color", value);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tint", "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.prefab",
                                   actor->prefab != NULL ? actor->prefab : "");
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.tags",
                                   actor->group != NULL ? actor->group : "");
    inspector_publish_light_state(scene_state, actor);
    inspector_publish_brush_color_draft(scene_state, (slayer3d_color){180, 184, 192, 255}, false,
                                        actor->name != NULL ? actor->name : "actor");
    inspector_publish_actor_color_draft(scene_state, actor->color, true, actor->name != NULL ? actor->name : "actor");
}

static void publish_active_inspector_selection_state(slayer3d_game_data_runtime *runtime, int selected_brush_count)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    if (selected_brush_count > 1)
    {
        publish_multi_brush_inspector_state(runtime, selected_brush_count);
        return;
    }
    if (selected_brush_count == 1)
    {
        publish_single_brush_inspector_state(runtime, &runtime->editor_selected_brushes[0]);
        return;
    }
    if (editor_selection_active_for_scene(runtime) && runtime->editor_active_selection.hit &&
        runtime->editor_active_selection.type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR)
    {
        publish_actor_inspector_state(runtime, &runtime->editor_active_selection);
        return;
    }
    inspector_set_empty_selection(runtime->scene_state);
}

bool editor_selected_brushes_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_brush_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) == 0;
}

void init_editor_source_vertex_selection(editor_source_vertex_selection *selection)
{
    if (selection == NULL)
        return;
    SDL_zero(*selection);
    selection->source_index = -1;
    selection->vertex_index = -1;
}

void init_editor_source_edge_selection(editor_source_edge_selection *selection)
{
    if (selection == NULL)
        return;
    SDL_zero(*selection);
    selection->source_index = -1;
    selection->edge_index = -1;
}

bool editor_selected_vertices_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_vertex_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_vertex_scene, active_scene) == 0;
}

bool editor_selected_edges_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_edge_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_edge_scene, active_scene) == 0;
}

void publish_editor_selected_vertex_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int count = editor_selected_vertices_active_for_scene(runtime) ? runtime->editor_selected_vertex_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.selection.multiple", count > 1);
    if (count <= 0)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.world", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush_stable_id", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.vertex", "");
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.index", -1);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.x", 0);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.y", 0);
        slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.z", 0);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.selection.coord",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        return;
    }

    const editor_source_vertex_selection *selection = &runtime->editor_selected_vertices[count - 1];
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.world", selection->world_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush", selection->brush_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.brush_stable_id",
                                   selection->brush_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.selection.vertex", selection->vertex_stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.index", selection->vertex_index);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.x", selection->coord[0]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.y", selection->coord[1]);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.selection.z", selection->coord[2]);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.selection.coord",
                                 slayer3d_vec3_make((float)selection->coord[0] * meters_per_unit,
                                                    (float)selection->coord[1] * meters_per_unit,
                                                    (float)selection->coord[2] * meters_per_unit));
}

void publish_editor_selected_edge_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int count = editor_selected_edges_active_for_scene(runtime) ? runtime->editor_selected_edge_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.selection.multiple", count > 1);
    if (count <= 0)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.world", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush_stable_id", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.edge", "");
        slayer3d_properties_set_int(runtime->scene_state, "editor.edge.selection.index", -1);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.start",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.end",
                                     slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        return;
    }

    const editor_source_edge_selection *selection = &runtime->editor_selected_edges[count - 1];
    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const float meters_per_unit = world_runtime != NULL && world_runtime->editor_source_meters_per_unit > 0.0f
                                      ? world_runtime->editor_source_meters_per_unit
                                      : 0.001f;
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.world", selection->world_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush", selection->brush_name);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.brush_stable_id",
                                   selection->brush_stable_id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.selection.edge", selection->edge_stable_id);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.selection.index", selection->edge_index);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.start",
                                 slayer3d_vec3_make((float)selection->coord[0][0] * meters_per_unit,
                                                    (float)selection->coord[0][1] * meters_per_unit,
                                                    (float)selection->coord[0][2] * meters_per_unit));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.selection.end",
                                 slayer3d_vec3_make((float)selection->coord[1][0] * meters_per_unit,
                                                    (float)selection->coord[1][1] * meters_per_unit,
                                                    (float)selection->coord[1][2] * meters_per_unit));
}

void clear_editor_selected_vertices(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_VERTEX_CAPACITY; ++i)
        init_editor_source_vertex_selection(&runtime->editor_selected_vertices[i]);
    runtime->editor_selected_vertex_count = 0;
    runtime->editor_selected_vertex_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_vertex_count(runtime);
}

void clear_editor_selected_edges(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_EDGE_CAPACITY; ++i)
        init_editor_source_edge_selection(&runtime->editor_selected_edges[i]);
    runtime->editor_selected_edge_count = 0;
    runtime->editor_selected_edge_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_edge_count(runtime);
}

void publish_editor_vertex_lasso_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                       int selected_count)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->vertex_lasso;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.lasso.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.lasso.additive",
                                 active ? drag->lasso_additive : false);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.start_x",
                                  active ? drag->start_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.start_y",
                                  active ? drag->start_mouse_y : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.end_x",
                                  active ? drag->current_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.vertex.lasso.end_y",
                                  active ? drag->current_mouse_y : 0.0f);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.lasso.selected_count", selected_count);
}

void publish_editor_edge_lasso_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag,
                                     int selected_count)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const bool active = drag != NULL && drag->active && drag->edge_lasso;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.lasso.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.lasso.additive",
                                 active ? drag->lasso_additive : false);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.start_x",
                                  active ? drag->start_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.start_y",
                                  active ? drag->start_mouse_y : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.end_x",
                                  active ? drag->current_mouse_x : 0.0f);
    slayer3d_properties_set_float(runtime->scene_state, "editor.edge.lasso.end_y",
                                  active ? drag->current_mouse_y : 0.0f);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.lasso.selected_count", selected_count);
}

void publish_editor_edge_drag_state(slayer3d_game_data_runtime *runtime, const editor_drag_move_state *drag)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const bool active = drag != NULL && drag->active && drag->edge_drag;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.drag.active", active);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.drag.moved", active ? drag->moved : false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.drag.axis_lock_y",
                                 active ? drag->axis_lock_y : false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.drag.axis_lock_dominant",
                                 active ? drag->axis_lock_dominant : false);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.drag.offset",
                                 active ? drag->applied_offset : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

void publish_editor_edge_move_result(slayer3d_game_data_runtime *runtime, bool valid, int edge_count,
                                     const char *message)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.move.valid", valid);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.move.edge_count", edge_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.move.message", message != NULL ? message : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");
}

void publish_editor_selected_brush_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const int count = editor_selected_brushes_active_for_scene(runtime) ? runtime->editor_selected_brush_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.selection.multiple", count > 1);
    publish_active_inspector_selection_state(runtime, count);
}

void clear_editor_selected_brushes(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    clear_editor_selected_vertices(runtime);
    clear_editor_selected_edges(runtime);
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY; ++i)
        init_editor_selection(&runtime->editor_selected_brushes[i]);
    runtime->editor_selected_brush_count = 0;
    runtime->editor_selected_brush_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_brush_count(runtime);
}

void clear_editor_active_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    clear_editor_selected_brushes(runtime);
    clear_editor_command_preview(runtime);
    clear_editor_placement_preview(runtime);
}

void clear_editor_vertex_hover_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.hit", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.brush_stable_id", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.vertex.hover.vertex", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.vertex.hover.selected", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.index", -1);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.shared_count", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.x", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.y", 0);
    slayer3d_properties_set_int(runtime->scene_state, "editor.vertex.hover.z", 0);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.vertex.hover.coord",
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

void clear_editor_edge_hover_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.hover.hit", false);
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.brush", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.brush_stable_id", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.edge.hover.edge", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.edge.hover.selected", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.edge.hover.index", -1);
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.hover.start", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, "editor.edge.hover.end", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}
