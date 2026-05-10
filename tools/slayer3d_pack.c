/**
 * @file slayer3d_pack.c
 * @brief Build-time SLAYER3D asset pack writer.
 */

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <stdio.h>

#include "slayer3d/asset.h"

#include "slayer3d_pack_cli.h"

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
    slayer3d_pack_args args;
    const slayer3d_tool_cli_result cli_result = slayer3d_pack_args_parse(argc, argv, &args, stderr);
    if (cli_result != SLAYER3D_TOOL_CLI_OK)
    {
        return cli_result == SLAYER3D_TOOL_CLI_HELP ? 0 : 2;
    }

    slayer3d_asset_pack_source *sources =
        (slayer3d_asset_pack_source *)SDL_calloc((size_t)args.file_count, sizeof(*sources));
    if (sources == NULL)
    {
        fprintf(stderr, "slayer3d_pack: failed to allocate source table\n");
        slayer3d_pack_args_destroy(&args);
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
        ok = slayer3d_asset_pack_write_file(args.output, sources, args.file_count, error, (int)sizeof(error));
    if (!ok)
        fprintf(stderr, "slayer3d_pack: %s\n", error[0] != '\0' ? error : "failed to write pack");

    for (int i = 0; i < args.file_count; ++i)
        SDL_free((void *)sources[i].source_path);
    SDL_free(sources);
    slayer3d_pack_args_destroy(&args);
    return ok ? 0 : 1;
}
