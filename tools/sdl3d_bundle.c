/**
 * @file sdl3d_bundle.c
 * @brief Build a Love2D-style fused SDL3D runner executable.
 */

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <stdint.h>
#include <stdio.h>

#include "sdl3d/asset.h"

#include "sdl3d_bundle_cli.h"
#include "sdl3d_fused.h"

typedef struct bundle_sources
{
    sdl3d_asset_pack_source *entries;
    int count;
    int capacity;
} bundle_sources;

typedef struct scan_context
{
    const char *root;
    const char *relative_dir;
    bundle_sources *sources;
    char error[256];
} scan_context;

static void bundle_set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "unknown error");
}

static char *bundle_join_path(const char *left, const char *right)
{
    if (left == NULL || left[0] == '\0')
        return right != NULL ? SDL_strdup(right) : NULL;
    if (right == NULL || right[0] == '\0')
        return SDL_strdup(left);

    const size_t left_len = SDL_strlen(left);
    const size_t right_len = SDL_strlen(right);
    const bool needs_sep = left[left_len - 1u] != '/' && left[left_len - 1u] != '\\';
    char *joined = (char *)SDL_malloc(left_len + (needs_sep ? 1u : 0u) + right_len + 1u);
    if (joined == NULL)
        return NULL;
    SDL_memcpy(joined, left, left_len);
    size_t offset = left_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, right, right_len);
    joined[offset + right_len] = '\0';
    return joined;
}

static char *bundle_append_suffix(const char *path, const char *suffix)
{
    if (path == NULL || suffix == NULL)
        return NULL;
    const size_t path_len = SDL_strlen(path);
    const size_t suffix_len = SDL_strlen(suffix);
    char *out = (char *)SDL_malloc(path_len + suffix_len + 1u);
    if (out == NULL)
        return NULL;
    SDL_memcpy(out, path, path_len);
    SDL_memcpy(out + path_len, suffix, suffix_len);
    out[path_len + suffix_len] = '\0';
    return out;
}

static void bundle_sources_destroy(bundle_sources *sources)
{
    if (sources == NULL)
        return;
    for (int i = 0; i < sources->count; ++i)
    {
        SDL_free((void *)sources->entries[i].asset_path);
        SDL_free((void *)sources->entries[i].source_path);
    }
    SDL_free(sources->entries);
    SDL_zero(*sources);
}

static bool bundle_sources_add(bundle_sources *sources, const char *asset_path, const char *source_path)
{
    if (sources == NULL || asset_path == NULL || asset_path[0] == '\0' || source_path == NULL || source_path[0] == '\0')
    {
        return false;
    }
    if (sources->count >= sources->capacity)
    {
        const int new_capacity = sources->capacity > 0 ? sources->capacity * 2 : 64;
        sdl3d_asset_pack_source *grown =
            (sdl3d_asset_pack_source *)SDL_realloc(sources->entries, (size_t)new_capacity * sizeof(*grown));
        if (grown == NULL)
            return false;
        sources->entries = grown;
        sources->capacity = new_capacity;
    }

    char *asset_copy = SDL_strdup(asset_path);
    char *source_copy = SDL_strdup(source_path);
    if (asset_copy == NULL || source_copy == NULL)
    {
        SDL_free(asset_copy);
        SDL_free(source_copy);
        return false;
    }
    sources->entries[sources->count].asset_path = asset_copy;
    sources->entries[sources->count].source_path = source_copy;
    sources->count++;
    return true;
}

static SDL_EnumerationResult SDLCALL scan_directory_callback(void *userdata, const char *dirname, const char *fname);

static bool scan_directory(scan_context *ctx, const char *relative_dir)
{
    char *path = bundle_join_path(ctx->root, relative_dir);
    if (path == NULL)
    {
        SDL_snprintf(ctx->error, sizeof(ctx->error), "%s", "failed to allocate scan path");
        return false;
    }

    const char *previous = ctx->relative_dir;
    ctx->relative_dir = relative_dir;
    const bool ok = SDL_EnumerateDirectory(path, scan_directory_callback, ctx);
    ctx->relative_dir = previous;
    SDL_free(path);
    if (!ok && ctx->error[0] == '\0')
        SDL_snprintf(ctx->error, sizeof(ctx->error), "%s", SDL_GetError());
    return ok;
}

static SDL_EnumerationResult SDLCALL scan_directory_callback(void *userdata, const char *dirname, const char *fname)
{
    scan_context *ctx = (scan_context *)userdata;
    (void)dirname;
    char *relative = bundle_join_path(ctx->relative_dir, fname);
    char *full = relative != NULL ? bundle_join_path(ctx->root, relative) : NULL;
    if (relative == NULL || full == NULL)
    {
        SDL_free(relative);
        SDL_free(full);
        SDL_snprintf(ctx->error, sizeof(ctx->error), "%s", "failed to allocate asset path");
        return SDL_ENUM_FAILURE;
    }

    SDL_PathInfo info;
    SDL_zero(info);
    if (!SDL_GetPathInfo(full, &info))
    {
        SDL_snprintf(ctx->error, sizeof(ctx->error), "failed to inspect '%s'", full);
        SDL_free(relative);
        SDL_free(full);
        return SDL_ENUM_FAILURE;
    }

    SDL_EnumerationResult result = SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY)
    {
        if (!scan_directory(ctx, relative))
            result = SDL_ENUM_FAILURE;
    }
    else if (info.type == SDL_PATHTYPE_FILE)
    {
        if (!bundle_sources_add(ctx->sources, relative, full))
        {
            SDL_snprintf(ctx->error, sizeof(ctx->error), "%s", "failed to record asset source");
            result = SDL_ENUM_FAILURE;
        }
    }

    SDL_free(relative);
    SDL_free(full);
    return result;
}

static bool bundle_collect_sources(const sdl3d_bundle_args *args, bundle_sources *sources, char *error_buffer,
                                   int error_buffer_size)
{
    if (args->file_count > 0)
    {
        for (int i = 0; i < args->file_count; ++i)
        {
            char *source_path = bundle_join_path(args->root, args->files[i]);
            if (source_path == NULL || !bundle_sources_add(sources, args->files[i], source_path))
            {
                SDL_free(source_path);
                bundle_set_error(error_buffer, error_buffer_size, "failed to collect explicit bundle files");
                return false;
            }
            SDL_free(source_path);
        }
    }
    else
    {
        scan_context ctx;
        SDL_zero(ctx);
        ctx.root = args->root;
        ctx.sources = sources;
        if (!scan_directory(&ctx, ""))
        {
            bundle_set_error(error_buffer, error_buffer_size,
                             ctx.error[0] != '\0' ? ctx.error : "failed to scan asset root");
            return false;
        }
    }

    if (sources->count <= 0)
    {
        bundle_set_error(error_buffer, error_buffer_size, "bundle asset root did not contain any files");
        return false;
    }
    return true;
}

static const char *bundle_data_asset_relative_path(const char *data_asset_path)
{
    return SDL_strncmp(data_asset_path, "asset://", 8) == 0 ? data_asset_path + 8 : data_asset_path;
}

static bool bundle_sources_include_data_asset(const bundle_sources *sources, const char *data_asset_path)
{
    const char *relative = bundle_data_asset_relative_path(data_asset_path);
    for (int i = 0; i < sources->count; ++i)
    {
        if (SDL_strcmp(sources->entries[i].asset_path, relative) == 0)
            return true;
    }
    return false;
}

static bool append_file_bytes(const char *output_path, const char *source_path, uint64_t *out_offset,
                              uint64_t *out_size, char *error_buffer, int error_buffer_size)
{
    if (out_offset != NULL)
        *out_offset = 0u;
    if (out_size != NULL)
        *out_size = 0u;

    SDL_IOStream *output = SDL_IOFromFile(output_path, "ab");
    if (output == NULL)
    {
        bundle_set_error(error_buffer, error_buffer_size, SDL_GetError());
        return false;
    }
    SDL_IOStream *source = SDL_IOFromFile(source_path, "rb");
    if (source == NULL)
    {
        bundle_set_error(error_buffer, error_buffer_size, SDL_GetError());
        SDL_CloseIO(output);
        return false;
    }

    const Sint64 offset = SDL_SeekIO(output, 0, SDL_IO_SEEK_END);
    bool ok = offset >= 0;
    uint64_t size = 0u;
    unsigned char buffer[64 * 1024];
    while (ok)
    {
        const size_t read = SDL_ReadIO(source, buffer, sizeof(buffer));
        if (read == 0u)
            break;
        if (SDL_WriteIO(output, buffer, read) != read)
        {
            bundle_set_error(error_buffer, error_buffer_size, "failed to append pack bytes");
            ok = false;
            break;
        }
        size += (uint64_t)read;
    }

    SDL_CloseIO(source);
    SDL_CloseIO(output);
    if (!ok)
        return false;
    if (out_offset != NULL)
        *out_offset = (uint64_t)offset;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

static void preserve_runner_permissions(const char *runner_path, const char *output_path)
{
#if !defined(_WIN32)
    struct stat st;
    if (runner_path != NULL && output_path != NULL && stat(runner_path, &st) == 0)
        (void)chmod(output_path, st.st_mode & 0777);
#else
    (void)runner_path;
    (void)output_path;
#endif
}

int main(int argc, char **argv)
{
    sdl3d_bundle_args args;
    const sdl3d_tool_cli_result cli_result = sdl3d_bundle_args_parse(argc, argv, &args, stderr);
    if (cli_result != SDL3D_TOOL_CLI_OK)
        return cli_result == SDL3D_TOOL_CLI_HELP ? 0 : 2;

    bundle_sources sources;
    SDL_zero(sources);
    char error[512] = "";
    bool ok = bundle_collect_sources(&args, &sources, error, (int)sizeof(error));
    if (ok && !bundle_sources_include_data_asset(&sources, args.data_asset_path))
    {
        SDL_snprintf(error, sizeof(error), "default data asset '%s' is not included in the bundle",
                     args.data_asset_path);
        ok = false;
    }

    char *temporary_pack = NULL;
    if (ok)
    {
        temporary_pack = bundle_append_suffix(args.output, ".tmp.sdl3dpak");
        if (temporary_pack == NULL)
        {
            bundle_set_error(error, (int)sizeof(error), "failed to allocate temporary pack path");
            ok = false;
        }
    }
    if (ok)
        ok = sdl3d_asset_pack_write_file(temporary_pack, sources.entries, sources.count, error, (int)sizeof(error));
    if (ok && !SDL_CopyFile(args.runner, args.output))
    {
        bundle_set_error(error, (int)sizeof(error), SDL_GetError());
        ok = false;
    }
    if (ok)
        preserve_runner_permissions(args.runner, args.output);

    uint64_t pack_offset = 0u;
    uint64_t pack_size = 0u;
    if (ok)
        ok = append_file_bytes(args.output, temporary_pack, &pack_offset, &pack_size, error, (int)sizeof(error));
    if (ok)
        ok = sdl3d_fused_append_footer(args.output, pack_offset, pack_size, args.data_asset_path, error,
                                       (int)sizeof(error));

    if (temporary_pack != NULL)
        (void)SDL_RemovePath(temporary_pack);
    SDL_free(temporary_pack);
    bundle_sources_destroy(&sources);
    sdl3d_bundle_args_destroy(&args);

    if (!ok)
    {
        fprintf(stderr, "sdl3d_bundle: %s\n", error[0] != '\0' ? error : "failed to build fused executable");
        return 1;
    }
    return 0;
}
