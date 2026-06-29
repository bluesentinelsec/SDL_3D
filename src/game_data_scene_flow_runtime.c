/**
 * @file game_data_scene_flow_runtime.c
 * @brief Scene timeline, transition, activity, and menu selection runtime helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_log.h>

static yyjson_val *active_skip_policy_json(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *root = scene != NULL ? scene->root : NULL;
    yyjson_val *policy = obj_get(root, "skip_policy");
    if (yyjson_is_obj(policy))
        return policy;

    yyjson_val *timeline = obj_get(root, "timeline");
    policy = obj_get(timeline, "skip_policy");
    return yyjson_is_obj(policy) ? policy : NULL;
}

bool slayer3d_game_data_get_active_skip_policy(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_game_data_skip_policy *out_policy)
{
    if (out_policy != NULL)
    {
        SDL_zero(*out_policy);
        out_policy->enabled = false;
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_ANY;
        out_policy->action_id = -1;
        out_policy->preserve_exit_transition = true;
        out_policy->consume_input = true;
        out_policy->block_menus = true;
        out_policy->block_scene_shortcuts = true;
    }
    if (runtime == NULL || out_policy == NULL)
        return false;

    yyjson_val *policy = active_skip_policy_json(runtime);
    if (!yyjson_is_obj(policy))
        return false;

    out_policy->enabled = json_bool(policy, "enabled", true);
    if (!out_policy->enabled)
        return false;

    const char *input = json_string(policy, "input", NULL);
    out_policy->action = json_string(policy, "action", NULL);
    if (input == NULL)
        out_policy->input =
            out_policy->action != NULL ? SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION : SLAYER3D_GAME_DATA_SKIP_INPUT_ANY;
    else if (SDL_strcmp(input, "any") == 0 || SDL_strcmp(input, "any_input") == 0)
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_ANY;
    else if (SDL_strcmp(input, "action") == 0)
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION;
    else
        out_policy->input = SLAYER3D_GAME_DATA_SKIP_INPUT_DISABLED;

    out_policy->action_id = slayer3d_game_data_find_action(runtime, out_policy->action);
    out_policy->scene = json_string(policy, "scene", json_string(policy, "target_scene", NULL));
    out_policy->preserve_exit_transition = json_bool(policy, "preserve_exit_transition", true);
    out_policy->consume_input = json_bool(policy, "consume_input", true);
    out_policy->block_menus = json_bool(policy, "block_menus", out_policy->consume_input);
    out_policy->block_scene_shortcuts =
        json_bool(policy, "block_scene_shortcuts", json_bool(policy, "block_shortcuts", out_policy->consume_input));
    return out_policy->input != SLAYER3D_GAME_DATA_SKIP_INPUT_DISABLED;
}

bool slayer3d_game_data_get_active_timeline_policy(const slayer3d_game_data_runtime *runtime,
                                                   slayer3d_game_data_timeline_policy *out_policy)
{
    if (out_policy != NULL)
        SDL_zero(*out_policy);
    if (runtime == NULL || out_policy == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *timeline = obj_get(scene != NULL ? scene->root : NULL, "timeline");
    if (!yyjson_is_obj(timeline) || !json_bool(timeline, "autoplay", false))
        return false;

    out_policy->block_menus = json_bool(timeline, "block_menus", false);
    out_policy->block_scene_shortcuts =
        json_bool(timeline, "block_scene_shortcuts", json_bool(timeline, "block_shortcuts", false));
    return true;
}

static yyjson_val *active_timeline_events(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *timeline = obj_get(scene != NULL ? scene->root : NULL, "timeline");
    if (!yyjson_is_obj(timeline) || !json_bool(timeline, "autoplay", false))
        return NULL;

    yyjson_val *events = obj_get(timeline, "events");
    if (events == NULL)
        events = obj_get(timeline, "tracks");
    return yyjson_is_arr(events) ? events : NULL;
}

bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const slayer3d_properties *payload);
bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions, const slayer3d_properties *payload);
bool execute_fps_controller_launch_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                          const slayer3d_properties *payload);
bool execute_fps_controller_teleport_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload);
void slayer3d_game_data_timeline_state_init(slayer3d_game_data_timeline_state *state)
{
    if (state != NULL)
        SDL_zero(*state);
}

bool slayer3d_game_data_update_timeline(slayer3d_game_data_runtime *runtime, slayer3d_game_data_timeline_state *state,
                                        float dt, slayer3d_game_data_timeline_update_result *out_result)
{
    if (out_result != NULL)
        SDL_zero(*out_result);
    if (runtime == NULL || state == NULL)
        return false;

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (state->scene != active_scene)
    {
        state->scene = active_scene;
        state->time = 0.0f;
        state->next_event_index = 0;
        state->complete = false;
    }

    yyjson_val *events = active_timeline_events(runtime);
    if (!yyjson_is_arr(events))
    {
        state->complete = true;
        if (out_result != NULL)
            out_result->complete = true;
        return true;
    }

    const int event_count = (int)yyjson_arr_size(events);
    if (state->next_event_index >= event_count)
    {
        state->complete = true;
        if (out_result != NULL)
            out_result->complete = true;
        return true;
    }

    state->time += dt > 0.0f ? dt : 0.0f;
    bool ok = true;
    while (state->next_event_index < event_count)
    {
        yyjson_val *event = yyjson_arr_get(events, (size_t)state->next_event_index);
        const float event_time = json_float(event, "time", 0.0f);
        if (event_time > state->time)
            break;

        ++state->next_event_index;
        yyjson_val *action = obj_get(event, "action");
        const char *type = json_string(action, "type", "");
        if (SDL_strcmp(type, "scene.request") == 0)
        {
            if (out_result != NULL)
                out_result->scene_request = json_string(action, "scene", NULL);
        }
        else if (yyjson_is_obj(action))
        {
            ok = execute_one_action(runtime, action, NULL) && ok;
        }

        if (out_result != NULL)
            ++out_result->actions_executed;
        if (out_result != NULL && out_result->scene_request != NULL)
            break;
    }

    state->complete = state->next_event_index >= event_count;
    if (out_result != NULL)
        out_result->complete = state->complete;
    return ok;
}

bool slayer3d_game_data_set_active_scene(slayer3d_game_data_runtime *runtime, const char *scene_name)
{
    return slayer3d_game_data_set_active_scene_with_payload(runtime, scene_name, NULL);
}

bool slayer3d_game_data_set_active_scene_with_payload(slayer3d_game_data_runtime *runtime, const char *scene_name,
                                                      const slayer3d_properties *payload)
{
    scene_entry *scene = find_scene(runtime, scene_name);
    if (runtime == NULL || scene == NULL)
        return false;

    const char *previous = slayer3d_game_data_active_scene(runtime);
    if (previous != NULL && scene->name != NULL && SDL_strcmp(previous, scene->name) != 0 &&
        !apply_actor_pool_scene_exit_policies(runtime, previous, scene->name))
    {
        return false;
    }
    runtime->active_scene_index = (int)(scene - runtime->scenes);
    runtime->input_capture.active = false;
    clear_menu_text_entry_capture(runtime);
    apply_scene_camera(runtime, scene);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D game data scene set: %s -> %s",
                 previous != NULL ? previous : "<none>", scene->name != NULL ? scene->name : "<none>");
    emit_scene_enter_signal(runtime, scene, previous, payload);
    return true;
}

bool slayer3d_game_data_active_scene_updates_game(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? json_bool(scene->root, "updates_game", true) : true;
}

static bool update_phase_default(const slayer3d_game_data_runtime *runtime, const char *phase, bool paused)
{
    if (phase != NULL && SDL_strcmp(phase, "simulation") == 0)
        return !paused && slayer3d_game_data_active_scene_updates_game(runtime);
    if (phase != NULL && SDL_strcmp(phase, "app_flow") == 0)
        return true;
    if (phase != NULL && (SDL_strcmp(phase, "scene_activity") == 0 || SDL_strcmp(phase, "presentation") == 0 ||
                          SDL_strcmp(phase, "property_effects") == 0 || SDL_strcmp(phase, "particles") == 0))
        return true;
    return !paused;
}

bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                         const slayer3d_game_data_ui_metrics *metrics);

static bool eval_update_phase_entry(const slayer3d_game_data_runtime *runtime, yyjson_val *entry, bool paused,
                                    bool fallback)
{
    if (yyjson_is_bool(entry))
        return yyjson_get_bool(entry);
    if (!yyjson_is_obj(entry))
        return fallback;
    yyjson_val *active_if = obj_get(entry, "active_if");
    if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
        return false;
    if (!json_bool(entry, "active", true))
        return false;
    if (paused)
        return json_bool(entry, "when_paused", fallback);
    return json_bool(entry, "when_unpaused", true);
}

bool slayer3d_game_data_active_scene_update_phase(const slayer3d_game_data_runtime *runtime, const char *phase,
                                                  bool paused)
{
    if (runtime == NULL || phase == NULL)
        return false;

    const bool fallback = update_phase_default(runtime, phase, paused);
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *scene_entry_json = obj_get(obj_get(scene != NULL ? scene->root : NULL, "update_phases"), phase);
    if (scene_entry_json != NULL)
        return eval_update_phase_entry(runtime, scene_entry_json, paused, fallback);

    yyjson_val *root_entry_json = obj_get(obj_get(runtime_root(runtime), "update_phases"), phase);
    return eval_update_phase_entry(runtime, root_entry_json, paused, fallback);
}

bool slayer3d_game_data_active_scene_renders_world(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return scene != NULL ? json_bool(scene->root, "renders_world", true) : true;
}

bool slayer3d_game_data_get_active_scene_skybox(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_game_data_scene_skybox *out_skybox)
{
    if (out_skybox != NULL)
        SDL_zero(*out_skybox);
    if (runtime == NULL || out_skybox == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *skybox = obj_get(obj_get(scene != NULL ? scene->root : NULL, "world"), "skybox");
    if (!yyjson_is_obj(skybox))
        return false;

    out_skybox->pos_x = json_string(skybox, "pos_x", NULL);
    out_skybox->neg_x = json_string(skybox, "neg_x", NULL);
    out_skybox->pos_y = json_string(skybox, "pos_y", NULL);
    out_skybox->neg_y = json_string(skybox, "neg_y", NULL);
    out_skybox->pos_z = json_string(skybox, "pos_z", NULL);
    out_skybox->neg_z = json_string(skybox, "neg_z", NULL);
    out_skybox->size = json_float(skybox, "size", 400.0f);
    return out_skybox->pos_x != NULL && out_skybox->neg_x != NULL && out_skybox->pos_y != NULL &&
           out_skybox->neg_y != NULL && out_skybox->pos_z != NULL && out_skybox->neg_z != NULL;
}

bool slayer3d_game_data_active_scene_has_entity(const slayer3d_game_data_runtime *runtime, const char *entity_name)
{
    return runtime != NULL && active_scene_has_entity_internal(runtime, entity_name);
}

static bool string_array_contains(yyjson_val *array, const char *value)
{
    for (size_t i = 0; value != NULL && yyjson_is_arr(array) && i < yyjson_arr_size(array); ++i)
    {
        yyjson_val *item = yyjson_arr_get(array, i);
        const char *text = yyjson_is_str(item) ? yyjson_get_str(item) : NULL;
        if (text != NULL && SDL_strcmp(text, value) == 0)
            return true;
    }
    return false;
}

static bool action_is_native_document_shortcut(const char *action)
{
    return action != NULL &&
           (SDL_strcmp(action, "action.editor.new") == 0 || SDL_strcmp(action, "action.editor.open") == 0 ||
            SDL_strcmp(action, "action.editor.export") == 0 || SDL_strcmp(action, "action.editor.save_as") == 0);
}

bool slayer3d_game_data_active_scene_allows_action(const slayer3d_game_data_runtime *runtime, int action_id)
{
    const char *action = find_action_name(runtime, action_id);
    if (runtime == NULL || action == NULL)
        return false;

    if (runtime->scene_state != NULL &&
        slayer3d_properties_get_bool(runtime->scene_state, "editor.console.focused", false) &&
        !action_is_native_document_shortcut(action) &&
        (SDL_strncmp(action, "action.editor.", SDL_strlen("action.editor.")) == 0 ||
         SDL_strncmp(action, "editor.", SDL_strlen("editor.")) == 0))
    {
        return false;
    }

    yyjson_val *global_actions =
        obj_get(obj_get(obj_get(runtime_root(runtime), "app"), "input_policy"), "global_actions");
    if (string_array_contains(global_actions, action))
        return true;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *actions = obj_get(obj_get(scene != NULL ? scene->root : NULL, "input"), "actions");
    if (actions == NULL)
        return true;
    return string_array_contains(actions, action);
}

bool slayer3d_game_data_active_scene_mouse_capture(const slayer3d_game_data_runtime *runtime, bool paused)
{
    if (runtime == NULL)
        return false;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *input = obj_get(scene != NULL ? scene->root : NULL, "input");
    const char *policy = json_string(input, "mouse_capture", "never");
    if (policy == NULL || SDL_strcmp(policy, "never") == 0)
        return false;
    yyjson_val *condition = obj_get(input, "mouse_capture_if");
    if (condition != NULL && !eval_data_condition(runtime, condition, NULL))
        return false;
    if (SDL_strcmp(policy, "always") == 0)
        return true;
    if (SDL_strcmp(policy, "unpaused") == 0)
        return !paused;
    return false;
}

bool slayer3d_game_data_get_scene_transition_policy(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_scene_transition_policy *out_policy)
{
    if (out_policy != NULL)
    {
        out_policy->allow_same_scene = false;
        out_policy->allow_interrupt = false;
        out_policy->reset_menu_input_on_request = true;
    }
    if (runtime == NULL || out_policy == NULL)
        return false;

    yyjson_val *policy = obj_get(obj_get(runtime_root(runtime), "app"), "scene_transition_policy");
    out_policy->allow_same_scene = json_bool(policy, "allow_same_scene", out_policy->allow_same_scene);
    out_policy->allow_interrupt = json_bool(policy, "allow_interrupt", out_policy->allow_interrupt);
    out_policy->reset_menu_input_on_request =
        json_bool(policy, "reset_menu_input_on_request", out_policy->reset_menu_input_on_request);
    return true;
}

bool slayer3d_game_data_get_scene_transition(const slayer3d_game_data_runtime *runtime, const char *scene_name,
                                             const char *phase, slayer3d_game_data_transition_desc *out_transition)
{
    const scene_entry *scene = find_scene_const(runtime, scene_name);
    if (scene == NULL || phase == NULL)
        return false;

    const char *transition_name = json_string(obj_get(scene->root, "transitions"), phase, NULL);
    return transition_name != NULL && slayer3d_game_data_get_transition(runtime, transition_name, out_transition);
}

bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                         const slayer3d_game_data_ui_metrics *metrics);

static const scene_menu_state *active_scene_menu_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                             const slayer3d_game_data_ui_metrics *metrics)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    if (runtime == NULL || scene == NULL || scene->menu_count <= 0)
        return NULL;

    for (int i = 0; i < scene->menu_count; ++i)
    {
        yyjson_val *active_if = obj_get(scene->menus[i].menu, "active_if");
        if (active_if == NULL || eval_data_condition(runtime, active_if, metrics))
            return &scene->menus[i];
    }
    return NULL;
}

bool slayer3d_game_data_get_active_menu_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                    const slayer3d_game_data_ui_metrics *metrics,
                                                    slayer3d_game_data_menu *out_menu)
{
    if (out_menu != NULL)
    {
        SDL_zero(*out_menu);
        out_menu->up_action_id = -1;
        out_menu->down_action_id = -1;
        out_menu->left_action_id = -1;
        out_menu->right_action_id = -1;
        out_menu->select_action_id = -1;
        out_menu->back_action_id = -1;
        out_menu->move_signal_id = -1;
        out_menu->select_signal_id = -1;
    }
    if (runtime == NULL || out_menu == NULL)
        return false;

    const scene_menu_state *state = active_scene_menu_for_metrics(runtime, metrics);
    if (state == NULL)
        return false;
    yyjson_val *menu = state->menu;
    out_menu->name = json_string(menu, "name", NULL);
    out_menu->up_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "up_action", NULL));
    out_menu->down_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "down_action", NULL));
    out_menu->left_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "left_action", NULL));
    out_menu->right_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "right_action", NULL));
    out_menu->select_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "select_action", NULL));
    out_menu->back_action_id = slayer3d_game_data_find_action(runtime, json_string(menu, "back_action", NULL));
    out_menu->move_signal_id = slayer3d_game_data_find_signal(runtime, json_string(menu, "move_signal", NULL));
    out_menu->select_signal_id = slayer3d_game_data_find_signal(runtime, json_string(menu, "select_signal", NULL));
    out_menu->item_count = menu_runtime_item_count(runtime, state);
    out_menu->selected_index =
        out_menu->item_count > 0 ? SDL_clamp(state->selected_index, 0, out_menu->item_count - 1) : -1;
    return out_menu->name != NULL && out_menu->item_count > 0;
}

bool slayer3d_game_data_get_active_menu(const slayer3d_game_data_runtime *runtime, slayer3d_game_data_menu *out_menu)
{
    return slayer3d_game_data_get_active_menu_for_metrics(runtime, NULL, out_menu);
}

bool slayer3d_game_data_menu_move(slayer3d_game_data_runtime *runtime, const char *menu_name, int delta)
{
    scene_menu_state *menu = find_scene_menu(active_scene_entry(runtime), menu_name);
    const int item_count = menu_runtime_item_count(runtime, menu);
    if (menu == NULL || item_count <= 0)
        return false;

    menu->selected_index = SDL_clamp(menu->selected_index, 0, item_count - 1);
    int next = (menu->selected_index + delta) % item_count;
    if (next < 0)
        next += item_count;
    menu->selected_index = next;
    update_dynamic_list_selection_state(runtime, menu);
    return true;
}

bool slayer3d_game_data_publish_menu_selection(slayer3d_game_data_runtime *runtime, const char *menu_name)
{
    scene_menu_state *menu = find_scene_menu(active_scene_entry(runtime), menu_name);
    const int item_count = menu_runtime_item_count(runtime, menu);
    if (menu == NULL || item_count <= 0)
        return false;

    menu->selected_index = SDL_clamp(menu->selected_index, 0, item_count - 1);
    update_dynamic_list_selection_state(runtime, menu);
    return true;
}
