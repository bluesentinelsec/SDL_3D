/**
 * @file storage.c
 * @brief Persistent storage implementation.
 */

#include "sdl3d/storage.h"

#include <stdint.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

struct sdl3d_storage
{
    char *roots[SDL3D_STORAGE_ROOT_COUNT];
};

static void set_storage_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "unknown storage error");
}

static const char *non_empty_or_default(const char *value, const char *fallback)
{
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static bool append_text(char *out, int out_size, size_t *used, const char *text)
{
    if (out == NULL || out_size <= 0 || used == NULL || text == NULL)
        return false;
    const size_t len = SDL_strlen(text);
    if (*used > (size_t)out_size || len >= (size_t)out_size - *used)
        return false;
    SDL_memcpy(out + *used, text, len + 1u);
    *used += len;
    return true;
}

static bool append_separator(char *out, int out_size, size_t *used)
{
    if (used == NULL)
        return false;
    if (*used == 0)
        return true;
    if (out[*used - 1u] == '/' || out[*used - 1u] == '\\')
        return true;
    return append_text(out, out_size, used, "/");
}

static bool is_valid_segment(const char *segment)
{
    if (segment == NULL || segment[0] == '\0')
        return false;
    if (SDL_strcmp(segment, ".") == 0 || SDL_strcmp(segment, "..") == 0)
        return false;
    return SDL_strchr(segment, '/') == NULL && SDL_strchr(segment, '\\') == NULL && SDL_strchr(segment, ':') == NULL;
}

static bool append_segment(char *out, int out_size, size_t *used, const char *segment)
{
    return is_valid_segment(segment) && append_separator(out, out_size, used) &&
           append_text(out, out_size, used, segment);
}

static bool copy_root(const char *root, char *out, int out_size)
{
    if (root == NULL || root[0] == '\0' || out == NULL || out_size <= 0)
        return false;
    const size_t len = SDL_strlen(root);
    if (len >= (size_t)out_size)
        return false;
    SDL_memcpy(out, root, len + 1u);
    return true;
}

static char *join_alloc(const char *base, const char *leaf)
{
    if (base == NULL || leaf == NULL)
        return NULL;
    const size_t base_len = SDL_strlen(base);
    const bool needs_sep = base_len > 0 && base[base_len - 1u] != '/' && base[base_len - 1u] != '\\';
    const size_t leaf_len = SDL_strlen(leaf);
    char *result = (char *)SDL_malloc(base_len + (needs_sep ? 1u : 0u) + leaf_len + 1u);
    if (result == NULL)
        return NULL;
    SDL_memcpy(result, base, base_len);
    size_t used = base_len;
    if (needs_sep)
        result[used++] = '/';
    SDL_memcpy(result + used, leaf, leaf_len + 1u);
    return result;
}

static bool has_uri_scheme(const char *path)
{
    return path != NULL && SDL_strstr(path, "://") != NULL;
}

static bool root_from_virtual_path(const char *virtual_path, sdl3d_storage_root *out_root, const char **out_relative)
{
    if (virtual_path == NULL)
        return false;
    if (SDL_strncmp(virtual_path, "user://", 7) == 0)
    {
        if (out_root != NULL)
            *out_root = SDL3D_STORAGE_ROOT_USER;
        if (out_relative != NULL)
            *out_relative = virtual_path + 7;
        return true;
    }
    if (SDL_strncmp(virtual_path, "cache://", 8) == 0)
    {
        if (out_root != NULL)
            *out_root = SDL3D_STORAGE_ROOT_CACHE;
        if (out_relative != NULL)
            *out_relative = virtual_path + 8;
        return true;
    }
    return false;
}

static bool validate_relative_path(const char *path)
{
    if (path == NULL || path[0] == '\0' || path[0] == '/' || path[0] == '\\' || has_uri_scheme(path))
        return false;
    if (SDL_strchr(path, '\\') != NULL || SDL_strchr(path, ':') != NULL)
        return false;
    const size_t path_len = SDL_strlen(path);
    if (path[path_len - 1u] == '/')
        return false;

    const char *segment = path;
    while (*segment != '\0')
    {
        const char *end = SDL_strchr(segment, '/');
        const size_t len = end != NULL ? (size_t)(end - segment) : SDL_strlen(segment);
        if (len == 0u)
            return false;
        if ((len == 1u && segment[0] == '.') || (len == 2u && segment[0] == '.' && segment[1] == '.'))
            return false;
        if (end == NULL)
            break;
        segment = end + 1;
    }
    return true;
}

static bool make_directory_recursive(const char *path)
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

static char *parent_directory(const char *path)
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

static bool write_all(SDL_IOStream *stream, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t written = 0;
    while (written < size)
    {
        const size_t chunk = SDL_WriteIO(stream, bytes + written, size - written);
        if (chunk == 0u)
            return false;
        written += chunk;
    }
    return true;
}

void sdl3d_storage_config_init(sdl3d_storage_config *config)
{
    if (config == NULL)
        return;
    SDL_zero(*config);
    config->organization = "SDL3D";
    config->application = "SDL3D";
}

bool sdl3d_storage_build_root_path(const sdl3d_storage_config *config, sdl3d_storage_platform platform,
                                   sdl3d_storage_root root, const char *base_path, char *out_path, int out_path_size)
{
    (void)platform;
    if (root < 0 || root >= SDL3D_STORAGE_ROOT_COUNT || !copy_root(base_path, out_path, out_path_size))
        return false;

    size_t used = SDL_strlen(out_path);
    const char *organization = non_empty_or_default(config != NULL ? config->organization : NULL, "SDL3D");
    const char *application = non_empty_or_default(config != NULL ? config->application : NULL, "SDL3D");
    if (!append_segment(out_path, out_path_size, &used, organization) ||
        !append_segment(out_path, out_path_size, &used, application))
    {
        return false;
    }

    const char *profile = config != NULL ? config->profile : NULL;
    if (profile != NULL && profile[0] != '\0')
    {
        if (!append_segment(out_path, out_path_size, &used, "profiles") ||
            !append_segment(out_path, out_path_size, &used, profile))
        {
            return false;
        }
    }

    if (root == SDL3D_STORAGE_ROOT_CACHE && !append_segment(out_path, out_path_size, &used, "cache"))
        return false;
    return true;
}

bool sdl3d_storage_create(const sdl3d_storage_config *config, sdl3d_storage **out_storage, char *error_buffer,
                          int error_buffer_size)
{
    if (out_storage == NULL)
    {
        set_storage_error(error_buffer, error_buffer_size, "storage output pointer is required");
        return false;
    }
    *out_storage = NULL;

    sdl3d_storage_config defaults;
    sdl3d_storage_config_init(&defaults);
    if (config != NULL)
        defaults = *config;
    defaults.organization = non_empty_or_default(defaults.organization, "SDL3D");
    defaults.application = non_empty_or_default(defaults.application, "SDL3D");

    if (!is_valid_segment(defaults.organization) || !is_valid_segment(defaults.application) ||
        (defaults.profile != NULL && defaults.profile[0] != '\0' && !is_valid_segment(defaults.profile)))
    {
        set_storage_error(error_buffer, error_buffer_size, "storage metadata contains unsafe path characters");
        return false;
    }

    sdl3d_storage *storage = (sdl3d_storage *)SDL_calloc(1u, sizeof(*storage));
    if (storage == NULL)
    {
        set_storage_error(error_buffer, error_buffer_size, "failed to allocate storage context");
        return false;
    }

    if (defaults.user_root_override != NULL && defaults.user_root_override[0] != '\0')
    {
        storage->roots[SDL3D_STORAGE_ROOT_USER] = SDL_strdup(defaults.user_root_override);
    }
    else
    {
        char *pref = SDL_GetPrefPath(defaults.organization, defaults.application);
        if (pref != NULL && defaults.profile != NULL && defaults.profile[0] != '\0')
        {
            char *profiles = join_alloc(pref, "profiles");
            storage->roots[SDL3D_STORAGE_ROOT_USER] = join_alloc(profiles, defaults.profile);
            SDL_free(profiles);
        }
        else
        {
            storage->roots[SDL3D_STORAGE_ROOT_USER] = SDL_strdup(pref);
        }
        SDL_free(pref);
    }

    if (defaults.cache_root_override != NULL && defaults.cache_root_override[0] != '\0')
        storage->roots[SDL3D_STORAGE_ROOT_CACHE] = SDL_strdup(defaults.cache_root_override);
    else
        storage->roots[SDL3D_STORAGE_ROOT_CACHE] = join_alloc(storage->roots[SDL3D_STORAGE_ROOT_USER], "cache");

    if (storage->roots[SDL3D_STORAGE_ROOT_USER] == NULL || storage->roots[SDL3D_STORAGE_ROOT_CACHE] == NULL)
    {
        sdl3d_storage_destroy(storage);
        set_storage_error(error_buffer, error_buffer_size, "failed to resolve storage roots");
        return false;
    }

    if (!make_directory_recursive(storage->roots[SDL3D_STORAGE_ROOT_USER]) ||
        !make_directory_recursive(storage->roots[SDL3D_STORAGE_ROOT_CACHE]))
    {
        sdl3d_storage_destroy(storage);
        set_storage_error(error_buffer, error_buffer_size, "failed to create storage roots");
        return false;
    }

    *out_storage = storage;
    return true;
}

void sdl3d_storage_destroy(sdl3d_storage *storage)
{
    if (storage == NULL)
        return;
    for (int i = 0; i < SDL3D_STORAGE_ROOT_COUNT; ++i)
        SDL_free(storage->roots[i]);
    SDL_free(storage);
}

const char *sdl3d_storage_get_root(const sdl3d_storage *storage, sdl3d_storage_root root)
{
    if (storage == NULL || root < 0 || root >= SDL3D_STORAGE_ROOT_COUNT)
        return NULL;
    return storage->roots[root];
}

bool sdl3d_storage_resolve_path(const sdl3d_storage *storage, const char *virtual_path, char *out_path,
                                int out_path_size)
{
    sdl3d_storage_root root;
    const char *relative = NULL;
    if (storage == NULL || out_path == NULL || out_path_size <= 0 ||
        !root_from_virtual_path(virtual_path, &root, &relative) || !validate_relative_path(relative))
    {
        return false;
    }

    const char *root_path = storage->roots[root];
    if (root_path == NULL || root_path[0] == '\0')
        return false;

    const size_t root_len = SDL_strlen(root_path);
    const bool needs_sep = root_len > 0u && root_path[root_len - 1u] != '/' && root_path[root_len - 1u] != '\\';
    const size_t relative_len = SDL_strlen(relative);
    const size_t total = root_len + (needs_sep ? 1u : 0u) + relative_len;
    if (total >= (size_t)out_path_size)
        return false;
    SDL_memcpy(out_path, root_path, root_len);
    size_t used = root_len;
    if (needs_sep)
        out_path[used++] = '/';
    SDL_memcpy(out_path + used, relative, relative_len + 1u);
    return true;
}

bool sdl3d_storage_create_directory(sdl3d_storage *storage, const char *virtual_path, char *error_buffer,
                                    int error_buffer_size)
{
    char path[4096];
    if (!sdl3d_storage_resolve_path(storage, virtual_path, path, sizeof(path)))
    {
        set_storage_error(error_buffer, error_buffer_size, "invalid storage directory path");
        return false;
    }
    if (!make_directory_recursive(path))
    {
        set_storage_error(error_buffer, error_buffer_size, "failed to create storage directory");
        return false;
    }
    return true;
}

bool sdl3d_storage_exists(const sdl3d_storage *storage, const char *virtual_path)
{
    char path[4096];
    SDL_PathInfo info;
    SDL_zero(info);
    return sdl3d_storage_resolve_path(storage, virtual_path, path, sizeof(path)) && SDL_GetPathInfo(path, &info);
}

bool sdl3d_storage_read_file(const sdl3d_storage *storage, const char *virtual_path, sdl3d_storage_buffer *out_buffer,
                             char *error_buffer, int error_buffer_size)
{
    if (out_buffer == NULL)
    {
        set_storage_error(error_buffer, error_buffer_size, "storage read output buffer is required");
        return false;
    }
    out_buffer->data = NULL;
    out_buffer->size = 0u;

    char path[4096];
    if (!sdl3d_storage_resolve_path(storage, virtual_path, path, sizeof(path)))
    {
        set_storage_error(error_buffer, error_buffer_size, "invalid storage read path");
        return false;
    }

    size_t size = 0u;
    void *data = SDL_LoadFile(path, &size);
    if (data == NULL)
    {
        set_storage_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }
    out_buffer->data = data;
    out_buffer->size = size;
    return true;
}

bool sdl3d_storage_write_file(sdl3d_storage *storage, const char *virtual_path, const void *data, size_t size,
                              char *error_buffer, int error_buffer_size)
{
    if (data == NULL && size > 0u)
    {
        set_storage_error(error_buffer, error_buffer_size, "storage write data is required");
        return false;
    }

    char path[4096];
    if (!sdl3d_storage_resolve_path(storage, virtual_path, path, sizeof(path)))
    {
        set_storage_error(error_buffer, error_buffer_size, "invalid storage write path");
        return false;
    }

    char *parent = parent_directory(path);
    if (parent == NULL || !make_directory_recursive(parent))
    {
        SDL_free(parent);
        set_storage_error(error_buffer, error_buffer_size, "failed to create storage write directory");
        return false;
    }
    SDL_free(parent);

    bool ok = false;
    char temp_path[4096];
    for (int attempt = 0; attempt < 16 && !ok; ++attempt)
    {
        SDL_snprintf(temp_path, sizeof(temp_path), "%s.tmp.%llu.%d", path, (unsigned long long)SDL_GetTicksNS(),
                     attempt);
        SDL_IOStream *stream = SDL_IOFromFile(temp_path, "wb");
        if (stream == NULL)
            continue;
        ok = write_all(stream, data, size) && SDL_FlushIO(stream);
        ok = SDL_CloseIO(stream) && ok;
        if (!ok)
        {
            SDL_RemovePath(temp_path);
            continue;
        }
        if (!SDL_RenamePath(temp_path, path))
        {
            SDL_RemovePath(path);
            ok = SDL_RenamePath(temp_path, path);
        }
        if (!ok)
            SDL_RemovePath(temp_path);
    }

    if (!ok)
    {
        set_storage_error(error_buffer, error_buffer_size, "failed to write storage file");
        return false;
    }
    return true;
}

bool sdl3d_storage_delete(sdl3d_storage *storage, const char *virtual_path, char *error_buffer, int error_buffer_size)
{
    char path[4096];
    if (!sdl3d_storage_resolve_path(storage, virtual_path, path, sizeof(path)))
    {
        set_storage_error(error_buffer, error_buffer_size, "invalid storage delete path");
        return false;
    }
    if (!SDL_RemovePath(path))
    {
        set_storage_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }
    return true;
}

void sdl3d_storage_buffer_free(sdl3d_storage_buffer *buffer)
{
    if (buffer == NULL)
        return;
    SDL_free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0u;
}
