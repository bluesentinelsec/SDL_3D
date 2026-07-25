/**
 * @file game_data_actions.c
 * @brief Data-authored action dispatcher.
 */

#include "game_data_editor_dialog_internal.h"
#include "game_data_internal.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_process.h>

void slayer3d_game_data_editor_file_dialog_request_free(editor_file_dialog_request *request)
{
    if (request == NULL)
        return;
    SDL_free(request->path_key);
    SDL_free(request->accept_signal);
    SDL_free(request->status_key);
    SDL_free(request->message_key);
    SDL_free(request->default_location);
    SDL_free(request->selected_path);
    SDL_free(request->message);
    SDL_free(request);
}

Uint32 slayer3d_game_data_editor_file_dialog_event_type(void)
{
    static Uint32 event_type = 0;
    if (event_type == 0)
        event_type = SDL_RegisterEvents(1);
    return event_type;
}

static void SDLCALL editor_file_dialog_callback(void *userdata, const char *const *filelist, int filter)
{
    (void)filter;
    editor_file_dialog_request *request = (editor_file_dialog_request *)userdata;
    if (request == NULL)
        return;

    if (filelist == NULL)
    {
        const char *error = SDL_GetError();
        request->failed = true;
        request->message = SDL_strdup(error != NULL && error[0] != '\0' ? error : "file dialog failed");
    }
    else if (filelist[0] == NULL)
    {
        request->canceled = true;
        request->message = SDL_strdup("file dialog canceled");
    }
    else
    {
        request->selected_path = SDL_strdup(filelist[0]);
        if (request->selected_path == NULL)
        {
            request->failed = true;
            request->message = SDL_strdup("file dialog result allocation failed");
        }
    }

    const Uint32 event_type = slayer3d_game_data_editor_file_dialog_event_type();
    if (event_type == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D editor file dialog could not allocate SDL event type");
        slayer3d_game_data_editor_file_dialog_request_free(request);
        return;
    }

    SDL_Event event;
    SDL_zero(event);
    event.type = event_type;
    event.user.data1 = request;
    if (!SDL_PushEvent(&event))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D editor file dialog could not push SDL event: %s",
                    SDL_GetError());
        slayer3d_game_data_editor_file_dialog_request_free(request);
    }
}

static bool execute_editor_file_dialog_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    static const SDL_DialogFileFilter map_filters[] = {
        {"Slayer3D maps", "slayermap.json;json"},
        {"JSON", "json"},
        {"All files", "*"},
    };

    const char *mode = json_string(action, "mode", "");
    const bool save = SDL_strcmp(mode, "save") == 0;
    const bool folder = SDL_strcmp(mode, "folder") == 0;
    if (!save && !folder && SDL_strcmp(mode, "open") != 0)
        return false;

    const char *path_key = json_string(action, "path_key", NULL);
    const char *accept_signal = json_string(action, "accept_signal", NULL);
    if (path_key == NULL || path_key[0] == '\0' || accept_signal == NULL || accept_signal[0] == '\0')
        return false;

    editor_file_dialog_request *request = (editor_file_dialog_request *)SDL_calloc(1, sizeof(*request));
    if (request == NULL)
        return false;
    request->path_key = SDL_strdup(path_key);
    request->accept_signal = SDL_strdup(accept_signal);
    request->status_key = SDL_strdup(json_string(action, "status_key", ""));
    request->message_key = SDL_strdup(json_string(action, "message_key", ""));
    const char *default_path_key = json_string(action, "default_path_key", NULL);
    const char *default_path = default_path_key != NULL && runtime != NULL && runtime->scene_state != NULL
                                   ? slayer3d_properties_get_string(runtime->scene_state, default_path_key, "")
                                   : "";
    if (default_path != NULL && default_path[0] != '\0')
        request->default_location = SDL_strdup(default_path);

    if (request->path_key == NULL || request->accept_signal == NULL || request->status_key == NULL ||
        request->message_key == NULL)
    {
        slayer3d_game_data_editor_file_dialog_request_free(request);
        return false;
    }
    if (slayer3d_game_data_editor_file_dialog_event_type() == 0)
    {
        slayer3d_game_data_editor_file_dialog_request_free(request);
        return false;
    }

    if (runtime != NULL && runtime->scene_state != NULL)
    {
        if (request->status_key[0] != '\0')
            slayer3d_properties_set_string(runtime->scene_state, request->status_key,
                                           save ? "save pending" : (folder ? "folder pending" : "open pending"));
        if (request->message_key[0] != '\0')
            slayer3d_properties_set_string(runtime->scene_state, request->message_key, "");
    }

    if (save)
    {
        SDL_ShowSaveFileDialog(editor_file_dialog_callback, request, NULL, map_filters, (int)SDL_arraysize(map_filters),
                               request->default_location);
    }
    else if (folder)
    {
        SDL_ShowOpenFolderDialog(editor_file_dialog_callback, request, NULL, request->default_location, false);
    }
    else
    {
        SDL_ShowOpenFileDialog(editor_file_dialog_callback, request, NULL, map_filters, (int)SDL_arraysize(map_filters),
                               request->default_location, false);
    }
    return true;
}

static bool debug_write_all(SDL_IOStream *stream, const char *text)
{
    if (stream == NULL || text == NULL)
        return false;
    const size_t size = SDL_strlen(text);
    return SDL_WriteIO(stream, text, size) == size;
}

static bool debug_make_directory_recursive(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;

    SDL_PathInfo info;
    SDL_zero(info);
    if (SDL_GetPathInfo(path, &info))
        return info.type == SDL_PATHTYPE_DIRECTORY;

    char *copy = SDL_strdup(path);
    if (copy == NULL)
        return false;

    bool ok = true;
    for (char *p = copy + 1; *p != '\0'; ++p)
    {
        if (*p != '/' && *p != '\\')
            continue;
        const char saved = *p;
        *p = '\0';
        if (copy[0] != '\0')
        {
            SDL_zero(info);
            if (!SDL_GetPathInfo(copy, &info))
                ok = SDL_CreateDirectory(copy);
            else
                ok = info.type == SDL_PATHTYPE_DIRECTORY;
        }
        *p = saved;
        if (!ok)
            break;
    }

    if (ok)
    {
        SDL_zero(info);
        if (!SDL_GetPathInfo(copy, &info))
            ok = SDL_CreateDirectory(copy);
        else
            ok = info.type == SDL_PATHTYPE_DIRECTORY;
    }

    SDL_free(copy);
    return ok;
}

static bool editor_texture_extension_supported(const char *filename)
{
    const char *dot = filename != NULL ? SDL_strrchr(filename, '.') : NULL;
    if (dot == NULL || dot[1] == '\0')
        return false;
    return SDL_strcasecmp(dot, ".png") == 0 || SDL_strcasecmp(dot, ".jpg") == 0 || SDL_strcasecmp(dot, ".jpeg") == 0 ||
           SDL_strcasecmp(dot, ".bmp") == 0 || SDL_strcasecmp(dot, ".tga") == 0 || SDL_strcasecmp(dot, ".webp") == 0;
}

static char editor_path_separator(void)
{
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

static bool editor_path_absolute(const char *path);

static bool editor_path_asset_uri(const char *path)
{
    return path != NULL && SDL_strncmp(path, "asset://", 8) == 0;
}

static char *editor_path_join(const char *base, const char *leaf)
{
    if (leaf == NULL)
        return NULL;
    if (base == NULL || base[0] == '\0')
        return SDL_strdup(leaf);
    const size_t base_len = SDL_strlen(base);
    const size_t leaf_len = SDL_strlen(leaf);
    const bool needs_sep = base_len > 0U && base[base_len - 1U] != '/' && base[base_len - 1U] != '\\';
    char *joined = (char *)SDL_malloc(base_len + (needs_sep ? 1U : 0U) + leaf_len + 1U);
    if (joined == NULL)
        return NULL;
    SDL_memcpy(joined, base, base_len);
    size_t offset = base_len;
    if (needs_sep)
        joined[offset++] = editor_path_separator();
    SDL_memcpy(joined + offset, leaf, leaf_len + 1U);
#if defined(_WIN32)
    for (char *p = joined; *p != '\0'; ++p)
    {
        if (*p == '/')
            *p = '\\';
    }
#endif
    return joined;
}

static char *editor_asset_path_join(const char *base, const char *leaf)
{
    if (leaf == NULL)
        return NULL;
    if (base == NULL || base[0] == '\0')
        return SDL_strdup(leaf);
    const size_t base_len = SDL_strlen(base);
    const size_t leaf_len = SDL_strlen(leaf);
    const bool needs_sep = base_len > 0U && base[base_len - 1U] != '/';
    char *joined = (char *)SDL_malloc(base_len + (needs_sep ? 1U : 0U) + leaf_len + 1U);
    if (joined == NULL)
        return NULL;
    SDL_memcpy(joined, base, base_len);
    size_t offset = base_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, leaf, leaf_len + 1U);
    return joined;
}

static char *editor_relative_path_join(const char *base, const char *leaf)
{
    char *joined = editor_asset_path_join(base, leaf);
    if (joined == NULL)
        return NULL;
    for (char *p = joined; *p != '\0'; ++p)
    {
        if (*p == '\\')
            *p = '/';
    }
    return joined;
}

static char *editor_path_make_absolute_from_cwd(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return NULL;
    if (editor_path_absolute(path))
        return SDL_strdup(path);

    char *cwd = SDL_GetCurrentDirectory();
    if (cwd == NULL)
        return NULL;
    char *absolute = editor_path_join(cwd, path);
    SDL_free(cwd);
    return absolute;
}

static bool editor_path_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
#if defined(_WIN32)
    const size_t len = SDL_strlen(path);
    return (len >= 3U && path[0] >= 'A' && path[0] <= 'Z' && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
           (len >= 2U && path[0] == '\\' && path[1] == '\\');
#else
    return path[0] == '/';
#endif
}

static char editor_path_compare_char(char c)
{
    return c == '\\' ? '/' : c;
}

static bool editor_path_text_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return false;
    while (*a != '\0' && *b != '\0')
    {
        if (editor_path_compare_char(*a) != editor_path_compare_char(*b))
            return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static char *editor_path_normalized_relative_copy(const char *path)
{
    if (path == NULL)
        return NULL;
    while (path[0] == '/' || path[0] == '\\')
        ++path;
    size_t len = SDL_strlen(path);
    while (len > 0U && (path[len - 1U] == '/' || path[len - 1U] == '\\'))
        --len;
    if (len == 0U)
        return NULL;
    char *copy = (char *)SDL_malloc(len + 1U);
    if (copy == NULL)
        return NULL;
    for (size_t i = 0; i < len; ++i)
    {
        const char c = path[i];
        copy[i] = c == '\\' ? '/' : c;
    }
    copy[len] = '\0';
    return copy;
}

static const char *editor_named_target_from_action_state(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                         const char *direct_key, const char *state_key)
{
    const char *target_from_state = json_string(action, state_key, NULL);
    if (target_from_state != NULL && target_from_state[0] != '\0' && runtime != NULL && runtime->scene_state != NULL)
        return slayer3d_properties_get_string(runtime->scene_state, target_from_state, NULL);
    return json_string(action, direct_key, NULL);
}

static bool slayer3d_game_data_editor_actor_update_action_targets_locked_actor(slayer3d_game_data_runtime *runtime,
                                                                               yyjson_val *action)
{
    const char *target_type = json_string(action, "target_type", "selection");
    if (SDL_strcmp(target_type, "selection") == 0)
        return slayer3d_game_data_editor_selection_contains_locked_objects(runtime);
    const char *target = editor_named_target_from_action_state(runtime, action, "target", "target_from_state");
    return slayer3d_game_data_editor_actor_locked(runtime, target);
}

static bool slayer3d_game_data_editor_property_action_targets_locked_object(slayer3d_game_data_runtime *runtime,
                                                                            yyjson_val *action)
{
    const char *target_type = json_string(action, "target_type", "selection");
    if (SDL_strcmp(target_type, "selection") == 0)
        return slayer3d_game_data_editor_selection_contains_locked_objects(runtime);
    if (SDL_strcmp(target_type, "editor_actor") != 0)
        return false;
    const char *target = editor_named_target_from_action_state(runtime, action, "target", "target_from_state");
    return slayer3d_game_data_editor_actor_locked(runtime, target);
}

static bool editor_path_relative_to_base(const char *path, const char *base, const char **relative)
{
    if (relative != NULL)
        *relative = NULL;
    if (path == NULL || base == NULL || path[0] == '\0' || base[0] == '\0')
        return false;

    size_t base_len = SDL_strlen(base);
    while (base_len > 0U && (base[base_len - 1U] == '/' || base[base_len - 1U] == '\\'))
        --base_len;
    if (base_len == 0U)
        return false;
    for (size_t i = 0; i < base_len; ++i)
    {
        if (path[i] == '\0' || editor_path_compare_char(path[i]) != editor_path_compare_char(base[i]))
            return false;
    }
    if (path[base_len] == '\0')
        return false;
    if (path[base_len] != '/' && path[base_len] != '\\')
        return false;
    if (relative != NULL)
        *relative = path + base_len + 1U;
    return true;
}

static bool editor_asset_uri_matches_relative_path(const char *asset_uri, const char *relative_path)
{
    static const char prefix[] = "asset://";
    if (asset_uri == NULL || relative_path == NULL || relative_path[0] == '\0')
        return false;
    if (SDL_strncmp(asset_uri, prefix, sizeof(prefix) - 1U) != 0)
        return false;
    return editor_path_text_equal(asset_uri + sizeof(prefix) - 1U, relative_path);
}

static char *editor_asset_uri_from_relative_path(const char *relative_path)
{
    static const char prefix[] = "asset://";
    if (relative_path == NULL || relative_path[0] == '\0')
        return NULL;
    if (editor_path_asset_uri(relative_path))
        return SDL_strdup(relative_path);

    while (relative_path[0] == '/' || relative_path[0] == '\\')
        ++relative_path;
    if (relative_path[0] == '\0')
        return NULL;

    const size_t prefix_len = sizeof(prefix) - 1U;
    const size_t relative_len = SDL_strlen(relative_path);
    char *uri = (char *)SDL_malloc(prefix_len + relative_len + 1U);
    if (uri == NULL)
        return NULL;
    SDL_memcpy(uri, prefix, prefix_len);
    for (size_t i = 0; i < relative_len; ++i)
    {
        const char c = relative_path[i];
        uri[prefix_len + i] = c == '\\' ? '/' : c;
    }
    uri[prefix_len + relative_len] = '\0';
    return uri;
}

static char *editor_relative_directory_for_source_path(slayer3d_game_data_runtime *runtime, const char *input_path,
                                                       const char *absolute_path)
{
    if (input_path != NULL && input_path[0] != '\0' && !editor_path_asset_uri(input_path) &&
        !editor_path_absolute(input_path))
    {
        char *relative = editor_path_normalized_relative_copy(input_path);
        if (relative != NULL)
            return relative;
    }

    const char *project_dir = runtime != NULL && runtime->scene_state != NULL
                                  ? slayer3d_properties_get_string(runtime->scene_state, "editor.project.dir", "")
                                  : "";
    char *absolute_project_dir = NULL;
    if (project_dir != NULL && project_dir[0] != '\0')
    {
        absolute_project_dir = editor_path_absolute(project_dir) ? SDL_strdup(project_dir)
                                                                 : editor_path_make_absolute_from_cwd(project_dir);
    }

    const char *project_relative = NULL;
    if (absolute_project_dir != NULL &&
        editor_path_relative_to_base(absolute_path, absolute_project_dir, &project_relative))
    {
        char *relative = editor_path_normalized_relative_copy(project_relative);
        SDL_free(absolute_project_dir);
        if (relative != NULL)
            return relative;
    }
    SDL_free(absolute_project_dir);

    char *trimmed = editor_path_normalized_relative_copy(absolute_path);
    if (trimmed == NULL)
        return NULL;
    char *last_separator = NULL;
    for (char *p = trimmed; *p != '\0'; ++p)
    {
        if (*p == '/')
            last_separator = p;
    }
    char *basename = SDL_strdup(last_separator != NULL ? last_separator + 1 : trimmed);
    SDL_free(trimmed);
    return basename;
}

static char *editor_resolve_directory(slayer3d_game_data_runtime *runtime, const char *directory)
{
    if (directory == NULL || directory[0] == '\0')
        return NULL;
    if (editor_path_asset_uri(directory))
        return SDL_strdup(directory);
    if (editor_path_absolute(directory))
        return SDL_strdup(directory);
    const char *base_dir = runtime != NULL && runtime->file_base_dir != NULL
                               ? runtime->file_base_dir
                               : (runtime != NULL ? runtime->base_dir : NULL);
    return editor_path_join(base_dir, directory);
}

static const char *editor_basename(const char *path)
{
    const char *base = path;
    if (path == NULL)
        return "";
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return base;
}

static void editor_texture_slug_from_filename(const char *filename, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    buffer[0] = '\0';
    const char *base = editor_basename(filename);
    size_t offset = 0U;
    for (const char *p = base; p != NULL && *p != '\0' && offset + 1U < buffer_size; ++p)
    {
        if (*p == '.')
            break;
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            buffer[offset++] = c;
        else if (offset > 0U && buffer[offset - 1U] != '_')
            buffer[offset++] = '_';
    }
    while (offset > 0U && buffer[offset - 1U] == '_')
        --offset;
    if (offset == 0U)
        SDL_strlcpy(buffer, "texture", buffer_size);
    else
        buffer[offset] = '\0';
}

static void editor_texture_slug_from_path(const char *path, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    buffer[0] = '\0';
    size_t offset = 0U;
    for (const char *p = path; p != NULL && *p != '\0' && offset + 1U < buffer_size; ++p)
    {
        if (*p == '.')
            break;
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            buffer[offset++] = c;
        else if (offset > 0U && buffer[offset - 1U] != '_')
            buffer[offset++] = '_';
    }
    while (offset > 0U && buffer[offset - 1U] == '_')
        --offset;
    if (offset == 0U)
        SDL_strlcpy(buffer, "texture", buffer_size);
    else
        buffer[offset] = '\0';
}

static void editor_texture_label_from_filename(const char *filename, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    buffer[0] = '\0';
    const char *base = editor_basename(filename);
    size_t offset = 0U;
    bool word_start = true;
    for (const char *p = base; p != NULL && *p != '\0' && offset + 1U < buffer_size; ++p)
    {
        if (*p == '.')
            break;
        char c = *p;
        if (c == '_' || c == '-')
        {
            if (offset > 0U && buffer[offset - 1U] != ' ')
                buffer[offset++] = ' ';
            word_start = true;
            continue;
        }
        if (word_start && c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        buffer[offset++] = c;
        word_start = false;
    }
    while (offset > 0U && buffer[offset - 1U] == ' ')
        --offset;
    if (offset == 0U)
        SDL_strlcpy(buffer, "Texture", buffer_size);
    else
        buffer[offset] = '\0';
}

typedef struct editor_texture_scan_entry
{
    char *filename;
    char *path;
    char *relative_path;
    char *directory;
    char *directory_label;
    char *search_key;
    char material[128];
    char label[96];
    char slug[96];
    int sort_order;
} editor_texture_scan_entry;

typedef struct editor_texture_scan_list
{
    editor_texture_scan_entry *entries;
    int count;
    int capacity;
} editor_texture_scan_list;

static void editor_texture_scan_list_free(editor_texture_scan_list *list)
{
    if (list == NULL)
        return;
    for (int i = 0; i < list->count; ++i)
    {
        SDL_free(list->entries[i].filename);
        SDL_free(list->entries[i].path);
        SDL_free(list->entries[i].relative_path);
        SDL_free(list->entries[i].directory);
        SDL_free(list->entries[i].directory_label);
        SDL_free(list->entries[i].search_key);
    }
    SDL_free(list->entries);
    SDL_zero(*list);
}

static int SDLCALL editor_texture_scan_entry_compare(const void *a, const void *b)
{
    const editor_texture_scan_entry *ea = (const editor_texture_scan_entry *)a;
    const editor_texture_scan_entry *eb = (const editor_texture_scan_entry *)b;
    return SDL_strcasecmp(ea->relative_path != NULL ? ea->relative_path : "",
                          eb->relative_path != NULL ? eb->relative_path : "");
}

static int SDLCALL editor_texture_scan_entry_order_compare(const void *a, const void *b)
{
    const editor_texture_scan_entry *ea = (const editor_texture_scan_entry *)a;
    const editor_texture_scan_entry *eb = (const editor_texture_scan_entry *)b;
    if (ea->sort_order < eb->sort_order)
        return -1;
    if (ea->sort_order > eb->sort_order)
        return 1;
    return editor_texture_scan_entry_compare(a, b);
}

static char *editor_texture_relative_parent(const char *relative_file)
{
    if (relative_file == NULL || relative_file[0] == '\0')
        return SDL_strdup("");
    const char *last = NULL;
    for (const char *p = relative_file; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p;
    }
    if (last == NULL)
        return SDL_strdup("");
    const size_t length = (size_t)(last - relative_file);
    char *parent = (char *)SDL_malloc(length + 1U);
    if (parent == NULL)
        return NULL;
    SDL_memcpy(parent, relative_file, length);
    parent[length] = '\0';
    return parent;
}

static char *editor_texture_directory_label(const char *directory)
{
    if (directory == NULL || directory[0] == '\0')
        return SDL_strdup("Root");
    char label[96];
    editor_texture_label_from_filename(editor_basename(directory), label, sizeof(label));
    return SDL_strdup(label);
}

static char *editor_texture_search_key(const char *relative_file, const char *label, const char *directory)
{
    const char *safe_relative = relative_file != NULL ? relative_file : "";
    const char *safe_label = label != NULL ? label : "";
    const char *safe_directory = directory != NULL ? directory : "";
    const size_t relative_len = SDL_strlen(safe_relative);
    const size_t label_len = SDL_strlen(safe_label);
    const size_t directory_len = SDL_strlen(safe_directory);
    char *key = (char *)SDL_malloc(relative_len + label_len + directory_len + 3U);
    if (key == NULL)
        return NULL;
    SDL_snprintf(key, relative_len + label_len + directory_len + 3U, "%s %s %s", safe_relative, safe_label,
                 safe_directory);
    return key;
}

static bool editor_ascii_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool editor_texture_fuzzy_token_matches(const char *haystack, const char *token, size_t token_len)
{
    if (token_len == 0U)
        return true;
    if (haystack == NULL || haystack[0] == '\0')
        return false;

    size_t token_index = 0U;
    for (const char *p = haystack; *p != '\0' && token_index < token_len; ++p)
    {
        char hc = *p;
        char tc = token[token_index];
        if (hc >= 'A' && hc <= 'Z')
            hc = (char)(hc - 'A' + 'a');
        if (tc >= 'A' && tc <= 'Z')
            tc = (char)(tc - 'A' + 'a');
        if (hc == tc)
            ++token_index;
    }
    return token_index == token_len;
}

static bool editor_texture_fuzzy_matches(const editor_texture_scan_entry *entry, const char *query)
{
    if (entry == NULL)
        return false;
    if (query == NULL || query[0] == '\0')
        return true;

    const char *token = query;
    while (*token != '\0')
    {
        while (editor_ascii_is_space(*token))
            ++token;
        const char *end = token;
        while (*end != '\0' && !editor_ascii_is_space(*end))
            ++end;
        const size_t token_len = (size_t)(end - token);
        if (token_len > 0U && !editor_texture_fuzzy_token_matches(entry->search_key, token, token_len))
            return false;
        token = end;
    }
    return true;
}

static bool editor_texture_scan_list_append_source(editor_texture_scan_list *list, const char *source_path,
                                                   const char *relative_directory, const char *relative_file)
{
    const char *filename = editor_basename(relative_file);
    if (list == NULL || source_path == NULL || relative_file == NULL || !editor_texture_extension_supported(filename))
        return true;
    if (list->count >= list->capacity)
    {
        const int next_capacity = list->capacity > 0 ? list->capacity * 2 : 16;
        editor_texture_scan_entry *next =
            (editor_texture_scan_entry *)SDL_realloc(list->entries, (size_t)next_capacity * sizeof(*next));
        if (next == NULL)
            return false;
        SDL_memset(next + list->capacity, 0, (size_t)(next_capacity - list->capacity) * sizeof(*next));
        list->entries = next;
        list->capacity = next_capacity;
    }
    editor_texture_scan_entry *entry = &list->entries[list->count];
    SDL_zero(*entry);
    entry->filename = SDL_strdup(filename);
    entry->path = SDL_strdup(source_path);
    entry->relative_path = editor_path_join(relative_directory != NULL ? relative_directory : "", relative_file);
    entry->directory = editor_texture_relative_parent(relative_file);
    entry->directory_label = editor_texture_directory_label(entry->directory);
    editor_texture_slug_from_path(relative_file, entry->slug, sizeof(entry->slug));
    editor_texture_label_from_filename(filename, entry->label, sizeof(entry->label));
    entry->search_key = editor_texture_search_key(relative_file, entry->label, entry->directory);
    entry->sort_order = list->count;
    if (entry->filename == NULL || entry->path == NULL || entry->relative_path == NULL || entry->directory == NULL ||
        entry->directory_label == NULL || entry->search_key == NULL)
    {
        SDL_free(entry->filename);
        SDL_free(entry->path);
        SDL_free(entry->relative_path);
        SDL_free(entry->directory);
        SDL_free(entry->directory_label);
        SDL_free(entry->search_key);
        SDL_zero(*entry);
        return false;
    }
    ++list->count;
    return true;
}

static bool editor_texture_scan_list_append(editor_texture_scan_list *list, const char *physical_directory,
                                            const char *relative_directory, const char *relative_file)
{
    if (physical_directory == NULL || relative_file == NULL)
        return true;
    char *source_path = editor_path_join(physical_directory, editor_basename(relative_file));
    const bool ok = source_path != NULL &&
                    editor_texture_scan_list_append_source(list, source_path, relative_directory, relative_file);
    SDL_free(source_path);
    return ok;
}

typedef struct editor_texture_enumerate_context
{
    editor_texture_scan_list *list;
    const char *relative_directory;
    const char *relative_subdirectory;
    int depth;
    bool ok;
} editor_texture_enumerate_context;

static bool editor_scan_texture_directory_recursive(const char *directory, const char *relative_directory,
                                                    const char *relative_subdirectory,
                                                    editor_texture_scan_list *out_list, int depth);

static SDL_EnumerationResult SDLCALL editor_texture_enumerate_directory_entry(void *userdata, const char *dirname,
                                                                              const char *fname)
{
    editor_texture_enumerate_context *ctx = (editor_texture_enumerate_context *)userdata;
    if (ctx == NULL || ctx->list == NULL)
        return SDL_ENUM_FAILURE;
    char *path = editor_path_join(dirname, fname);
    SDL_PathInfo file_info;
    SDL_zero(file_info);
    const bool has_info = path != NULL && SDL_GetPathInfo(path, &file_info);
    if (!has_info)
    {
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }

    if (file_info.type == SDL_PATHTYPE_DIRECTORY)
    {
        char *child_relative = editor_path_join(ctx->relative_subdirectory, fname);
        if (child_relative == NULL || !editor_scan_texture_directory_recursive(
                                          path, ctx->relative_directory, child_relative, ctx->list, ctx->depth + 1))
        {
            SDL_free(child_relative);
            SDL_free(path);
            ctx->ok = false;
            return SDL_ENUM_FAILURE;
        }
        SDL_free(child_relative);
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }

    if (file_info.type != SDL_PATHTYPE_FILE)
    {
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }

    char *relative_file = editor_path_join(ctx->relative_subdirectory, fname);
    if (relative_file == NULL ||
        !editor_texture_scan_list_append(ctx->list, dirname, ctx->relative_directory, relative_file))
    {
        SDL_free(relative_file);
        SDL_free(path);
        ctx->ok = false;
        return SDL_ENUM_FAILURE;
    }
    SDL_free(relative_file);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static bool editor_scan_texture_directory_recursive(const char *directory, const char *relative_directory,
                                                    const char *relative_subdirectory,
                                                    editor_texture_scan_list *out_list, int depth)
{
    if (directory == NULL || directory[0] == '\0' || out_list == NULL || depth > 32)
        return false;
    editor_texture_enumerate_context enumerate_ctx = {
        out_list, relative_directory, relative_subdirectory != NULL ? relative_subdirectory : "", depth, true};
    return SDL_EnumerateDirectory(directory, editor_texture_enumerate_directory_entry, &enumerate_ctx) &&
           enumerate_ctx.ok;
}

static bool editor_scan_texture_directory(const char *directory, const char *relative_directory,
                                          editor_texture_scan_list *out_list)
{
    if (out_list != NULL)
        SDL_zero(*out_list);
    if (directory == NULL || directory[0] == '\0' || out_list == NULL)
        return false;

    SDL_PathInfo info;
    SDL_zero(info);
    if (!SDL_GetPathInfo(directory, &info) || info.type != SDL_PATHTYPE_DIRECTORY)
        return false;

    const bool ok = editor_scan_texture_directory_recursive(directory, relative_directory, "", out_list, 0);
    if (ok && out_list->count > 1)
        SDL_qsort(out_list->entries, (size_t)out_list->count, sizeof(out_list->entries[0]),
                  editor_texture_scan_entry_compare);
    if (!ok)
        editor_texture_scan_list_free(out_list);
    return ok;
}

typedef struct editor_asset_texture_enumerate_context
{
    slayer3d_game_data_runtime *runtime;
    editor_texture_scan_list *list;
    const char *relative_directory;
    const char *relative_subdirectory;
    int depth;
    bool ok;
} editor_asset_texture_enumerate_context;

static bool editor_scan_texture_asset_directory_recursive(slayer3d_game_data_runtime *runtime,
                                                          const char *asset_directory, const char *relative_directory,
                                                          const char *relative_subdirectory,
                                                          editor_texture_scan_list *out_list, int depth);

static slayer3d_asset_enumeration_result editor_asset_texture_enumerate_entry(void *userdata, const char *directory,
                                                                              const char *name,
                                                                              slayer3d_asset_entry_type type)
{
    editor_asset_texture_enumerate_context *ctx = (editor_asset_texture_enumerate_context *)userdata;
    if (ctx == NULL || ctx->runtime == NULL || ctx->list == NULL)
        return SLAYER3D_ASSET_ENUM_FAILURE;

    char *child_asset_path = editor_asset_path_join(directory, name);
    if (child_asset_path == NULL)
    {
        ctx->ok = false;
        return SLAYER3D_ASSET_ENUM_FAILURE;
    }

    if (type == SLAYER3D_ASSET_ENTRY_DIRECTORY)
    {
        char *child_relative = editor_asset_path_join(ctx->relative_subdirectory, name);
        const bool ok = child_relative != NULL && editor_scan_texture_asset_directory_recursive(
                                                      ctx->runtime, child_asset_path, ctx->relative_directory,
                                                      child_relative, ctx->list, ctx->depth + 1);
        SDL_free(child_relative);
        SDL_free(child_asset_path);
        if (!ok)
        {
            ctx->ok = false;
            return SLAYER3D_ASSET_ENUM_FAILURE;
        }
        return SLAYER3D_ASSET_ENUM_CONTINUE;
    }

    if (type == SLAYER3D_ASSET_ENTRY_FILE)
    {
        char *relative_file = editor_asset_path_join(ctx->relative_subdirectory, name);
        const bool ok =
            relative_file != NULL &&
            editor_texture_scan_list_append_source(ctx->list, child_asset_path, ctx->relative_directory, relative_file);
        SDL_free(relative_file);
        SDL_free(child_asset_path);
        if (!ok)
        {
            ctx->ok = false;
            return SLAYER3D_ASSET_ENUM_FAILURE;
        }
        return SLAYER3D_ASSET_ENUM_CONTINUE;
    }

    SDL_free(child_asset_path);
    return SLAYER3D_ASSET_ENUM_CONTINUE;
}

static bool editor_scan_texture_asset_directory_recursive(slayer3d_game_data_runtime *runtime,
                                                          const char *asset_directory, const char *relative_directory,
                                                          const char *relative_subdirectory,
                                                          editor_texture_scan_list *out_list, int depth)
{
    if (runtime == NULL || runtime->assets == NULL || asset_directory == NULL || asset_directory[0] == '\0' ||
        out_list == NULL || depth > 32)
    {
        return false;
    }
    editor_asset_texture_enumerate_context enumerate_ctx = {
        runtime, out_list, relative_directory, relative_subdirectory != NULL ? relative_subdirectory : "", depth, true};
    char error[256];
    error[0] = '\0';
    return slayer3d_asset_resolver_enumerate(runtime->assets, asset_directory, editor_asset_texture_enumerate_entry,
                                             &enumerate_ctx, error, (int)sizeof(error)) &&
           enumerate_ctx.ok;
}

static bool editor_scan_texture_source(slayer3d_game_data_runtime *runtime, const char *directory,
                                       const char *relative_directory, editor_texture_scan_list *out_list)
{
    if (editor_path_asset_uri(directory))
    {
        if (out_list != NULL)
            SDL_zero(*out_list);
        const bool ok =
            editor_scan_texture_asset_directory_recursive(runtime, directory, relative_directory, "", out_list, 0);
        if (ok && out_list->count > 1)
            SDL_qsort(out_list->entries, (size_t)out_list->count, sizeof(out_list->entries[0]),
                      editor_texture_scan_entry_compare);
        if (!ok)
            editor_texture_scan_list_free(out_list);
        return ok;
    }
    return editor_scan_texture_directory(directory, relative_directory, out_list);
}

static int editor_brush_material_index_by_name_or_texture(const brush_world_runtime *world_runtime, const char *name,
                                                          const char *texture_path, const char *relative_texture_path)
{
    const slayer3d_game_data_brush_world *world = world_runtime != NULL ? &world_runtime->desc : NULL;
    if (world == NULL)
        return -1;
    for (int i = 0; i < world->material_count; ++i)
    {
        const slayer3d_game_data_brush_material *material = &world->materials[i];
        if (name != NULL && name[0] != '\0' && material->name != NULL && SDL_strcmp(material->name, name) == 0)
            return i;
        if (texture_path != NULL && texture_path[0] != '\0' && material->texture != NULL)
        {
            if (editor_path_text_equal(material->texture, texture_path))
                return i;
            if (relative_texture_path != NULL && relative_texture_path[0] != '\0' &&
                editor_asset_uri_matches_relative_path(material->texture, relative_texture_path))
            {
                return i;
            }
        }
    }
    return -1;
}

static const char *editor_texture_material_reference(const editor_texture_scan_entry *entry, char **owned)
{
    if (owned != NULL)
        *owned = NULL;
    if (entry == NULL)
        return NULL;
    if (entry->relative_path != NULL && entry->relative_path[0] != '\0' &&
        !editor_path_absolute(entry->relative_path) && owned != NULL)
    {
        *owned = editor_asset_uri_from_relative_path(entry->relative_path);
        if (*owned != NULL)
            return *owned;
    }
    return entry->path;
}

static bool editor_append_brush_material(brush_world_runtime *world_runtime, const char *material_name,
                                         const char *texture_path)
{
    if (world_runtime == NULL || material_name == NULL || material_name[0] == '\0' || texture_path == NULL ||
        texture_path[0] == '\0')
    {
        return false;
    }

    slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (editor_brush_material_index_by_name_or_texture(world_runtime, material_name, NULL, NULL) >= 0)
        return true;

    const int next_count = world->material_count + 1;
    slayer3d_game_data_brush_material *materials = (slayer3d_game_data_brush_material *)SDL_realloc(
        (void *)world->materials, (size_t)next_count * sizeof(*materials));
    if (materials == NULL)
        return false;
    world->materials = materials;

    slayer3d_game_data_brush_material *material = &materials[world->material_count];
    SDL_zero(*material);
    material->name = SDL_strdup(material_name);
    material->texture = SDL_strdup(texture_path);
    material->albedo = slayer3d_vec4_make(1.0f, 1.0f, 1.0f, 1.0f);
    material->metallic = 0.0f;
    material->roughness = 1.0f;
    material->emissive = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    material->tex_scale = 1.0f;
    if (material->name == NULL || material->texture == NULL)
    {
        SDL_free((void *)material->name);
        SDL_free((void *)material->texture);
        SDL_zero(*material);
        return false;
    }
    world->material_count = next_count;
    return true;
}

static bool editor_texture_scan_contains_path(const editor_texture_scan_list *list, const char *path)
{
    if (list == NULL || path == NULL || path[0] == '\0')
        return false;
    for (int i = 0; i < list->count; ++i)
    {
        if (list->entries[i].path != NULL && editor_path_text_equal(list->entries[i].path, path))
            return true;
        if (list->entries[i].relative_path != NULL && editor_path_text_equal(list->entries[i].relative_path, path))
            return true;
        if (list->entries[i].relative_path != NULL &&
            editor_asset_uri_matches_relative_path(path, list->entries[i].relative_path))
            return true;
    }
    return false;
}

static int editor_invalidate_missing_texture_materials(brush_world_runtime *world_runtime,
                                                       const editor_texture_scan_list *list,
                                                       const char *material_prefix)
{
    slayer3d_game_data_brush_world *world = world_runtime != NULL ? &world_runtime->desc : NULL;
    if (world == NULL || material_prefix == NULL)
        return 0;
    int invalidated = 0;
    const size_t prefix_len = SDL_strlen(material_prefix);
    for (int i = 0; i < world->material_count; ++i)
    {
        slayer3d_game_data_brush_material *material = (slayer3d_game_data_brush_material *)&world->materials[i];
        if (material->name == NULL || SDL_strncmp(material->name, material_prefix, prefix_len) != 0 ||
            material->texture == NULL || material->texture[0] == '\0' ||
            editor_texture_scan_contains_path(list, material->texture))
        {
            continue;
        }
        SDL_free((void *)material->texture);
        material->texture = NULL;
        ++invalidated;
    }
    return invalidated;
}

static bool editor_texture_collection_publish_row(slayer3d_game_data_runtime *runtime, const char *collection,
                                                  int row_index, const editor_texture_scan_entry *entry)
{
    if (runtime == NULL || collection == NULL || entry == NULL)
        return false;
    return slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "id", entry->slug) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "label", entry->label) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "filename",
                                                            entry->filename) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "path", entry->path) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "relative_path",
                                                            entry->relative_path) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "directory",
                                                            entry->directory) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "directory_label",
                                                            entry->directory_label) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "search_key",
                                                            entry->search_key) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "material",
                                                            entry->material);
}

static bool editor_texture_collection_entry_from_row(
    const runtime_collection *collection, int row_index, editor_texture_scan_entry *entry, char *filename,
    size_t filename_size, char *path, size_t path_size, char *relative_path, size_t relative_path_size, char *directory,
    size_t directory_size, char *directory_label, size_t directory_label_size, char *search_key, size_t search_key_size)
{
    if (collection == NULL || entry == NULL || filename == NULL || path == NULL || relative_path == NULL ||
        directory == NULL || directory_label == NULL || search_key == NULL)
    {
        return false;
    }
    SDL_zero(*entry);
    if (!runtime_collection_field_to_string(collection, row_index, "id", entry->slug, sizeof(entry->slug)) ||
        !runtime_collection_field_to_string(collection, row_index, "label", entry->label, sizeof(entry->label)) ||
        !runtime_collection_field_to_string(collection, row_index, "filename", filename, filename_size) ||
        !runtime_collection_field_to_string(collection, row_index, "path", path, path_size) ||
        !runtime_collection_field_to_string(collection, row_index, "relative_path", relative_path,
                                            relative_path_size) ||
        !runtime_collection_field_to_string(collection, row_index, "directory", directory, directory_size) ||
        !runtime_collection_field_to_string(collection, row_index, "directory_label", directory_label,
                                            directory_label_size) ||
        !runtime_collection_field_to_string(collection, row_index, "search_key", search_key, search_key_size) ||
        !runtime_collection_field_to_string(collection, row_index, "material", entry->material,
                                            sizeof(entry->material)) ||
        entry->material[0] == '\0')
    {
        return false;
    }
    entry->filename = filename;
    entry->path = path;
    entry->relative_path = relative_path;
    entry->directory = directory;
    entry->directory_label = directory_label;
    entry->search_key = search_key;
    return true;
}

static bool editor_texture_directory_collapsed(const slayer3d_properties *scene_state,
                                               const editor_texture_scan_entry *entry)
{
    if (scene_state == NULL || entry == NULL || entry->directory == NULL || entry->directory[0] == '\0')
        return false;
    char directory_slug[96];
    char key[160];
    editor_texture_slug_from_path(entry->directory, directory_slug, sizeof(directory_slug));
    SDL_snprintf(key, sizeof(key), "editor.texture.directory.%s.collapsed", directory_slug);
    return slayer3d_properties_get_bool(scene_state, key, false);
}

static void editor_texture_clear_slots(slayer3d_properties *scene_state, int slot_count)
{
    for (int i = 0; scene_state != NULL && i < slot_count; ++i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.label", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.material", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.path", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.available", i);
        slayer3d_properties_set_bool(scene_state, key, false);
        SDL_snprintf(key, sizeof(key), "asset_warmup.ui_image.image.editor_shell.texture.slot_%d.pending", i);
        slayer3d_properties_set_bool(scene_state, key, false);
        SDL_snprintf(key, sizeof(key), "asset_warmup.ui_image.image.editor_shell.texture.slot_%d.failed", i);
        slayer3d_properties_set_bool(scene_state, key, false);
    }
}

static void editor_texture_set_empty_state(slayer3d_properties *scene_state, bool visible, const char *message)
{
    if (scene_state == NULL)
        return;
    slayer3d_properties_set_bool(scene_state, "editor.texture.empty.visible", visible);
    slayer3d_properties_set_string(scene_state, "editor.texture.empty.message", message != NULL ? message : "");
}

static void editor_texture_publish_slot(slayer3d_properties *scene_state, int slot_index,
                                        const editor_texture_scan_entry *entry)
{
    if (scene_state == NULL || entry == NULL)
        return;
    char key[128];
    SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.label", slot_index);
    slayer3d_properties_set_string(scene_state, key, entry->label);
    SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.material", slot_index);
    slayer3d_properties_set_string(scene_state, key, entry->material);
    SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.path", slot_index);
    slayer3d_properties_set_string(scene_state, key, entry->path);
    SDL_snprintf(key, sizeof(key), "editor.texture.slot.%d.available", slot_index);
    slayer3d_properties_set_bool(scene_state, key, true);
    SDL_snprintf(key, sizeof(key), "asset_warmup.ui_image.image.editor_shell.texture.slot_%d.pending", slot_index);
    slayer3d_properties_set_bool(scene_state, key, false);
    SDL_snprintf(key, sizeof(key), "asset_warmup.ui_image.image.editor_shell.texture.slot_%d.failed", slot_index);
    slayer3d_properties_set_bool(scene_state, key, false);
}

static int editor_texture_publish_filtered_catalog(slayer3d_game_data_runtime *runtime, const char *catalog_collection,
                                                   const char *collection, int slot_count, const char *scroll_index_key,
                                                   const char *scroll_y_key, const char *search_query)
{
    if (runtime == NULL || runtime->scene_state == NULL || catalog_collection == NULL || collection == NULL)
        return 0;

    editor_texture_clear_slots(runtime->scene_state, slot_count);
    slayer3d_game_data_runtime_collection_clear(runtime, collection);

    const runtime_collection *catalog = find_runtime_collection_const(runtime, catalog_collection);
    const int catalog_count = catalog != NULL ? catalog->row_count : 0;
    const char *current_material =
        slayer3d_properties_get_string(runtime->scene_state, "editor.palette.material.cursor", "");
    int published_count = 0;
    int selected_index = -1;
    for (int i = 0; i < catalog_count; ++i)
    {
        editor_texture_scan_entry entry;
        char filename[256];
        char path[1024];
        char relative_path[1024];
        char directory[256];
        char directory_label[128];
        char search_key[1536];
        if (!editor_texture_collection_entry_from_row(catalog, i, &entry, filename, sizeof(filename), path,
                                                      sizeof(path), relative_path, sizeof(relative_path), directory,
                                                      sizeof(directory), directory_label, sizeof(directory_label),
                                                      search_key, sizeof(search_key)))
        {
            continue;
        }
        if (!editor_texture_fuzzy_matches(&entry, search_query))
            continue;
        if ((search_query == NULL || search_query[0] == '\0') &&
            editor_texture_directory_collapsed(runtime->scene_state, &entry))
        {
            continue;
        }
        (void)editor_texture_collection_publish_row(runtime, collection, published_count, &entry);
        if (current_material != NULL && current_material[0] != '\0' &&
            SDL_strcmp(current_material, entry.material) == 0)
        {
            selected_index = published_count;
        }
        ++published_count;
    }

    const int max_scroll_index = SDL_max(0, published_count - slot_count);
    int scroll_index = slayer3d_properties_get_int(runtime->scene_state, scroll_index_key, 0);
    scroll_index = SDL_clamp(scroll_index, 0, max_scroll_index);
    slayer3d_properties_set_int(runtime->scene_state, scroll_index_key, scroll_index);
    const float thumb_travel = 246.0f;
    const float thumb_offset =
        max_scroll_index > 0 ? -((float)scroll_index / (float)max_scroll_index) * thumb_travel : 0.0f;
    slayer3d_properties_set_float(runtime->scene_state, scroll_y_key, thumb_offset);

    const runtime_collection *filtered = find_runtime_collection_const(runtime, collection);
    for (int slot_index = 0; slot_index < slot_count; ++slot_index)
    {
        editor_texture_scan_entry entry;
        char filename[256];
        char path[1024];
        char relative_path[1024];
        char directory[256];
        char directory_label[128];
        char search_key[1536];
        const int collection_index = scroll_index + slot_index;
        if (collection_index >= published_count ||
            !editor_texture_collection_entry_from_row(filtered, collection_index, &entry, filename, sizeof(filename),
                                                      path, sizeof(path), relative_path, sizeof(relative_path),
                                                      directory, sizeof(directory), directory_label,
                                                      sizeof(directory_label), search_key, sizeof(search_key)))
        {
            continue;
        }
        editor_texture_publish_slot(runtime->scene_state, slot_index, &entry);
    }

    if (published_count > 0)
    {
        if (selected_index < 0)
        {
            char first_material[128];
            if (runtime_collection_field_to_string(filtered, 0, "material", first_material, sizeof(first_material)) &&
                first_material[0] != '\0')
            {
                slayer3d_properties_set_string(runtime->scene_state, "editor.palette.material.cursor", first_material);
                slayer3d_properties_set_string(runtime->scene_state, "editor.texture.material", first_material);
                selected_index = 0;
            }
        }
        const int selected_slot = selected_index >= scroll_index && selected_index < scroll_index + slot_count
                                      ? selected_index - scroll_index
                                      : -1;
        slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_index", selected_index);
        slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_slot", selected_slot);
    }
    else
    {
        slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_index", -1);
        slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_slot", -1);
    }
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.count", published_count);
    const bool has_query = search_query != NULL && search_query[0] != '\0';
    editor_texture_set_empty_state(runtime->scene_state, published_count == 0,
                                   has_query ? "No textures match search" : "No textures found");
    return published_count;
}

static void editor_asset_source_set_diagnostic(slayer3d_properties *state, const char *source_name, const char *status,
                                               bool available, bool empty)
{
    if (state == NULL || source_name == NULL || source_name[0] == '\0')
        return;

    char key[160];
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.status", source_name);
    slayer3d_properties_set_string(state, key, status != NULL ? status : "");
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.diagnostic", source_name);
    slayer3d_properties_set_string(state, key, status != NULL ? status : "");
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.available", source_name);
    slayer3d_properties_set_bool(state, key, available);
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.unavailable", source_name);
    slayer3d_properties_set_bool(state, key, !available);
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.empty", source_name);
    slayer3d_properties_set_bool(state, key, available && empty);
}

static void editor_actor_set_model_empty_state(slayer3d_properties *state, int scanned_model_count,
                                               int filtered_model_count, bool source_scanned, bool search_active)
{
    if (state == NULL)
        return;

    slayer3d_properties_set_int(state, "editor.actor.models.count", source_scanned ? scanned_model_count : 0);
    slayer3d_properties_set_int(state, "editor.actor.models.filtered_count", source_scanned ? filtered_model_count : 0);
    const bool show_empty_state =
        !source_scanned || scanned_model_count == 0 || (search_active && filtered_model_count == 0);
    slayer3d_properties_set_bool(state, "editor.actor.models.empty.visible", show_empty_state);
    if (!source_scanned)
        slayer3d_properties_set_string(state, "editor.actor.models.empty.message", "Model directory unavailable");
    else if (scanned_model_count == 0)
        slayer3d_properties_set_string(state, "editor.actor.models.empty.message", "No project models found");
    else if (search_active && filtered_model_count == 0)
        slayer3d_properties_set_string(state, "editor.actor.models.empty.message", "No project models match search");
    else
        slayer3d_properties_set_string(state, "editor.actor.models.empty.message", "");
}

static bool execute_editor_texture_scan_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    yyjson_val *outputs = obj_get(action, "outputs");
    const char *directory_key = json_string(action, "directory_key", "editor.asset_source.textures.path");
    const char *relative_directory_key =
        json_string(action, "relative_directory_key", "editor.asset_source.textures.relative");
    const char *fallback_directory = json_string(action, "directory", "textures");
    const char *fallback_relative_directory = json_string(action, "relative_directory", "textures");
    const char *collection = json_string(action, "collection", "editor.textures");
    const char *catalog_collection = json_string(action, "catalog_collection", "editor.textures.catalog");
    const char *world_name = json_string(action, "world", "brush.editor_shell.target");
    const char *material_prefix = json_string(action, "material_prefix", "mat.project.texture.");
    const int slot_count = SDL_max(0, json_int(action, "slot_count", 6));
    const char *scroll_index_key = json_string(action, "scroll_index_key", "editor.texture.scroll.index");
    const char *scroll_y_key = json_string(action, "scroll_y_key", "editor.texture.scroll.y");
    const char *directory = slayer3d_properties_get_string(runtime->scene_state, directory_key, "");
    const char *relative_directory = slayer3d_properties_get_string(runtime->scene_state, relative_directory_key, "");
    const char *search_query = slayer3d_properties_get_string(runtime->scene_state, "editor.texture.search", "");
    char *resolved_fallback_directory = NULL;
    char *resolved_texture_directory = NULL;
    if (directory == NULL || directory[0] == '\0')
    {
        resolved_fallback_directory = editor_resolve_directory(runtime, fallback_directory);
        directory = resolved_fallback_directory != NULL ? resolved_fallback_directory : "";
    }
    else if (!editor_path_asset_uri(directory) && !editor_path_absolute(directory))
    {
        resolved_texture_directory = editor_path_make_absolute_from_cwd(directory);
        SDL_PathInfo texture_info;
        SDL_zero(texture_info);
        if (resolved_texture_directory == NULL || !SDL_GetPathInfo(resolved_texture_directory, &texture_info) ||
            texture_info.type != SDL_PATHTYPE_DIRECTORY)
        {
            SDL_free(resolved_texture_directory);
            resolved_texture_directory = editor_resolve_directory(runtime, directory);
        }
        directory = resolved_texture_directory != NULL ? resolved_texture_directory : directory;
    }
    if (relative_directory == NULL || relative_directory[0] == '\0')
        relative_directory = fallback_relative_directory != NULL ? fallback_relative_directory : "";
    editor_texture_clear_slots(runtime->scene_state, slot_count);
    slayer3d_game_data_runtime_collection_clear(runtime, collection);
    slayer3d_game_data_runtime_collection_clear(runtime, catalog_collection);

    editor_texture_scan_list list;
    SDL_zero(list);
    if (!editor_scan_texture_source(runtime, directory, relative_directory, &list))
    {
        editor_set_int_output(runtime->scene_state, outputs, "count_key", 0);
        editor_set_string_output(runtime->scene_state, outputs, "status_key", "texture directory unavailable");
        slayer3d_properties_set_int(runtime->scene_state, "editor.texture.count", 0);
        slayer3d_properties_set_string(runtime->scene_state, "editor.texture.scan.status",
                                       "texture directory unavailable");
        editor_texture_set_empty_state(runtime->scene_state, true, "Texture directory unavailable");
        editor_asset_source_set_diagnostic(runtime->scene_state, "textures", "texture directory unavailable", false,
                                           false);
        SDL_free(resolved_fallback_directory);
        SDL_free(resolved_texture_directory);
        return true;
    }
    brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, world_name);
    const int invalidated_count = editor_invalidate_missing_texture_materials(world_runtime, &list, material_prefix);
    int registered_count = 0;
    for (int i = 0; i < list.count; ++i)
    {
        editor_texture_scan_entry *entry = &list.entries[i];
        int existing_index =
            editor_brush_material_index_by_name_or_texture(world_runtime, NULL, entry->path, entry->relative_path);
        if (existing_index >= 0)
        {
            slayer3d_game_data_brush_material *material =
                (slayer3d_game_data_brush_material *)&world_runtime->desc.materials[existing_index];
            SDL_strlcpy(entry->material, material->name != NULL ? material->name : "", sizeof(entry->material));
            char *owned_material_reference = NULL;
            const char *material_reference = editor_texture_material_reference(entry, &owned_material_reference);
            if (material_reference != NULL && material_reference[0] != '\0' &&
                (material->texture == NULL || !editor_path_text_equal(material->texture, material_reference)))
            {
                char *texture_path = SDL_strdup(material_reference);
                if (texture_path != NULL)
                {
                    SDL_free((void *)material->texture);
                    material->texture = texture_path;
                }
            }
            SDL_free(owned_material_reference);
            entry->sort_order = existing_index;
        }
        else
        {
            SDL_snprintf(entry->material, sizeof(entry->material), "%s%s", material_prefix, entry->slug);
            char *owned_material_reference = NULL;
            const char *material_reference = editor_texture_material_reference(entry, &owned_material_reference);
            if (editor_append_brush_material(world_runtime, entry->material, material_reference))
            {
                ++registered_count;
                entry->sort_order = 100000 + i;
            }
            else
                entry->material[0] = '\0';
            SDL_free(owned_material_reference);
        }
    }

    if (list.count > 1)
        SDL_qsort(list.entries, (size_t)list.count, sizeof(list.entries[0]), editor_texture_scan_entry_order_compare);

    int catalog_count = 0;
    for (int i = 0; i < list.count; ++i)
    {
        const editor_texture_scan_entry *entry = &list.entries[i];
        if (entry->material[0] == '\0')
            continue;
        (void)editor_texture_collection_publish_row(runtime, catalog_collection, catalog_count, entry);
        ++catalog_count;
    }
    const int published_count = editor_texture_publish_filtered_catalog(
        runtime, catalog_collection, collection, slot_count, scroll_index_key, scroll_y_key, search_query);

    const bool has_search_query = search_query != NULL && search_query[0] != '\0';
    char status[128];
    char source_status[128];
    if (invalidated_count > 0)
    {
        SDL_snprintf(source_status, sizeof(source_status), "loaded %d texture%s; %d stale material%s reverted",
                     catalog_count, catalog_count == 1 ? "" : "s", invalidated_count,
                     invalidated_count == 1 ? "" : "s");
    }
    else if (catalog_count == 0)
        SDL_snprintf(source_status, sizeof(source_status), "no textures found");
    else
        SDL_snprintf(source_status, sizeof(source_status), "loaded %d texture%s", catalog_count,
                     catalog_count == 1 ? "" : "s");

    if (catalog_count == 0)
        SDL_strlcpy(status, source_status, sizeof(status));
    else if (has_search_query && published_count == 0)
        SDL_snprintf(status, sizeof(status), "no textures match search");
    else if (has_search_query)
        SDL_snprintf(status, sizeof(status), "showing %d texture%s", published_count, published_count == 1 ? "" : "s");
    else
        SDL_strlcpy(status, source_status, sizeof(status));
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.count", published_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.registered_count", registered_count);
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.invalidated_count", invalidated_count);
    slayer3d_properties_set_string(runtime->scene_state, "editor.texture.scan.status", status);
    editor_asset_source_set_diagnostic(runtime->scene_state, "textures", source_status, true, catalog_count == 0);
    editor_set_int_output(runtime->scene_state, outputs, "count_key", published_count);
    editor_set_int_output(runtime->scene_state, outputs, "registered_count_key", registered_count);
    editor_set_string_output(runtime->scene_state, outputs, "status_key", status);
    editor_texture_scan_list_free(&list);
    SDL_free(resolved_fallback_directory);
    SDL_free(resolved_texture_directory);
    return true;
}

static bool execute_editor_texture_filter_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *collection = json_string(action, "collection", "editor.textures");
    const char *catalog_collection = json_string(action, "catalog_collection", "editor.textures.catalog");
    const int slot_count = SDL_max(0, json_int(action, "slot_count", 6));
    const char *scroll_index_key = json_string(action, "scroll_index_key", "editor.texture.scroll.index");
    const char *scroll_y_key = json_string(action, "scroll_y_key", "editor.texture.scroll.y");
    const char *search_query = slayer3d_properties_get_string(runtime->scene_state, "editor.texture.search", "");
    const int published_count = editor_texture_publish_filtered_catalog(
        runtime, catalog_collection, collection, slot_count, scroll_index_key, scroll_y_key, search_query);
    char status[128];
    if (published_count == 0 && search_query != NULL && search_query[0] != '\0')
        SDL_snprintf(status, sizeof(status), "no textures match search");
    else if (published_count == 0)
        SDL_snprintf(status, sizeof(status), "no textures found");
    else
        SDL_snprintf(status, sizeof(status), "showing %d texture%s", published_count, published_count == 1 ? "" : "s");
    slayer3d_properties_set_string(runtime->scene_state, "editor.texture.scan.status", status);
    editor_set_int_output(runtime->scene_state, outputs, "count_key", published_count);
    editor_set_string_output(runtime->scene_state, outputs, "status_key", status);
    return true;
}

static bool editor_media_set_child_source(slayer3d_game_data_runtime *runtime, const char *name, const char *media_root,
                                          const char *media_relative_root, const char *leaf)
{
    if (runtime == NULL || runtime->scene_state == NULL || name == NULL || media_root == NULL || leaf == NULL)
        return false;
    char *path = editor_path_join(media_root, leaf);
    if (path == NULL)
        return false;
    char *relative_path = media_relative_root != NULL && media_relative_root[0] != '\0'
                              ? editor_relative_path_join(media_relative_root, leaf)
                              : SDL_strdup(leaf);
    if (relative_path == NULL)
    {
        SDL_free(path);
        return false;
    }

    SDL_PathInfo info;
    SDL_zero(info);
    const bool available = SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
    char key[128];
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.path", name);
    slayer3d_properties_set_string(runtime->scene_state, key, path);
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.relative", name);
    slayer3d_properties_set_string(runtime->scene_state, key, relative_path);
    SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.available", name);
    slayer3d_properties_set_bool(runtime->scene_state, key, available);
    char status[160];
    SDL_snprintf(status, sizeof(status), "%s directory %s", name, available ? "ready" : "missing");
    editor_asset_source_set_diagnostic(runtime->scene_state, name, status, available, false);
    SDL_free(relative_path);
    SDL_free(path);
    return true;
}

static void editor_media_refresh_missing_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    static const char *const names[] = {"textures", "models", "sprites", "skyboxes", "liquids", "effects"};
    bool any_missing = false;
    for (size_t i = 0; i < SDL_arraysize(names); ++i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.available", names[i]);
        any_missing = any_missing || !slayer3d_properties_get_bool(runtime->scene_state, key, false);
    }
    slayer3d_properties_set_bool(runtime->scene_state, "editor.asset_source.any_missing", any_missing);
}

static void editor_media_mark_child_sources_unavailable(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    static const char *const names[] = {"textures", "models", "sprites", "skyboxes", "liquids", "effects"};
    for (size_t i = 0; i < SDL_arraysize(names); ++i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "editor.asset_source.%s.available", names[i]);
        slayer3d_properties_set_bool(runtime->scene_state, key, false);
        char status[160];
        SDL_snprintf(status, sizeof(status), "%s directory missing", names[i]);
        editor_asset_source_set_diagnostic(runtime->scene_state, names[i], status, false, false);
    }
    slayer3d_properties_set_bool(runtime->scene_state, "editor.asset_source.any_missing", true);
}

static bool execute_editor_media_path_apply_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const char *path_key = json_string(action, "path_key", "editor.media.path.input");
    const char *status_key = json_string(action, "status_key", "editor.media.status");
    const char *path = slayer3d_properties_get_string(runtime->scene_state, path_key, "");
    if (path == NULL || path[0] == '\0')
        path = slayer3d_properties_get_string(runtime->scene_state, "editor.media.path", "");

    char *absolute_path = editor_path_make_absolute_from_cwd(path);
    SDL_PathInfo info;
    SDL_zero(info);
    const bool valid_directory = absolute_path != NULL && absolute_path[0] != '\0' &&
                                 SDL_GetPathInfo(absolute_path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
    if (!valid_directory)
    {
        slayer3d_properties_set_bool(runtime->scene_state, "editor.media.available", false);
        editor_media_mark_child_sources_unavailable(runtime);
        slayer3d_properties_set_string(runtime->scene_state, status_key, "media path is not a directory");
        editor_publish_console_message(runtime, "Media path is not a directory");
        SDL_free(absolute_path);
        return true;
    }

    const bool was_startup_prompt =
        slayer3d_properties_get_bool(runtime->scene_state, "editor.media.startup_prompt.open", false);
    char *relative_path = editor_relative_directory_for_source_path(runtime, path, absolute_path);
    slayer3d_properties_set_string(runtime->scene_state, "editor.media.path", absolute_path);
    slayer3d_properties_set_string(runtime->scene_state, "editor.media.path.input", absolute_path);
    slayer3d_properties_set_string(runtime->scene_state, "editor.media.relative",
                                   relative_path != NULL ? relative_path : "media");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.media.available", true);
    const char *media_relative_root = relative_path != NULL ? relative_path : "media";
    (void)editor_media_set_child_source(runtime, "textures", absolute_path, media_relative_root, "textures");
    (void)editor_media_set_child_source(runtime, "models", absolute_path, media_relative_root, "models");
    (void)editor_media_set_child_source(runtime, "sprites", absolute_path, media_relative_root, "sprites");
    (void)editor_media_set_child_source(runtime, "skyboxes", absolute_path, media_relative_root, "skyboxes");
    (void)editor_media_set_child_source(runtime, "liquids", absolute_path, media_relative_root, "liquids");
    (void)editor_media_set_child_source(runtime, "effects", absolute_path, media_relative_root, "effects");
    editor_media_refresh_missing_state(runtime);

    slayer3d_properties_set_bool(runtime->scene_state, "editor.media.startup_prompt.open", false);
    if (was_startup_prompt)
    {
        slayer3d_properties_set_bool(runtime->scene_state, "editor.settings.open", false);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.file.menu.open", false);
    }
    slayer3d_properties_set_string(runtime->scene_state, status_key, "media path updated");
    editor_publish_console_message(runtime, "Media path updated");
    SDL_free(relative_path);
    SDL_free(absolute_path);
    return true;
}

/* ------------------------------------------------------------------ */
/* Editor play mode and external runner                                */
/* ------------------------------------------------------------------ */

#define EDITOR_RUNNER_ARG_MAX 64
#define EDITOR_RUNNER_TOKEN_MAX 4096

static char *editor_runner_substitute_current_map(const char *text, const char *current_map)
{
    static const char placeholder[] = "{current_map}";
    if (text == NULL || current_map == NULL)
        return NULL;
    size_t count = 0U;
    const char *cursor = text;
    while ((cursor = SDL_strstr(cursor, placeholder)) != NULL)
    {
        ++count;
        cursor += sizeof(placeholder) - 1U;
    }
    const size_t text_size = SDL_strlen(text);
    const size_t map_size = SDL_strlen(current_map);
    const size_t placeholder_size = sizeof(placeholder) - 1U;
    size_t result_size = text_size;
    if (map_size >= placeholder_size)
        result_size += count * (map_size - placeholder_size);
    else
        result_size -= count * (placeholder_size - map_size);
    char *result = (char *)SDL_malloc(result_size + 1U);
    if (result == NULL)
        return NULL;

    char *output = result;
    cursor = text;
    const char *match = NULL;
    while ((match = SDL_strstr(cursor, placeholder)) != NULL)
    {
        const size_t prefix_size = (size_t)(match - cursor);
        SDL_memcpy(output, cursor, prefix_size);
        output += prefix_size;
        SDL_memcpy(output, current_map, map_size);
        output += map_size;
        cursor = match + placeholder_size;
    }
    SDL_strlcpy(output, cursor, result_size + 1U - (size_t)(output - result));
    return result;
}

static void editor_runner_free_argv(char **argv, int count)
{
    if (argv == NULL)
        return;
    for (int i = 0; i < count; ++i)
        SDL_free(argv[i]);
    SDL_free(argv);
}

static char **editor_runner_build_argv(const char *executable, const char *arguments, const char *current_map,
                                       int *out_count, char *error, size_t error_size)
{
    if (out_count != NULL)
        *out_count = 0;
    char **argv = (char **)SDL_calloc(EDITOR_RUNNER_ARG_MAX + 2U, sizeof(*argv));
    if (argv == NULL)
        return NULL;
    argv[0] = SDL_strdup(executable);
    int count = argv[0] != NULL ? 1 : 0;
    const char *cursor = arguments != NULL ? arguments : "";
    while (argv[0] != NULL && *cursor != '\0')
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            ++cursor;
        if (*cursor == '\0')
            break;
        if (count > EDITOR_RUNNER_ARG_MAX)
        {
            SDL_snprintf(error, error_size, "runner accepts at most %d arguments", EDITOR_RUNNER_ARG_MAX);
            editor_runner_free_argv(argv, count);
            return NULL;
        }

        char token[EDITOR_RUNNER_TOKEN_MAX];
        size_t token_size = 0U;
        char quote = '\0';
        bool started = false;
        while (*cursor != '\0')
        {
            const char ch = *cursor;
            if (quote == '\0' && (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'))
                break;
            ++cursor;
            started = true;
            if (ch == '\\' && *cursor != '\0')
            {
                if (token_size + 1U < sizeof(token))
                    token[token_size++] = *cursor;
                ++cursor;
                continue;
            }
            if (ch == '\'' || ch == '"')
            {
                if (quote == '\0')
                {
                    quote = ch;
                    continue;
                }
                if (quote == ch)
                {
                    quote = '\0';
                    continue;
                }
            }
            if (token_size + 1U >= sizeof(token))
            {
                SDL_snprintf(error, error_size, "runner argument is too long");
                editor_runner_free_argv(argv, count);
                return NULL;
            }
            token[token_size++] = ch;
        }
        if (quote != '\0')
        {
            SDL_snprintf(error, error_size, "runner arguments contain an unterminated quote");
            editor_runner_free_argv(argv, count);
            return NULL;
        }
        if (!started)
            continue;
        token[token_size] = '\0';
        argv[count] = editor_runner_substitute_current_map(token, current_map);
        if (argv[count] == NULL)
        {
            editor_runner_free_argv(argv, count);
            return NULL;
        }
        ++count;
    }
    if (argv[0] == NULL)
    {
        editor_runner_free_argv(argv, count);
        return NULL;
    }
    argv[count] = NULL;
    if (out_count != NULL)
        *out_count = count;
    return argv;
}

static bool editor_start_external_runner(slayer3d_game_data_runtime *runtime)
{
    slayer3d_properties *state = runtime != NULL ? runtime->scene_state : NULL;
    const char *configured = state != NULL ? slayer3d_properties_get_string(state, "editor.runner.executable", "") : "";
    const char *save_path = state != NULL ? slayer3d_properties_get_string(state, "editor.save.path", "") : "";
    if (configured[0] == '\0' || save_path[0] == '\0')
    {
        editor_publish_console_message(runtime, configured[0] == '\0' ? "External runner executable is not configured"
                                                                      : "Save the map before running it");
        return false;
    }
#ifdef __EMSCRIPTEN__
    editor_publish_console_message(runtime, "External runner programs are unavailable in web builds");
    return false;
#else
    const char *project_dir = slayer3d_properties_get_string(state, "editor.project.dir", "");
    char *executable =
        editor_path_absolute(configured) ? SDL_strdup(configured) : editor_path_join(project_dir, configured);
    char *current_map = editor_path_make_absolute_from_cwd(save_path);
    char error[256] = {0};
    int argc = 0;
    char **argv =
        executable != NULL && current_map != NULL
            ? editor_runner_build_argv(executable, slayer3d_properties_get_string(state, "editor.runner.arguments", ""),
                                       current_map, &argc, error, sizeof(error))
            : NULL;
    SDL_Process *process = argv != NULL ? SDL_CreateProcess((const char *const *)argv, false) : NULL;
    if (process == NULL)
    {
        editor_publish_console_messagef(runtime, "External runner failed: %s",
                                        error[0] != '\0' ? error : SDL_GetError());
    }
    else
    {
        editor_publish_console_messagef(runtime, "External runner started: %s", executable);
        SDL_DestroyProcess(process);
    }
    editor_runner_free_argv(argv, argc);
    SDL_free(current_map);
    SDL_free(executable);
    return process != NULL;
#endif
}

static bool editor_actor_is_player_character(const editor_actor_runtime *actor)
{
    return actor != NULL && actor->properties != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(actor->properties, "actor-type", ""), "player-character") == 0;
}

static bool editor_start_builtin_play(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.editor_shell.player");
    slayer3d_registered_actor *fly = slayer3d_game_data_find_actor(runtime, "entity.editor_shell.fly_player");
    if (player == NULL || fly == NULL)
        return false;

    const editor_actor_runtime *player_marker = NULL;
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (editor_actor_is_player_character(&runtime->editor_actors[i]))
        {
            player_marker = &runtime->editor_actors[i];
            break;
        }
    }

    const char *camera = NULL;
    const char *message = NULL;
    if (player_marker != NULL)
    {
        actor_set_position(player, player_marker->position);
        slayer3d_properties_set_float(player->props, "yaw", player_marker->rotation.y);
        slayer3d_properties_set_float(player->props, "pitch", player_marker->rotation.x);
        fps_controller_runtime *controller = find_fps_controller(runtime, player->name);
        if (controller != NULL)
            controller->initialized = false;
        player->active = true;
        fly->active = false;
        camera = "camera.editor_shell.player";
        slayer3d_properties_set_string(runtime->scene_state, "editor.play.mode", "player");
        message = "Built-in play mode started at player-character";
    }
    else
    {
        actor_set_position(
            fly, slayer3d_properties_get_vec3(fly->props, "play_spawn_position", slayer3d_vec3_make(0.0f, 1.8f, 0.0f)));
        slayer3d_properties_set_float(fly->props, "yaw",
                                      slayer3d_properties_get_float(fly->props, "play_spawn_yaw", 0.0f));
        slayer3d_properties_set_float(fly->props, "pitch",
                                      slayer3d_properties_get_float(fly->props, "play_spawn_pitch", -0.25f));
        player->active = false;
        fly->active = true;
        camera = "camera.editor_shell.fly";
        slayer3d_properties_set_string(runtime->scene_state, "editor.play.mode", "fly");
        message = "Built-in fly mode started (no player-character in map)";
    }
    if (!slayer3d_game_data_set_active_scene(runtime, "scene.editor_shell.test_run"))
        return false;
    runtime->active_camera = camera;
    slayer3d_properties_set_bool(runtime->scene_state, "editor.test_run.enter.valid", true);
    slayer3d_properties_set_string(runtime->scene_state, "editor.test_run.enter.message", message);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    editor_publish_console_message(runtime, message);
    return true;
}

static bool execute_editor_runner_start_action(slayer3d_game_data_runtime *runtime)
{
    const char *kind = runtime != NULL && runtime->scene_state != NULL
                           ? slayer3d_properties_get_string(runtime->scene_state, "editor.runner.kind", "builtin")
                           : "builtin";
    const char *executable = runtime != NULL && runtime->scene_state != NULL
                                 ? slayer3d_properties_get_string(runtime->scene_state, "editor.runner.executable", "")
                                 : "";
    return SDL_strcmp(kind, "external") == 0 && executable[0] != '\0' ? editor_start_external_runner(runtime)
                                                                      : editor_start_builtin_play(runtime);
}

/* ------------------------------------------------------------------ */
/* Editor skybox panel actions                                         */
/* ------------------------------------------------------------------ */

#define EDITOR_SKY_ITEM_CAP 32
#define EDITOR_SKY_SLOT_COUNT 6
#define EDITOR_SKY_SCAN_MAX_DEPTH 4

typedef struct editor_sky_scan_item
{
    char name[96];
    char base[512];
    bool has_sphere;
    bool has_faces;
    int layer_count;
} editor_sky_scan_item;

typedef struct editor_sky_scan_list
{
    editor_sky_scan_item items[EDITOR_SKY_ITEM_CAP];
    int count;
} editor_sky_scan_list;

static const char *const editor_sky_face_files[6] = {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};

static void editor_sky_child_name(const char *parent, const char *leaf, char *buffer, size_t buffer_size)
{
    if (parent == NULL || parent[0] == '\0')
        SDL_strlcpy(buffer, leaf != NULL ? leaf : "", buffer_size);
    else
        SDL_snprintf(buffer, buffer_size, "%s/%s", parent, leaf != NULL ? leaf : "");
}

static void editor_sky_scan_add_item(editor_sky_scan_list *list, const char *name, const char *base, bool has_sphere,
                                     bool has_faces, int layer_count)
{
    if (list == NULL || list->count >= EDITOR_SKY_ITEM_CAP || name == NULL || name[0] == '\0' || base == NULL ||
        base[0] == '\0' || (!has_sphere && !has_faces && layer_count == 0))
    {
        return;
    }
    editor_sky_scan_item *item = &list->items[list->count++];
    SDL_strlcpy(item->name, name, sizeof(item->name));
    SDL_strlcpy(item->base, base, sizeof(item->base));
    item->has_sphere = has_sphere;
    item->has_faces = has_faces;
    item->layer_count = layer_count;
}

static int SDLCALL editor_sky_scan_item_compare(const void *a, const void *b)
{
    return SDL_strcmp(((const editor_sky_scan_item *)a)->name, ((const editor_sky_scan_item *)b)->name);
}

static bool editor_sky_fs_file_exists(const char *directory, const char *leaf)
{
    char *path = editor_path_join(directory, leaf);
    SDL_PathInfo info;
    SDL_zero(info);
    const bool exists = path != NULL && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
    SDL_free(path);
    return exists;
}

static bool editor_sky_fs_directory_flags(const char *directory, bool *out_has_sphere, bool *out_has_faces,
                                          int *out_layer_count)
{
    const bool has_sphere = editor_sky_fs_file_exists(directory, "sphere.png");
    bool has_faces = true;
    for (size_t i = 0; i < SDL_arraysize(editor_sky_face_files); ++i)
        has_faces = has_faces && editor_sky_fs_file_exists(directory, editor_sky_face_files[i]);
    int layer_count = 0;
    if (editor_sky_fs_file_exists(directory, "layer_outer.png"))
        layer_count = editor_sky_fs_file_exists(directory, "layer_inner.png") ? 2 : 1;
    *out_has_sphere = has_sphere;
    *out_has_faces = has_faces;
    *out_layer_count = layer_count;
    return has_sphere || has_faces || layer_count > 0;
}

static void editor_sky_scan_fs_directory(const char *directory, const char *relative_name, editor_sky_scan_list *list,
                                         int depth);

typedef struct editor_sky_fs_enum_context
{
    editor_sky_scan_list *list;
    const char *relative_name;
    int depth;
} editor_sky_fs_enum_context;

static SDL_EnumerationResult SDLCALL editor_sky_fs_enumerate_entry(void *userdata, const char *dirname,
                                                                   const char *fname)
{
    editor_sky_fs_enum_context *ctx = (editor_sky_fs_enum_context *)userdata;
    if (ctx == NULL || ctx->list == NULL || ctx->list->count >= EDITOR_SKY_ITEM_CAP)
        return SDL_ENUM_SUCCESS;

    char *path = editor_path_join(dirname, fname);
    SDL_PathInfo info;
    SDL_zero(info);
    if (path == NULL || !SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY)
    {
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }

    char child_name[96];
    editor_sky_child_name(ctx->relative_name, fname, child_name, sizeof(child_name));
    editor_sky_scan_fs_directory(path, child_name, ctx->list, ctx->depth + 1);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static void editor_sky_scan_fs_directory(const char *directory, const char *relative_name, editor_sky_scan_list *list,
                                         int depth)
{
    if (directory == NULL || directory[0] == '\0' || list == NULL || depth > EDITOR_SKY_SCAN_MAX_DEPTH ||
        list->count >= EDITOR_SKY_ITEM_CAP)
    {
        return;
    }
    if (relative_name != NULL && relative_name[0] != '\0')
    {
        bool has_sphere = false;
        bool has_faces = false;
        int layer_count = 0;
        if (editor_sky_fs_directory_flags(directory, &has_sphere, &has_faces, &layer_count))
        {
            editor_sky_scan_add_item(list, relative_name, directory, has_sphere, has_faces, layer_count);
            return;
        }
    }
    editor_sky_fs_enum_context enumerate_ctx = {list, relative_name != NULL ? relative_name : "", depth};
    (void)SDL_EnumerateDirectory(directory, editor_sky_fs_enumerate_entry, &enumerate_ctx);
}

static void editor_sky_scan_asset_directory(slayer3d_game_data_runtime *runtime, const char *asset_directory,
                                            const char *relative_name, editor_sky_scan_list *list, int depth);

typedef struct editor_sky_asset_enum_context
{
    slayer3d_game_data_runtime *runtime;
    editor_sky_scan_list *list;
    const char *asset_directory;
    const char *relative_name;
    int depth;
    bool face_seen[6];
    bool sphere;
    bool layer_outer;
    bool layer_inner;
} editor_sky_asset_enum_context;

static slayer3d_asset_enumeration_result editor_sky_asset_enumerate_entry(void *userdata, const char *directory,
                                                                          const char *name,
                                                                          slayer3d_asset_entry_type type)
{
    editor_sky_asset_enum_context *ctx = (editor_sky_asset_enum_context *)userdata;
    if (ctx == NULL || ctx->list == NULL)
        return SLAYER3D_ASSET_ENUM_FAILURE;
    (void)directory;

    if (type == SLAYER3D_ASSET_ENTRY_FILE)
    {
        for (size_t i = 0; i < SDL_arraysize(editor_sky_face_files); ++i)
        {
            if (SDL_strcmp(name, editor_sky_face_files[i]) == 0)
                ctx->face_seen[i] = true;
        }
        if (SDL_strcmp(name, "layer_outer.png") == 0)
            ctx->layer_outer = true;
        if (SDL_strcmp(name, "layer_inner.png") == 0)
            ctx->layer_inner = true;
        if (SDL_strcmp(name, "sphere.png") == 0)
            ctx->sphere = true;
        return SLAYER3D_ASSET_ENUM_CONTINUE;
    }

    if (type == SLAYER3D_ASSET_ENTRY_DIRECTORY && ctx->depth < EDITOR_SKY_SCAN_MAX_DEPTH)
    {
        char *child_path = editor_asset_path_join(ctx->asset_directory, name);
        char child_name[96];
        editor_sky_child_name(ctx->relative_name, name, child_name, sizeof(child_name));
        if (child_path != NULL)
            editor_sky_scan_asset_directory(ctx->runtime, child_path, child_name, ctx->list, ctx->depth + 1);
        SDL_free(child_path);
    }
    return SLAYER3D_ASSET_ENUM_CONTINUE;
}

static void editor_sky_scan_asset_directory(slayer3d_game_data_runtime *runtime, const char *asset_directory,
                                            const char *relative_name, editor_sky_scan_list *list, int depth)
{
    if (runtime == NULL || runtime->assets == NULL || asset_directory == NULL || asset_directory[0] == '\0' ||
        list == NULL || depth > EDITOR_SKY_SCAN_MAX_DEPTH || list->count >= EDITOR_SKY_ITEM_CAP)
    {
        return;
    }
    editor_sky_asset_enum_context enumerate_ctx;
    SDL_zero(enumerate_ctx);
    enumerate_ctx.runtime = runtime;
    enumerate_ctx.list = list;
    enumerate_ctx.asset_directory = asset_directory;
    enumerate_ctx.relative_name = relative_name != NULL ? relative_name : "";
    enumerate_ctx.depth = depth;
    char error[256];
    error[0] = '\0';
    if (!slayer3d_asset_resolver_enumerate(runtime->assets, asset_directory, editor_sky_asset_enumerate_entry,
                                           &enumerate_ctx, error, (int)sizeof(error)))
    {
        return;
    }
    if (relative_name == NULL || relative_name[0] == '\0')
        return;
    bool has_faces = true;
    for (size_t i = 0; i < SDL_arraysize(enumerate_ctx.face_seen); ++i)
        has_faces = has_faces && enumerate_ctx.face_seen[i];
    const int layer_count = enumerate_ctx.layer_outer ? (enumerate_ctx.layer_inner ? 2 : 1) : 0;
    editor_sky_scan_add_item(list, relative_name, asset_directory, enumerate_ctx.sphere, has_faces, layer_count);
}

static void editor_sky_item_key(char *buffer, size_t buffer_size, int index, const char *field)
{
    SDL_snprintf(buffer, buffer_size, "editor.sky.item.%d.%s", index, field);
}

static void editor_sky_slot_key(char *buffer, size_t buffer_size, int slot, const char *field)
{
    SDL_snprintf(buffer, buffer_size, "editor.sky.slot.%d.%s", slot, field);
}

static void editor_sky_preview_path(char *buffer, size_t buffer_size, const char *base, bool has_sphere, bool has_faces)
{
    SDL_snprintf(buffer, buffer_size, "%s/%s", base,
                 has_sphere ? "sphere.png" : (has_faces ? "pz.png" : "layer_outer.png"));
}

static void editor_sky_publish_slots(slayer3d_game_data_runtime *runtime)
{
    slayer3d_properties *state = runtime->scene_state;
    const int count = SDL_clamp(slayer3d_properties_get_int(state, "editor.sky.count", 0), 0, EDITOR_SKY_ITEM_CAP);
    const int max_scroll = count > EDITOR_SKY_SLOT_COUNT ? count - EDITOR_SKY_SLOT_COUNT : 0;
    const int scroll = SDL_clamp(slayer3d_properties_get_int(state, "editor.sky.scroll", 0), 0, max_scroll);
    slayer3d_properties_set_int(state, "editor.sky.scroll", scroll);

    const int selected_index = slayer3d_properties_get_int(state, "editor.sky.selected_index", -1);
    const bool selected_visible = selected_index >= scroll && selected_index < scroll + EDITOR_SKY_SLOT_COUNT;
    slayer3d_properties_set_int(state, "editor.sky.selected_slot", selected_visible ? selected_index - scroll : -1);

    for (int slot = 0; slot < EDITOR_SKY_SLOT_COUNT; ++slot)
    {
        const int index = scroll + slot;
        char item_key[96];
        char slot_key[96];
        const char *name = "";
        const char *base = "";
        bool has_faces = false;
        bool has_sphere = false;
        int layer_count = 0;
        if (index < count)
        {
            editor_sky_item_key(item_key, sizeof(item_key), index, "name");
            name = slayer3d_properties_get_string(state, item_key, "");
            editor_sky_item_key(item_key, sizeof(item_key), index, "base");
            base = slayer3d_properties_get_string(state, item_key, "");
            editor_sky_item_key(item_key, sizeof(item_key), index, "faces");
            has_faces = slayer3d_properties_get_bool(state, item_key, false);
            editor_sky_item_key(item_key, sizeof(item_key), index, "sphere");
            has_sphere = slayer3d_properties_get_bool(state, item_key, false);
            editor_sky_item_key(item_key, sizeof(item_key), index, "layer_count");
            layer_count = slayer3d_properties_get_int(state, item_key, 0);
        }
        const bool available = name[0] != '\0' && base[0] != '\0';
        editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "name");
        slayer3d_properties_set_string(state, slot_key, name);
        editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "base");
        slayer3d_properties_set_string(state, slot_key, base);
        editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "faces");
        slayer3d_properties_set_bool(state, slot_key, has_faces);
        editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "sphere");
        slayer3d_properties_set_bool(state, slot_key, has_sphere);
        editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "layer_count");
        slayer3d_properties_set_int(state, slot_key, layer_count);
        editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "available");
        slayer3d_properties_set_bool(state, slot_key, available);
        char preview[640] = "";
        if (available)
            editor_sky_preview_path(preview, sizeof(preview), base, has_sphere, has_faces);
        editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "path");
        slayer3d_properties_set_string(state, slot_key, preview);
    }
}

static bool execute_editor_sky_scan_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    slayer3d_properties *state = runtime->scene_state;

    const int scroll_delta = json_int(action, "scroll_delta", 0);
    if (scroll_delta != 0)
    {
        const int scroll = slayer3d_properties_get_int(state, "editor.sky.scroll", 0);
        slayer3d_properties_set_int(state, "editor.sky.scroll", scroll + scroll_delta);
        editor_sky_publish_slots(runtime);
        return true;
    }
    /* Rebind the visible slot window at the current scroll index without
     * rescanning the directory; scrollbar drags use this. */
    if (json_bool(action, "rebind", false))
    {
        editor_sky_publish_slots(runtime);
        return true;
    }

    const char *directory = slayer3d_properties_get_string(state, "editor.asset_source.skyboxes.path", "");
    char *resolved_fallback = NULL;
    if (directory == NULL || directory[0] == '\0')
    {
        resolved_fallback = editor_resolve_directory(runtime, "skyboxes");
        directory = resolved_fallback != NULL ? resolved_fallback : "";
    }
    bool source_available = directory != NULL && directory[0] != '\0';
    if (source_available && !editor_path_asset_uri(directory))
    {
        SDL_PathInfo info;
        SDL_zero(info);
        source_available = SDL_GetPathInfo(directory, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
    }

    editor_sky_scan_list list;
    SDL_zero(list);
    if (source_available)
    {
        if (editor_path_asset_uri(directory))
            editor_sky_scan_asset_directory(runtime, directory, "", &list, 0);
        else
            editor_sky_scan_fs_directory(directory, "", &list, 0);
    }
    SDL_free(resolved_fallback);
    if (list.count > 1)
        SDL_qsort(list.items, (size_t)list.count, sizeof(list.items[0]), editor_sky_scan_item_compare);

    slayer3d_properties_set_int(state, "editor.sky.count", list.count);
    for (int i = 0; i < EDITOR_SKY_ITEM_CAP; ++i)
    {
        char item_key[96];
        const editor_sky_scan_item *item = i < list.count ? &list.items[i] : NULL;
        editor_sky_item_key(item_key, sizeof(item_key), i, "name");
        slayer3d_properties_set_string(state, item_key, item != NULL ? item->name : "");
        editor_sky_item_key(item_key, sizeof(item_key), i, "base");
        slayer3d_properties_set_string(state, item_key, item != NULL ? item->base : "");
        editor_sky_item_key(item_key, sizeof(item_key), i, "faces");
        slayer3d_properties_set_bool(state, item_key, item != NULL && item->has_faces);
        editor_sky_item_key(item_key, sizeof(item_key), i, "sphere");
        slayer3d_properties_set_bool(state, item_key, item != NULL && item->has_sphere);
        editor_sky_item_key(item_key, sizeof(item_key), i, "layer_count");
        slayer3d_properties_set_int(state, item_key, item != NULL ? item->layer_count : 0);
    }
    editor_sky_publish_slots(runtime);

    char status[96];
    if (!source_available)
        SDL_strlcpy(status, "skybox directory unavailable", sizeof(status));
    else if (list.count == 0)
        SDL_strlcpy(status, "no skyboxes found", sizeof(status));
    else
        SDL_snprintf(status, sizeof(status), "%d skybox%s found", list.count, list.count == 1 ? "" : "es");
    slayer3d_properties_set_string(state, "editor.sky.status", status);
    editor_asset_source_set_diagnostic(state, "skyboxes", status, source_available,
                                       source_available && list.count == 0);
    slayer3d_properties_set_string(state, "editor.tool.last_action", status);
    return true;
}

static bool execute_editor_sky_select_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    slayer3d_properties *state = runtime->scene_state;
    const int slot = json_int(action, "slot", -1);
    if (slot < 0 || slot >= EDITOR_SKY_SLOT_COUNT)
        return false;

    char slot_key[96];
    editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "available");
    if (!slayer3d_properties_get_bool(state, slot_key, false))
    {
        slayer3d_properties_set_string(state, "editor.tool.last_action", "skybox slot is empty");
        return true;
    }

    editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "name");
    const char *name = slayer3d_properties_get_string(state, slot_key, "");
    slayer3d_properties_set_string(state, "editor.sky.selected.name", name);
    editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "base");
    slayer3d_properties_set_string(state, "editor.sky.selected.base",
                                   slayer3d_properties_get_string(state, slot_key, ""));
    editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "faces");
    slayer3d_properties_set_bool(state, "editor.sky.selected.faces",
                                 slayer3d_properties_get_bool(state, slot_key, false));
    editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "sphere");
    slayer3d_properties_set_bool(state, "editor.sky.selected.sphere",
                                 slayer3d_properties_get_bool(state, slot_key, false));
    editor_sky_slot_key(slot_key, sizeof(slot_key), slot, "layer_count");
    slayer3d_properties_set_int(state, "editor.sky.selected.layer_count",
                                slayer3d_properties_get_int(state, slot_key, 0));

    const int scroll = slayer3d_properties_get_int(state, "editor.sky.scroll", 0);
    slayer3d_properties_set_int(state, "editor.sky.selected_index", scroll + slot);
    slayer3d_properties_set_int(state, "editor.sky.selected_slot", slot);

    char message[160];
    SDL_snprintf(message, sizeof(message), "selected skybox %s", name);
    slayer3d_properties_set_string(state, "editor.tool.last_action", message);
    return true;
}

static const char *editor_sky_preset_leaf(const char *name)
{
    const char *leaf = name;
    for (const char *p = name; p != NULL && *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    }
    return leaf;
}

static char *editor_sky_build_override_json(const char *base, const char *preset, bool has_sphere, bool has_faces,
                                            int layer_count)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *obj = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    bool ok = doc != NULL && obj != NULL;
    if (ok)
    {
        yyjson_mut_doc_set_root(doc, obj);
        ok = yyjson_mut_obj_add_strcpy(doc, obj, "mode",
                                       has_sphere ? "sphere" : (layer_count > 0 ? "layers" : "cubemap")) &&
             yyjson_mut_obj_add_strcpy(doc, obj, "preset", preset) && yyjson_mut_obj_add_real(doc, obj, "size", 400.0);
    }
    char face_path[640];
    if (ok && has_sphere)
    {
        SDL_snprintf(face_path, sizeof(face_path), "%s/sphere.png", base);
        ok = yyjson_mut_obj_add_strcpy(doc, obj, "sphere", face_path);
    }
    if (ok && !has_sphere && has_faces)
    {
        static const char *const face_keys[6] = {"pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z"};
        for (int i = 0; ok && i < 6; ++i)
        {
            SDL_snprintf(face_path, sizeof(face_path), "%s/%s", base, editor_sky_face_files[i]);
            ok = yyjson_mut_obj_add_strcpy(doc, obj, face_keys[i], face_path);
        }
    }
    if (ok && !has_sphere && layer_count > 0)
    {
        yyjson_mut_val *layers = yyjson_mut_arr(doc);
        ok = layers != NULL && yyjson_mut_obj_add_val(doc, obj, "layers", layers);
        for (int i = 0; ok && i < layer_count && i < 2; ++i)
        {
            const bool outer = i == 0;
            SDL_snprintf(face_path, sizeof(face_path), "%s/%s", base, outer ? "layer_outer.png" : "layer_inner.png");
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_val *scroll = yyjson_mut_arr(doc);
            ok = entry != NULL && scroll != NULL && yyjson_mut_arr_add_val(layers, entry) &&
                 yyjson_mut_obj_add_strcpy(doc, entry, "texture", face_path) &&
                 yyjson_mut_obj_add_val(doc, entry, "scroll", scroll) &&
                 yyjson_mut_arr_add_real(doc, scroll, outer ? 0.01 : -0.025) &&
                 yyjson_mut_arr_add_real(doc, scroll, outer ? 0.0 : 0.006) &&
                 yyjson_mut_obj_add_real(doc, entry, "scale", outer ? 1.0 : 2.0) &&
                 yyjson_mut_obj_add_real(doc, entry, "opacity", outer ? 1.0 : 0.65) &&
                 yyjson_mut_obj_add_real(doc, entry, "depth", outer ? 1.0 : 0.55);
        }
    }
    char *json = ok ? yyjson_mut_write(doc, 0, NULL) : NULL;
    yyjson_mut_doc_free(doc);
    return json;
}

static bool execute_editor_sky_apply_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    slayer3d_properties *state = runtime->scene_state;

    const char *name = slayer3d_properties_get_string(state, "editor.sky.selected.name", "");
    if (name == NULL || name[0] == '\0' || SDL_strcmp(name, "default") == 0)
    {
        (void)slayer3d_game_data_set_scene_sky_override_json(runtime, NULL);
        slayer3d_properties_set_string(state, "editor.sky.active", "default");
        slayer3d_properties_set_string(state, "editor.sky.mode", "");
        editor_publish_console_message(runtime, "Skybox reset to editor default");
        return true;
    }

    const char *base = slayer3d_properties_get_string(state, "editor.sky.selected.base", "");
    const bool has_sphere = slayer3d_properties_get_bool(state, "editor.sky.selected.sphere", false);
    const bool has_faces = slayer3d_properties_get_bool(state, "editor.sky.selected.faces", false);
    const int layer_count = slayer3d_properties_get_int(state, "editor.sky.selected.layer_count", 0);
    if (base == NULL || base[0] == '\0' || (!has_sphere && !has_faces && layer_count == 0))
    {
        editor_publish_console_message(runtime, "Skybox apply failed: selection is not a usable skybox");
        return true;
    }

    char *json = editor_sky_build_override_json(base, editor_sky_preset_leaf(name), has_sphere, has_faces, layer_count);
    if (json == NULL || !slayer3d_game_data_set_scene_sky_override_json(runtime, json))
    {
        free(json);
        editor_publish_console_message(runtime, "Skybox apply failed: could not build sky configuration");
        return true;
    }
    free(json);

    slayer3d_properties_set_string(state, "editor.sky.active", name);
    slayer3d_properties_set_string(state, "editor.sky.mode",
                                   has_sphere ? "sphere" : (layer_count > 0 ? "layers" : "cubemap"));
    char message[192];
    SDL_snprintf(message, sizeof(message), "Skybox applied: %s%s", name, layer_count > 0 ? " (animated)" : "");
    editor_publish_console_message(runtime, message);
    return true;
}

static bool execute_editor_texture_select_index_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    const char *collection_name = json_string(action, "collection", "editor.textures");
    const char *index_key = json_string(action, "index_key", NULL);
    const char *index_offset_key = json_string(action, "index_offset_key", NULL);
    const int slot_index = index_key != NULL ? slayer3d_properties_get_int(runtime->scene_state, index_key, 0)
                                             : json_int(action, "index", 0);
    const int index_offset =
        index_offset_key != NULL ? slayer3d_properties_get_int(runtime->scene_state, index_offset_key, 0) : 0;
    const int index = slot_index + index_offset;
    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    char warmup_key[128];
    SDL_snprintf(warmup_key, sizeof(warmup_key), "asset_warmup.ui_image.image.editor_shell.texture.slot_%d.pending",
                 slot_index);
    if (slayer3d_properties_get_bool(runtime->scene_state, warmup_key, false))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture loading");
        return true;
    }
    SDL_snprintf(warmup_key, sizeof(warmup_key), "asset_warmup.ui_image.image.editor_shell.texture.slot_%d.failed",
                 slot_index);
    if (slayer3d_properties_get_bool(runtime->scene_state, warmup_key, false))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture unavailable");
        return true;
    }

    char material[128];
    char label[128];
    if (!runtime_collection_field_to_string(collection, index, "material", material, sizeof(material)) ||
        material[0] == '\0')
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture slot is empty");
        return true;
    }
    if (!runtime_collection_field_to_string(collection, index, "label", label, sizeof(label)))
        SDL_strlcpy(label, "texture", sizeof(label));

    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_index", index);
    slayer3d_properties_set_int(runtime->scene_state, "editor.texture.selected_slot", slot_index);
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.material.cursor", material);
    slayer3d_properties_set_string(runtime->scene_state, "editor.texture.material", material);
    char message[192];
    SDL_snprintf(message, sizeof(message), "selected texture %s", label);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    return true;
}

static bool editor_model_extension_supported(const char *filename)
{
    const char *dot = filename != NULL ? SDL_strrchr(filename, '.') : NULL;
    if (dot == NULL || dot[1] == '\0')
        return false;
    return SDL_strcasecmp(dot, ".obj") == 0 || SDL_strcasecmp(dot, ".gltf") == 0 || SDL_strcasecmp(dot, ".glb") == 0 ||
           SDL_strcasecmp(dot, ".fbx") == 0;
}

typedef struct editor_actor_scan_entry
{
    char *filename;
    char *path;
    char *relative_path;
    char id[96];
    char label[96];
    char model[128];
    char mesh[48];
    char name_prefix[128];
    char archetype[128];
    char group[64];
    char tool_mode[48];
    char classname[96];
    char role[64];
    char sensor_profile[64];
    slayer3d_color color;
    slayer3d_vec3 scale;
    int sort_order;
    bool scanned_model;
} editor_actor_scan_entry;

typedef struct editor_actor_scan_list
{
    editor_actor_scan_entry *entries;
    int count;
    int capacity;
} editor_actor_scan_list;

static void editor_actor_scan_list_free(editor_actor_scan_list *list)
{
    if (list == NULL)
        return;
    for (int i = 0; i < list->count; ++i)
    {
        SDL_free(list->entries[i].filename);
        SDL_free(list->entries[i].path);
        SDL_free(list->entries[i].relative_path);
    }
    SDL_free(list->entries);
    SDL_zero(*list);
}

static int SDLCALL editor_actor_scan_entry_compare(const void *a, const void *b)
{
    const editor_actor_scan_entry *ea = (const editor_actor_scan_entry *)a;
    const editor_actor_scan_entry *eb = (const editor_actor_scan_entry *)b;
    if (ea->sort_order < eb->sort_order)
        return -1;
    if (ea->sort_order > eb->sort_order)
        return 1;
    return SDL_strcasecmp(ea->label, eb->label);
}

static bool editor_actor_scan_list_append_blank(editor_actor_scan_list *list, editor_actor_scan_entry **out_entry)
{
    if (out_entry != NULL)
        *out_entry = NULL;
    if (list == NULL || out_entry == NULL)
        return false;
    if (list->count >= list->capacity)
    {
        const int next_capacity = list->capacity > 0 ? list->capacity * 2 : 8;
        editor_actor_scan_entry *next =
            (editor_actor_scan_entry *)SDL_realloc(list->entries, (size_t)next_capacity * sizeof(*next));
        if (next == NULL)
            return false;
        SDL_memset(next + list->capacity, 0, (size_t)(next_capacity - list->capacity) * sizeof(*next));
        list->entries = next;
        list->capacity = next_capacity;
    }
    editor_actor_scan_entry *entry = &list->entries[list->count++];
    SDL_zero(*entry);
    *out_entry = entry;
    return true;
}

static bool editor_actor_scan_list_append_builtin(editor_actor_scan_list *list, const char *id, const char *label,
                                                  const char *mesh, const char *model, const char *name_prefix,
                                                  const char *archetype, const char *group, const char *tool_mode,
                                                  const char *classname, const char *role, const char *sensor_profile,
                                                  slayer3d_color color, slayer3d_vec3 scale, int sort_order)
{
    editor_actor_scan_entry *entry = NULL;
    if (!editor_actor_scan_list_append_blank(list, &entry))
        return false;
    SDL_strlcpy(entry->id, id != NULL ? id : "", sizeof(entry->id));
    SDL_strlcpy(entry->label, label != NULL ? label : "", sizeof(entry->label));
    SDL_strlcpy(entry->mesh, mesh != NULL ? mesh : "box", sizeof(entry->mesh));
    SDL_strlcpy(entry->model, model != NULL ? model : "", sizeof(entry->model));
    SDL_strlcpy(entry->name_prefix, name_prefix != NULL ? name_prefix : "actor.editor_shell",
                sizeof(entry->name_prefix));
    SDL_strlcpy(entry->archetype, archetype != NULL ? archetype : "", sizeof(entry->archetype));
    SDL_strlcpy(entry->group, group != NULL ? group : "Actors", sizeof(entry->group));
    SDL_strlcpy(entry->tool_mode, tool_mode != NULL ? tool_mode : "actor_selected", sizeof(entry->tool_mode));
    SDL_strlcpy(entry->classname, classname != NULL ? classname : id, sizeof(entry->classname));
    SDL_strlcpy(entry->role, role != NULL ? role : "actor", sizeof(entry->role));
    SDL_strlcpy(entry->sensor_profile, sensor_profile != NULL ? sensor_profile : "", sizeof(entry->sensor_profile));
    entry->color = color;
    entry->scale = scale;
    entry->sort_order = sort_order;
    return entry->id[0] != '\0' && entry->label[0] != '\0';
}

static bool editor_actor_scan_field_matches_token(const char *field, const char *token, size_t token_len)
{
    return field != NULL && field[0] != '\0' && editor_texture_fuzzy_token_matches(field, token, token_len);
}

static bool editor_actor_scan_entry_token_matches(const editor_actor_scan_entry *entry, const char *token,
                                                  size_t token_len)
{
    if (entry == NULL)
        return false;
    return editor_actor_scan_field_matches_token(entry->id, token, token_len) ||
           editor_actor_scan_field_matches_token(entry->label, token, token_len) ||
           editor_actor_scan_field_matches_token(entry->filename, token, token_len) ||
           editor_actor_scan_field_matches_token(entry->relative_path, token, token_len) ||
           editor_actor_scan_field_matches_token(entry->model, token, token_len) ||
           editor_actor_scan_field_matches_token(entry->group, token, token_len) ||
           editor_actor_scan_field_matches_token(entry->classname, token, token_len) ||
           editor_actor_scan_field_matches_token(entry->role, token, token_len);
}

static bool editor_actor_scan_entry_matches_model_search(const editor_actor_scan_entry *entry, const char *query)
{
    if (entry == NULL || !entry->scanned_model)
        return true;
    if (query == NULL || query[0] == '\0')
        return true;

    const char *token = query;
    while (*token != '\0')
    {
        while (editor_ascii_is_space(*token))
            ++token;
        const char *end = token;
        while (*end != '\0' && !editor_ascii_is_space(*end))
            ++end;
        const size_t token_len = (size_t)(end - token);
        if (token_len > 0U && !editor_actor_scan_entry_token_matches(entry, token, token_len))
            return false;
        token = end;
    }
    return true;
}

static bool editor_actor_scan_model_id_used(const editor_actor_scan_list *list, const char *model)
{
    if (list == NULL || model == NULL || model[0] == '\0')
        return false;
    for (int i = 0; i < list->count; ++i)
    {
        if (list->entries[i].model[0] != '\0' && SDL_strcmp(list->entries[i].model, model) == 0)
            return true;
    }
    return false;
}

static bool editor_model_asset_id_for_filename(const slayer3d_game_data_runtime *runtime, const char *filename,
                                               char *buffer, size_t buffer_size)
{
    if (buffer != NULL && buffer_size > 0U)
        buffer[0] = '\0';
    yyjson_val *models = obj_get(obj_get(runtime_root(runtime), "assets"), "models");
    for (size_t i = 0; yyjson_is_arr(models) && i < yyjson_arr_size(models); ++i)
    {
        yyjson_val *model = yyjson_arr_get(models, i);
        const char *id = json_string(model, "id", NULL);
        const char *path = json_string(model, "path", NULL);
        if (id == NULL || path == NULL)
            continue;
        if (SDL_strcasecmp(editor_basename(path), filename) == 0)
        {
            SDL_strlcpy(buffer, id, buffer_size);
            return buffer != NULL && buffer[0] != '\0';
        }
    }
    return false;
}

static bool editor_actor_scan_list_append_model_source(editor_actor_scan_list *list,
                                                       slayer3d_game_data_runtime *runtime, const char *source_path,
                                                       const char *relative_directory, const char *relative_file,
                                                       const char *model_prefix)
{
    const char *filename = editor_basename(relative_file);
    if (list == NULL || source_path == NULL || relative_file == NULL || !editor_model_extension_supported(filename))
        return true;

    char slug[96];
    char label[96];
    char model[128];
    editor_texture_slug_from_filename(filename, slug, sizeof(slug));
    editor_texture_label_from_filename(filename, label, sizeof(label));
    if (!editor_model_asset_id_for_filename(runtime, filename, model, sizeof(model)))
        SDL_snprintf(model, sizeof(model), "%s%s", model_prefix != NULL ? model_prefix : "model.project.actor.", slug);
    if (editor_actor_scan_model_id_used(list, model))
        return true;

    editor_actor_scan_entry *entry = NULL;
    if (!editor_actor_scan_list_append_blank(list, &entry))
        return false;
    entry->filename = SDL_strdup(filename);
    entry->path = SDL_strdup(source_path);
    entry->relative_path = editor_path_join(relative_directory != NULL ? relative_directory : "", relative_file);
    SDL_strlcpy(entry->id, slug, sizeof(entry->id));
    SDL_strlcpy(entry->label, label, sizeof(entry->label));
    SDL_strlcpy(entry->mesh, "box", sizeof(entry->mesh));
    SDL_strlcpy(entry->model, model, sizeof(entry->model));
    SDL_snprintf(entry->name_prefix, sizeof(entry->name_prefix), "actor.editor_shell.%s", slug);
    SDL_strlcpy(entry->group, "Meshes", sizeof(entry->group));
    SDL_strlcpy(entry->tool_mode, "actor_model", sizeof(entry->tool_mode));
    SDL_strlcpy(entry->classname, slug, sizeof(entry->classname));
    SDL_strlcpy(entry->role, "actor", sizeof(entry->role));
    SDL_strlcpy(entry->sensor_profile, "model", sizeof(entry->sensor_profile));
    entry->color = (slayer3d_color){112, 178, 255, 210};
    entry->scale = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    entry->sort_order = 5000 + list->count;
    entry->scanned_model = true;
    if (entry->filename == NULL || entry->path == NULL || entry->relative_path == NULL)
    {
        SDL_free(entry->filename);
        SDL_free(entry->path);
        SDL_free(entry->relative_path);
        --list->count;
        SDL_zero(*entry);
        return false;
    }
    return true;
}

static bool editor_actor_scan_list_append_model(editor_actor_scan_list *list, slayer3d_game_data_runtime *runtime,
                                                const char *directory, const char *relative_directory,
                                                const char *relative_file, const char *model_prefix)
{
    if (directory == NULL || relative_file == NULL)
        return true;
    char *source_path = editor_path_join(directory, editor_basename(relative_file));
    const bool ok = source_path != NULL &&
                    editor_actor_scan_list_append_model_source(list, runtime, source_path, relative_directory,
                                                               relative_file, model_prefix);
    SDL_free(source_path);
    return ok;
}

typedef struct editor_actor_enumerate_context
{
    editor_actor_scan_list *list;
    slayer3d_game_data_runtime *runtime;
    const char *relative_directory;
    const char *relative_subdirectory;
    const char *model_prefix;
    int depth;
    bool ok;
} editor_actor_enumerate_context;

static bool editor_scan_actor_model_directory_recursive(slayer3d_game_data_runtime *runtime, const char *directory,
                                                        const char *relative_directory,
                                                        const char *relative_subdirectory, const char *model_prefix,
                                                        editor_actor_scan_list *list, int depth);

static SDL_EnumerationResult SDLCALL editor_actor_enumerate_directory_entry(void *userdata, const char *dirname,
                                                                            const char *fname)
{
    editor_actor_enumerate_context *ctx = (editor_actor_enumerate_context *)userdata;
    if (ctx == NULL || ctx->list == NULL)
        return SDL_ENUM_FAILURE;
    char *path = editor_path_join(dirname, fname);
    SDL_PathInfo file_info;
    SDL_zero(file_info);
    if (path == NULL || !SDL_GetPathInfo(path, &file_info))
    {
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }
    if (file_info.type == SDL_PATHTYPE_DIRECTORY)
    {
        char *child_relative = editor_path_join(ctx->relative_subdirectory, fname);
        if (child_relative == NULL ||
            !editor_scan_actor_model_directory_recursive(ctx->runtime, path, ctx->relative_directory, child_relative,
                                                         ctx->model_prefix, ctx->list, ctx->depth + 1))
        {
            SDL_free(child_relative);
            SDL_free(path);
            ctx->ok = false;
            return SDL_ENUM_FAILURE;
        }
        SDL_free(child_relative);
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }
    if (file_info.type != SDL_PATHTYPE_FILE)
    {
        SDL_free(path);
        return SDL_ENUM_CONTINUE;
    }
    char *relative_file = editor_path_join(ctx->relative_subdirectory, fname);
    if (relative_file == NULL ||
        !editor_actor_scan_list_append_model(ctx->list, ctx->runtime, dirname, ctx->relative_directory, relative_file,
                                             ctx->model_prefix))
    {
        SDL_free(relative_file);
        SDL_free(path);
        ctx->ok = false;
        return SDL_ENUM_FAILURE;
    }
    SDL_free(relative_file);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static bool editor_scan_actor_model_directory_recursive(slayer3d_game_data_runtime *runtime, const char *directory,
                                                        const char *relative_directory,
                                                        const char *relative_subdirectory, const char *model_prefix,
                                                        editor_actor_scan_list *list, int depth)
{
    if (directory == NULL || directory[0] == '\0' || list == NULL || depth > 32)
        return false;

    SDL_PathInfo info;
    SDL_zero(info);
    if (!SDL_GetPathInfo(directory, &info) || info.type != SDL_PATHTYPE_DIRECTORY)
        return false;

    editor_actor_enumerate_context enumerate_ctx = {
        list,  runtime, relative_directory, relative_subdirectory != NULL ? relative_subdirectory : "", model_prefix,
        depth, true};
    return SDL_EnumerateDirectory(directory, editor_actor_enumerate_directory_entry, &enumerate_ctx) &&
           enumerate_ctx.ok;
}

typedef struct editor_asset_actor_enumerate_context
{
    editor_actor_scan_list *list;
    slayer3d_game_data_runtime *runtime;
    const char *relative_directory;
    const char *relative_subdirectory;
    const char *model_prefix;
    int depth;
    bool ok;
} editor_asset_actor_enumerate_context;

static bool editor_scan_actor_model_asset_directory_recursive(
    slayer3d_game_data_runtime *runtime, const char *asset_directory, const char *relative_directory,
    const char *relative_subdirectory, const char *model_prefix, editor_actor_scan_list *list, int depth);

static slayer3d_asset_enumeration_result editor_asset_actor_enumerate_entry(void *userdata, const char *directory,
                                                                            const char *name,
                                                                            slayer3d_asset_entry_type type)
{
    editor_asset_actor_enumerate_context *ctx = (editor_asset_actor_enumerate_context *)userdata;
    if (ctx == NULL || ctx->runtime == NULL || ctx->list == NULL)
        return SLAYER3D_ASSET_ENUM_FAILURE;

    char *child_asset_path = editor_asset_path_join(directory, name);
    if (child_asset_path == NULL)
    {
        ctx->ok = false;
        return SLAYER3D_ASSET_ENUM_FAILURE;
    }
    if (type == SLAYER3D_ASSET_ENTRY_DIRECTORY)
    {
        char *child_relative = editor_asset_path_join(ctx->relative_subdirectory, name);
        const bool ok = child_relative != NULL && editor_scan_actor_model_asset_directory_recursive(
                                                      ctx->runtime, child_asset_path, ctx->relative_directory,
                                                      child_relative, ctx->model_prefix, ctx->list, ctx->depth + 1);
        SDL_free(child_relative);
        SDL_free(child_asset_path);
        if (!ok)
        {
            ctx->ok = false;
            return SLAYER3D_ASSET_ENUM_FAILURE;
        }
        return SLAYER3D_ASSET_ENUM_CONTINUE;
    }
    if (type == SLAYER3D_ASSET_ENTRY_FILE)
    {
        char *relative_file = editor_asset_path_join(ctx->relative_subdirectory, name);
        const bool ok = relative_file != NULL && editor_actor_scan_list_append_model_source(
                                                     ctx->list, ctx->runtime, child_asset_path, ctx->relative_directory,
                                                     relative_file, ctx->model_prefix);
        SDL_free(relative_file);
        SDL_free(child_asset_path);
        if (!ok)
        {
            ctx->ok = false;
            return SLAYER3D_ASSET_ENUM_FAILURE;
        }
        return SLAYER3D_ASSET_ENUM_CONTINUE;
    }
    SDL_free(child_asset_path);
    return SLAYER3D_ASSET_ENUM_CONTINUE;
}

static bool editor_scan_actor_model_asset_directory_recursive(
    slayer3d_game_data_runtime *runtime, const char *asset_directory, const char *relative_directory,
    const char *relative_subdirectory, const char *model_prefix, editor_actor_scan_list *list, int depth)
{
    if (runtime == NULL || runtime->assets == NULL || asset_directory == NULL || asset_directory[0] == '\0' ||
        list == NULL || depth > 32)
    {
        return false;
    }
    editor_asset_actor_enumerate_context enumerate_ctx = {
        list,  runtime, relative_directory, relative_subdirectory != NULL ? relative_subdirectory : "", model_prefix,
        depth, true};
    char error[256];
    error[0] = '\0';
    return slayer3d_asset_resolver_enumerate(runtime->assets, asset_directory, editor_asset_actor_enumerate_entry,
                                             &enumerate_ctx, error, (int)sizeof(error)) &&
           enumerate_ctx.ok;
}

static bool editor_scan_actor_model_source(slayer3d_game_data_runtime *runtime, const char *directory,
                                           const char *relative_directory, const char *model_prefix,
                                           editor_actor_scan_list *list)
{
    if (editor_path_asset_uri(directory))
        return editor_scan_actor_model_asset_directory_recursive(runtime, directory, relative_directory, "",
                                                                 model_prefix, list, 0);
    return editor_scan_actor_model_directory_recursive(runtime, directory, relative_directory, "", model_prefix, list,
                                                       0);
}

static bool editor_actor_collection_publish_row(slayer3d_game_data_runtime *runtime, const char *collection,
                                                int row_index, const editor_actor_scan_entry *entry)
{
    if (runtime == NULL || collection == NULL || entry == NULL)
        return false;
    return slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "id", entry->id) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "label", entry->label) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "mesh", entry->mesh) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "model", entry->model) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "name_prefix",
                                                            entry->name_prefix) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "archetype",
                                                            entry->archetype) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "group", entry->group) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "tool_mode",
                                                            entry->tool_mode) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "classname",
                                                            entry->classname) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "role", entry->role) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "sensor_profile",
                                                            entry->sensor_profile) &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "path",
                                                            entry->path != NULL ? entry->path : "") &&
           slayer3d_game_data_runtime_collection_set_string(runtime, collection, row_index, "relative_path",
                                                            entry->relative_path != NULL ? entry->relative_path : "") &&
           slayer3d_game_data_runtime_collection_set_int(runtime, collection, row_index, "color_r", entry->color.r) &&
           slayer3d_game_data_runtime_collection_set_int(runtime, collection, row_index, "color_g", entry->color.g) &&
           slayer3d_game_data_runtime_collection_set_int(runtime, collection, row_index, "color_b", entry->color.b) &&
           slayer3d_game_data_runtime_collection_set_int(runtime, collection, row_index, "color_a", entry->color.a) &&
           slayer3d_game_data_runtime_collection_set_float(runtime, collection, row_index, "scale_x", entry->scale.x) &&
           slayer3d_game_data_runtime_collection_set_float(runtime, collection, row_index, "scale_y", entry->scale.y) &&
           slayer3d_game_data_runtime_collection_set_float(runtime, collection, row_index, "scale_z", entry->scale.z) &&
           slayer3d_game_data_runtime_collection_set_bool(runtime, collection, row_index, "scanned_model",
                                                          entry->scanned_model);
}

static void editor_actor_clear_slots(slayer3d_properties *scene_state, int slot_count)
{
    for (int i = 0; scene_state != NULL && i < slot_count; ++i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.id", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.label", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.model", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.group", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.available", i);
        slayer3d_properties_set_bool(scene_state, key, false);
    }
}

static void editor_actor_publish_slot(slayer3d_properties *scene_state, int slot_index,
                                      const editor_actor_scan_entry *entry)
{
    if (scene_state == NULL || entry == NULL)
        return;
    char key[128];
    SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.id", slot_index);
    slayer3d_properties_set_string(scene_state, key, entry->id);
    SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.label", slot_index);
    slayer3d_properties_set_string(scene_state, key, entry->label);
    SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.model", slot_index);
    slayer3d_properties_set_string(scene_state, key, entry->model);
    SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.group", slot_index);
    slayer3d_properties_set_string(scene_state, key, entry->group);
    SDL_snprintf(key, sizeof(key), "editor.actor.slot.%d.available", slot_index);
    slayer3d_properties_set_bool(scene_state, key, true);
}

static const slayer3d_value *editor_actor_collection_value(const runtime_collection *collection, int row_index,
                                                           const char *field)
{
    if (collection == NULL || row_index < 0 || row_index >= collection->row_count || field == NULL ||
        collection->rows[row_index] == NULL)
    {
        return NULL;
    }
    return slayer3d_properties_get_value(collection->rows[row_index], field);
}

static int editor_actor_collection_int(const runtime_collection *collection, int row_index, const char *field,
                                       int fallback)
{
    const slayer3d_value *value = editor_actor_collection_value(collection, row_index, field);
    if (value == NULL)
        return fallback;
    if (value->type == SLAYER3D_VALUE_INT)
        return value->as_int;
    if (value->type == SLAYER3D_VALUE_FLOAT)
        return (int)value->as_float;
    return fallback;
}

static float editor_actor_collection_float(const runtime_collection *collection, int row_index, const char *field,
                                           float fallback)
{
    const slayer3d_value *value = editor_actor_collection_value(collection, row_index, field);
    if (value == NULL)
        return fallback;
    if (value->type == SLAYER3D_VALUE_FLOAT)
        return value->as_float;
    if (value->type == SLAYER3D_VALUE_INT)
        return (float)value->as_int;
    return fallback;
}

static bool editor_actor_select_collection_row(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                               int index, bool activate_tool)
{
    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    char id[128];
    char label[128];
    char tool_mode[64];
    if (!runtime_collection_field_to_string(collection, index, "id", id, sizeof(id)) || id[0] == '\0')
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "actor slot is empty");
        return true;
    }
    if (!runtime_collection_field_to_string(collection, index, "label", label, sizeof(label)))
        SDL_strlcpy(label, "actor", sizeof(label));
    if (!runtime_collection_field_to_string(collection, index, "tool_mode", tool_mode, sizeof(tool_mode)) ||
        tool_mode[0] == '\0')
    {
        SDL_strlcpy(tool_mode, "actor_selected", sizeof(tool_mode));
    }

    slayer3d_properties_set_int(runtime->scene_state, "editor.actor.selected_index", index);
    slayer3d_properties_set_string(runtime->scene_state, "editor.actor.selected", id);
    slayer3d_properties_set_string(runtime->scene_state, "editor.actor.selected.label", label);
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.game_object.cursor", id);
    if (activate_tool)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.mode", "thing");
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", tool_mode);
        char message[192];
        SDL_snprintf(message, sizeof(message), "selected actor %s", label);
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    return true;
}

static bool execute_editor_actor_scan_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    yyjson_val *outputs = obj_get(action, "outputs");
    const char *directory_key = json_string(action, "directory_key", "editor.asset_source.models.path");
    const char *relative_directory_key =
        json_string(action, "relative_directory_key", "editor.asset_source.models.relative");
    const char *fallback_directory = json_string(action, "directory", "models");
    const char *fallback_relative_directory = json_string(action, "relative_directory", "models");
    const char *collection = json_string(action, "collection", "editor.actors");
    const char *model_prefix = json_string(action, "model_prefix", "model.project.actor.");
    const int slot_count = SDL_max(0, json_int(action, "slot_count", 6));
    const char *directory = slayer3d_properties_get_string(runtime->scene_state, directory_key, "");
    const char *relative_directory = slayer3d_properties_get_string(runtime->scene_state, relative_directory_key, "");
    const char *model_search_query =
        slayer3d_properties_get_string(runtime->scene_state, "editor.actor.model.search", "");
    const bool model_search_active = model_search_query != NULL && model_search_query[0] != '\0';
    char *resolved_fallback_directory = NULL;
    char *resolved_model_directory = NULL;
    if (directory == NULL || directory[0] == '\0')
    {
        resolved_fallback_directory = editor_resolve_directory(runtime, fallback_directory);
        directory = resolved_fallback_directory != NULL ? resolved_fallback_directory : "";
    }
    else if (!editor_path_asset_uri(directory) && !editor_path_absolute(directory))
    {
        resolved_model_directory = editor_path_make_absolute_from_cwd(directory);
        SDL_PathInfo model_info;
        SDL_zero(model_info);
        if (resolved_model_directory == NULL || !SDL_GetPathInfo(resolved_model_directory, &model_info) ||
            model_info.type != SDL_PATHTYPE_DIRECTORY)
        {
            SDL_free(resolved_model_directory);
            resolved_model_directory = editor_resolve_directory(runtime, directory);
        }
        directory = resolved_model_directory != NULL ? resolved_model_directory : directory;
    }
    if (relative_directory == NULL || relative_directory[0] == '\0')
        relative_directory = fallback_relative_directory != NULL ? fallback_relative_directory : "";

    editor_actor_clear_slots(runtime->scene_state, slot_count);
    slayer3d_game_data_runtime_collection_clear(runtime, collection);

    editor_actor_scan_list list;
    SDL_zero(list);
    bool ok = editor_actor_scan_list_append_builtin(
        &list, "player_capsule", "Player", "capsule", "", "actor.editor_shell.player",
        "actor.editor_shell.player_placeholder", "Placeholders", "actor_player", "player_start_placeholder", "player",
        "player", (slayer3d_color){80, 235, 130, 205}, slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 0);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "opponent_rectangle", "Opponent", "rectangle", "", "actor.editor_shell.opponent",
                   "actor.editor_shell.opponent_placeholder", "Placeholders", "actor_opponent", "opponent_placeholder",
                   "opponent", "enemy", (slayer3d_color){235, 128, 84, 190}, slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 1);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "trigger_volume", "Trigger", "box", "", "actor.editor_shell.trigger",
                   "actor.editor_shell.trigger_volume", "Placeholders", "actor_trigger", "trigger_volume", "trigger",
                   "volume", (slayer3d_color){80, 220, 255, 135}, slayer3d_vec3_make(2.0f, 1.0f, 2.0f), 2);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "sensor_volume", "Sensor", "box", "", "actor.editor_shell.sensor",
                   "actor.editor_shell.sensor_volume", "Placeholders", "actor_sensor", "sensor_volume", "sensor",
                   "volume", (slayer3d_color){210, 120, 255, 135}, slayer3d_vec3_make(2.0f, 1.0f, 2.0f), 3);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "simple_robot", "Robot", "box", "model.editor_shell.simple_robot", "actor.editor_shell.robot",
                   "actor.editor_shell.simple_robot", "Meshes", "actor_robot", "simple_robot", "actor", "robot",
                   (slayer3d_color){112, 178, 255, 210}, slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 4);
    ok = ok && editor_actor_scan_list_append_builtin(&list, "object_box", "Box", "box", "", "object.editor_shell.box",
                                                     "object.editor_shell.box", "Objects", "actor_object", "object_box",
                                                     "object", "box", (slayer3d_color){128, 174, 245, 190},
                                                     slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 1000);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "object_capsule", "Capsule", "capsule", "", "object.editor_shell.capsule",
                   "object.editor_shell.capsule", "Objects", "actor_object", "object_capsule", "object", "capsule",
                   (slayer3d_color){80, 235, 130, 255}, slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 1001);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "object_sphere", "Sphere", "sphere", "", "object.editor_shell.sphere",
                   "object.editor_shell.sphere", "Objects", "actor_object", "object_sphere", "object", "sphere",
                   (slayer3d_color){198, 158, 245, 190}, slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 1002);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "object_rectangle", "Rectangle", "rectangle", "", "object.editor_shell.rectangle",
                   "object.editor_shell.rectangle", "Objects", "actor_object", "object_rectangle", "object",
                   "rectangle", (slayer3d_color){230, 166, 108, 190}, slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 1003);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "light_point", "Point", "sphere", "", "light.editor_shell.point", "light.editor_shell.point",
                   "Lights", "actor_light", "point_light", "light", "point", (slayer3d_color){255, 230, 112, 220},
                   slayer3d_vec3_make(0.65f, 0.65f, 0.65f), 1501);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "light_spot", "Spot", "sphere", "", "light.editor_shell.spot", "light.editor_shell.spot",
                   "Lights", "actor_light", "spot_light", "light", "spot", (slayer3d_color){255, 208, 130, 210},
                   slayer3d_vec3_make(0.75f, 0.75f, 0.75f), 1502);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "light_area_rect", "Area Rect", "rectangle", "", "light.editor_shell.area_rect",
                   "light.editor_shell.area_rect", "Lights", "actor_light", "area_rect_light", "light", "area_rect",
                   (slayer3d_color){255, 222, 178, 195}, slayer3d_vec3_make(1.5f, 0.2f, 1.5f), 1503);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "light_area_sphere", "Area Sphere", "sphere", "", "light.editor_shell.area_sphere",
                   "light.editor_shell.area_sphere", "Lights", "actor_light", "area_sphere_light", "light",
                   "area_sphere", (slayer3d_color){210, 228, 255, 185}, slayer3d_vec3_make(0.9f, 0.9f, 0.9f), 1504);
    ok = ok &&
         editor_actor_scan_list_append_builtin(
             &list, "particle_emitter", "Particle Emitter", "sphere", "", "actor.editor_shell.effect.particles",
             "actor.editor_shell.particle_emitter", "Effects", "actor_effect", "particle_emitter", "effect",
             "particle_emitter", (slayer3d_color){255, 214, 92, 180}, slayer3d_vec3_make(0.65f, 0.65f, 0.65f), 2000);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "fog_volume", "Fog Volume", "box", "", "actor.editor_shell.effect.fog",
                   "actor.editor_shell.fog_volume", "Effects", "actor_effect", "fog_volume", "effect", "fog_volume",
                   (slayer3d_color){138, 190, 255, 96}, slayer3d_vec3_make(3.0f, 1.5f, 3.0f), 2001);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "fire_marker", "Fire", "sphere", "", "actor.editor_shell.effect.fire",
                   "actor.editor_shell.fire_marker", "Effects", "actor_effect", "fire_marker", "effect", "fire",
                   (slayer3d_color){255, 112, 48, 210}, slayer3d_vec3_make(0.6f, 0.9f, 0.6f), 2002);
    ok = ok && editor_actor_scan_list_append_builtin(
                   &list, "smoke_marker", "Smoke", "sphere", "", "actor.editor_shell.effect.smoke",
                   "actor.editor_shell.smoke_marker", "Effects", "actor_effect", "smoke_marker", "effect", "smoke",
                   (slayer3d_color){148, 154, 166, 165}, slayer3d_vec3_make(0.8f, 0.8f, 0.8f), 2003);
    const bool scanned = editor_scan_actor_model_source(runtime, directory, relative_directory, model_prefix, &list);
    int scanned_model_count = 0;
    for (int i = 0; i < list.count; ++i)
    {
        if (list.entries[i].scanned_model)
            ++scanned_model_count;
    }
    if (ok && list.count > 1)
        SDL_qsort(list.entries, (size_t)list.count, sizeof(list.entries[0]), editor_actor_scan_entry_compare);

    int published_count = 0;
    int filtered_model_count = 0;
    int selected_index = -1;
    const char *current_actor = slayer3d_properties_get_string(runtime->scene_state, "editor.actor.selected", "");
    for (int i = 0; ok && i < list.count; ++i)
    {
        const editor_actor_scan_entry *entry = &list.entries[i];
        if (!editor_actor_scan_entry_matches_model_search(entry, model_search_query))
            continue;
        if (entry->scanned_model)
            ++filtered_model_count;
        ok = editor_actor_collection_publish_row(runtime, collection, published_count, entry);
        if (!ok)
            break;
        if (published_count < slot_count)
            editor_actor_publish_slot(runtime->scene_state, published_count, entry);
        if (current_actor != NULL && current_actor[0] != '\0' && SDL_strcmp(current_actor, entry->id) == 0)
            selected_index = published_count;
        ++published_count;
    }

    if (ok && published_count > 0)
    {
        if (selected_index < 0)
            selected_index = 0;
        (void)editor_actor_select_collection_row(runtime, collection, selected_index, false);
    }
    else
    {
        slayer3d_properties_set_int(runtime->scene_state, "editor.actor.selected_index", -1);
        slayer3d_properties_set_string(runtime->scene_state, "editor.actor.selected", "");
    }

    char status[128];
    if (!ok)
        SDL_strlcpy(status, "actor browser allocation failed", sizeof(status));
    else if (model_search_active && scanned)
        SDL_snprintf(status, sizeof(status), "loaded %d actor%s (%d matching project model%s)", published_count,
                     published_count == 1 ? "" : "s", filtered_model_count, filtered_model_count == 1 ? "" : "s");
    else
        SDL_snprintf(status, sizeof(status), "loaded %d actor%s%s", published_count, published_count == 1 ? "" : "s",
                     scanned ? "" : " (model directory unavailable)");
    slayer3d_properties_set_int(runtime->scene_state, "editor.actor.browser.count", ok ? published_count : 0);
    slayer3d_properties_set_string(runtime->scene_state, "editor.actor.scan.status", status);
    editor_actor_set_model_empty_state(runtime->scene_state, scanned_model_count, filtered_model_count, scanned,
                                       model_search_active);
    if (!scanned)
        editor_asset_source_set_diagnostic(runtime->scene_state, "models", "model directory unavailable", false, false);
    else if (scanned_model_count == 0)
        editor_asset_source_set_diagnostic(runtime->scene_state, "models", "no models found", true, true);
    else
    {
        char model_status[96];
        SDL_snprintf(model_status, sizeof(model_status), "loaded %d model%s", scanned_model_count,
                     scanned_model_count == 1 ? "" : "s");
        editor_asset_source_set_diagnostic(runtime->scene_state, "models", model_status, true, false);
    }
    editor_set_int_output(runtime->scene_state, outputs, "count_key", ok ? published_count : 0);
    editor_set_string_output(runtime->scene_state, outputs, "status_key", status);
    editor_actor_scan_list_free(&list);
    SDL_free(resolved_fallback_directory);
    SDL_free(resolved_model_directory);
    return true;
}

static bool execute_editor_actor_select_index_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    const char *collection_name = json_string(action, "collection", "editor.actors");
    const char *index_key = json_string(action, "index_key", NULL);
    const int index = index_key != NULL ? slayer3d_properties_get_int(runtime->scene_state, index_key, 0)
                                        : json_int(action, "index", 0);
    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    char model[128];
    if (runtime_collection_field_to_string(collection, index, "model", model, sizeof(model)) && model[0] != '\0')
    {
        char warmup_key[192];
        SDL_snprintf(warmup_key, sizeof(warmup_key), "asset_warmup.model.%s.pending", model);
        if (slayer3d_properties_get_bool(runtime->scene_state, warmup_key, false))
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "actor model loading");
            return true;
        }
        SDL_snprintf(warmup_key, sizeof(warmup_key), "asset_warmup.model.%s.failed", model);
        if (slayer3d_properties_get_bool(runtime->scene_state, warmup_key, false))
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "actor model unavailable");
            return true;
        }
    }
    return editor_actor_select_collection_row(runtime, collection_name, index, true);
}

#define EDITOR_GLOBAL_PROPERTY_CAP 64

static bool editor_global_property_text_blank(const char *text)
{
    if (text == NULL)
        return true;
    while (*text != '\0')
    {
        if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n')
            return false;
        ++text;
    }
    return true;
}

static int editor_global_property_count(const slayer3d_properties *scene_state)
{
    int count = slayer3d_properties_get_int(scene_state, "editor.global.property.count", 0);
    if (count < 0)
        count = 0;
    if (count > EDITOR_GLOBAL_PROPERTY_CAP)
        count = EDITOR_GLOBAL_PROPERTY_CAP;
    return count;
}

static void editor_global_property_key(char *buffer, size_t buffer_size, int index, const char *suffix)
{
    SDL_snprintf(buffer, buffer_size, "editor.global.property.%d.%s", index, suffix != NULL ? suffix : "");
}

static int editor_global_property_find(const slayer3d_properties *scene_state, const char *key)
{
    if (editor_global_property_text_blank(key))
        return -1;
    const int count = editor_global_property_count(scene_state);
    char state_key[96];
    for (int i = 0; i < count; ++i)
    {
        editor_global_property_key(state_key, sizeof(state_key), i, "key");
        if (SDL_strcmp(slayer3d_properties_get_string(scene_state, state_key, ""), key) == 0)
            return i;
    }
    return -1;
}

static void editor_global_property_publish_slots(slayer3d_properties *scene_state)
{
    if (scene_state == NULL)
        return;
    const int count = editor_global_property_count(scene_state);
    slayer3d_properties_set_int(scene_state, "editor.global.property.count", count);
    for (int i = 0; i < EDITOR_GLOBAL_PROPERTY_CAP; ++i)
    {
        char key[96];
        editor_global_property_key(key, sizeof(key), i, "available");
        slayer3d_properties_set_bool(scene_state, key, i < count);
        if (i >= count)
        {
            editor_global_property_key(key, sizeof(key), i, "key");
            slayer3d_properties_set_string(scene_state, key, "");
            editor_global_property_key(key, sizeof(key), i, "value");
            slayer3d_properties_set_string(scene_state, key, "");
        }
    }
}

static bool execute_editor_global_property_new_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.key", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.value", "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.focus", "key");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.global.data.edit.replace_on_text", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.global.data.edit.selected_slot", -1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.message", "new global property");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "new global property");
    return true;
}

static bool execute_editor_global_property_apply_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    slayer3d_properties *scene_state = runtime->scene_state;
    const char *key = slayer3d_properties_get_string(scene_state, "editor.global.data.edit.key", "");
    const char *value = slayer3d_properties_get_string(scene_state, "editor.global.data.edit.value", "");
    if (editor_global_property_text_blank(key) || editor_global_property_text_blank(value))
    {
        slayer3d_properties_set_string(scene_state, "editor.global.data.message", "key and value are required");
        slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                       "global property requires key and value");
        return true;
    }

    int count = editor_global_property_count(scene_state);
    int slot = slayer3d_properties_get_int(scene_state, "editor.global.data.edit.selected_slot", -1);
    const int existing = editor_global_property_find(scene_state, key);
    if (slot < 0)
        slot = existing;
    if (slot < 0)
    {
        if (count >= EDITOR_GLOBAL_PROPERTY_CAP)
        {
            slayer3d_properties_set_string(scene_state, "editor.global.data.message", "global property cap reached");
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action", "global property cap reached");
            return true;
        }
        slot = count++;
        slayer3d_properties_set_int(scene_state, "editor.global.property.count", count);
    }
    else if (existing >= 0 && existing != slot)
    {
        slayer3d_properties_set_string(scene_state, "editor.global.data.message", "duplicate global property key");
        slayer3d_properties_set_string(scene_state, "editor.tool.last_action", "duplicate global property key");
        return true;
    }

    char state_key[96];
    editor_global_property_key(state_key, sizeof(state_key), slot, "key");
    slayer3d_properties_set_string(scene_state, state_key, key);
    editor_global_property_key(state_key, sizeof(state_key), slot, "value");
    slayer3d_properties_set_string(scene_state, state_key, value);
    editor_global_property_publish_slots(scene_state);
    slayer3d_properties_set_string(scene_state, "editor.global.data.edit.key", "");
    slayer3d_properties_set_string(scene_state, "editor.global.data.edit.value", "");
    slayer3d_properties_set_string(scene_state, "editor.global.data.edit.focus", "key");
    slayer3d_properties_set_bool(scene_state, "editor.global.data.edit.replace_on_text", false);
    slayer3d_properties_set_int(scene_state, "editor.global.data.edit.selected_slot", -1);
    slayer3d_properties_set_string(scene_state, "editor.global.data.message", "global property saved");
    slayer3d_properties_set_string(scene_state, "editor.tool.last_action", "global property saved");
    return true;
}

static bool execute_editor_global_property_select_slot_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const int slot = json_int(action, "slot", -1);
    if (slot < 0 || slot >= editor_global_property_count(runtime->scene_state))
        return true;
    char state_key[96];
    editor_global_property_key(state_key, sizeof(state_key), slot, "key");
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.key",
                                   slayer3d_properties_get_string(runtime->scene_state, state_key, ""));
    editor_global_property_key(state_key, sizeof(state_key), slot, "value");
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.value",
                                   slayer3d_properties_get_string(runtime->scene_state, state_key, ""));
    slayer3d_properties_set_int(runtime->scene_state, "editor.global.data.edit.selected_slot", slot);
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.focus",
                                   json_string(action, "focus", "value"));
    slayer3d_properties_set_bool(runtime->scene_state, "editor.global.data.edit.replace_on_text", true);
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.message", "editing global property");
    return true;
}

static bool execute_editor_global_property_remove_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const int slot = json_int(action, "slot", -1);
    int count = editor_global_property_count(runtime->scene_state);
    if (slot < 0 || slot >= count)
        return true;

    for (int i = slot; i + 1 < count; ++i)
    {
        char dst[96];
        char src[96];
        editor_global_property_key(dst, sizeof(dst), i, "key");
        editor_global_property_key(src, sizeof(src), i + 1, "key");
        slayer3d_properties_set_string(runtime->scene_state, dst,
                                       slayer3d_properties_get_string(runtime->scene_state, src, ""));
        editor_global_property_key(dst, sizeof(dst), i, "value");
        editor_global_property_key(src, sizeof(src), i + 1, "value");
        slayer3d_properties_set_string(runtime->scene_state, dst,
                                       slayer3d_properties_get_string(runtime->scene_state, src, ""));
    }
    --count;
    slayer3d_properties_set_int(runtime->scene_state, "editor.global.property.count", count);
    editor_global_property_publish_slots(runtime->scene_state);
    slayer3d_properties_set_int(runtime->scene_state, "editor.global.data.edit.selected_slot", -1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.message", "global property deleted");
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "global property deleted");
    return true;
}

static void editor_actor_publish_place_outputs(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool ok,
                                               const char *message, const char *name)
{
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_string_output(scene_state, outputs, "actor_key", ok ? name : "");
    editor_set_bool_output(scene_state, outputs, "dirty_key", runtime != NULL ? runtime->editor_actor_dirty : false);
    editor_set_int_output(scene_state, outputs, "revision_key",
                          runtime != NULL ? (int)runtime->editor_actor_revision : 0);
    editor_set_int_output(scene_state, outputs, "count_key", runtime != NULL ? runtime->editor_actor_count : 0);
}

static slayer3d_bounding_box editor_actor_selection_bounds(const editor_actor_runtime *actor)
{
    slayer3d_vec3 scale = actor != NULL ? actor->scale : slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    slayer3d_vec3 half = slayer3d_vec3_make(0.5f * scale.x, 0.5f * scale.y, 0.5f * scale.z);
    const char *mesh = actor != NULL ? actor->mesh : NULL;
    if (mesh != NULL && SDL_strcmp(mesh, "capsule") == 0)
        half = slayer3d_vec3_make(0.35f * scale.x, 0.9f * scale.y, 0.35f * scale.z);
    else if (mesh != NULL && SDL_strcmp(mesh, "rectangle") == 0)
        half = slayer3d_vec3_make(0.45f * scale.x, 0.9f * scale.y, 0.25f * scale.z);

    slayer3d_bounding_box bounds;
    const slayer3d_vec3 position = actor != NULL ? actor->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    bounds.min = slayer3d_vec3_make(position.x - half.x, position.y, position.z - half.z);
    bounds.max = slayer3d_vec3_make(position.x + half.x, position.y + half.y * 2.0f, position.z + half.z);
    return bounds;
}

static void editor_select_placed_actor(slayer3d_game_data_runtime *runtime, const char *actor_name)
{
    if (runtime == NULL || actor_name == NULL || actor_name[0] == '\0')
        return;
    const editor_actor_runtime *actor = find_editor_actor(runtime, actor_name);
    if (actor == NULL)
        return;

    clear_editor_selected_brushes(runtime);
    init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_active_selection.hit = true;
    runtime->editor_active_selection.type = SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR;
    runtime->editor_active_selection.world_name = "editor_actors";
    runtime->editor_active_selection.element_name = actor->name;
    runtime->editor_active_selection.element_index = (int)(actor - runtime->editor_actors);
    runtime->editor_active_selection.face_index = -1;
    runtime->editor_active_selection.point = actor->position;
    runtime->editor_active_selection.normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    runtime->editor_active_selection.bounds = editor_actor_selection_bounds(actor);
    runtime->editor_active_selection.has_bounds = true;
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_brush_count(runtime);
}

static const char *editor_actor_placement_noun(const char *role)
{
    if (role != NULL && SDL_strcmp(role, "object") == 0)
        return "object";
    if (role != NULL && SDL_strcmp(role, "light") == 0)
        return "light";
    if (role != NULL && SDL_strcmp(role, "effect") == 0)
        return "effect";
    return "actor";
}

static bool execute_editor_actor_place_selected_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL || runtime->scene_state == NULL)
    {
        editor_actor_publish_place_outputs(runtime, outputs, false, "actor placement requires runtime", "");
        return false;
    }

    const char *collection_name = json_string(action, "collection", "editor.actors");
    const char *index_key = json_string(action, "index_key", "editor.actor.selected_index");
    const int index = slayer3d_properties_get_int(runtime->scene_state, index_key, 0);
    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    char id[128];
    char label[128];
    char name_prefix[128];
    char scene[128];
    char archetype[128];
    char mesh[64];
    char model[128];
    char group[96];
    char classname[128];
    char role[96];
    char sensor_profile[96];
    char relative_path[256];
    if (!runtime_collection_field_to_string(collection, index, "id", id, sizeof(id)) || id[0] == '\0')
    {
        editor_actor_publish_place_outputs(runtime, outputs, false, "select an actor before placing", "");
        return true;
    }
    (void)runtime_collection_field_to_string(collection, index, "label", label, sizeof(label));
    (void)runtime_collection_field_to_string(collection, index, "name_prefix", name_prefix, sizeof(name_prefix));
    (void)runtime_collection_field_to_string(collection, index, "archetype", archetype, sizeof(archetype));
    (void)runtime_collection_field_to_string(collection, index, "mesh", mesh, sizeof(mesh));
    (void)runtime_collection_field_to_string(collection, index, "model", model, sizeof(model));
    (void)runtime_collection_field_to_string(collection, index, "group", group, sizeof(group));
    (void)runtime_collection_field_to_string(collection, index, "classname", classname, sizeof(classname));
    (void)runtime_collection_field_to_string(collection, index, "role", role, sizeof(role));
    (void)runtime_collection_field_to_string(collection, index, "sensor_profile", sensor_profile,
                                             sizeof(sensor_profile));
    (void)runtime_collection_field_to_string(collection, index, "relative_path", relative_path, sizeof(relative_path));
    SDL_strlcpy(scene, json_string(action, "scene", ""), sizeof(scene));

    slayer3d_properties *properties = slayer3d_properties_create();
    if (properties == NULL)
    {
        editor_actor_publish_place_outputs(runtime, outputs, false, "failed to allocate actor properties", "");
        return true;
    }
    slayer3d_properties_set_string(properties, "classname", classname[0] != '\0' ? classname : id);
    slayer3d_properties_set_string(properties, "role", role[0] != '\0' ? role : "actor");
    if (SDL_strcmp(role, "trigger") == 0 || SDL_strcmp(role, "sensor") == 0)
        slayer3d_properties_set_string(properties, "display_mode", "wireframe");
    else
        slayer3d_properties_set_string(properties, "display_mode", "solid");
    if (sensor_profile[0] != '\0')
        slayer3d_properties_set_string(properties, "sensor_profile", sensor_profile);
    if (SDL_strcmp(role, "light") == 0)
    {
        const char *light_type = sensor_profile[0] != '\0' ? sensor_profile : "point";
        slayer3d_properties_set_string(properties, "light_type", light_type);
        slayer3d_properties_set_string(properties, "light_kind",
                                       SDL_strcmp(light_type, "area_rect") == 0 ? "baked" : "dynamic");
        slayer3d_properties_set_float(properties, "light_intensity", 1.0f);
        slayer3d_properties_set_string(properties, "shadow_mode", "none");
        if (SDL_strcmp(light_type, "directional") == 0)
        {
            slayer3d_properties_set_string(properties, "falloff", "none");
            slayer3d_properties_set_vec3(properties, "light_direction", slayer3d_vec3_make(0.0f, -1.0f, 0.0f));
        }
        else
        {
            slayer3d_properties_set_float(properties, "light_range", 8.0f);
            slayer3d_properties_set_string(properties, "falloff", "inverse_square");
        }
        if (SDL_strcmp(light_type, "spot") == 0)
        {
            slayer3d_properties_set_vec3(properties, "light_direction", slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
            slayer3d_properties_set_float(properties, "inner_angle_degrees", 25.0f);
            slayer3d_properties_set_float(properties, "outer_angle_degrees", 40.0f);
        }
        else if (SDL_strcmp(light_type, "area_rect") == 0)
        {
            slayer3d_properties_set_float(properties, "width", 2.0f);
            slayer3d_properties_set_float(properties, "height", 1.0f);
        }
        else if (SDL_strcmp(light_type, "area_sphere") == 0)
        {
            slayer3d_properties_set_float(properties, "radius", 1.0f);
        }
    }
    else if (SDL_strcmp(role, "effect") == 0)
    {
        slayer3d_properties_set_string(properties, "effect_kind",
                                       sensor_profile[0] != '\0' ? sensor_profile : "effect");
        slayer3d_properties_set_bool(properties, "preview", true);
    }
    slayer3d_properties_set_string(properties, "actor_browser_id", id);
    if (model[0] != '\0')
        slayer3d_properties_set_string(properties, "model", model);
    if (relative_path[0] != '\0')
        slayer3d_properties_set_string(properties, "model_path", relative_path);

    slayer3d_game_data_place_editor_actor_desc desc;
    SDL_zero(desc);
    desc.name_prefix = name_prefix[0] != '\0' ? name_prefix : "actor.editor_shell";
    desc.scene = scene[0] != '\0' ? scene : NULL;
    desc.display_name = label[0] != '\0' ? label : id;
    desc.archetype = archetype[0] != '\0' ? archetype : NULL;
    desc.mesh = mesh[0] != '\0' ? mesh : "box";
    desc.model = model[0] != '\0' ? model : NULL;
    desc.group = group[0] != '\0' ? group : "Actors";
    desc.scale = slayer3d_vec3_make(editor_actor_collection_float(collection, index, "scale_x", 1.0f),
                                    editor_actor_collection_float(collection, index, "scale_y", 1.0f),
                                    editor_actor_collection_float(collection, index, "scale_z", 1.0f));
    desc.has_scale = true;
    desc.color =
        (slayer3d_color){(Uint8)SDL_clamp(editor_actor_collection_int(collection, index, "color_r", 120), 0, 255),
                         (Uint8)SDL_clamp(editor_actor_collection_int(collection, index, "color_g", 200), 0, 255),
                         (Uint8)SDL_clamp(editor_actor_collection_int(collection, index, "color_b", 255), 0, 255),
                         (Uint8)SDL_clamp(editor_actor_collection_int(collection, index, "color_a", 210), 0, 255)};
    desc.has_color = true;
    desc.properties = properties;
    const char *position_from = json_string(action, "position_from", "placement_preview");
    if (SDL_strcmp(position_from, "placement_preview") == 0)
    {
        desc.position = runtime->editor_placement_preview.anchor;
        desc.has_position = editor_placement_preview_active_for_scene(runtime);
    }
    else if (SDL_strcmp(position_from, "selection_point") == 0)
    {
        slayer3d_game_data_editor_selection selection;
        SDL_zero(selection);
        if (slayer3d_game_data_get_active_editor_selection(runtime, &selection) && selection.hit)
        {
            desc.position = selection.point;
            desc.has_position = true;
        }
    }

    char actor_name[128];
    char error[192];
    const bool ok =
        slayer3d_game_data_place_editor_actor(runtime, &desc, actor_name, sizeof(actor_name), error, sizeof(error));
    slayer3d_properties_destroy(properties);
    char message[192];
    if (ok)
    {
        editor_select_placed_actor(runtime, actor_name);
        SDL_snprintf(message, sizeof(message), "%s %s placed", label[0] != '\0' ? label : id,
                     editor_actor_placement_noun(role));
    }
    editor_actor_publish_place_outputs(runtime, outputs, ok, ok ? message : error, ok ? actor_name : "");
    return true;
}

static char *debug_parent_directory(const char *path)
{
    if (path == NULL)
        return NULL;
    const char *slash = SDL_strrchr(path, '/');
    const char *backslash = SDL_strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
    if (slash == NULL)
        return SDL_strdup(".");
    const size_t len = (size_t)(slash - path);
    if (len == 0u)
        return SDL_strdup("/");
    char *parent = (char *)SDL_malloc(len + 1u);
    if (parent == NULL)
        return NULL;
    SDL_memcpy(parent, path, len);
    parent[len] = '\0';
    return parent;
}

static bool debug_ensure_parent_directory(const char *path)
{
    char *parent = debug_parent_directory(path);
    if (parent == NULL)
        return false;
    const bool ok = debug_make_directory_recursive(parent);
    SDL_free(parent);
    return ok;
}

static SDL_IOStream *debug_open_property_dump(const char *path, bool append)
{
    if (path == NULL || path[0] == '\0')
        return NULL;
    if (!debug_ensure_parent_directory(path))
        return NULL;
    if (!append)
        return SDL_IOFromFile(path, "wb");

    SDL_IOStream *stream = SDL_IOFromFile(path, "r+b");
    if (stream == NULL)
        return SDL_IOFromFile(path, "wb");
    if (SDL_SeekIO(stream, 0, SDL_IO_SEEK_END) < 0)
    {
        (void)SDL_CloseIO(stream);
        return NULL;
    }
    return stream;
}

static bool debug_write_property_value(SDL_IOStream *stream, const char *key, const slayer3d_value *value)
{
    char line[256];
    if (stream == NULL || key == NULL)
        return false;
    if (value == NULL)
        SDL_snprintf(line, sizeof(line), "%s=<missing>\n", key);
    else if (value->type == SLAYER3D_VALUE_INT)
        SDL_snprintf(line, sizeof(line), "%s=%d\n", key, value->as_int);
    else if (value->type == SLAYER3D_VALUE_FLOAT)
        SDL_snprintf(line, sizeof(line), "%s=%.6f\n", key, value->as_float);
    else if (value->type == SLAYER3D_VALUE_BOOL)
        SDL_snprintf(line, sizeof(line), "%s=%s\n", key, value->as_bool ? "true" : "false");
    else if (value->type == SLAYER3D_VALUE_STRING)
        SDL_snprintf(line, sizeof(line), "%s=%s\n", key, value->as_string != NULL ? value->as_string : "");
    else if (value->type == SLAYER3D_VALUE_VEC3)
        SDL_snprintf(line, sizeof(line), "%s=[%.6f, %.6f, %.6f]\n", key, value->as_vec3.x, value->as_vec3.y,
                     value->as_vec3.z);
    else if (value->type == SLAYER3D_VALUE_COLOR)
        SDL_snprintf(line, sizeof(line), "%s=[%u, %u, %u, %u]\n", key, value->as_color.r, value->as_color.g,
                     value->as_color.b, value->as_color.a);
    else
        SDL_snprintf(line, sizeof(line), "%s=<unsupported>\n", key);
    return debug_write_all(stream, line);
}

static bool debug_write_actor_properties(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    const char *path = json_string(action, "path", NULL);
    yyjson_val *properties = obj_get(action, "properties");
    if (actor == NULL || path == NULL || path[0] == '\0' || !yyjson_is_arr(properties))
        return false;

    const bool append = json_bool(action, "append", false);
    SDL_IOStream *stream = debug_open_property_dump(path, append);
    if (stream == NULL)
        return false;

    bool ok = debug_write_all(stream, "# Slayer 3D actor property dump\n");
    char line[256];
    SDL_snprintf(line, sizeof(line), "actor=%s\n", actor->name != NULL ? actor->name : "");
    ok = debug_write_all(stream, line) && ok;
    const char *label = json_string(action, "label", NULL);
    if (label != NULL)
    {
        SDL_snprintf(line, sizeof(line), "label=%s\n", label);
        ok = debug_write_all(stream, line) && ok;
    }
    ok = debug_write_all(stream, "\n") && ok;

    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        const char *key = yyjson_get_str(yyjson_arr_get(properties, i));
        if (key != NULL)
            ok = debug_write_property_value(stream, key, slayer3d_properties_get_value(actor->props, key)) && ok;
    }

    ok = SDL_CloseIO(stream) && ok;
    if (ok)
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "wrote actor property dump: %s", path);
    return ok;
}

static bool console_write_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                 const slayer3d_properties *payload)
{
    if (runtime == NULL || runtime->scene_state == NULL || action == NULL)
        return false;

    char message[512];
    const char *message_template = json_string(action, "message", NULL);
    const char *message_from_state = json_string(action, "message_from_state", NULL);
    if (message_template != NULL)
    {
        if (!format_payload_string(payload, message_template, message, sizeof(message)))
            return false;
    }
    else if (message_from_state != NULL)
    {
        SDL_strlcpy(message, slayer3d_properties_get_string(runtime->scene_state, message_from_state, ""),
                    sizeof(message));
    }
    else
    {
        return false;
    }

    if (message[0] == '\0')
        return false;

    const char *line_key_prefix = json_string(action, "line_key_prefix", "editor.console.line");
    const char *count_key = json_string(action, "count_key", "editor.console.count");
    int line_count = json_int(action, "line_count", 64);
    line_count = SDL_clamp(line_count, 1, 64);
    if (SDL_strcmp(line_key_prefix, "editor.console.line") == 0 &&
        (count_key == NULL || SDL_strcmp(count_key, "editor.console.count") == 0) && line_count == 64)
    {
        editor_publish_console_message(runtime, message);
        return true;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);

    char previous[64][512];
    SDL_zeroa(previous);
    for (int i = 0; i < line_count; ++i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "%s%d", line_key_prefix, i);
        SDL_strlcpy(previous[i], slayer3d_properties_get_string(runtime->scene_state, key, ""), sizeof(previous[i]));
    }

    for (int i = line_count - 1; i > 0; --i)
    {
        char key[128];
        SDL_snprintf(key, sizeof(key), "%s%d", line_key_prefix, i);
        slayer3d_properties_set_string(runtime->scene_state, key, previous[i - 1]);
    }

    char key[128];
    SDL_snprintf(key, sizeof(key), "%s0", line_key_prefix);
    slayer3d_properties_set_string(runtime->scene_state, key, message);
    if (count_key != NULL && count_key[0] != '\0')
    {
        slayer3d_properties_set_int(
            runtime->scene_state, count_key,
            SDL_min(line_count, slayer3d_properties_get_int(runtime->scene_state, count_key, 0) + 1));
    }
    return true;
}

static bool action_value_to_string(const slayer3d_value *value, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (value == NULL)
        return true;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        SDL_snprintf(buffer, buffer_size, "%d", value->as_int);
        return true;
    case SLAYER3D_VALUE_FLOAT:
        SDL_snprintf(buffer, buffer_size, "%.3f", value->as_float);
        return true;
    case SLAYER3D_VALUE_BOOL:
        SDL_strlcpy(buffer, value->as_bool ? "true" : "false", buffer_size);
        return true;
    case SLAYER3D_VALUE_VEC3:
        SDL_snprintf(buffer, buffer_size, "%.3f, %.3f, %.3f", value->as_vec3.x, value->as_vec3.y, value->as_vec3.z);
        return true;
    case SLAYER3D_VALUE_STRING:
        SDL_strlcpy(buffer, value->as_string != NULL ? value->as_string : "", buffer_size);
        return true;
    case SLAYER3D_VALUE_COLOR:
        SDL_snprintf(buffer, buffer_size, "%u, %u, %u, %u", (unsigned)value->as_color.r, (unsigned)value->as_color.g,
                     (unsigned)value->as_color.b, (unsigned)value->as_color.a);
        return true;
    }

    return false;
}

static bool format_scene_state_string(const slayer3d_properties *scene_state, const slayer3d_properties *payload,
                                      const char *format, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    buffer[0] = '\0';
    if (format == NULL)
        return true;
    if (SDL_strchr(format, '{') == NULL)
    {
        SDL_strlcpy(buffer, format, buffer_size);
        return SDL_strlen(format) < buffer_size;
    }

    size_t offset = 0U;
    const char *cursor = format;
    while (*cursor != '\0' && offset + 1U < buffer_size)
    {
        const char *open = SDL_strchr(cursor, '{');
        if (open == NULL)
        {
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, cursor, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
            if (copied < remaining)
                cursor += copied;
            break;
        }

        const size_t literal_len = (size_t)(open - cursor);
        const size_t literal_copy = SDL_min(literal_len, buffer_size - offset - 1U);
        SDL_memcpy(buffer + offset, cursor, literal_copy);
        offset += literal_copy;
        buffer[offset] = '\0';
        if (literal_copy < literal_len)
            break;

        const char *close = SDL_strchr(open + 1, '}');
        if (close == NULL)
        {
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, open, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
            if (copied < remaining)
                cursor = open + copied;
            break;
        }

        char key[128];
        const size_t key_len = (size_t)(close - open - 1);
        if (key_len > 0U && key_len < sizeof(key))
        {
            SDL_memcpy(key, open + 1, key_len);
            key[key_len] = '\0';

            const slayer3d_value *replacement = payload != NULL ? slayer3d_properties_get_value(payload, key) : NULL;
            if (replacement == NULL && scene_state != NULL)
                replacement = slayer3d_properties_get_value(scene_state, key);

            char replacement_text[256];
            if (!action_value_to_string(replacement, replacement_text, sizeof(replacement_text)))
                return false;
            const size_t remaining = buffer_size - offset;
            const size_t copied = SDL_strlcpy(buffer + offset, replacement_text, remaining);
            offset += SDL_min(copied, remaining > 0U ? remaining - 1U : 0U);
        }
        cursor = close + 1;
    }
    buffer[buffer_size - 1U] = '\0';
    return cursor[0] == '\0';
}

static void publish_editor_brush_color_draft(slayer3d_properties *scene_state, const char *color_key,
                                             slayer3d_color color)
{
    char key[128];
    char label[64];
    if (scene_state == NULL || color_key == NULL || color_key[0] == '\0')
        return;

    slayer3d_properties_set_color(scene_state, color_key, color);
    SDL_snprintf(key, sizeof(key), "%s.r", color_key);
    slayer3d_properties_set_int(scene_state, key, (int)color.r);
    SDL_snprintf(key, sizeof(key), "%s.g", color_key);
    slayer3d_properties_set_int(scene_state, key, (int)color.g);
    SDL_snprintf(key, sizeof(key), "%s.b", color_key);
    slayer3d_properties_set_int(scene_state, key, (int)color.b);
    SDL_snprintf(key, sizeof(key), "%s.a", color_key);
    slayer3d_properties_set_int(scene_state, key, (int)color.a);
    SDL_snprintf(label, sizeof(label), "%u, %u, %u, %u", color.r, color.g, color.b, color.a);
    SDL_snprintf(key, sizeof(key), "%s.label", color_key);
    slayer3d_properties_set_string(scene_state, key, label);
    slayer3d_properties_set_string(scene_state, "editor.inspector.selection.color", label);
}

static bool execute_editor_brush_color_channel_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL || action == NULL)
        return false;

    const char *color_key = json_string(action, "color_key", "editor.inspector.brush.color");
    if (color_key == NULL || color_key[0] == '\0')
        return false;
    const char *dirty_key = json_string(action, "dirty_key", "editor.inspector.brush.color.dirty");
    if (dirty_key == NULL || dirty_key[0] == '\0')
        return false;
    const char *message = json_string(action, "message", "brush color edited");
    if (message == NULL || message[0] == '\0')
        return false;

    slayer3d_color color =
        slayer3d_properties_get_color(runtime->scene_state, color_key, (slayer3d_color){180, 184, 192, 255});
    if (obj_get(action, "color") != NULL)
    {
        color = json_color(action, "color", color);
    }
    else
    {
        const char channel = first_json_string_char(action, "channel", '\0');
        const int delta = json_int(action, "delta", 0);
        switch (channel)
        {
        case 'r':
            color.r = (Uint8)SDL_clamp((int)color.r + delta, 0, 255);
            break;
        case 'g':
            color.g = (Uint8)SDL_clamp((int)color.g + delta, 0, 255);
            break;
        case 'b':
            color.b = (Uint8)SDL_clamp((int)color.b + delta, 0, 255);
            break;
        case 'a':
            color.a = (Uint8)SDL_clamp((int)color.a + delta, 0, 255);
            break;
        default:
            return false;
        }
    }

    publish_editor_brush_color_draft(runtime->scene_state, color_key, color);
    slayer3d_properties_set_bool(runtime->scene_state, dirty_key, true);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    return true;
}

static bool editor_selected_brushes_bounds(const slayer3d_game_data_runtime *runtime, slayer3d_bounding_box *out_bounds)
{
    if (runtime == NULL || out_bounds == NULL || runtime->editor_selected_brush_count <= 0)
        return false;

    bool has_bounds = false;
    slayer3d_bounding_box bounds;
    SDL_zero(bounds);
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        slayer3d_game_data_editor_selection selection =
            resolved_editor_selection(runtime, &runtime->editor_selected_brushes[i]);
        if (!selection.hit || !selection.has_bounds)
            continue;
        if (!has_bounds)
        {
            bounds = selection.bounds;
            has_bounds = true;
            continue;
        }
        bounds.min.x = SDL_min(bounds.min.x, selection.bounds.min.x);
        bounds.min.y = SDL_min(bounds.min.y, selection.bounds.min.y);
        bounds.min.z = SDL_min(bounds.min.z, selection.bounds.min.z);
        bounds.max.x = SDL_max(bounds.max.x, selection.bounds.max.x);
        bounds.max.y = SDL_max(bounds.max.y, selection.bounds.max.y);
        bounds.max.z = SDL_max(bounds.max.z, selection.bounds.max.z);
    }
    if (!has_bounds)
        return false;

    *out_bounds = bounds;
    return true;
}

static bool editor_selected_brushes_bounds_center(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 *out_center)
{
    if (out_center == NULL)
        return false;
    slayer3d_bounding_box bounds;
    if (!editor_selected_brushes_bounds(runtime, &bounds))
        return false;
    *out_center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    return true;
}

static bool editor_selection_rotate_selected_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "pivot") != NULL)
        pivot = json_vec3(action, "pivot", pivot);
    else if (!editor_selected_brushes_bounds_center(runtime, &pivot))
        return false;

    const slayer3d_vec3 axis = json_vec3(action, "axis", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    const float angle_radians = obj_get(action, "angle_radians") != NULL
                                    ? json_float(action, "angle_radians", 0.0f)
                                    : slayer3d_degrees_to_radians(json_float(action, "angle_degrees", 0.0f));
    return slayer3d_game_data_rotate_selected_editor_brushes(runtime, pivot, axis, angle_radians);
}

static bool editor_selection_scale_selected_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_vec3 anchor = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "anchor") != NULL)
        anchor = json_vec3(action, "anchor", anchor);
    else if (!editor_selected_brushes_bounds_center(runtime, &anchor))
        return false;

    const slayer3d_vec3 factors = json_vec3(action, "factors", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
    return slayer3d_game_data_scale_selected_editor_brushes(runtime, anchor, factors);
}

static bool editor_selection_shear_selected_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_bounding_box bounds;
    if (obj_get(action, "bounds_min") != NULL && obj_get(action, "bounds_max") != NULL)
    {
        bounds.min = json_vec3(action, "bounds_min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        bounds.max = json_vec3(action, "bounds_max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    }
    else if (!editor_selected_brushes_bounds(runtime, &bounds))
    {
        return false;
    }

    const slayer3d_vec3 side_normal = json_vec3(action, "side_normal", slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
    const slayer3d_vec3 delta = json_vec3(action, "delta", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return slayer3d_game_data_shear_selected_editor_brushes(runtime, bounds, side_normal, delta);
}

static const char *editor_flip_axis_label(slayer3d_vec3 normal)
{
    const float abs_x = SDL_fabsf(normal.x);
    const float abs_y = SDL_fabsf(normal.y);
    const float abs_z = SDL_fabsf(normal.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
        return normal.x >= 0.0f ? "+X" : "-X";
    if (abs_y >= abs_x && abs_y >= abs_z)
        return normal.y >= 0.0f ? "+Y" : "-Y";
    return normal.z >= 0.0f ? "+Z" : "-Z";
}

static slayer3d_vec3 editor_flip_dominant_axis_vector(slayer3d_vec3 v)
{
    const float abs_x = SDL_fabsf(v.x);
    const float abs_y = SDL_fabsf(v.y);
    const float abs_z = SDL_fabsf(v.z);
    if (abs_x >= abs_y && abs_x >= abs_z)
        return slayer3d_vec3_make(v.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
    if (abs_y >= abs_x && abs_y >= abs_z)
        return slayer3d_vec3_make(0.0f, v.y < 0.0f ? -1.0f : 1.0f, 0.0f);
    return slayer3d_vec3_make(0.0f, 0.0f, v.z < 0.0f ? -1.0f : 1.0f);
}

static bool editor_flip_vec3_same_direction(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return SDL_fabsf(a.x - b.x) <= 0.0001f && SDL_fabsf(a.y - b.y) <= 0.0001f && SDL_fabsf(a.z - b.z) <= 0.0001f;
}

static slayer3d_vec3 editor_flip_perspective_forward_axis(slayer3d_vec3 forward, slayer3d_vec3 up)
{
    slayer3d_vec3 projected = slayer3d_vec3_make(forward.x, 0.0f, forward.z);
    if (slayer3d_vec3_length_squared(projected) <= 0.000001f)
    {
        projected = slayer3d_vec3_make(up.x, 0.0f, up.z);
        if (forward.y > 0.0f)
            projected = slayer3d_vec3_negate(projected);
    }
    if (slayer3d_vec3_length_squared(projected) <= 0.000001f)
        return slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    return editor_flip_dominant_axis_vector(projected);
}

static slayer3d_vec3 editor_flip_perspective_right_axis(slayer3d_vec3 right, slayer3d_vec3 forward_axis)
{
    slayer3d_vec3 axis = editor_flip_dominant_axis_vector(right);
    if (editor_flip_vec3_same_direction(axis, forward_axis))
    {
        axis = slayer3d_vec3_cross(axis, slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
        if (slayer3d_vec3_length_squared(axis) <= 0.000001f)
            axis = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    }
    return editor_flip_dominant_axis_vector(axis);
}

static bool editor_flip_normal_valid(slayer3d_vec3 normal, const char *adjective, char *message, size_t message_size)
{
    if (SDL_isnan(normal.x) || SDL_isinf(normal.x) || SDL_isnan(normal.y) || SDL_isinf(normal.y) ||
        SDL_isnan(normal.z) || SDL_isinf(normal.z) || slayer3d_vec3_length_squared(normal) <= 0.000001f)
    {
        if (message != NULL && message_size > 0u)
            SDL_snprintf(message, message_size, "%s flip requires a finite non-zero normal", adjective);
        return false;
    }
    return true;
}

static bool editor_selection_flip_vertical_normal(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                  slayer3d_vec3 *out_normal, const char **out_axis_label, char *message,
                                                  size_t message_size)
{
    if (out_normal == NULL)
        return false;
    if (message != NULL && message_size > 0u)
        message[0] = '\0';

    slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    if (obj_get(action, "normal") != NULL)
    {
        normal = json_vec3(action, "normal", normal);
    }
    else
    {
        const char *normal_key = json_string(action, "normal_key", NULL);
        const slayer3d_value *normal_value =
            runtime != NULL && runtime->scene_state != NULL && normal_key != NULL && normal_key[0] != '\0'
                ? slayer3d_properties_get_value(runtime->scene_state, normal_key)
                : NULL;
        if (normal_value != NULL && normal_value->type == SLAYER3D_VALUE_VEC3)
        {
            normal = normal_value->as_vec3;
        }
        else
        {
            const char *view_mode_key = json_string(action, "view_mode_key", "editor.view.mode");
            const char *view_mode =
                runtime != NULL && runtime->scene_state != NULL && view_mode_key != NULL && view_mode_key[0] != '\0'
                    ? slayer3d_properties_get_string(runtime->scene_state, view_mode_key, "flyby_3d")
                    : "flyby_3d";
            const char *top_mode = json_string(action, "top_mode", "orthographic_top");
            const char *front_mode = json_string(action, "front_mode", "orthographic_front");
            const char *side_mode = json_string(action, "side_mode", "orthographic_side");
            const char *flyby_mode = json_string(action, "flyby_mode", "flyby_3d");
            if (SDL_strcmp(view_mode, top_mode) == 0)
                normal = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
            else if (SDL_strcmp(view_mode, front_mode) == 0 || SDL_strcmp(view_mode, side_mode) == 0 ||
                     SDL_strcmp(view_mode, flyby_mode) == 0)
                normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
            else
            {
                if (message != NULL && message_size > 0u)
                    SDL_snprintf(message, message_size, "vertical flip unsupported for view '%s'", view_mode);
                return false;
            }
        }
    }

    if (!editor_flip_normal_valid(normal, "vertical", message, message_size))
        return false;
    normal = slayer3d_vec3_normalize(normal);
    *out_normal = normal;
    if (out_axis_label != NULL)
        *out_axis_label = editor_flip_axis_label(normal);
    return true;
}

static bool editor_selection_flip_horizontal_normal(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                    slayer3d_vec3 *out_normal, const char **out_axis_label,
                                                    char *message, size_t message_size)
{
    if (out_normal == NULL)
        return false;
    if (message != NULL && message_size > 0u)
        message[0] = '\0';

    slayer3d_vec3 normal = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    if (obj_get(action, "normal") != NULL)
    {
        normal = json_vec3(action, "normal", normal);
    }
    else
    {
        const char *normal_key = json_string(action, "normal_key", NULL);
        const slayer3d_value *normal_value =
            runtime != NULL && runtime->scene_state != NULL && normal_key != NULL && normal_key[0] != '\0'
                ? slayer3d_properties_get_value(runtime->scene_state, normal_key)
                : NULL;
        if (normal_value != NULL && normal_value->type == SLAYER3D_VALUE_VEC3)
        {
            normal = normal_value->as_vec3;
        }
        else
        {
            slayer3d_vec3 forward;
            slayer3d_vec3 right;
            slayer3d_vec3 up;
            bool orthographic = false;
            if (editor_camera_basis(runtime, &forward, &right, &up, &orthographic))
            {
                if (orthographic)
                    normal = editor_flip_dominant_axis_vector(right);
                else
                {
                    const slayer3d_vec3 forward_axis = editor_flip_perspective_forward_axis(forward, up);
                    normal = editor_flip_perspective_right_axis(right, forward_axis);
                }
            }
            else
            {
                const char *view_mode_key = json_string(action, "view_mode_key", "editor.view.mode");
                const char *view_mode =
                    runtime != NULL && runtime->scene_state != NULL && view_mode_key != NULL && view_mode_key[0] != '\0'
                        ? slayer3d_properties_get_string(runtime->scene_state, view_mode_key, "flyby_3d")
                        : "flyby_3d";
                const char *top_mode = json_string(action, "top_mode", "orthographic_top");
                const char *front_mode = json_string(action, "front_mode", "orthographic_front");
                const char *side_mode = json_string(action, "side_mode", "orthographic_side");
                const char *flyby_mode = json_string(action, "flyby_mode", "flyby_3d");
                if (SDL_strcmp(view_mode, top_mode) == 0 || SDL_strcmp(view_mode, front_mode) == 0 ||
                    SDL_strcmp(view_mode, flyby_mode) == 0)
                {
                    normal = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
                }
                else if (SDL_strcmp(view_mode, side_mode) == 0)
                {
                    normal = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
                }
                else
                {
                    if (message != NULL && message_size > 0u)
                        SDL_snprintf(message, message_size, "horizontal flip unsupported for view '%s'", view_mode);
                    return false;
                }
            }
        }
    }

    if (!editor_flip_normal_valid(normal, "horizontal", message, message_size))
        return false;
    normal = slayer3d_vec3_normalize(normal);
    *out_normal = normal;
    if (out_axis_label != NULL)
        *out_axis_label = editor_flip_axis_label(normal);
    return true;
}

static bool editor_selection_flip_vertical_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "pivot") != NULL)
        pivot = json_vec3(action, "pivot", pivot);
    else if (!editor_selected_brushes_bounds_center(runtime, &pivot))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key", "select brushes before vertical flip");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           "select brushes before vertical flip");
        return false;
    }

    slayer3d_vec3 normal = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    const char *axis_label = "+Y";
    char message[128];
    if (!editor_selection_flip_vertical_normal(runtime, action, &normal, &axis_label, message, sizeof(message)))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key",
                                 message[0] != '\0' ? message : "vertical flip failed");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           message[0] != '\0' ? message : "vertical flip failed");
        return false;
    }

    const bool flipped = slayer3d_game_data_flip_selected_editor_brushes(runtime, pivot, normal);
    SDL_snprintf(message, sizeof(message),
                 flipped ? "flipped selected brushes vertically around %s" : "vertical flip failed around %s",
                 axis_label);
    editor_set_bool_output(scene_state, outputs, "valid_key", flipped);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_string_output(scene_state, outputs, "axis_key", axis_label);
    editor_set_vec3_output(scene_state, outputs, "center_key", pivot);
    if (scene_state != NULL && !flipped)
        slayer3d_properties_set_string(scene_state, "editor.tool.last_action", message);
    return flipped;
}

static bool editor_selection_flip_horizontal_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    slayer3d_vec3 pivot = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    if (obj_get(action, "pivot") != NULL)
        pivot = json_vec3(action, "pivot", pivot);
    else if (!editor_selected_brushes_bounds_center(runtime, &pivot))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key", "select brushes before horizontal flip");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           "select brushes before horizontal flip");
        return false;
    }

    slayer3d_vec3 normal = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    const char *axis_label = "+X";
    char message[128];
    if (!editor_selection_flip_horizontal_normal(runtime, action, &normal, &axis_label, message, sizeof(message)))
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_string_output(scene_state, outputs, "message_key",
                                 message[0] != '\0' ? message : "horizontal flip failed");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action",
                                           message[0] != '\0' ? message : "horizontal flip failed");
        return false;
    }

    const bool flipped = slayer3d_game_data_flip_selected_editor_brushes_horizontal(runtime, pivot, normal);
    SDL_snprintf(message, sizeof(message),
                 flipped ? "flipped selected brushes horizontally around %s" : "horizontal flip failed around %s",
                 axis_label);
    editor_set_bool_output(scene_state, outputs, "valid_key", flipped);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_string_output(scene_state, outputs, "axis_key", axis_label);
    editor_set_vec3_output(scene_state, outputs, "center_key", pivot);
    if (scene_state != NULL && !flipped)
        slayer3d_properties_set_string(scene_state, "editor.tool.last_action", message);
    return flipped;
}

static bool editor_brush_duplicate_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_properties *scene_state = runtime != NULL ? runtime->scene_state : NULL;
    const char *mode = json_string(action, "mode", "in_place");
    slayer3d_vec3 offset = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    bool use_last_offset = false;

    if (SDL_strcmp(mode, "last_offset") == 0)
    {
        use_last_offset = true;
    }
    else if (SDL_strcmp(mode, "offset") == 0)
    {
        offset = json_vec3(action, "offset", offset);
    }
    else
    {
        mode = "in_place";
    }

    if (runtime == NULL || runtime->editor_selected_brush_count <= 0)
    {
        editor_set_bool_output(scene_state, outputs, "valid_key", false);
        editor_set_int_output(scene_state, outputs, "count_key", 0);
        editor_set_string_output(scene_state, outputs, "mode_key", mode);
        editor_set_vec3_output(scene_state, outputs, "offset_key", offset);
        editor_set_string_output(scene_state, outputs, "message_key", "select brushes before duplicating");
        if (scene_state != NULL)
            slayer3d_properties_set_string(scene_state, "editor.tool.last_action", "select brushes before duplicating");
        return false;
    }

    const bool duplicated = slayer3d_game_data_duplicate_selected_editor_brushes(runtime, offset, use_last_offset);
    const int count = duplicated ? runtime->editor_selected_brush_count : 0;
    const char *message = duplicated && scene_state != NULL
                              ? slayer3d_properties_get_string(scene_state, "editor.tool.last_action", "")
                              : "selected brushes could not be duplicated";
    if (duplicated && use_last_offset && runtime->editor_has_last_duplicate_offset)
        offset = runtime->editor_last_duplicate_offset;

    editor_set_bool_output(scene_state, outputs, "valid_key", duplicated);
    editor_set_int_output(scene_state, outputs, "count_key", count);
    editor_set_string_output(scene_state, outputs, "mode_key", mode);
    editor_set_vec3_output(scene_state, outputs, "offset_key", offset);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    return duplicated;
}

bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const slayer3d_properties *payload)
{
    const char *type = json_string(action, "type", "");
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    slayer3d_timer_pool *timers = runtime_timers(runtime);

    if (SDL_strcmp(type, "signal.emit") == 0)
    {
        const int signal_id = action_signal_id(runtime, action, "signal");
        if (signal_id >= 0 && bus != NULL)
            slayer3d_signal_emit(bus, signal_id, payload);
        return signal_id >= 0;
    }

    if (SDL_strcmp(type, "editor.file.dialog") == 0)
        return execute_editor_file_dialog_action(runtime, action);

    if (SDL_strcmp(type, "timer.start") == 0)
    {
        const int timer_index = find_timer_index(runtime, json_string(action, "timer", NULL));
        if (timer_index < 0 || timers == NULL)
            return false;
        const named_timer *timer = &runtime->timers[timer_index];
        return slayer3d_timer_start(timers, json_float(action, "delay", timer->delay), timer->signal_id,
                                    timer->repeating, timer->interval) != 0;
    }

    if (SDL_strcmp(type, "property.set") == 0 || SDL_strcmp(type, "property.add") == 0)
    {
        slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        const char *value_from_payload = json_string(action, "value_from_payload", NULL);
        const char *value_from_state = json_string(action, "value_from_state", NULL);
        const slayer3d_value *payload_value = value_from_payload != NULL && payload != NULL
                                                  ? slayer3d_properties_get_value(payload, value_from_payload)
                                                  : NULL;
        const slayer3d_value *state_value = value_from_state != NULL && runtime != NULL && runtime->scene_state != NULL
                                                ? slayer3d_properties_get_value(runtime->scene_state, value_from_state)
                                                : NULL;
        const slayer3d_value *source_value = payload_value != NULL ? payload_value : state_value;
        if (actor == NULL || key == NULL || (value == NULL && payload_value == NULL && state_value == NULL))
            return false;

        if (SDL_strcmp(type, "property.add") == 0)
        {
            const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
            if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && source_value != NULL &&
                source_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_int(actor->props, key, existing->as_int + source_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && source_value != NULL &&
                     source_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + source_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && source_value != NULL &&
                     source_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)source_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && source_value != NULL &&
                     source_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, (float)existing->as_int + source_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && yyjson_is_num(value))
                slayer3d_properties_set_int(actor->props, key, existing->as_int + (int)yyjson_get_int(value));
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && yyjson_is_num(value))
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)yyjson_get_real(value));
            else
                return false;
        }
        else if (source_value != NULL)
        {
            copy_property_value(actor->props, key, source_value);
        }
        else
        {
            set_actor_property_from_json(actor, key, value);
        }
        return true;
    }

    if (SDL_strcmp(type, "property.snapshot") == 0)
        return snapshot_actor_properties(runtime, action);

    if (SDL_strcmp(type, "property.restore_snapshot") == 0)
        return restore_actor_property_snapshot(runtime, action);

    if (SDL_strcmp(type, "property.animate") == 0)
        return start_property_animation_from_json(runtime, action, payload);

    if (SDL_strcmp(type, "property.reset_defaults") == 0)
        return reset_actor_properties_to_authored_defaults(runtime, action);

    if (SDL_strcmp(type, "debug.write_actor_properties") == 0)
        return debug_write_actor_properties(runtime, action, payload);

    if (SDL_strcmp(type, "input.reset_bindings") == 0)
        return slayer3d_game_data_reset_menu_input_bindings(runtime, json_string(action, "menu", NULL));

    if (SDL_strcmp(type, "input.apply_profile") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *profile = json_string(action, "profile", NULL);
        if (!slayer3d_game_data_apply_input_profile(runtime, input, profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "input.apply_active_profile") == 0)
    {
        char error[256] = {0};
        const char *profile = NULL;
        slayer3d_input_manager *input = runtime_input(runtime);
        if (!slayer3d_game_data_apply_active_input_profile(runtime, input, &profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile applied: profile=%s",
                    profile != NULL ? profile : "<none>");
        return true;
    }

    if (SDL_strcmp(type, "input.clear_network_input_overrides") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *channel = json_string(action, "channel", NULL);
        if (!slayer3d_game_data_clear_network_input_overrides(runtime, channel, input, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data network input override clear failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "scene_state.set") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        const char *value_from_state = json_string(action, "value_from_state", NULL);
        yyjson_val *value_from_property = obj_get(action, "value_from_property");
        yyjson_val *value = obj_get(action, "value");
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
            return false;
        if (value_from_state != NULL && value_from_state[0] != '\0')
        {
            const slayer3d_value *source = slayer3d_properties_get_value(runtime->scene_state, value_from_state);
            if (source == NULL)
                return false;
            switch (source->type)
            {
            case SLAYER3D_VALUE_INT:
                slayer3d_properties_set_int(runtime->scene_state, key, source->as_int);
                return true;
            case SLAYER3D_VALUE_FLOAT:
                slayer3d_properties_set_float(runtime->scene_state, key, source->as_float);
                return true;
            case SLAYER3D_VALUE_BOOL:
                slayer3d_properties_set_bool(runtime->scene_state, key, source->as_bool);
                return true;
            case SLAYER3D_VALUE_VEC3:
                slayer3d_properties_set_vec3(runtime->scene_state, key, source->as_vec3);
                return true;
            case SLAYER3D_VALUE_STRING:
                slayer3d_properties_set_string(runtime->scene_state, key,
                                               source->as_string != NULL ? source->as_string : "");
                return true;
            case SLAYER3D_VALUE_COLOR:
                slayer3d_properties_set_color(runtime->scene_state, key, source->as_color);
                return true;
            }
            return false;
        }
        if (yyjson_is_obj(value_from_property))
        {
            slayer3d_registered_actor *actor =
                slayer3d_game_data_find_actor(runtime, json_string(value_from_property, "target", NULL));
            const char *property_key = json_string(value_from_property, "key", NULL);
            const slayer3d_value *source = actor != NULL && property_key != NULL
                                               ? slayer3d_properties_get_value(actor->props, property_key)
                                               : NULL;
            if (source == NULL)
                return false;
            copy_property_value(runtime->scene_state, key, source);
            return true;
        }
        if (value == NULL)
            return false;
        if (yyjson_is_str(value))
        {
            char formatted[512];
            if (!format_scene_state_string(runtime->scene_state, payload, yyjson_get_str(value), formatted,
                                           sizeof(formatted)))
            {
                return false;
            }
            slayer3d_properties_set_string(runtime->scene_state, key, formatted);
            return true;
        }
        return set_property_from_json_with_payload(runtime->scene_state, key, value, payload);
    }

    if (SDL_strcmp(type, "scene_state.toggle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
            return false;
        const bool current = scene_state_bool(runtime, key, json_bool(action, "default", false));
        slayer3d_properties_set_bool(runtime->scene_state, key, !current);
        return true;
    }

    if (SDL_strcmp(type, "scene_state.add") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
            return false;

        const float delta = json_float(action, "value", 0.0f);
        const float fallback = json_float(action, "default", 0.0f);
        float value = fallback;
        const slayer3d_value *current = slayer3d_properties_get_value(runtime->scene_state, key);
        if (current != NULL && current->type == SLAYER3D_VALUE_FLOAT)
            value = current->as_float;
        else if (current != NULL && current->type == SLAYER3D_VALUE_INT)
            value = (float)current->as_int;
        else if (current != NULL)
            return false;

        value += delta;
        yyjson_val *min_value = obj_get(action, "min");
        yyjson_val *max_value = obj_get(action, "max");
        if (yyjson_is_num(min_value))
            value = SDL_max(value, (float)yyjson_get_num(min_value));
        if (yyjson_is_num(max_value))
            value = SDL_min(value, (float)yyjson_get_num(max_value));
        slayer3d_properties_set_float(runtime->scene_state, key, value);
        return true;
    }

    if (SDL_strcmp(type, "scene_state.cycle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *values = obj_get(action, "values");
        const size_t count = yyjson_is_arr(values) ? yyjson_arr_size(values) : 0U;
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || count == 0U)
            return false;

        const slayer3d_value *current = slayer3d_properties_get_value(runtime->scene_state, key);
        slayer3d_value fallback;
        if (current == NULL && json_scalar_to_value(obj_get(action, "default"), &fallback))
            current = &fallback;
        const int direction = json_int(action, "direction", 1);
        size_t next = 0U;
        for (size_t i = 0; i < count; ++i)
        {
            yyjson_val *value = yyjson_arr_get(values, i);
            if (json_value_matches_property(value, current))
            {
                const int signed_count = (int)count;
                int signed_next = ((int)i + direction) % signed_count;
                if (signed_next < 0)
                    signed_next += signed_count;
                next = (size_t)signed_next;
                break;
            }
        }

        return set_property_from_json(runtime->scene_state, key, yyjson_arr_get(values, next));
    }

    if (SDL_strcmp(type, "console.write") == 0)
        return console_write_action(runtime, action, payload);

    if (SDL_strcmp(type, "editor.selection.clear") == 0)
        return slayer3d_game_data_clear_active_editor_selection(runtime);

    if (SDL_strcmp(type, "editor.vertex.selection.clear") == 0)
        return slayer3d_game_data_clear_editor_vertex_selection(runtime);

    if (SDL_strcmp(type, "editor.edge.selection.clear") == 0)
        return slayer3d_game_data_clear_editor_edge_selection(runtime);

    if (SDL_strcmp(type, "editor.tool.set_mode") == 0)
        return slayer3d_game_data_set_editor_tool_mode(runtime, json_string(action, "mode", NULL),
                                                       json_string(action, "message", NULL));

    if (SDL_strcmp(type, "editor.escape") == 0)
    {
        if (runtime == NULL || runtime->scene_state == NULL)
            return false;
        const scene_entry *scene = active_scene_entry_const(runtime);
        if (scene != NULL && scene->name != NULL && SDL_strcmp(scene->name, "scene.editor_shell.test_run") == 0)
        {
            slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.editor_shell.player");
            slayer3d_registered_actor *fly = slayer3d_game_data_find_actor(runtime, "entity.editor_shell.fly_player");
            if (player != NULL)
                player->active = false;
            if (fly != NULL)
                fly->active = false;
            runtime->active_camera = "camera.editor_shell.viewport";
            slayer3d_properties_set_string(runtime->scene_state, "editor.play.mode", "");
            editor_publish_console_message(runtime, "Returned to editor");
            return slayer3d_game_data_set_active_scene(runtime, "scene.slayer3d_editor.main");
        }
        if (editor_mode_is_paint(runtime) ||
            slayer3d_properties_get_bool(runtime->scene_state, "editor.texture.viewer.active", false) ||
            slayer3d_properties_get_bool(runtime->scene_state, "editor.actor.viewer.active", false) ||
            slayer3d_properties_get_bool(runtime->scene_state, "editor.sky.panel.active", false) ||
            slayer3d_properties_get_bool(runtime->scene_state, "editor.liquid.panel.open", false))
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
            slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.viewer.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.viewer.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.sky.panel.active", false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.liquid.panel.open", false);
            (void)slayer3d_game_data_set_editor_tool_mode(runtime, "select", NULL);
            return true;
        }
        if (SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.palette.active", ""), "") != 0)
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "palette closed");
            return true;
        }
        if (slayer3d_properties_get_bool(runtime->scene_state, "editor.global.panel.open", false))
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.global.panel.open", false);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "global panel closed");
            return true;
        }
        if (slayer3d_properties_get_int(runtime->scene_state, "editor.vertex.selection.count", 0) > 0)
        {
            if (!slayer3d_game_data_clear_editor_vertex_selection(runtime))
                return false;
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "vertex selection cleared");
            return true;
        }

        const char *mode = slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select");
        if (SDL_strcmp(mode, "clip") == 0)
            return slayer3d_game_data_escape_editor_clip_tool(runtime);
        if (SDL_strcmp(mode, "select") != 0)
            return slayer3d_game_data_set_editor_tool_mode(runtime, "select", NULL);
        slayer3d_game_data_editor_selection active_selection;
        SDL_zero(active_selection);
        const bool active_actor_selection =
            runtime != NULL && slayer3d_game_data_get_active_editor_selection(runtime, &active_selection) &&
            active_selection.hit && active_selection.type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR;
        if (slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0) > 0 ||
            active_actor_selection)
        {
            if (!slayer3d_game_data_clear_active_editor_selection(runtime))
                return false;
            (void)slayer3d_game_data_clear_editor_command_preview(runtime, action);
            slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "selection cleared");
            return true;
        }
        return slayer3d_game_data_clear_editor_command_preview(runtime, action);
    }

    if (SDL_strcmp(type, "editor.clip.cancel") == 0)
        return slayer3d_game_data_cancel_editor_clip_tool(runtime, json_string(action, "message", NULL));

    if (SDL_strcmp(type, "editor.clip.escape") == 0)
        return slayer3d_game_data_escape_editor_clip_tool(runtime);

    if (SDL_strcmp(type, "editor.clip.cycle_keep_mode") == 0)
        return slayer3d_game_data_cycle_editor_clip_keep_mode(runtime);

    if (SDL_strcmp(type, "editor.clip.commit") == 0)
        return slayer3d_game_data_commit_editor_clip_tool(runtime);

    if (SDL_strcmp(type, "editor.placement_preview.cancel") == 0)
        return editor_cancel_pending_brush_preview(runtime, json_string(action, "message", NULL));

    if (SDL_strcmp(type, "editor.vertex.snap_selected") == 0)
        return slayer3d_game_data_snap_selected_editor_vertices(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.delete_selected") == 0)
        return slayer3d_game_data_delete_selected_editor_vertices(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.merge_selected_to_hover") == 0)
        return slayer3d_game_data_merge_selected_editor_vertices_to_hover(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.add_to_source") == 0)
        return slayer3d_game_data_add_editor_vertex_to_source(runtime, action);

    if (SDL_strcmp(type, "editor.vertex.validate_source") == 0)
        return slayer3d_game_data_validate_editor_vertex_source(runtime, action);

    if (SDL_strcmp(type, "editor.selection.select_brush") == 0)
        return slayer3d_game_data_select_editor_brush_action(runtime, action);

    if (SDL_strcmp(type, "editor.selection.delete_selected") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);

        slayer3d_game_data_editor_selection active_selection;
        SDL_zero(active_selection);
        const bool active_actor_selection =
            runtime != NULL && slayer3d_game_data_get_active_editor_selection(runtime, &active_selection) &&
            active_selection.hit && active_selection.type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR;
        if (active_actor_selection)
        {
            return slayer3d_game_data_delete_selected_editor_actor_action(runtime, action);
        }
        return slayer3d_game_data_delete_selected_editor_brushes(runtime, action, payload);
    }

    if (SDL_strcmp(type, "editor.visibility.hide_selected") == 0)
        return slayer3d_game_data_hide_selected_editor_objects_action(runtime, action);

    if (SDL_strcmp(type, "editor.visibility.show_all") == 0)
        return slayer3d_game_data_show_all_editor_objects_action(runtime, action);

    if (SDL_strcmp(type, "editor.lock.lock_selected") == 0)
        return slayer3d_game_data_lock_selected_editor_objects_action(runtime, action);

    if (SDL_strcmp(type, "editor.lock.unlock_selected") == 0)
        return slayer3d_game_data_unlock_selected_editor_objects_action(runtime, action);

    if (SDL_strcmp(type, "editor.lock.unlock_all") == 0)
        return slayer3d_game_data_unlock_all_editor_objects_action(runtime, action);

    if (SDL_strcmp(type, "editor.stair.toggle_direction") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_toggle_selected_editor_stair_direction_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.stair.add_step") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_add_selected_editor_stair_step_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.stair.remove_step") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_remove_selected_editor_stair_step_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.selection.resize_y") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_resize_selected_editor_brushes_y(runtime, action, payload);
    }

    if (SDL_strcmp(type, "editor.selection.rotate_selected") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return editor_selection_rotate_selected_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.selection.scale_selected") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return editor_selection_scale_selected_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.selection.flip_vertical") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return editor_selection_flip_vertical_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.selection.flip_horizontal") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return editor_selection_flip_horizontal_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.brush.duplicate") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return editor_brush_duplicate_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.brush.paint") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_paint_selected_editor_brushes(runtime, action, payload);
    }

    if (SDL_strcmp(type, "editor.brush.color") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_color_selected_editor_brushes(runtime, action, payload);
    }

    if (SDL_strcmp(type, "editor.inspector.brush.color_channel") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return execute_editor_brush_color_channel_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.texture.scan") == 0)
        return execute_editor_texture_scan_action(runtime, action);

    if (SDL_strcmp(type, "editor.texture.filter") == 0)
        return execute_editor_texture_filter_action(runtime, action);

    if (SDL_strcmp(type, "editor.media.path.apply") == 0)
        return execute_editor_media_path_apply_action(runtime, action);

    if (SDL_strcmp(type, "editor.texture.select_index") == 0)
        return execute_editor_texture_select_index_action(runtime, action);

    if (SDL_strcmp(type, "editor.sky.scan") == 0)
        return execute_editor_sky_scan_action(runtime, action);

    /* Clamp the console scroll and rebind the visible line window; the
     * shared scrollbar/wheel path emits this after changing the index. */
    if (SDL_strcmp(type, "editor.console.sync") == 0)
        return editor_scroll_console_by(runtime, 0);

    if (SDL_strcmp(type, "editor.sky.select") == 0)
        return execute_editor_sky_select_action(runtime, action);

    if (SDL_strcmp(type, "editor.sky.apply") == 0)
        return execute_editor_sky_apply_action(runtime, action);

    if (SDL_strcmp(type, "editor.actor.scan") == 0)
        return execute_editor_actor_scan_action(runtime, action);

    if (SDL_strcmp(type, "editor.actor.select_index") == 0)
        return execute_editor_actor_select_index_action(runtime, action);

    if (SDL_strcmp(type, "editor.global.property.new") == 0)
        return execute_editor_global_property_new_action(runtime, action);

    if (SDL_strcmp(type, "editor.global.property.apply") == 0)
        return execute_editor_global_property_apply_action(runtime, action);

    if (SDL_strcmp(type, "editor.global.property.select_slot") == 0)
        return execute_editor_global_property_select_slot_action(runtime, action);

    if (SDL_strcmp(type, "editor.global.property.remove") == 0)
        return execute_editor_global_property_remove_action(runtime, action);

    if (SDL_strcmp(type, "editor.actor.place_selected") == 0)
        return execute_editor_actor_place_selected_action(runtime, action);

    if (SDL_strcmp(type, "editor.connection.mark_source") == 0)
        return slayer3d_game_data_mark_editor_connection_source_action(runtime, action);

    if (SDL_strcmp(type, "editor.connection.add") == 0)
        return slayer3d_game_data_place_editor_connection_action(runtime, action);

    if (SDL_strcmp(type, "editor.prefab.define") == 0)
        return slayer3d_game_data_place_editor_prefab_action(runtime, action);

    if (SDL_strcmp(type, "editor.prefab.instantiate") == 0)
        return slayer3d_game_data_instantiate_editor_prefab_action(runtime, action);

    if (SDL_strcmp(type, "editor.prefab.unlink_actor") == 0)
        return slayer3d_game_data_unlink_editor_actor_prefab_action(runtime, action);

    if (SDL_strcmp(type, "editor.property.set") == 0)
    {
        if (slayer3d_game_data_editor_property_action_targets_locked_object(runtime, action))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_set_editor_property_action(runtime, action, payload);
    }

    if (SDL_strcmp(type, "editor.property.remove") == 0)
    {
        if (slayer3d_game_data_editor_property_action_targets_locked_object(runtime, action))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_remove_editor_property_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.property.select_slot") == 0)
        return slayer3d_game_data_select_editor_property_slot_action(runtime, action);

    if (SDL_strcmp(type, "editor.selection.shear_selected") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return editor_selection_shear_selected_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.selection.run") == 0)
    {
        slayer3d_game_data_editor_selection selection;
        if (slayer3d_game_data_get_active_editor_selection(runtime, &selection))
        {
            slayer3d_properties *selection_payload = slayer3d_game_data_create_editor_selection_payload(&selection);
            if (selection_payload == NULL)
                return false;
            const bool ok = execute_action_array(runtime, obj_get(action, "actions"), selection_payload);
            slayer3d_properties_destroy(selection_payload);
            return ok;
        }
        return execute_optional_action_array(runtime, obj_get(action, "else"), payload);
    }

    if (SDL_strcmp(type, "editor.command.preview") == 0)
        return slayer3d_game_data_preview_editor_command(runtime, action);

    if (SDL_strcmp(type, "editor.command.clear_preview") == 0)
        return slayer3d_game_data_clear_editor_command_preview(runtime, action);

    if (SDL_strcmp(type, "editor.command.commit") == 0)
    {
        if (slayer3d_game_data_editor_selection_contains_locked_objects(runtime))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        if (SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", ""), "clip") == 0)
            return slayer3d_game_data_commit_editor_clip_tool(runtime);
        return slayer3d_game_data_commit_editor_command(runtime, action, payload);
    }

    if (SDL_strcmp(type, "editor.command.undo") == 0)
        return slayer3d_game_data_undo_editor_command(runtime, action, payload);

    if (SDL_strcmp(type, "editor.command.redo") == 0)
        return slayer3d_game_data_redo_editor_command(runtime, action, payload);

    if (SDL_strcmp(type, "editor.brush_world.export") == 0)
        return slayer3d_game_data_export_editor_brush_world_action(runtime, action);

    if (SDL_strcmp(type, "editor.level.export") == 0)
        return slayer3d_game_data_export_editor_level_action(runtime, action);

    if (SDL_strcmp(type, "editor.level.save") == 0)
        return slayer3d_game_data_save_editor_level_action(runtime, action);

    if (SDL_strcmp(type, "editor.level.load") == 0)
        return slayer3d_game_data_load_editor_level_action(runtime, action);

    if (SDL_strcmp(type, "editor.map.export") == 0)
        return slayer3d_game_data_export_editor_map_action(runtime, action);

    if (SDL_strcmp(type, "editor.map.new") == 0)
        return slayer3d_game_data_new_editor_map_action(runtime, action);

    if (SDL_strcmp(type, "editor.map.save") == 0)
        return slayer3d_game_data_save_editor_map_action(runtime, action);

    if (SDL_strcmp(type, "editor.map.load") == 0)
        return slayer3d_game_data_load_editor_map_action(runtime, action);

    if (SDL_strcmp(type, "editor.map.validate") == 0)
        return slayer3d_game_data_validate_editor_map_action(runtime, action);

    if (SDL_strcmp(type, "editor.map.lighting_plan") == 0)
        return slayer3d_game_data_plan_editor_map_lighting_action(runtime, action);

    if (SDL_strcmp(type, "editor.test_run.prepare") == 0)
        return slayer3d_game_data_prepare_editor_test_run_action(runtime, action);

    if (SDL_strcmp(type, "editor.test_run.save_manifest") == 0)
        return slayer3d_game_data_save_editor_test_run_manifest_action(runtime, action);

    if (SDL_strcmp(type, "editor.runner.start") == 0)
        return execute_editor_runner_start_action(runtime);

    if (SDL_strcmp(type, "editor.brush_world.status") == 0)
        return slayer3d_game_data_publish_editor_brush_world_status_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush_world.validate_source") == 0)
        return slayer3d_game_data_validate_editor_brush_source_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush_world.validate_enclosure") == 0)
        return slayer3d_game_data_validate_editor_brush_enclosure_action(runtime, action);

    if (SDL_strcmp(type, "editor.brush_world.create_box") == 0)
        return slayer3d_game_data_create_box_brush_action(runtime, action);

    if (SDL_strcmp(type, "editor.player_start.place") == 0)
        return slayer3d_game_data_place_editor_player_start_action(runtime, action);

    if (SDL_strcmp(type, "editor.player_start.apply") == 0)
        return slayer3d_game_data_apply_editor_player_start_action(runtime, action);

    if (SDL_strcmp(type, "editor.player_start.delete") == 0)
        return slayer3d_game_data_delete_editor_player_start_action(runtime, action);

    if (SDL_strcmp(type, "editor.actor.place") == 0)
        return slayer3d_game_data_place_editor_actor_action(runtime, action);

    if (SDL_strcmp(type, "editor.actor.update") == 0)
    {
        if (slayer3d_game_data_editor_actor_update_action_targets_locked_actor(runtime, action))
            return slayer3d_game_data_reject_locked_editor_selection_action(runtime, NULL);
        return slayer3d_game_data_update_editor_actor_action(runtime, action);
    }

    if (SDL_strcmp(type, "editor.prefab.define") == 0)
        return slayer3d_game_data_place_editor_prefab_action(runtime, action);

    if (SDL_strcmp(type, "editor.prefab.instantiate") == 0)
        return slayer3d_game_data_instantiate_editor_prefab_action(runtime, action);

    if (SDL_strcmp(type, "editor.prefab.unlink_actor") == 0)
        return slayer3d_game_data_unlink_editor_actor_prefab_action(runtime, action);

    if (SDL_strcmp(type, "network.direct_connect.start") == 0)
    {
        const char *name = json_string(action, "name", NULL);
        const char *host_key = json_string(action, "host_key", NULL);
        const char *port_key = json_string(action, "port_key", NULL);
        const char *host = runtime != NULL && host_key != NULL
                               ? slayer3d_properties_get_string(runtime->scene_state, host_key,
                                                                json_string(action, "default_host", "127.0.0.1"))
                               : json_string(action, "host", json_string(action, "default_host", "127.0.0.1"));
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const char *port_text = runtime != NULL && port_key != NULL
                                    ? slayer3d_properties_get_string(runtime->scene_state, port_key, NULL)
                                    : NULL;
        const int port = port_text != NULL ? SDL_atoi(port_text) : json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_direct_connect_start(
            runtime, name, host, port, json_string(action, "status_key", NULL), json_string(action, "state_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.direct_connect.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Disconnected");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_direct_connect_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.direct_connect.observe") == 0)
    {
        return slayer3d_game_data_network_direct_connect_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.start") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_host_start(
            runtime, json_string(action, "name", NULL), port,
            json_string(action, "session_name", json_string(action, "advertised_name", NULL)),
            json_string(action, "status_key", NULL), json_string(action, "endpoint_key", NULL),
            json_string(action, "peer_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Not hosting");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_host_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.host.observe") == 0)
    {
        return slayer3d_game_data_network_host_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.start") == 0 || SDL_strcmp(type, "network.discovery.refresh") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        const int local_port = json_int_or_string(action, "local_port", 0);
        return slayer3d_game_data_network_discovery_start(
            runtime, json_string(action, "name", NULL), json_string(action, "host", NULL), port, local_port,
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.observe") == 0)
    {
        return slayer3d_game_data_network_discovery_update(
            runtime, json_string(action, "name", NULL),
            json_float(action, "dt", json_float(action, "update_seconds", 0.016f)),
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.cancel") == 0)
    {
        return slayer3d_game_data_network_discovery_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL),
            json_string(action, "status_key", NULL), json_string(action, "count_key", NULL),
            json_string(action, "status", "Discovery canceled"));
    }

    if (SDL_strcmp(type, "network.discovery.connect_selected") == 0)
    {
        const char *index_key = json_string(action, "selected_index_key", NULL);
        const int selected_index =
            runtime != NULL && runtime->scene_state != NULL && index_key != NULL
                ? slayer3d_properties_get_int(runtime->scene_state, index_key, json_int(action, "selected_index", 0))
                : json_int(action, "selected_index", 0);
        return slayer3d_game_data_network_discovery_connect_selected(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL), selected_index,
            json_string(action, "direct_connect_name", NULL), json_string(action, "host_key", NULL),
            json_string(action, "port_key", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL),
            json_string(action, "connecting_status", NULL));
    }

    if (SDL_strcmp(type, "ui.animate") == 0)
        return start_ui_animation_from_json(runtime, action);

    if (SDL_strncmp(type, "audio.", 6) == 0)
        return execute_audio_action(runtime, action, payload, type);

    if (SDL_strncmp(type, "persistence.", 12) == 0)
        return execute_persistence_action(runtime, action, type);

    if (SDL_strcmp(type, "entity.set_active") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        yyjson_val *active = obj_get(action, "active");
        if (actor == NULL || !yyjson_is_bool(active))
            return false;
        actor->active = yyjson_get_bool(active);
        return true;
    }

    if (SDL_strcmp(type, "actor.spawn") == 0)
        return execute_actor_spawn_action(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn") == 0)
        return execute_actor_despawn_action_with_payload(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
        return execute_actor_despawn_by_tag_action(runtime, action);

    if (SDL_strcmp(type, "combat.damage") == 0)
        return execute_combat_damage_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.heal") == 0)
        return execute_combat_heal_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.kill") == 0)
        return execute_combat_kill_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.revive") == 0)
        return execute_combat_revive_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.add") == 0)
        return execute_resource_amount_action(runtime, action, payload, false);
    if (SDL_strcmp(type, "resource.consume") == 0)
        return execute_resource_amount_action(runtime, action, payload, true);
    if (SDL_strcmp(type, "resource.set") == 0)
        return execute_resource_set_action(runtime, action, payload);
    if (SDL_strcmp(type, "pickup.collect") == 0)
        return execute_pickup_collect_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.station.use") == 0)
        return execute_resource_station_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "status_effect.apply") == 0)
        return execute_status_effect_apply_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.reload") == 0)
        return execute_weapon_reload_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.hitscan") == 0)
        return execute_weapon_hitscan_action(runtime, action, payload);
    if (SDL_strcmp(type, "interaction.use") == 0)
        return execute_interaction_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "effect.explosion") == 0)
        return execute_effect_explosion_action(runtime, action, payload);
    if (SDL_strcmp(type, "noise.emit") == 0)
        return execute_noise_emit_action(runtime, action, payload);

    if (SDL_strcmp(type, "projectile.fire") == 0)
        return execute_projectile_fire_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0)
        return execute_fps_controller_launch_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.teleport") == 0 || SDL_strcmp(type, "controller.fps_sector.teleport") == 0)
        return execute_fps_controller_teleport_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.push") == 0)
        return execute_fps_controller_push_action(runtime, action, payload);
    if (SDL_strcmp(type, "grid.spawn_from_glyphs") == 0)
        return execute_grid_spawn_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.spawn_runs_from_glyphs") == 0)
        return execute_grid_spawn_runs_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.pickup_layer.reset") == 0)
        return execute_grid_pickup_layer_reset_action(runtime, action);

    if (SDL_strcmp(type, "sector_door.open") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "open");
    if (SDL_strcmp(type, "sector_door.close") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "close");
    if (SDL_strcmp(type, "sector_door.toggle") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "toggle");
    if (SDL_strcmp(type, "sector_door.interact") == 0)
        return execute_sector_door_interact_action(runtime, action);
    if (SDL_strcmp(type, "sector_lighting.set") == 0)
    {
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        yyjson_val *color_json = obj_get(action, "color");
        if (yyjson_is_arr(color_json) && yyjson_arr_size(color_json) == 3U)
        {
            (void)json_float_array(color_json, color, 3, color);
            color[3] = 1.0f;
        }
        else
        {
            (void)json_float_array(color_json, color, 4, color);
        }
        char error[256] = {0};
        char sector_index_text[32];
        const char *sector = json_string(action, "sector", NULL);
        yyjson_val *sector_index_value = obj_get(action, "sector_index");
        if (sector == NULL && yyjson_is_int(sector_index_value))
        {
            SDL_snprintf(sector_index_text, sizeof(sector_index_text), "%d", (int)yyjson_get_int(sector_index_value));
            sector = sector_index_text;
        }
        const bool ok = slayer3d_game_data_set_sector_lighting(runtime, json_string(action, "sector_level", NULL),
                                                               sector, json_float(action, "level", 255.0f), color,
                                                               error, (int)sizeof(error));
        if (!ok)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sector_lighting.set failed: %s",
                        error[0] != '\0' ? error : "unknown error");
        return ok;
    }

    if (SDL_strcmp(type, "transform.set_position") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        if (actor == NULL)
            return false;
        actor_set_position(actor, json_vec3(action, "position", actor->position));
        return true;
    }

    if (SDL_strcmp(type, "camera.toggle") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        const char *fallback = json_string(action, "fallback", NULL);
        runtime->active_camera =
            runtime->active_camera != NULL && camera != NULL && SDL_strcmp(runtime->active_camera, camera) == 0
                ? fallback
                : camera;
        return runtime->active_camera != NULL;
    }

    if (SDL_strcmp(type, "camera.set") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        if (camera == NULL)
            return false;
        runtime->active_camera = camera;
        return true;
    }

    if (SDL_strcmp(type, "scene.set") == 0)
    {
        yyjson_val *authored_payload = obj_get(action, "payload");
        if (authored_payload == NULL)
            return slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                    payload);

        slayer3d_properties *scene_payload = properties_from_json_payload(authored_payload, payload);
        if (scene_payload == NULL)
            return false;
        const bool ok = slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                         scene_payload);
        slayer3d_properties_destroy(scene_payload);
        return ok;
    }

    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter_name = json_string(action, "adapter", NULL);
        adapter_entry *adapter = find_adapter(runtime, adapter_name);
        if (adapter == NULL)
            return false;
        const char *target_name = json_string(action, "target", NULL);
        if ((target_name == NULL || target_name[0] == '\0') && payload != NULL)
            target_name = slayer3d_properties_get_string(payload, "actor_name", NULL);
        slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, target_name);
        return invoke_adapter(runtime, adapter, target, payload);
    }

    if (SDL_strcmp(type, "branch") == 0)
    {
        yyjson_val *condition = obj_get(action, "if");
        const bool passed = eval_data_condition_with_payload(runtime, condition, NULL, payload);
        return execute_optional_action_array(runtime, obj_get(action, passed ? "then" : "else"), payload);
    }

    return false;
}

bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions, const slayer3d_properties *payload)
{
    if (!yyjson_is_arr(actions))
        return false;

    actor_lifecycle_defer_begin(runtime);
    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        yyjson_val *action = yyjson_arr_get(actions, i);
        if (yyjson_is_obj(action))
            ok = execute_one_action(runtime, action, payload) && ok;
    }
    actor_lifecycle_defer_end(runtime);
    return ok;
}

bool execute_optional_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                   const slayer3d_properties *payload)
{
    if (actions == NULL)
        return true;
    return execute_action_array(runtime, actions, payload);
}
