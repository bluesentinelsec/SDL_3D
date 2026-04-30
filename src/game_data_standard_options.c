/**
 * @file game_data_standard_options.c
 * @brief Generated standard options scenes for JSON-authored games.
 */

#include "game_data_standard_options.h"

#include <SDL3/SDL_stdinc.h>

typedef struct standard_options_scene_layout
{
    double title_y;
    double menu_x;
    double menu_y;
    double gap;
    double cursor_offset_x;
} standard_options_scene_layout;

typedef struct standard_options_layout
{
    double title_x;
    double menu_x;
    double status_x;
    double status_y;
    const char *menu_align;
    const char *cursor_align;
    bool selected_pulse_alpha;
    bool title_divider;
    standard_options_scene_layout root;
    standard_options_scene_layout display;
    standard_options_scene_layout keyboard;
    standard_options_scene_layout mouse;
    standard_options_scene_layout gamepad;
    standard_options_scene_layout audio;
} standard_options_layout;

typedef struct standard_options_config
{
    yyjson_val *root;
    yyjson_val *theme;
    yyjson_val *bindings;
    standard_options_layout layout;
    const char *settings_actor;
    const char *return_scene;
    const char *root_scene;
    const char *display_scene;
    const char *keyboard_scene;
    const char *mouse_scene;
    const char *gamepad_scene;
    const char *audio_scene;
    const char *root_menu;
    const char *display_menu;
    const char *keyboard_menu;
    const char *mouse_menu;
    const char *gamepad_menu;
    const char *audio_menu;
    const char *up_action;
    const char *down_action;
    const char *left_action;
    const char *right_action;
    const char *select_action;
    const char *move_signal;
    const char *select_signal;
    const char *apply_signal;
    const char *apply_audio_signal;
    const char *reset_display_signal;
    const char *reset_keyboard_signal;
    const char *reset_mouse_signal;
    const char *reset_gamepad_signal;
    const char *reset_audio_signal;
    const char *title_font;
    const char *menu_font;
    const char *keyboard_status_key;
    const char *mouse_status_key;
    const char *gamepad_status_key;
    const char *enter_transition;
    const char *exit_transition;
    yyjson_val *background;
    const char *background_camera;
    bool background_renders_world;
} standard_options_config;

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
}

static const char *json_string(yyjson_val *object, const char *key, const char *fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : fallback;
}

static bool json_bool(yyjson_val *object, const char *key, bool fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

static double json_double(yyjson_val *object, const char *key, double fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_num(value) ? yyjson_get_num(value) : fallback;
}

static const char *section_string(yyjson_val *object, const char *section, const char *key, const char *fallback)
{
    return json_string(obj_get(object, section), key, fallback);
}

static standard_options_scene_layout scene_layout_defaults(double title_y, double menu_x, double menu_y, double gap,
                                                           double cursor_offset_x)
{
    return (standard_options_scene_layout){
        .title_y = title_y,
        .menu_x = menu_x,
        .menu_y = menu_y,
        .gap = gap,
        .cursor_offset_x = cursor_offset_x,
    };
}

static standard_options_scene_layout load_scene_layout(yyjson_val *layout, const char *section,
                                                       standard_options_scene_layout fallback)
{
    yyjson_val *scene = obj_get(layout, section);
    fallback.title_y = json_double(scene, "title_y", fallback.title_y);
    fallback.menu_x = json_double(scene, "menu_x", json_double(layout, "menu_x", fallback.menu_x));
    fallback.menu_y = json_double(scene, "menu_y", fallback.menu_y);
    fallback.gap = json_double(scene, "gap", fallback.gap);
    fallback.cursor_offset_x =
        json_double(scene, "cursor_offset_x", json_double(scene, "cursor_offset", fallback.cursor_offset_x));
    return fallback;
}

static void load_layout(standard_options_config *config)
{
    yyjson_val *layout = obj_get(config->root, "layout");
    config->layout.title_x = json_double(layout, "title_x", 0.5);
    config->layout.menu_x = json_double(layout, "menu_x", 0.5);
    config->layout.status_x = json_double(layout, "status_x", 0.5);
    config->layout.status_y = json_double(layout, "status_y", 0.88);
    config->layout.menu_align = json_string(layout, "menu_align", "left");
    config->layout.cursor_align = json_string(layout, "cursor_align", "right");
    config->layout.selected_pulse_alpha = json_bool(layout, "selected_pulse_alpha", true);
    config->layout.title_divider = json_bool(layout, "title_divider", true);
    config->layout.root = load_scene_layout(layout, "root", scene_layout_defaults(0.18, 0.43, 0.36, 0.078, -0.035));
    config->layout.display =
        load_scene_layout(layout, "display", scene_layout_defaults(0.20, 0.30, 0.38, 0.074, -0.035));
    config->layout.keyboard =
        load_scene_layout(layout, "keyboard", scene_layout_defaults(0.13, 0.36, 0.29, 0.062, -0.035));
    config->layout.mouse = load_scene_layout(layout, "mouse", scene_layout_defaults(0.13, 0.40, 0.30, 0.062, -0.035));
    config->layout.gamepad =
        load_scene_layout(layout, "gamepad", scene_layout_defaults(0.105, 0.34, 0.24, 0.055, -0.035));
    config->layout.audio = load_scene_layout(layout, "audio", scene_layout_defaults(0.18, 0.34, 0.39, 0.078, -0.035));
}

static void set_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "standard options error");
}

static bool add_str(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, const char *value)
{
    return value != NULL && yyjson_mut_obj_add_strcpy(doc, obj, key, value);
}

static bool add_arr_str(yyjson_mut_doc *doc, yyjson_mut_val *arr, const char *value)
{
    return value != NULL && yyjson_mut_arr_add_strcpy(doc, arr, value);
}

static bool add_color(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *key, yyjson_val *theme,
                      const char *theme_key, int r, int g, int b, int a)
{
    yyjson_val *authored = obj_get(theme, theme_key);
    yyjson_mut_val *color = yyjson_is_arr(authored) ? yyjson_val_mut_copy(doc, authored) : yyjson_mut_arr(doc);
    if (color == NULL)
        return false;
    if (!yyjson_is_arr(authored))
    {
        if (!yyjson_mut_arr_add_int(doc, color, r) || !yyjson_mut_arr_add_int(doc, color, g) ||
            !yyjson_mut_arr_add_int(doc, color, b) || !yyjson_mut_arr_add_int(doc, color, a))
            return false;
    }
    return yyjson_mut_obj_add_val(doc, obj, key, color);
}

static yyjson_mut_val *add_obj(yyjson_mut_doc *doc, yyjson_mut_val *parent, const char *key)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || !yyjson_mut_obj_add_val(doc, parent, key, obj))
        return NULL;
    return obj;
}

static yyjson_mut_val *add_arr(yyjson_mut_doc *doc, yyjson_mut_val *parent, const char *key)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (arr == NULL || !yyjson_mut_obj_add_val(doc, parent, key, arr))
        return NULL;
    return arr;
}

static bool add_scene_input(yyjson_mut_doc *doc, yyjson_mut_val *scene, const standard_options_config *config,
                            bool include_lr)
{
    yyjson_mut_val *input = add_obj(doc, scene, "input");
    yyjson_mut_val *actions = input != NULL ? add_arr(doc, input, "actions") : NULL;
    if (actions == NULL)
        return false;
    if (!add_arr_str(doc, actions, config->up_action) || !add_arr_str(doc, actions, config->down_action))
        return false;
    if (include_lr &&
        (!add_arr_str(doc, actions, config->left_action) || !add_arr_str(doc, actions, config->right_action)))
        return false;
    return add_arr_str(doc, actions, config->select_action);
}

static bool add_scene_transitions(yyjson_mut_doc *doc, yyjson_mut_val *scene, const standard_options_config *config)
{
    yyjson_mut_val *transitions = add_obj(doc, scene, "transitions");
    return transitions != NULL && add_str(doc, transitions, "enter", config->enter_transition) &&
           add_str(doc, transitions, "exit", config->exit_transition);
}

static bool add_scene_entities(yyjson_mut_doc *doc, yyjson_mut_val *scene, const standard_options_config *config)
{
    yyjson_mut_val *entities = add_arr(doc, scene, "entities");
    yyjson_val *authored_entities = obj_get(config->background, "entities");
    if (entities == NULL)
        return false;

    for (size_t i = 0; yyjson_is_arr(authored_entities) && i < yyjson_arr_size(authored_entities); ++i)
    {
        const char *entity = yyjson_get_str(yyjson_arr_get(authored_entities, i));
        if (entity != NULL && !add_arr_str(doc, entities, entity))
            return false;
    }
    return true;
}

static yyjson_mut_val *create_scene(yyjson_mut_doc *doc, const standard_options_config *config, const char *name,
                                    bool include_lr)
{
    yyjson_mut_val *scene = yyjson_mut_obj(doc);
    if (scene == NULL || !add_str(doc, scene, "schema", "sdl3d.scene.v0") || !add_str(doc, scene, "name", name) ||
        !yyjson_mut_obj_add_bool(doc, scene, "updates_game", false) ||
        !yyjson_mut_obj_add_bool(doc, scene, "renders_world", config->background_renders_world) ||
        (config->background_camera != NULL && !add_str(doc, scene, "camera", config->background_camera)) ||
        !add_scene_entities(doc, scene, config) || !add_scene_input(doc, scene, config, include_lr) ||
        !add_scene_transitions(doc, scene, config))
        return NULL;
    return scene;
}

static yyjson_mut_val *add_menu(yyjson_mut_doc *doc, yyjson_mut_val *scene, const standard_options_config *config,
                                const char *name, bool include_lr)
{
    yyjson_mut_val *menus = add_arr(doc, scene, "menus");
    yyjson_mut_val *menu = yyjson_mut_obj(doc);
    yyjson_mut_val *items = NULL;
    if (menus == NULL || menu == NULL || !yyjson_mut_arr_add_val(menus, menu) || !add_str(doc, menu, "name", name) ||
        !add_str(doc, menu, "up_action", config->up_action) || !add_str(doc, menu, "down_action", config->down_action))
        return NULL;
    if (include_lr && (!add_str(doc, menu, "left_action", config->left_action) ||
                       !add_str(doc, menu, "right_action", config->right_action)))
        return NULL;
    if (!add_str(doc, menu, "select_action", config->select_action) ||
        !add_str(doc, menu, "move_signal", config->move_signal) ||
        !add_str(doc, menu, "select_signal", config->select_signal) ||
        !yyjson_mut_obj_add_int(doc, menu, "selected", 0) || (items = add_arr(doc, menu, "items")) == NULL)
        return NULL;
    return items;
}

static yyjson_mut_val *add_menu_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const char *label)
{
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    if (item == NULL || !yyjson_mut_arr_add_val(items, item) || !add_str(doc, item, "label", label))
        return NULL;
    return item;
}

static bool add_scene_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const char *label, const char *scene)
{
    yyjson_mut_val *item = add_menu_item(doc, items, label);
    return item != NULL && add_str(doc, item, "scene", scene);
}

static bool add_back_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const standard_options_config *config,
                          const char *scene)
{
    yyjson_mut_val *item = add_menu_item(doc, items, "Back");
    if (item == NULL)
        return false;
    if (scene != NULL)
        return add_str(doc, item, "scene", scene);
    return yyjson_mut_obj_add_bool(doc, item, "return_scene", true) &&
           add_str(doc, item, "scene", config->return_scene);
}

static yyjson_mut_val *add_control(yyjson_mut_doc *doc, yyjson_mut_val *item, const char *type)
{
    yyjson_mut_val *control = add_obj(doc, item, "control");
    if (control == NULL || !add_str(doc, control, "type", type))
        return NULL;
    return control;
}

static bool add_choice(yyjson_mut_doc *doc, yyjson_mut_val *choices, const char *label, const char *value)
{
    yyjson_mut_val *choice = yyjson_mut_obj(doc);
    return choice != NULL && yyjson_mut_arr_add_val(choices, choice) && add_str(doc, choice, "label", label) &&
           add_str(doc, choice, "value", value);
}

static bool add_display_mode_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const standard_options_config *config)
{
    yyjson_mut_val *item = add_menu_item(doc, items, "Display Mode");
    yyjson_mut_val *control = item != NULL ? add_control(doc, item, "choice") : NULL;
    yyjson_mut_val *choices = control != NULL ? add_arr(doc, control, "choices") : NULL;
    return item != NULL && add_str(doc, item, "signal", config->apply_signal) && control != NULL &&
           add_str(doc, control, "target", config->settings_actor) && add_str(doc, control, "key", "display_mode") &&
           choices != NULL && add_choice(doc, choices, "Windowed", "windowed") &&
           add_choice(doc, choices, "Fullscreen Exclusive", "fullscreen_exclusive") &&
           add_choice(doc, choices, "Fullscreen Borderless", "fullscreen_borderless");
}

static bool add_toggle_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const char *label, const char *signal,
                            const char *target, const char *key)
{
    yyjson_mut_val *item = add_menu_item(doc, items, label);
    yyjson_mut_val *control = item != NULL ? add_control(doc, item, "toggle") : NULL;
    return item != NULL && (signal == NULL || add_str(doc, item, "signal", signal)) && control != NULL &&
           add_str(doc, control, "target", target) && add_str(doc, control, "key", key);
}

static bool add_renderer_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const standard_options_config *config)
{
    yyjson_mut_val *item = add_menu_item(doc, items, "Renderer");
    yyjson_mut_val *control = item != NULL ? add_control(doc, item, "choice") : NULL;
    yyjson_mut_val *choices = control != NULL ? add_arr(doc, control, "choices") : NULL;
    return item != NULL && add_str(doc, item, "signal", config->apply_signal) && control != NULL &&
           add_str(doc, control, "target", config->settings_actor) && add_str(doc, control, "key", "renderer") &&
           choices != NULL && add_choice(doc, choices, "Software", "software") &&
           add_choice(doc, choices, "OpenGL", "opengl");
}

static bool add_reset_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const char *signal)
{
    yyjson_mut_val *item = add_menu_item(doc, items, "Reset Settings");
    return item != NULL && add_str(doc, item, "signal", signal);
}

static bool add_range_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const standard_options_config *config,
                           const char *label, const char *signal, const char *target, const char *key,
                           int default_value)
{
    yyjson_mut_val *item = add_menu_item(doc, items, label);
    yyjson_mut_val *control = item != NULL ? add_control(doc, item, "range") : NULL;
    return item != NULL && add_str(doc, item, "signal", signal) && control != NULL &&
           add_str(doc, control, "target", target) && add_str(doc, control, "key", key) &&
           add_str(doc, control, "value_type", "int") && add_str(doc, control, "display", "slider") &&
           add_str(doc, control, "slider_fill", json_string(config->theme, "slider_fill", "#")) &&
           add_str(doc, control, "slider_empty", json_string(config->theme, "slider_empty", "-")) &&
           add_str(doc, control, "slider_left", json_string(config->theme, "slider_left", "[")) &&
           add_str(doc, control, "slider_right", json_string(config->theme, "slider_right", "]")) &&
           yyjson_mut_obj_add_int(doc, control, "min", 0) && yyjson_mut_obj_add_int(doc, control, "max", 10) &&
           yyjson_mut_obj_add_int(doc, control, "step", 1) &&
           yyjson_mut_obj_add_int(doc, control, "default", default_value);
}

static bool add_gamepad_icons_item(yyjson_mut_doc *doc, yyjson_mut_val *items, const standard_options_config *config)
{
    yyjson_mut_val *item = add_menu_item(doc, items, "Button Icons");
    yyjson_mut_val *control = item != NULL ? add_control(doc, item, "choice") : NULL;
    yyjson_mut_val *choices = control != NULL ? add_arr(doc, control, "choices") : NULL;
    return item != NULL && control != NULL && add_str(doc, control, "target", config->settings_actor) &&
           add_str(doc, control, "key", "gamepad_icons") && choices != NULL &&
           add_choice(doc, choices, "Xbox", "xbox") && add_choice(doc, choices, "Nintendo", "nintendo") &&
           add_choice(doc, choices, "PlayStation", "playstation");
}

static bool add_input_binding_item(yyjson_mut_doc *doc, yyjson_mut_val *items, yyjson_val *row)
{
    const char *label = json_string(row, "label", NULL);
    const char *default_input = json_string(row, "default", NULL);
    yyjson_val *bindings = obj_get(row, "bindings");
    yyjson_mut_val *item = add_menu_item(doc, items, label);
    yyjson_mut_val *control = item != NULL ? add_control(doc, item, "input_binding") : NULL;
    yyjson_mut_val *bindings_copy = yyjson_is_arr(bindings) ? yyjson_val_mut_copy(doc, bindings) : NULL;
    return label != NULL && default_input != NULL && bindings_copy != NULL && control != NULL &&
           add_str(doc, control, "default", default_input) &&
           yyjson_mut_obj_add_val(doc, control, "bindings", bindings_copy);
}

static bool add_input_binding_items(yyjson_mut_doc *doc, yyjson_mut_val *items, yyjson_val *rows)
{
    for (size_t i = 0; yyjson_is_arr(rows) && i < yyjson_arr_size(rows); ++i)
    {
        if (!add_input_binding_item(doc, items, yyjson_arr_get(rows, i)))
            return false;
    }
    return true;
}

static yyjson_mut_val *add_ui(yyjson_mut_doc *doc, yyjson_mut_val *scene)
{
    return add_obj(doc, scene, "ui");
}

static bool add_title_text(yyjson_mut_doc *doc, yyjson_mut_val *texts, const standard_options_config *config,
                           const char *name, const char *text, double y)
{
    yyjson_mut_val *title = yyjson_mut_obj(doc);
    return texts != NULL && title != NULL && yyjson_mut_arr_add_val(texts, title) &&
           add_str(doc, title, "name", name) && add_str(doc, title, "font", config->title_font) &&
           add_str(doc, title, "text", text) && yyjson_mut_obj_add_real(doc, title, "x", config->layout.title_x) &&
           yyjson_mut_obj_add_real(doc, title, "y", y) && yyjson_mut_obj_add_bool(doc, title, "normalized", true) &&
           yyjson_mut_obj_add_bool(doc, title, "centered", true) &&
           add_color(doc, title, "color", config->theme, "title_color", 242, 248, 255, 255);
}

static bool add_title_divider(yyjson_mut_doc *doc, yyjson_mut_val *texts, const standard_options_config *config,
                              const char *name, double title_y)
{
    if (!config->layout.title_divider)
        return true;

    char divider_name[128];
    SDL_snprintf(divider_name, sizeof(divider_name), "%s.divider", name != NULL ? name : "ui.options.title");

    yyjson_mut_val *divider = yyjson_mut_obj(doc);
    return texts != NULL && divider != NULL && yyjson_mut_arr_add_val(texts, divider) &&
           add_str(doc, divider, "name", divider_name) && add_str(doc, divider, "font", config->menu_font) &&
           add_str(doc, divider, "text", "----------------") &&
           yyjson_mut_obj_add_real(doc, divider, "x", config->layout.title_x) &&
           yyjson_mut_obj_add_real(doc, divider, "y", title_y + 0.095) &&
           yyjson_mut_obj_add_bool(doc, divider, "normalized", true) &&
           yyjson_mut_obj_add_bool(doc, divider, "centered", true) &&
           yyjson_mut_obj_add_real(doc, divider, "scale", 0.55) &&
           add_color(doc, divider, "color", config->theme, "divider_color", 126, 168, 238, 170);
}

static bool add_status_text(yyjson_mut_doc *doc, yyjson_mut_val *texts, const standard_options_config *config,
                            const char *name, const char *status_key, int r, int g, int b)
{
    yyjson_mut_val *text = yyjson_mut_obj(doc);
    yyjson_mut_val *bindings = NULL;
    yyjson_mut_val *binding = NULL;
    return text != NULL && yyjson_mut_arr_add_val(texts, text) && add_str(doc, text, "name", name) &&
           add_str(doc, text, "font", config->menu_font) && add_str(doc, text, "format", "%s") &&
           (bindings = add_arr(doc, text, "bindings")) != NULL && (binding = yyjson_mut_obj(doc)) != NULL &&
           yyjson_mut_arr_add_val(bindings, binding) && add_str(doc, binding, "type", "scene_state") &&
           add_str(doc, binding, "key", status_key) &&
           yyjson_mut_obj_add_real(doc, text, "x", config->layout.status_x) &&
           yyjson_mut_obj_add_real(doc, text, "y", config->layout.status_y) &&
           yyjson_mut_obj_add_bool(doc, text, "normalized", true) &&
           yyjson_mut_obj_add_bool(doc, text, "centered", true) &&
           add_color(doc, text, "color", config->theme, "status_color", r, g, b, 230);
}

static bool add_menu_presenter(yyjson_mut_doc *doc, yyjson_mut_val *ui, const standard_options_config *config,
                               const char *name, const char *menu_name, double x, double y, double gap,
                               double cursor_offset)
{
    yyjson_mut_val *menus = add_arr(doc, ui, "menus");
    yyjson_mut_val *presenter = yyjson_mut_obj(doc);
    yyjson_mut_val *cursor = NULL;
    return menus != NULL && presenter != NULL && yyjson_mut_arr_add_val(menus, presenter) &&
           add_str(doc, presenter, "name", name) && add_str(doc, presenter, "menu", menu_name) &&
           add_str(doc, presenter, "font", config->menu_font) && yyjson_mut_obj_add_real(doc, presenter, "x", x) &&
           yyjson_mut_obj_add_real(doc, presenter, "y", y) && yyjson_mut_obj_add_real(doc, presenter, "gap", gap) &&
           yyjson_mut_obj_add_bool(doc, presenter, "normalized", true) &&
           add_str(doc, presenter, "align", config->layout.menu_align) &&
           add_color(doc, presenter, "color", config->theme, "menu_color", 225, 236, 255, 245) &&
           add_color(doc, presenter, "selected_color", config->theme, "selected_color", 255, 245, 208, 255) &&
           yyjson_mut_obj_add_bool(doc, presenter, "selected_pulse_alpha", config->layout.selected_pulse_alpha) &&
           (cursor = add_obj(doc, presenter, "cursor")) != NULL &&
           add_str(doc, cursor, "text", json_string(config->theme, "cursor", ">")) &&
           yyjson_mut_obj_add_real(doc, cursor, "offset_x", cursor_offset) &&
           add_str(doc, cursor, "align", config->layout.cursor_align) &&
           add_color(doc, cursor, "color", config->theme, "cursor_color", 255, 222, 140, 255);
}

static bool add_basic_ui(yyjson_mut_doc *doc, yyjson_mut_val *scene, const standard_options_config *config,
                         const char *title_name, const char *title, double title_y, const char *presenter_name,
                         const char *menu_name, double menu_x, double menu_y, double gap, double cursor_offset)
{
    yyjson_mut_val *ui = add_ui(doc, scene);
    yyjson_mut_val *texts = ui != NULL ? add_arr(doc, ui, "text") : NULL;
    return texts != NULL && add_title_text(doc, texts, config, title_name, title, title_y) &&
           add_title_divider(doc, texts, config, title_name, title_y) &&
           add_menu_presenter(doc, ui, config, presenter_name, menu_name, menu_x, menu_y, gap, cursor_offset);
}

static yyjson_doc *finish_scene_doc(yyjson_mut_doc *doc, yyjson_mut_val *scene)
{
    yyjson_doc *immutable = NULL;
    if (doc != NULL && scene != NULL)
    {
        yyjson_mut_doc_set_root(doc, scene);
        immutable = yyjson_mut_doc_imut_copy(doc, NULL);
    }
    yyjson_mut_doc_free(doc);
    return immutable;
}

static yyjson_doc *build_root_scene(const standard_options_config *config)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *scene = doc != NULL ? create_scene(doc, config, config->root_scene, false) : NULL;
    yyjson_mut_val *items = scene != NULL ? add_menu(doc, scene, config, config->root_menu, false) : NULL;
    if (items == NULL || !add_scene_item(doc, items, "Display", config->display_scene) ||
        !add_scene_item(doc, items, "Keyboard", config->keyboard_scene) ||
        !add_scene_item(doc, items, "Mouse", config->mouse_scene) ||
        !add_scene_item(doc, items, "Gamepad", config->gamepad_scene) ||
        !add_scene_item(doc, items, "Audio", config->audio_scene) || !add_back_item(doc, items, config, NULL) ||
        !add_basic_ui(doc, scene, config, "ui.options.title", "OPTIONS", config->layout.root.title_y, "ui.options.menu",
                      config->root_menu, config->layout.root.menu_x, config->layout.root.menu_y,
                      config->layout.root.gap, config->layout.root.cursor_offset_x))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return finish_scene_doc(doc, scene);
}

static yyjson_doc *build_display_scene(const standard_options_config *config)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *scene = doc != NULL ? create_scene(doc, config, config->display_scene, false) : NULL;
    yyjson_mut_val *items = scene != NULL ? add_menu(doc, scene, config, config->display_menu, false) : NULL;
    if (items == NULL || !add_display_mode_item(doc, items, config) ||
        !add_toggle_item(doc, items, "Vsync", config->apply_signal, config->settings_actor, "vsync") ||
        !add_renderer_item(doc, items, config) || !add_reset_item(doc, items, config->reset_display_signal) ||
        !add_back_item(doc, items, config, config->root_scene) ||
        !add_basic_ui(doc, scene, config, "ui.options.display.title", "DISPLAY", config->layout.display.title_y,
                      "ui.options.display.menu", config->display_menu, config->layout.display.menu_x,
                      config->layout.display.menu_y, config->layout.display.gap,
                      config->layout.display.cursor_offset_x))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return finish_scene_doc(doc, scene);
}

static yyjson_doc *build_audio_scene(const standard_options_config *config)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *scene = doc != NULL ? create_scene(doc, config, config->audio_scene, true) : NULL;
    yyjson_mut_val *items = scene != NULL ? add_menu(doc, scene, config, config->audio_menu, true) : NULL;
    if (items == NULL ||
        !add_range_item(doc, items, config, "Sound Effects", config->apply_audio_signal, config->settings_actor,
                        "sfx_volume", 8) ||
        !add_range_item(doc, items, config, "Music", config->apply_audio_signal, config->settings_actor, "music_volume",
                        7) ||
        !add_reset_item(doc, items, config->reset_audio_signal) ||
        !add_back_item(doc, items, config, config->root_scene) ||
        !add_basic_ui(doc, scene, config, "ui.options.audio.title", "AUDIO", config->layout.audio.title_y,
                      "ui.options.audio.menu", config->audio_menu, config->layout.audio.menu_x,
                      config->layout.audio.menu_y, config->layout.audio.gap, config->layout.audio.cursor_offset_x))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return finish_scene_doc(doc, scene);
}

static yyjson_doc *build_keyboard_scene(const standard_options_config *config)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *scene = doc != NULL ? create_scene(doc, config, config->keyboard_scene, false) : NULL;
    yyjson_mut_val *items = scene != NULL ? add_menu(doc, scene, config, config->keyboard_menu, false) : NULL;
    yyjson_mut_val *ui = NULL;
    yyjson_mut_val *texts = NULL;
    if (items == NULL || !add_input_binding_items(doc, items, obj_get(config->bindings, "keyboard")) ||
        !add_reset_item(doc, items, config->reset_keyboard_signal) ||
        !add_back_item(doc, items, config, config->root_scene) || (ui = add_ui(doc, scene)) == NULL ||
        (texts = add_arr(doc, ui, "text")) == NULL ||
        !add_title_text(doc, texts, config, "ui.options.keyboard.title", "KEYBOARD", config->layout.keyboard.title_y))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    if (!add_status_text(doc, texts, config, "ui.options.keyboard.status", config->keyboard_status_key, 255, 222,
                         140) ||
        !add_title_divider(doc, texts, config, "ui.options.keyboard.title", config->layout.keyboard.title_y) ||
        !add_menu_presenter(doc, ui, config, "ui.options.keyboard.menu", config->keyboard_menu,
                            config->layout.keyboard.menu_x, config->layout.keyboard.menu_y, config->layout.keyboard.gap,
                            config->layout.keyboard.cursor_offset_x))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return finish_scene_doc(doc, scene);
}

static yyjson_doc *build_mouse_scene(const standard_options_config *config)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *scene = doc != NULL ? create_scene(doc, config, config->mouse_scene, false) : NULL;
    yyjson_mut_val *items = scene != NULL ? add_menu(doc, scene, config, config->mouse_menu, false) : NULL;
    yyjson_mut_val *ui = NULL;
    yyjson_mut_val *texts = NULL;
    if (items == NULL || !add_input_binding_items(doc, items, obj_get(config->bindings, "mouse")) ||
        !add_reset_item(doc, items, config->reset_mouse_signal) ||
        !add_back_item(doc, items, config, config->root_scene) || (ui = add_ui(doc, scene)) == NULL ||
        (texts = add_arr(doc, ui, "text")) == NULL ||
        !add_title_text(doc, texts, config, "ui.options.mouse.title", "MOUSE", config->layout.mouse.title_y))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    if (!add_status_text(doc, texts, config, "ui.options.mouse.status", config->mouse_status_key, 255, 222, 140) ||
        !add_title_divider(doc, texts, config, "ui.options.mouse.title", config->layout.mouse.title_y) ||
        !add_menu_presenter(doc, ui, config, "ui.options.mouse.menu", config->mouse_menu, config->layout.mouse.menu_x,
                            config->layout.mouse.menu_y, config->layout.mouse.gap,
                            config->layout.mouse.cursor_offset_x))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return finish_scene_doc(doc, scene);
}

static yyjson_doc *build_gamepad_scene(const standard_options_config *config)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *scene = doc != NULL ? create_scene(doc, config, config->gamepad_scene, true) : NULL;
    yyjson_mut_val *items = scene != NULL ? add_menu(doc, scene, config, config->gamepad_menu, true) : NULL;
    yyjson_mut_val *ui = NULL;
    yyjson_mut_val *texts = NULL;
    if (items == NULL || !add_gamepad_icons_item(doc, items, config) ||
        !add_toggle_item(doc, items, "Vibration", NULL, config->settings_actor, "vibration") ||
        !add_input_binding_items(doc, items, obj_get(config->bindings, "gamepad")) ||
        !add_reset_item(doc, items, config->reset_gamepad_signal) ||
        !add_back_item(doc, items, config, config->root_scene) || (ui = add_ui(doc, scene)) == NULL ||
        (texts = add_arr(doc, ui, "text")) == NULL ||
        !add_title_text(doc, texts, config, "ui.options.gamepad.title", "GAMEPAD", config->layout.gamepad.title_y) ||
        !add_status_text(doc, texts, config, "ui.options.gamepad.status", config->gamepad_status_key, 172, 206, 255) ||
        !add_title_divider(doc, texts, config, "ui.options.gamepad.title", config->layout.gamepad.title_y) ||
        !add_menu_presenter(doc, ui, config, "ui.options.gamepad.menu", config->gamepad_menu,
                            config->layout.gamepad.menu_x, config->layout.gamepad.menu_y, config->layout.gamepad.gap,
                            config->layout.gamepad.cursor_offset_x))
    {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return finish_scene_doc(doc, scene);
}

static bool load_config(yyjson_val *game_root, const char *package_name, standard_options_config *config,
                        char *error_buffer, int error_buffer_size)
{
    yyjson_val *scenes = obj_get(game_root, "scenes");
    yyjson_val *root = obj_get(scenes, "standard_options");
    if (SDL_strcmp(package_name != NULL ? package_name : "", "standard_options") != 0)
    {
        set_error(error_buffer, error_buffer_size, "unknown scene package");
        return false;
    }
    if (!yyjson_is_obj(root))
    {
        set_error(error_buffer, error_buffer_size, "scenes.standard_options must be an object");
        return false;
    }

    SDL_zero(*config);
    config->root = root;
    config->theme = obj_get(root, "theme");
    config->bindings = obj_get(root, "bindings");
    config->background = obj_get(root, "background");
    config->background_camera = json_string(config->background, "camera", NULL);
    config->background_renders_world =
        yyjson_is_obj(config->background) ? json_bool(config->background, "renders_world", true) : false;
    config->settings_actor = json_string(root, "settings", "entity.settings");
    config->return_scene = json_string(root, "return_scene", "scene.title");
    config->root_scene = section_string(root, "scenes", "root", "scene.options");
    config->display_scene = section_string(root, "scenes", "display", "scene.options.display");
    config->keyboard_scene = section_string(root, "scenes", "keyboard", "scene.options.keyboard");
    config->mouse_scene = section_string(root, "scenes", "mouse", "scene.options.mouse");
    config->gamepad_scene = section_string(root, "scenes", "gamepad", "scene.options.gamepad");
    config->audio_scene = section_string(root, "scenes", "audio", "scene.options.audio");
    config->root_menu = section_string(root, "menus", "root", "menu.options");
    config->display_menu = section_string(root, "menus", "display", "menu.options.display");
    config->keyboard_menu = section_string(root, "menus", "keyboard", "menu.options.keyboard");
    config->mouse_menu = section_string(root, "menus", "mouse", "menu.options.mouse");
    config->gamepad_menu = section_string(root, "menus", "gamepad", "menu.options.gamepad");
    config->audio_menu = section_string(root, "menus", "audio", "menu.options.audio");
    config->up_action = section_string(root, "actions", "up", "action.menu.up");
    config->down_action = section_string(root, "actions", "down", "action.menu.down");
    config->left_action = section_string(root, "actions", "left", "action.menu.left");
    config->right_action = section_string(root, "actions", "right", "action.menu.right");
    config->select_action = section_string(root, "actions", "select", "action.menu.select");
    config->move_signal = section_string(root, "signals", "move", "signal.ui.menu.move");
    config->select_signal = section_string(root, "signals", "select", "signal.ui.menu.select");
    config->apply_signal = section_string(root, "signals", "apply", "signal.settings.apply");
    config->apply_audio_signal = section_string(root, "signals", "apply_audio", "signal.settings.apply_audio");
    config->reset_display_signal = section_string(root, "signals", "reset_display", "signal.settings.reset_display");
    config->reset_keyboard_signal = section_string(root, "signals", "reset_keyboard", "signal.settings.reset_keyboard");
    config->reset_mouse_signal = section_string(root, "signals", "reset_mouse", "signal.settings.reset_mouse");
    config->reset_gamepad_signal = section_string(root, "signals", "reset_gamepad", "signal.settings.reset_gamepad");
    config->reset_audio_signal = section_string(root, "signals", "reset_audio", "signal.settings.reset_audio");
    config->title_font = section_string(root, "fonts", "title", "font.title");
    config->menu_font = section_string(root, "fonts", "menu", "font.hud");
    config->keyboard_status_key = json_string(root, "keyboard_status_key", "keyboard_binding_status");
    config->mouse_status_key = json_string(root, "mouse_status_key", "mouse_binding_status");
    config->gamepad_status_key = json_string(root, "gamepad_status_key", "gamepad_binding_status");
    config->enter_transition = section_string(root, "transitions", "enter", "scene_in");
    config->exit_transition = section_string(root, "transitions", "exit", "scene_out");
    load_layout(config);
    return true;
}

bool sdl3d_standard_options_build_scene_docs(yyjson_val *game_root, const char *package_name,
                                             sdl3d_standard_options_scene_docs *out_docs, char *error_buffer,
                                             int error_buffer_size)
{
    if (out_docs != NULL)
        SDL_zero(*out_docs);
    if (game_root == NULL || out_docs == NULL)
    {
        set_error(error_buffer, error_buffer_size, "invalid standard options arguments");
        return false;
    }

    standard_options_config config;
    if (!load_config(game_root, package_name, &config, error_buffer, error_buffer_size))
        return false;

    yyjson_doc **docs = (yyjson_doc **)SDL_calloc(SDL3D_STANDARD_OPTIONS_SCENE_COUNT, sizeof(*docs));
    if (docs == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate standard options scenes");
        return false;
    }

    docs[0] = build_root_scene(&config);
    docs[1] = build_display_scene(&config);
    docs[2] = build_keyboard_scene(&config);
    docs[3] = build_mouse_scene(&config);
    docs[4] = build_gamepad_scene(&config);
    docs[5] = build_audio_scene(&config);
    for (int i = 0; i < SDL3D_STANDARD_OPTIONS_SCENE_COUNT; ++i)
    {
        if (docs[i] == NULL)
        {
            sdl3d_standard_options_scene_docs partial = {.docs = docs, .count = SDL3D_STANDARD_OPTIONS_SCENE_COUNT};
            sdl3d_standard_options_scene_docs_free(&partial);
            set_error(error_buffer, error_buffer_size, "failed to build standard options scene");
            return false;
        }
    }

    out_docs->docs = docs;
    out_docs->count = SDL3D_STANDARD_OPTIONS_SCENE_COUNT;
    return true;
}

void sdl3d_standard_options_scene_docs_free(sdl3d_standard_options_scene_docs *docs)
{
    if (docs == NULL)
        return;
    for (int i = 0; i < docs->count; ++i)
        yyjson_doc_free(docs->docs[i]);
    SDL_free(docs->docs);
    SDL_zero(*docs);
}
