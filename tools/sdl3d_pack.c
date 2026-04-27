/**
 * @file sdl3d_pack.c
 * @brief Build-time SDL3D asset pack writer.
 */

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdio.h>

#include "sdl3d/asset.h"

typedef struct pack_args
{
    const char *output;
    const char *root;
    const char **files;
    int file_count;
} pack_args;

static void print_usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s --output <pack.sdl3dpak> --root <asset-root> --file <relative-path> [--file ...]\n",
            argv0 != NULL ? argv0 : "sdl3d_pack");
}

static bool append_file_arg(pack_args *args, const char *file)
{
    const char **files = (const char **)SDL_realloc(args->files, (size_t)(args->file_count + 1) * sizeof(*files));
    if (files == NULL)
        return false;
    args->files = files;
    args->files[args->file_count++] = file;
    return true;
}

static bool parse_args(int argc, char **argv, pack_args *args)
{
    SDL_zero(*args);
    for (int i = 1; i < argc; ++i)
    {
        if (SDL_strcmp(argv[i], "--output") == 0 && i + 1 < argc)
        {
            args->output = argv[++i];
        }
        else if (SDL_strcmp(argv[i], "--root") == 0 && i + 1 < argc)
        {
            args->root = argv[++i];
        }
        else if (SDL_strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
            if (!append_file_arg(args, argv[++i]))
                return false;
        }
        else
        {
            return false;
        }
    }

    return args->output != NULL && args->root != NULL && args->file_count > 0;
}

static char *join_path(const char *root, const char *relative)
{
    const size_t root_len = SDL_strlen(root);
    const size_t rel_len = SDL_strlen(relative);
    const bool needs_sep = root_len > 0u && root[root_len - 1u] != '/' && root[root_len - 1u] != '\\';
    char *joined = (char *)SDL_malloc(root_len + (needs_sep ? 1u : 0u) + rel_len + 1u);
    if (joined == NULL)
        return NULL;

    SDL_memcpy(joined, root, root_len);
    size_t offset = root_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, relative, rel_len);
    joined[offset + rel_len] = '\0';
    return joined;
}

int main(int argc, char **argv)
{
    pack_args args;
    if (!parse_args(argc, argv, &args))
    {
        print_usage(argc > 0 ? argv[0] : NULL);
        SDL_free(args.files);
        return 2;
    }

    sdl3d_asset_pack_source *sources = (sdl3d_asset_pack_source *)SDL_calloc((size_t)args.file_count, sizeof(*sources));
    if (sources == NULL)
    {
        fprintf(stderr, "sdl3d_pack: failed to allocate source table\n");
        SDL_free(args.files);
        return 1;
    }

    bool ok = true;
    for (int i = 0; i < args.file_count; ++i)
    {
        sources[i].asset_path = args.files[i];
        sources[i].source_path = join_path(args.root, args.files[i]);
        if (sources[i].source_path == NULL)
        {
            ok = false;
            break;
        }
    }

    char error[512] = "";
    if (ok)
        ok = sdl3d_asset_pack_write_file(args.output, sources, args.file_count, error, (int)sizeof(error));
    if (!ok)
        fprintf(stderr, "sdl3d_pack: %s\n", error[0] != '\0' ? error : "failed to write pack");

    for (int i = 0; i < args.file_count; ++i)
        SDL_free((void *)sources[i].source_path);
    SDL_free(sources);
    SDL_free(args.files);
    return ok ? 0 : 1;
}
