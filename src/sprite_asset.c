#include "sdl3d/sprite_asset.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/image.h"
#include "yyjson.h"

static void sprite_asset_set_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "sprite asset load failed");
}

static char *sprite_asset_strdup_range(const char *start, size_t length)
{
    char *copy = (char *)SDL_malloc(length + 1);
    if (copy == NULL)
        return NULL;

    if (length > 0)
        SDL_memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static char *sprite_asset_path_dirname(const char *path)
{
    const char *last_sep = NULL;
    if (path == NULL || path[0] == '\0')
        return NULL;

    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last_sep = p;
    }

    if (last_sep == NULL)
        return SDL_strdup(".");
    if (last_sep == path)
        return sprite_asset_strdup_range(path, 1);
    return sprite_asset_strdup_range(path, (size_t)(last_sep - path));
}

static char *sprite_asset_path_basename(const char *path)
{
    const char *last_sep = NULL;
    if (path == NULL || path[0] == '\0')
        return NULL;

    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last_sep = p;
    }

    return SDL_strdup(last_sep != NULL ? last_sep + 1 : path);
}

static bool sprite_asset_path_uses_resolver(const char *path)
{
    return path != NULL && SDL_strstr(path, "://") != NULL;
}

static bool sprite_asset_path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return SDL_strlen(path) > 2 && path[1] == ':';
}

static bool sprite_asset_load_image(const sdl3d_asset_resolver *assets, const char *path, sdl3d_image *out_image,
                                    char *error_buffer, int error_buffer_size)
{
    sdl3d_asset_buffer buffer;
    SDL_zero(buffer);

    if (path == NULL || out_image == NULL)
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite asset requires a valid image path");
        return SDL_InvalidParamError(path == NULL ? "path" : "out_image");
    }

    if (assets != NULL && (sprite_asset_path_uses_resolver(path) || !sprite_asset_path_is_absolute(path)))
    {
        if (!sdl3d_asset_resolver_read_file(assets, path, &buffer, error_buffer, error_buffer_size))
            return false;
        const bool decoded = sdl3d_load_image_from_memory(buffer.data, buffer.size, out_image);
        sdl3d_asset_buffer_free(&buffer);
        return decoded;
    }

    return sdl3d_load_image_from_file(path, out_image);
}

static void sprite_asset_free_textures(sdl3d_texture2d *textures, int count)
{
    if (textures == NULL || count <= 0)
        return;
    for (int i = 0; i < count; ++i)
        sdl3d_free_texture(&textures[i]);
    SDL_free(textures);
}

static bool sprite_asset_copy_slice(const sdl3d_image *source, int x, int y, int width, int height, sdl3d_image *out)
{
    const size_t bytes = (size_t)width * (size_t)height * 4u;
    Uint8 *pixels = (Uint8 *)SDL_malloc(bytes);
    if (pixels == NULL)
        return SDL_OutOfMemory();

    for (int row = 0; row < height; ++row)
    {
        const Uint8 *src = &source->pixels[((y + row) * source->width + x) * 4];
        Uint8 *dst = &pixels[(size_t)row * (size_t)width * 4u];
        SDL_memcpy(dst, src, (size_t)width * 4u);
    }

    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return true;
}

static bool sprite_asset_load_texture_slice(const sdl3d_image *source, int x, int y, int width, int height,
                                            sdl3d_texture2d *out_texture)
{
    sdl3d_image slice;
    SDL_zero(slice);
    if (!sprite_asset_copy_slice(source, x, y, width, height, &slice))
        return false;

    const bool ok = sdl3d_create_texture_from_image(&slice, out_texture);
    SDL_free(slice.pixels);
    SDL_zero(slice);
    return ok;
}

static bool sprite_asset_load_base_from_sheet(const sdl3d_image *sheet, const sdl3d_sprite_asset_source *source,
                                              sdl3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                              int error_buffer_size)
{
    for (int direction = 0; direction < source->direction_count; ++direction)
    {
        const int cell_index = direction;
        const int cell_x = (cell_index % source->columns) * source->frame_width;
        const int cell_y = (cell_index / source->columns) * source->frame_height;
        if (!sprite_asset_load_texture_slice(sheet, cell_x, cell_y, source->frame_width, source->frame_height,
                                             &out_sprite->base_textures[direction]))
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "failed to slice sprite base frame");
            return false;
        }
    }

    out_sprite->base_texture_count = source->direction_count;
    return true;
}

static bool sprite_asset_load_animation_from_sheet(const sdl3d_image *sheet, const sdl3d_sprite_asset_source *source,
                                                   sdl3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                                   int error_buffer_size)
{
    const int total_cells = source->frame_count * source->direction_count;
    for (int frame = 0; frame < source->frame_count; ++frame)
    {
        for (int direction = 0; direction < source->direction_count; ++direction)
        {
            const int cell_index = frame * source->direction_count + direction;
            if (cell_index >= source->columns * source->rows)
            {
                sprite_asset_set_error(error_buffer, error_buffer_size,
                                       "sprite sheet is too small for the declared layout");
                return false;
            }

            const int cell_x = (cell_index % source->columns) * source->frame_width;
            const int cell_y = (cell_index / source->columns) * source->frame_height;
            if (!sprite_asset_load_texture_slice(sheet, cell_x, cell_y, source->frame_width, source->frame_height,
                                                 &out_sprite->animation_textures[cell_index]))
            {
                sprite_asset_set_error(error_buffer, error_buffer_size, "failed to slice sprite animation frame");
                return false;
            }
        }
    }

    out_sprite->animation_texture_count = total_cells;
    return true;
}

static bool sprite_asset_load_textures_from_paths(const sdl3d_asset_resolver *assets, const char *const *paths,
                                                  int count, sdl3d_texture2d *out_textures, char *error_buffer,
                                                  int error_buffer_size)
{
    for (int i = 0; i < count; ++i)
    {
        sdl3d_image image;
        SDL_zero(image);
        if (!sprite_asset_load_image(assets, paths[i], &image, error_buffer, error_buffer_size))
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "failed to load sprite source image");
            return false;
        }

        if (!sdl3d_create_texture_from_image(&image, &out_textures[i]))
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "failed to create sprite texture");
            sdl3d_free_image(&image);
            return false;
        }
        sdl3d_free_image(&image);
    }
    return true;
}

static void sprite_asset_assign_rotation_set(sdl3d_sprite_rotation_set *set, sdl3d_texture2d *textures, int count)
{
    SDL_zero(*set);
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        set->frames[i] = (i < count) ? &textures[i] : NULL;
}

static bool sprite_asset_validate_source(const sdl3d_sprite_asset_source *source, char *error_buffer,
                                         int error_buffer_size)
{
    if (source == NULL)
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite asset source is required");
        return SDL_InvalidParamError("source");
    }
    if (source->direction_count <= 0 || source->direction_count > SDL3D_SPRITE_ROTATION_COUNT)
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite direction count must be between 1 and 8");
        return SDL_SetError("sprite direction count must be between 1 and 8");
    }
    if (source->kind == SDL3D_SPRITE_ASSET_SOURCE_SHEET)
    {
        if (source->sheet_path == NULL || source->sheet_path[0] == '\0' || source->frame_width <= 0 ||
            source->frame_height <= 0 || source->columns <= 0 || source->rows <= 0 || source->frame_count <= 0)
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "invalid sprite sheet metadata");
            return SDL_SetError("invalid sprite sheet metadata");
        }
        if (source->columns * source->rows < source->direction_count ||
            source->columns * source->rows < source->frame_count * source->direction_count)
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "sprite sheet dimensions do not cover all frames");
            return SDL_SetError("sprite sheet dimensions do not cover all frames");
        }
    }
    else if (source->kind == SDL3D_SPRITE_ASSET_SOURCE_FILES)
    {
        if (source->base_paths == NULL || source->frame_paths == NULL || source->frame_count <= 0)
        {
            sprite_asset_set_error(error_buffer, error_buffer_size,
                                   "file-list sprite assets require base and frame paths");
            return SDL_SetError("file-list sprite assets require base and frame paths");
        }
    }
    else
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "unknown sprite asset source kind");
        return SDL_SetError("unknown sprite asset source kind");
    }
    return true;
}

typedef struct sprite_asset_manifest
{
    sdl3d_sprite_asset_source source;
    const char **base_paths;
    int base_path_count;
    const char **frame_paths;
    int frame_path_count;
} sprite_asset_manifest;

static void sprite_asset_manifest_free(sprite_asset_manifest *manifest)
{
    if (manifest == NULL)
        return;

    SDL_free(manifest->base_paths);
    SDL_free(manifest->frame_paths);
    SDL_zero(*manifest);
}

static const char *sprite_asset_manifest_get_string(yyjson_val *object, const char *key, const char *fallback)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, key));
    return value != NULL ? value : fallback;
}

static int sprite_asset_manifest_get_int(yyjson_val *object, const char *key, int fallback)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    return yyjson_is_int(value) ? (int)yyjson_get_int(value) : fallback;
}

static float sprite_asset_manifest_get_float(yyjson_val *object, const char *key, float fallback)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    return yyjson_is_real(value) || yyjson_is_int(value) ? (float)yyjson_get_real(value) : fallback;
}

static bool sprite_asset_manifest_get_bool(yyjson_val *object, const char *key, bool fallback)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

static bool sprite_asset_manifest_read_string_array(yyjson_val *array, const char ***out_paths, int *out_count,
                                                    char *error_buffer, int error_buffer_size)
{
    const size_t count = yyjson_arr_size(array);
    const char **paths = NULL;

    if (!yyjson_is_arr(array))
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite manifest path list must be an array");
        return SDL_SetError("sprite manifest path list must be an array");
    }
    if (count == 0)
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite manifest path list cannot be empty");
        return SDL_SetError("sprite manifest path list cannot be empty");
    }

    paths = (const char **)SDL_calloc(count, sizeof(const char *));
    if (paths == NULL)
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "failed to allocate sprite manifest path list");
        return SDL_OutOfMemory();
    }

    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *item = yyjson_arr_get(array, i);
        const char *text = yyjson_get_str(item);
        if (text == NULL || text[0] == '\0')
        {
            SDL_free(paths);
            sprite_asset_set_error(error_buffer, error_buffer_size, "sprite manifest path entries must be strings");
            return SDL_SetError("sprite manifest path entries must be strings");
        }
        paths[i] = text;
    }

    *out_paths = paths;
    *out_count = (int)count;
    return true;
}

static bool sprite_asset_manifest_parse(yyjson_val *root, sprite_asset_manifest *manifest, char *error_buffer,
                                        int error_buffer_size)
{
    yyjson_val *kind_value;
    const char *kind;

    if (manifest == NULL)
        return SDL_InvalidParamError("manifest");
    SDL_zero(*manifest);

    if (!yyjson_is_obj(root))
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite manifest root must be an object");
        return SDL_SetError("sprite manifest root must be an object");
    }

    kind_value = yyjson_obj_get(root, "kind");
    kind = yyjson_get_str(kind_value);
    if (kind == NULL || kind[0] == '\0')
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite manifest requires a kind");
        return SDL_SetError("sprite manifest requires a kind");
    }

    SDL_zero(manifest->source);
    manifest->source.kind = SDL3D_SPRITE_ASSET_SOURCE_SHEET;
    manifest->source.direction_count = 1;
    manifest->source.frame_count = 1;
    manifest->source.loop = true;
    manifest->source.lighting = true;

    if (SDL_strcmp(kind, "sheet") == 0)
    {
        manifest->source.kind = SDL3D_SPRITE_ASSET_SOURCE_SHEET;
        manifest->source.sheet_path = sprite_asset_manifest_get_string(root, "sheet_path", NULL);
        manifest->source.frame_width = sprite_asset_manifest_get_int(root, "frame_width", 0);
        manifest->source.frame_height = sprite_asset_manifest_get_int(root, "frame_height", 0);
        manifest->source.columns = sprite_asset_manifest_get_int(root, "columns", 0);
        manifest->source.rows = sprite_asset_manifest_get_int(root, "rows", 0);
        manifest->source.frame_count = sprite_asset_manifest_get_int(root, "frame_count", 0);
        manifest->source.direction_count = sprite_asset_manifest_get_int(root, "direction_count", 0);
    }
    else if (SDL_strcmp(kind, "files") == 0)
    {
        manifest->source.kind = SDL3D_SPRITE_ASSET_SOURCE_FILES;
        yyjson_val *base_paths = yyjson_obj_get(root, "base_paths");
        yyjson_val *frame_paths = yyjson_obj_get(root, "frame_paths");
        if (!sprite_asset_manifest_read_string_array(base_paths, &manifest->base_paths, &manifest->base_path_count,
                                                     error_buffer, error_buffer_size))
            return false;
        if (!sprite_asset_manifest_read_string_array(frame_paths, &manifest->frame_paths, &manifest->frame_path_count,
                                                     error_buffer, error_buffer_size))
        {
            sprite_asset_manifest_free(manifest);
            return false;
        }
        manifest->source.base_paths = manifest->base_paths;
        manifest->source.frame_paths = manifest->frame_paths;
        manifest->source.frame_count = sprite_asset_manifest_get_int(root, "frame_count", 0);
        manifest->source.direction_count = sprite_asset_manifest_get_int(root, "direction_count", 0);
    }
    else
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "sprite manifest kind must be 'sheet' or 'files'");
        return SDL_SetError("sprite manifest kind must be 'sheet' or 'files'");
    }

    manifest->source.fps = sprite_asset_manifest_get_float(root, "fps", 0.0f);
    manifest->source.loop = sprite_asset_manifest_get_bool(root, "loop", true);
    manifest->source.lighting = sprite_asset_manifest_get_bool(root, "lighting", true);
    manifest->source.emissive = sprite_asset_manifest_get_bool(root, "emissive", false);
    manifest->source.visual_ground_offset = sprite_asset_manifest_get_float(root, "visual_ground_offset", 0.0f);

    return true;
}

bool sdl3d_sprite_asset_load(const sdl3d_asset_resolver *assets, const sdl3d_sprite_asset_source *source,
                             sdl3d_sprite_asset_runtime *out_sprite, char *error_buffer, int error_buffer_size)
{
    sdl3d_image sheet;
    SDL_zero(sheet);

    if (out_sprite != NULL)
        SDL_zero(*out_sprite);
    if (out_sprite == NULL)
        return SDL_InvalidParamError("out_sprite");
    if (!sprite_asset_validate_source(source, error_buffer, error_buffer_size))
        return false;

    out_sprite->direction_count = source->direction_count;
    out_sprite->fps = source->fps;
    out_sprite->loop = source->loop;
    out_sprite->lighting = source->lighting;
    out_sprite->emissive = source->emissive;
    out_sprite->visual_ground_offset = source->visual_ground_offset;

    if (source->kind == SDL3D_SPRITE_ASSET_SOURCE_SHEET)
    {
        if (!sprite_asset_load_image(assets, source->sheet_path, &sheet, error_buffer, error_buffer_size))
            goto fail;

        out_sprite->base_texture_count = source->direction_count;
        out_sprite->base_textures =
            (sdl3d_texture2d *)SDL_calloc((size_t)out_sprite->base_texture_count, sizeof(sdl3d_texture2d));
        if (out_sprite->base_textures == NULL)
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "failed to allocate sprite base textures");
            goto fail;
        }
        if (!sprite_asset_load_base_from_sheet(&sheet, source, out_sprite, error_buffer, error_buffer_size))
            goto fail;

        out_sprite->animation_texture_count = source->frame_count * source->direction_count;
        out_sprite->animation_textures =
            (sdl3d_texture2d *)SDL_calloc((size_t)out_sprite->animation_texture_count, sizeof(sdl3d_texture2d));
        out_sprite->animation_frames =
            (sdl3d_sprite_rotation_set *)SDL_calloc((size_t)source->frame_count, sizeof(sdl3d_sprite_rotation_set));
        if (out_sprite->animation_textures == NULL || out_sprite->animation_frames == NULL)
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "failed to allocate sprite animation textures");
            goto fail;
        }
        if (!sprite_asset_load_animation_from_sheet(&sheet, source, out_sprite, error_buffer, error_buffer_size))
            goto fail;
    }
    else
    {
        out_sprite->base_texture_count = source->direction_count;
        out_sprite->base_textures =
            (sdl3d_texture2d *)SDL_calloc((size_t)out_sprite->base_texture_count, sizeof(sdl3d_texture2d));
        out_sprite->animation_texture_count = source->frame_count * source->direction_count;
        out_sprite->animation_textures =
            (sdl3d_texture2d *)SDL_calloc((size_t)out_sprite->animation_texture_count, sizeof(sdl3d_texture2d));
        out_sprite->animation_frames =
            (sdl3d_sprite_rotation_set *)SDL_calloc((size_t)source->frame_count, sizeof(sdl3d_sprite_rotation_set));
        if (out_sprite->base_textures == NULL || out_sprite->animation_textures == NULL ||
            out_sprite->animation_frames == NULL)
        {
            sprite_asset_set_error(error_buffer, error_buffer_size, "failed to allocate sprite textures");
            goto fail;
        }

        if (!sprite_asset_load_textures_from_paths(assets, source->base_paths, source->direction_count,
                                                   out_sprite->base_textures, error_buffer, error_buffer_size))
            goto fail;
        if (!sprite_asset_load_textures_from_paths(assets, source->frame_paths,
                                                   source->frame_count * source->direction_count,
                                                   out_sprite->animation_textures, error_buffer, error_buffer_size))
            goto fail;
    }

    sprite_asset_assign_rotation_set(&out_sprite->base_rotations, out_sprite->base_textures,
                                     out_sprite->base_texture_count);
    for (int frame = 0; frame < source->frame_count; ++frame)
    {
        sprite_asset_assign_rotation_set(&out_sprite->animation_frames[frame],
                                         &out_sprite->animation_textures[frame * source->direction_count],
                                         source->direction_count);
    }
    out_sprite->animation_frame_count = source->frame_count;
    sdl3d_free_image(&sheet);
    return true;

fail:
    sdl3d_free_image(&sheet);
    sdl3d_sprite_asset_free(out_sprite);
    return false;
}

bool sdl3d_sprite_asset_load_file(const char *path, sdl3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                  int error_buffer_size)
{
    sdl3d_asset_resolver *assets = NULL;
    sdl3d_asset_buffer buffer;
    yyjson_doc *doc = NULL;
    sprite_asset_manifest manifest;
    char *base_dir = NULL;
    char *file_name = NULL;
    bool ok = false;

    if (out_sprite != NULL)
        SDL_zero(*out_sprite);
    if (path == NULL || path[0] == '\0' || out_sprite == NULL)
        return SDL_InvalidParamError(path == NULL || path[0] == '\0' ? "path" : "out_sprite");

    base_dir = sprite_asset_path_dirname(path);
    file_name = sprite_asset_path_basename(path);
    assets = sdl3d_asset_resolver_create();
    SDL_zero(buffer);
    if (base_dir == NULL || file_name == NULL || assets == NULL)
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "failed to create sprite manifest resolver");
        goto done;
    }

    if (!sdl3d_asset_resolver_mount_directory(assets, base_dir, error_buffer, error_buffer_size))
        goto done;

    if (!sdl3d_asset_resolver_read_file(assets, file_name, &buffer, error_buffer, error_buffer_size))
        goto done;

    doc = yyjson_read(buffer.data, buffer.size, YYJSON_READ_NOFLAG);
    if (doc == NULL)
    {
        sprite_asset_set_error(error_buffer, error_buffer_size, "failed to parse sprite manifest JSON");
        goto done;
    }

    if (!sprite_asset_manifest_parse(yyjson_doc_get_root(doc), &manifest, error_buffer, error_buffer_size))
        goto done;

    ok = sdl3d_sprite_asset_load(assets, &manifest.source, out_sprite, error_buffer, error_buffer_size);
    sprite_asset_manifest_free(&manifest);

done:
    yyjson_doc_free(doc);
    sdl3d_asset_buffer_free(&buffer);
    sdl3d_asset_resolver_destroy(assets);
    SDL_free(base_dir);
    SDL_free(file_name);
    if (!ok && out_sprite != NULL)
        SDL_zero(*out_sprite);
    return ok;
}

void sdl3d_sprite_asset_free(sdl3d_sprite_asset_runtime *sprite)
{
    if (sprite == NULL)
        return;

    if (sprite->base_textures != NULL)
        sprite_asset_free_textures(sprite->base_textures, sprite->base_texture_count);
    if (sprite->animation_textures != NULL)
        sprite_asset_free_textures(sprite->animation_textures, sprite->animation_texture_count);
    SDL_free(sprite->animation_frames);
    SDL_zero(*sprite);
}

const sdl3d_sprite_rotation_set *sdl3d_sprite_asset_base_rotations(const sdl3d_sprite_asset_runtime *sprite)
{
    return sprite != NULL ? &sprite->base_rotations : NULL;
}

const sdl3d_sprite_rotation_set *sdl3d_sprite_asset_animation_frames(const sdl3d_sprite_asset_runtime *sprite)
{
    return sprite != NULL ? sprite->animation_frames : NULL;
}

int sdl3d_sprite_asset_animation_frame_count(const sdl3d_sprite_asset_runtime *sprite)
{
    return sprite != NULL ? sprite->animation_frame_count : 0;
}

void sdl3d_sprite_asset_apply_actor(sdl3d_sprite_actor *actor, const sdl3d_sprite_asset_runtime *sprite)
{
    if (actor == NULL || sprite == NULL)
        return;

    actor->texture = sprite->base_texture_count > 0 ? sprite->base_textures : NULL;
    actor->rotations = sdl3d_sprite_asset_base_rotations(sprite);
    actor->animation_frames = sdl3d_sprite_asset_animation_frames(sprite);
    actor->animation_frame_count = sdl3d_sprite_asset_animation_frame_count(sprite);
    actor->animation_fps = sprite->fps;
    actor->animation_loop = sprite->loop;
    actor->visual_ground_offset = sprite->visual_ground_offset;
}
