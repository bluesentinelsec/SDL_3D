/**
 * @file game_data_editor_text_edit.c
 * @brief Shared text-entry handling for editor UI fields.
 *
 * The property inspector, texture search/path, and global data fields each
 * used to run their own copy of the same keystroke state machine, which was a
 * recurring source of input bleed regressions. This module owns one generic
 * implementation of focus lookup, escape/enter handling, backspace, text
 * append, replace-on-next-text, submit signal emission, cursor display
 * refresh, and max-length clamping. Each field group only declares data
 * bindings plus an optional change callback.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

typedef struct editor_text_field_binding
{
    const char *focus_value;     /* value of the focus key that selects this field */
    const char *state_key;       /* scene-state key holding the field text */
    const char *display_key;     /* optional key for text + blinking cursor */
    size_t max_bytes;            /* maximum text length in bytes */
    const char *submit_signal;   /* signal emitted when Enter is pressed */
    const char *changed_message; /* editor.tool.last_action text while typing */
} editor_text_field_binding;

typedef struct editor_text_edit_context
{
    const char *focus_key;
    const char *replace_on_text_key;
    const editor_text_field_binding *fields;
    size_t field_count;
    const char *cancel_message; /* editor.tool.last_action text on Escape */
    bool delete_key_backspaces; /* treat Delete like Backspace */
    bool clear_focus_on_submit; /* Enter clears focus in addition to applying */
    void (*on_changed)(slayer3d_game_data_runtime *runtime, const editor_text_field_binding *field);
} editor_text_edit_context;

static const editor_text_field_binding *editor_text_edit_active_field(const slayer3d_game_data_runtime *runtime,
                                                                      const editor_text_edit_context *context)
{
    if (runtime == NULL || runtime->scene_state == NULL || context == NULL)
        return NULL;
    const char *focus = slayer3d_properties_get_string(runtime->scene_state, context->focus_key, "");
    for (size_t i = 0; i < context->field_count; ++i)
    {
        if (SDL_strcmp(focus, context->fields[i].focus_value) == 0)
            return &context->fields[i];
    }
    return NULL;
}

static void editor_text_edit_refresh_displays(slayer3d_game_data_runtime *runtime,
                                              const editor_text_edit_context *context)
{
    if (runtime == NULL || runtime->scene_state == NULL || context == NULL)
        return;

    const char *focus = slayer3d_properties_get_string(runtime->scene_state, context->focus_key, "");
    const bool cursor_visible = ((SDL_GetTicks() / 500U) % 2U) == 0U;
    for (size_t i = 0; i < context->field_count; ++i)
    {
        const editor_text_field_binding *field = &context->fields[i];
        if (field->display_key == NULL)
            continue;
        const char *text = slayer3d_properties_get_string(runtime->scene_state, field->state_key, "");
        const bool focused = SDL_strcmp(focus, field->focus_value) == 0;
        char display[320];
        SDL_snprintf(display, sizeof(display), "%s%s", text != NULL ? text : "", focused && cursor_visible ? "|" : "");
        slayer3d_properties_set_string(runtime->scene_state, field->display_key, display);
    }
}

static bool editor_text_edit_append(slayer3d_properties *scene_state, const char *key, const char *text,
                                    size_t max_bytes)
{
    if (scene_state == NULL || key == NULL || text == NULL || text[0] == '\0' || max_bytes == 0U)
        return false;
    char buffer[256];
    SDL_strlcpy(buffer, slayer3d_properties_get_string(scene_state, key, ""), sizeof(buffer));
    const size_t len = SDL_strlen(buffer);
    if (len >= max_bytes)
        return false;
    SDL_strlcpy(buffer + len, text, SDL_min(sizeof(buffer) - len, max_bytes - len + 1U));
    slayer3d_properties_set_string(scene_state, key, buffer);
    return true;
}

static bool editor_text_edit_backspace(slayer3d_properties *scene_state, const char *replace_on_text_key,
                                       const char *key)
{
    if (scene_state == NULL || key == NULL)
        return false;
    if (slayer3d_properties_get_bool(scene_state, replace_on_text_key, false))
    {
        slayer3d_properties_set_string(scene_state, key, "");
        slayer3d_properties_set_bool(scene_state, replace_on_text_key, false);
        return true;
    }
    char buffer[256];
    SDL_strlcpy(buffer, slayer3d_properties_get_string(scene_state, key, ""), sizeof(buffer));
    size_t len = SDL_strlen(buffer);
    if (len == 0U)
        return false;
    /* Step back over UTF-8 continuation bytes so one keypress removes one glyph. */
    do
    {
        --len;
    } while (len > 0U && ((unsigned char)buffer[len] & 0xC0U) == 0x80U);
    buffer[len] = '\0';
    slayer3d_properties_set_string(scene_state, key, buffer);
    return true;
}

static bool editor_update_text_edit(slayer3d_game_data_runtime *runtime, const editor_text_edit_context *context)
{
    if (runtime == NULL || runtime->scene_state == NULL || context == NULL)
        return false;
    editor_text_edit_refresh_displays(runtime, context);
    const editor_text_field_binding *field = editor_text_edit_active_field(runtime, context);
    if (field == NULL)
        return false;

    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_ESCAPE))
    {
        slayer3d_properties_set_string(runtime->scene_state, context->focus_key, "");
        editor_text_edit_refresh_displays(runtime, context);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", context->cancel_message);
        return true;
    }
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_RETURN) ||
        slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_KP_ENTER))
    {
        (void)editor_emit_signal_by_name(runtime, field->submit_signal);
        if (context->clear_focus_on_submit)
            slayer3d_properties_set_string(runtime->scene_state, context->focus_key, "");
        editor_text_edit_refresh_displays(runtime, context);
        return true;
    }

    bool changed = false;
    if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_BACKSPACE) ||
        (context->delete_key_backspaces && slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_DELETE)))
    {
        changed =
            editor_text_edit_backspace(runtime->scene_state, context->replace_on_text_key, field->state_key) || changed;
    }
    const char *input_text = slayer3d_input_get_text_input(input);
    if (input_text != NULL && input_text[0] != '\0' &&
        slayer3d_properties_get_bool(runtime->scene_state, context->replace_on_text_key, false))
    {
        slayer3d_properties_set_string(runtime->scene_state, field->state_key, "");
        slayer3d_properties_set_bool(runtime->scene_state, context->replace_on_text_key, false);
    }
    changed = editor_text_edit_append(runtime->scene_state, field->state_key, input_text, field->max_bytes) || changed;
    if (changed)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", field->changed_message);
        if (context->on_changed != NULL)
            context->on_changed(runtime, field);
        editor_text_edit_refresh_displays(runtime, context);
    }
    return true;
}

/* Property inspector key/value fields. */

static void editor_property_edit_on_changed(slayer3d_game_data_runtime *runtime, const editor_text_field_binding *field)
{
    if (runtime == NULL || runtime->scene_state == NULL || field == NULL)
        return;
    const int slot = slayer3d_properties_get_int(runtime->scene_state, "editor.property.edit.selected_slot", -1);
    if (slot < 0)
        return;
    char slot_key[96];
    SDL_snprintf(slot_key, sizeof(slot_key), "editor.property.slot.%d.%s", slot, field->focus_value);
    slayer3d_properties_set_string(runtime->scene_state, slot_key,
                                   slayer3d_properties_get_string(runtime->scene_state, field->state_key, ""));
}

static const editor_text_field_binding editor_property_edit_fields[] = {
    {"key", "editor.property.edit.key", NULL, 64U, "signal.editor.property.apply", "editing property"},
    {"value", "editor.property.edit.value", NULL, 192U, "signal.editor.property.apply", "editing property"},
};

static const editor_text_edit_context editor_property_edit_context = {
    "editor.property.edit.focus",
    "editor.property.edit.replace_on_text",
    editor_property_edit_fields,
    SDL_arraysize(editor_property_edit_fields),
    "property edit cancelled",
    false,
    true,
    editor_property_edit_on_changed,
};

/* Texture viewer search/path fields. */

static void editor_texture_edit_on_changed(slayer3d_game_data_runtime *runtime, const editor_text_field_binding *field)
{
    /* Mark the fuzzy search pending instead of running it per keystroke. */
    if (runtime != NULL && runtime->scene_state != NULL && field != NULL &&
        SDL_strcmp(field->focus_value, "search") == 0)
    {
        slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.search.pending", true);
    }
}

static const editor_text_field_binding editor_texture_edit_fields[] = {
    {"search", "editor.texture.search", "editor.texture.search.display", 96U, "signal.editor.texture.search.apply",
     "editing texture search"},
    {"path", "editor.texture.path.input", "editor.texture.path.display", 240U, "signal.editor.texture.path.apply",
     "editing texture path"},
};

static const editor_text_edit_context editor_texture_edit_context = {
    "editor.texture.edit.focus",
    "editor.texture.edit.replace_on_text",
    editor_texture_edit_fields,
    SDL_arraysize(editor_texture_edit_fields),
    "texture edit cancelled",
    true,
    true,
    editor_texture_edit_on_changed,
};

/* Skybox panel path field. */

static const editor_text_field_binding editor_sky_edit_fields[] = {
    {"path", "editor.sky.path.input", "editor.sky.path.display", 240U, "signal.editor.sky.path.apply",
     "editing skybox path"},
};

static const editor_text_edit_context editor_sky_edit_context = {
    "editor.sky.edit.focus",
    "editor.sky.edit.replace_on_text",
    editor_sky_edit_fields,
    SDL_arraysize(editor_sky_edit_fields),
    "skybox edit cancelled",
    true,
    true,
    NULL,
};

/* Global panel data key/value fields. */

static const editor_text_field_binding editor_global_edit_fields[] = {
    {"key", "editor.global.data.edit.key", "editor.global.data.edit.key.display", 64U,
     "signal.editor.global.data.apply", "editing global data"},
    {"value", "editor.global.data.edit.value", "editor.global.data.edit.value.display", 192U,
     "signal.editor.global.data.apply", "editing global data"},
};

static const editor_text_edit_context editor_global_edit_context = {
    "editor.global.data.edit.focus",
    "editor.global.data.edit.replace_on_text",
    editor_global_edit_fields,
    SDL_arraysize(editor_global_edit_fields),
    "global data edit cancelled",
    true,
    false,
    NULL,
};

bool editor_property_edit_has_focus(const slayer3d_game_data_runtime *runtime)
{
    return editor_text_edit_active_field(runtime, &editor_property_edit_context) != NULL;
}

bool editor_texture_edit_has_focus(const slayer3d_game_data_runtime *runtime)
{
    return editor_text_edit_active_field(runtime, &editor_texture_edit_context) != NULL;
}

bool editor_global_edit_has_focus(const slayer3d_game_data_runtime *runtime)
{
    return editor_text_edit_active_field(runtime, &editor_global_edit_context) != NULL;
}

bool editor_sky_edit_has_focus(const slayer3d_game_data_runtime *runtime)
{
    return editor_text_edit_active_field(runtime, &editor_sky_edit_context) != NULL;
}

bool editor_update_property_text_edit(slayer3d_game_data_runtime *runtime)
{
    return editor_update_text_edit(runtime, &editor_property_edit_context);
}

bool editor_update_texture_text_edit(slayer3d_game_data_runtime *runtime)
{
    return editor_update_text_edit(runtime, &editor_texture_edit_context);
}

bool editor_update_global_text_edit(slayer3d_game_data_runtime *runtime)
{
    return editor_update_text_edit(runtime, &editor_global_edit_context);
}

bool editor_update_sky_text_edit(slayer3d_game_data_runtime *runtime)
{
    return editor_update_text_edit(runtime, &editor_sky_edit_context);
}

void editor_update_texture_edit_display(slayer3d_game_data_runtime *runtime)
{
    editor_text_edit_refresh_displays(runtime, &editor_texture_edit_context);
}

void editor_update_global_edit_display(slayer3d_game_data_runtime *runtime)
{
    editor_text_edit_refresh_displays(runtime, &editor_global_edit_context);
}

void editor_update_sky_edit_display(slayer3d_game_data_runtime *runtime)
{
    editor_text_edit_refresh_displays(runtime, &editor_sky_edit_context);
}
