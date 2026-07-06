/**
 * @file slayer3d_editor_cli.c
 * @brief Command-line parser for the SLAYER3D editor host.
 */

#include "slayer3d_editor_cli.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <argtable3.h>

#include "slayer3d/map.h"

#include "../vendor/yyjson/yyjson.h"

#define SLAYER3D_EDITOR_EMBEDDED_PROJECT_URI "embedded://slayer3d_editor"
#if !defined(SLAYER3D_EDITOR_EMBEDDED_DATA_ASSET)
#define SLAYER3D_EDITOR_EMBEDDED_DATA_ASSET "asset://slayer3d_editor.game.json"
#endif

static void editor_set_error(char *buffer, int buffer_size, const char *message)
{
    if (buffer != NULL && buffer_size > 0)
        SDL_snprintf(buffer, (size_t)buffer_size, "%s", message != NULL ? message : "unknown editor CLI error");
}

static void editor_set_errorf(char *buffer, int buffer_size, const char *format, const char *value)
{
    if (buffer != NULL && buffer_size > 0)
        SDL_snprintf(buffer, (size_t)buffer_size, format != NULL ? format : "%s", value != NULL ? value : "");
}

void slayer3d_editor_args_print_usage(const char *argv0, FILE *stream)
{
    FILE *out = stream != NULL ? stream : stderr;
    const char *program = argv0 != NULL ? argv0 : "slayer3d_editor";
    fprintf(
        out,
        "Usage:\n"
        "  %s [--texture-path <dir>] [--model-path <dir>]\n"
        "  %s new --project <project-dir-or-json> --output <level.json> [--texture-path <dir>] [--model-path <dir>] "
        "[--overwrite]\n"
        "  %s open --project <project-dir-or-json> --input <level.json> [--output <level.json>] [--texture-path <dir>] "
        "[--model-path <dir>] "
        "[--overwrite]\n"
        "  %s lighting-plan --input <map.slayermap.json> [--preview|--final] [--max-dynamic-lights <n>] "
        "[--max-static-lights <n>] [--no-dynamic-preview] [--manifest|--static-artifact] [--output <file>]\n"
        "  %s lighting-artifact-validate --input <lighting-static.json>\n"
        "\n"
        "Commands:\n"
        "  no args  Launch the embedded Slayer3D Editor shell with a new untitled map.\n"
        "  new    Start from the project's empty editor level and save to --output.\n"
        "  open   Load an editable level fragment from --input and save to --output, or back to --input if omitted.\n"
        "  lighting-plan  Print the shared map lighting build/bake plan without launching the editor.\n"
        "  lighting-artifact-validate  Validate a generated static-lighting artifact JSON file.\n"
        "\n"
        "Project manifests use schema slayer3d.project.v0 and define data_root plus editor_entry.\n",
        program, program, program, program, program);
}

static bool path_is_absolute_tool(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return SDL_strlen(path) > 2u && path[1] == ':';
}

static void path_normalize_host_separators_tool(char *path)
{
#if defined(_WIN32)
    if (path == NULL)
        return;
    for (char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/')
            *p = '\\';
    }
#else
    (void)path;
#endif
}

static char path_separator_tool(void)
{
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

static char *path_dirname_tool(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return SDL_strdup(".");
    const char *last = NULL;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p;
    }
    if (last == NULL)
        return SDL_strdup(".");
    const size_t length = (size_t)(last - path);
    if (length == 0u)
        return SDL_strdup(path[0] == '\\' ? "\\" : "/");
    char *dir = (char *)SDL_malloc(length + 1u);
    if (dir == NULL)
        return NULL;
    SDL_memcpy(dir, path, length);
    dir[length] = '\0';
    return dir;
}

static char *path_join_tool(const char *base, const char *path)
{
    if (path == NULL)
        return NULL;
    if (path_is_absolute_tool(path) || base == NULL || base[0] == '\0')
    {
        char *copy = SDL_strdup(path);
        path_normalize_host_separators_tool(copy);
        return copy;
    }
    const size_t base_len = SDL_strlen(base);
    const size_t path_len = SDL_strlen(path);
    const bool needs_sep = base_len > 0u && base[base_len - 1u] != '/' && base[base_len - 1u] != '\\';
    char *joined = (char *)SDL_malloc(base_len + (needs_sep ? 1u : 0u) + path_len + 1u);
    if (joined == NULL)
        return NULL;
    SDL_memcpy(joined, base, base_len);
    size_t offset = base_len;
    if (needs_sep)
        joined[offset++] = path_separator_tool();
    SDL_memcpy(joined + offset, path, path_len + 1u);
    path_normalize_host_separators_tool(joined);
    return joined;
}

static char *path_make_absolute_tool(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return NULL;
    if (path_is_absolute_tool(path))
    {
        char *copy = SDL_strdup(path);
        path_normalize_host_separators_tool(copy);
        return copy;
    }

    char *cwd = SDL_GetCurrentDirectory();
    if (cwd == NULL)
        return NULL;
    char *absolute = path_join_tool(cwd, path);
    SDL_free(cwd);
    return absolute;
}

static char *path_join_relative_tool(const char *base, const char *path)
{
    if (path == NULL || path[0] == '\0')
        return SDL_strdup(base != NULL ? base : "");
    if (path_is_absolute_tool(path) || base == NULL || base[0] == '\0')
        return SDL_strdup(path);
    const size_t base_len = SDL_strlen(base);
    const size_t path_len = SDL_strlen(path);
    const bool needs_sep = base_len > 0u && base[base_len - 1u] != '/' && base[base_len - 1u] != '\\';
    char *joined = (char *)SDL_malloc(base_len + (needs_sep ? 1u : 0u) + path_len + 1u);
    if (joined == NULL)
        return NULL;
    SDL_memcpy(joined, base, base_len);
    size_t offset = base_len;
    if (needs_sep)
        joined[offset++] = '/';
    SDL_memcpy(joined + offset, path, path_len + 1u);
    return joined;
}

static bool file_exists_tool(const char *path, bool *out_is_file)
{
    if (out_is_file != NULL)
        *out_is_file = false;
    SDL_PathInfo info;
    SDL_zero(info);
    if (path == NULL || path[0] == '\0' || !SDL_GetPathInfo(path, &info))
        return false;
    if (out_is_file != NULL)
        *out_is_file = info.type == SDL_PATHTYPE_FILE;
    return true;
}

static bool directory_exists_tool(const char *path)
{
    SDL_PathInfo info;
    SDL_zero(info);
    return path != NULL && path[0] != '\0' && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static bool make_directory_recursive_tool(const char *path)
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
    for (char *p = copy; *p != '\0'; ++p)
    {
        if (*p != '/' && *p != '\\')
            continue;
        const char saved = *p;
        *p = '\0';
        if (copy[0] != '\0' && !(SDL_strlen(copy) == 2u && copy[1] == ':'))
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

static bool editor_write_output_bytes(const char *path, const char *bytes, size_t byte_count, char *error_buffer,
                                      int error_buffer_size)
{
    if (path == NULL || path[0] == '\0')
    {
        editor_set_error(error_buffer, error_buffer_size, "output path is required");
        return false;
    }

    char *parent = path_dirname_tool(path);
    if (parent == NULL || !make_directory_recursive_tool(parent))
    {
        editor_set_errorf(error_buffer, error_buffer_size, "failed to create output directory for '%s'", path);
        SDL_free(parent);
        return false;
    }
    SDL_free(parent);

    SDL_IOStream *stream = SDL_IOFromFile(path, "wb");
    if (stream == NULL)
    {
        editor_set_errorf(error_buffer, error_buffer_size, "failed to open output '%s'", path);
        return false;
    }
    const bool ok = SDL_WriteIO(stream, bytes, byte_count) == byte_count;
    SDL_CloseIO(stream);
    if (!ok)
    {
        editor_set_errorf(error_buffer, error_buffer_size, "failed to write output '%s'", path);
        return false;
    }
    return true;
}

static char *editor_default_project_path(void)
{
    const char *env_project = SDL_getenv("SLAYER3D_EDITOR_PROJECT");
    if (directory_exists_tool(env_project))
        return SDL_strdup(env_project);

#if defined(SLAYER3D_EDITOR_EMBEDDED_ASSETS)
    return SDL_strdup(SLAYER3D_EDITOR_EMBEDDED_PROJECT_URI);
#else
#if defined(SLAYER3D_EDITOR_DEFAULT_PROJECT)
    if (directory_exists_tool(SLAYER3D_EDITOR_DEFAULT_PROJECT))
        return SDL_strdup(SLAYER3D_EDITOR_DEFAULT_PROJECT);
#endif

    if (directory_exists_tool("apps/slayer3d_editor"))
        return SDL_strdup("apps/slayer3d_editor");

    const char *base_path = SDL_GetBasePath();
    char *candidate = NULL;
    if (base_path != NULL)
    {
        candidate = path_join_tool(base_path, "apps/slayer3d_editor");
        if (!directory_exists_tool(candidate))
        {
            SDL_free(candidate);
            candidate = path_join_tool(base_path, "slayer3d_editor");
        }
        if (!directory_exists_tool(candidate))
        {
            SDL_free(candidate);
            candidate = path_join_tool(base_path, "../share/slayer3d/apps/slayer3d_editor");
        }
        if (!directory_exists_tool(candidate))
        {
            SDL_free(candidate);
            candidate = path_join_tool(base_path, "../Resources/slayer3d_editor");
        }
    }
    if (directory_exists_tool(candidate))
        return candidate;
    SDL_free(candidate);

#if defined(SLAYER3D_EDITOR_DEFAULT_PROJECT)
    return SDL_strdup(SLAYER3D_EDITOR_DEFAULT_PROJECT);
#else
    return SDL_strdup("apps/slayer3d_editor");
#endif
#endif
}

static char *editor_default_output_path(void)
{
    char *pref_path = SDL_GetPrefPath("bluesentinelsec", "Slayer3DEditor");
    if (pref_path == NULL)
        return SDL_strdup("untitled.slayermap.json");

    char leaf[64];
    char *candidate = NULL;
    bool is_file = false;
    for (int i = 0; i < 10000; ++i)
    {
        if (i == 0)
            SDL_strlcpy(leaf, "untitled.slayermap.json", sizeof(leaf));
        else
            SDL_snprintf(leaf, sizeof(leaf), "untitled-%d.slayermap.json", i);
        candidate = path_join_tool(pref_path, leaf);
        if (candidate == NULL || !file_exists_tool(candidate, &is_file))
            break;
        SDL_free(candidate);
        candidate = NULL;
    }

    SDL_free(pref_path);
    return candidate != NULL ? candidate : SDL_strdup("untitled.slayermap.json");
}

static void editor_asset_source_destroy(slayer3d_editor_asset_source *source)
{
    if (source == NULL)
        return;
    SDL_free(source->path);
    SDL_free(source->relative_path);
    SDL_zero(*source);
}

static void editor_asset_sources_destroy(slayer3d_editor_asset_sources *sources);

static bool editor_asset_source_copy(const slayer3d_editor_asset_source *source,
                                     slayer3d_editor_asset_source *out_source)
{
    if (source == NULL || out_source == NULL)
        return false;
    SDL_zero(*out_source);
    out_source->path = SDL_strdup(source->path != NULL ? source->path : "");
    out_source->relative_path = SDL_strdup(source->relative_path != NULL ? source->relative_path : "");
    out_source->available = source->available;
    if (out_source->path == NULL || out_source->relative_path == NULL)
    {
        editor_asset_source_destroy(out_source);
        return false;
    }
    return true;
}

#if defined(SLAYER3D_EDITOR_EMBEDDED_ASSETS)
static bool editor_asset_source_init(slayer3d_editor_asset_source *source, const char *path, const char *relative_path,
                                     bool available)
{
    if (source == NULL)
        return false;
    SDL_zero(*source);
    source->path = SDL_strdup(path != NULL ? path : "");
    source->relative_path = SDL_strdup(relative_path != NULL ? relative_path : "");
    source->available = available;
    if (source->path == NULL || source->relative_path == NULL)
    {
        editor_asset_source_destroy(source);
        return false;
    }
    return true;
}
#endif

static bool editor_asset_sources_copy(const slayer3d_editor_asset_sources *sources,
                                      slayer3d_editor_asset_sources *out_sources)
{
    if (sources == NULL || out_sources == NULL)
        return false;
    SDL_zero(*out_sources);
    if (!editor_asset_source_copy(&sources->textures, &out_sources->textures) ||
        !editor_asset_source_copy(&sources->models, &out_sources->models) ||
        !editor_asset_source_copy(&sources->sprites, &out_sources->sprites) ||
        !editor_asset_source_copy(&sources->skyboxes, &out_sources->skyboxes) ||
        !editor_asset_source_copy(&sources->effects, &out_sources->effects))
    {
        editor_asset_sources_destroy(out_sources);
        return false;
    }
    return true;
}

static bool editor_asset_source_available(const char *path)
{
    bool is_file = false;
    return file_exists_tool(path, &is_file) && !is_file;
}

static const char *manifest_optional_string_member(yyjson_val *object, const char *key, bool *out_valid)
{
    if (object == NULL)
        return NULL;
    yyjson_val *value = yyjson_obj_get(object, key);
    if (value == NULL)
        return NULL;
    if (!yyjson_is_str(value))
    {
        if (out_valid != NULL)
            *out_valid = false;
        return NULL;
    }
    return yyjson_get_str(value);
}

static bool load_editor_asset_source(yyjson_val *asset_sources, const char *key, const char *project_dir,
                                     const char *media_root, const char *fallback_leaf,
                                     slayer3d_editor_asset_source *out_source, char *error_buffer,
                                     int error_buffer_size)
{
    if (out_source == NULL)
        return false;
    bool valid = true;
    const char *configured = manifest_optional_string_member(asset_sources, key, &valid);
    if (!valid)
    {
        editor_set_errorf(error_buffer, error_buffer_size, "project asset source '%s' must be a string", key);
        return false;
    }

    char *relative = NULL;
    if (configured != NULL && configured[0] != '\0')
        relative = SDL_strdup(configured);
    else if (media_root != NULL && media_root[0] != '\0')
        relative = path_join_relative_tool(media_root, fallback_leaf);
    else
        relative = SDL_strdup(fallback_leaf);

    char *resolved = path_join_tool(project_dir, relative);
    if (relative == NULL || resolved == NULL)
    {
        editor_set_error(error_buffer, error_buffer_size, "failed to allocate project asset source path");
        SDL_free(relative);
        SDL_free(resolved);
        return false;
    }

    out_source->relative_path = relative;
    out_source->path = resolved;
    out_source->available = editor_asset_source_available(resolved);
    return true;
}

static bool load_editor_asset_sources(yyjson_val *root, const char *project_dir, const char *media_root,
                                      slayer3d_editor_asset_sources *out_sources, char *error_buffer,
                                      int error_buffer_size)
{
    yyjson_val *asset_sources = yyjson_obj_get(root, "asset_sources");
    if (asset_sources != NULL && !yyjson_is_obj(asset_sources))
    {
        editor_set_error(error_buffer, error_buffer_size, "project manifest asset_sources must be an object");
        return false;
    }

    return load_editor_asset_source(asset_sources, "textures", project_dir, media_root, "textures",
                                    &out_sources->textures, error_buffer, error_buffer_size) &&
           load_editor_asset_source(asset_sources, "models", project_dir, media_root, "models", &out_sources->models,
                                    error_buffer, error_buffer_size) &&
           load_editor_asset_source(asset_sources, "sprites", project_dir, media_root, "sprites", &out_sources->sprites,
                                    error_buffer, error_buffer_size) &&
           load_editor_asset_source(asset_sources, "skyboxes", project_dir, media_root, "skyboxes",
                                    &out_sources->skyboxes, error_buffer, error_buffer_size) &&
           load_editor_asset_source(asset_sources, "effects", project_dir, media_root, "effects", &out_sources->effects,
                                    error_buffer, error_buffer_size);
}

static void editor_asset_sources_destroy(slayer3d_editor_asset_sources *sources)
{
    if (sources == NULL)
        return;
    editor_asset_source_destroy(&sources->textures);
    editor_asset_source_destroy(&sources->models);
    editor_asset_source_destroy(&sources->sprites);
    editor_asset_source_destroy(&sources->skyboxes);
    editor_asset_source_destroy(&sources->effects);
}

static const char *manifest_string(yyjson_val *root, const char *key)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static bool validate_editable_level_fragment_file(const char *path, char *error_buffer, int error_buffer_size)
{
    yyjson_read_err read_error;
    SDL_zero(read_error);
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, &read_error);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    const char *schema = manifest_string(root, "schema");
    if (doc == NULL || !yyjson_is_obj(root))
    {
        editor_set_errorf(error_buffer, error_buffer_size, "input level '%s' is not valid JSON", path);
        yyjson_doc_free(doc);
        return false;
    }
    if (SDL_strcmp(schema != NULL ? schema : "", "slayer3d.fragment.v0") != 0)
    {
        editor_set_error(error_buffer, error_buffer_size, "input level must use schema 'slayer3d.fragment.v0'");
        yyjson_doc_free(doc);
        return false;
    }
    if (!yyjson_is_arr(yyjson_obj_get(root, "brush_worlds")))
    {
        editor_set_error(error_buffer, error_buffer_size, "input level must contain a brush_worlds array");
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_doc_free(doc);
    return true;
}

static char *project_manifest_path(const char *project_arg, char *error_buffer, int error_buffer_size)
{
    if (project_arg == NULL || project_arg[0] == '\0')
    {
        editor_set_error(error_buffer, error_buffer_size, "--project is required");
        return NULL;
    }

    SDL_PathInfo info;
    SDL_zero(info);
    if (!SDL_GetPathInfo(project_arg, &info))
    {
        editor_set_errorf(error_buffer, error_buffer_size, "project path '%s' does not exist", project_arg);
        return NULL;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY)
        return path_join_tool(project_arg, "slayer3d.project.json");
    if (info.type == SDL_PATHTYPE_FILE)
        return SDL_strdup(project_arg);

    editor_set_errorf(error_buffer, error_buffer_size, "project path '%s' must be a directory or manifest file",
                      project_arg);
    return NULL;
}

slayer3d_tool_cli_result slayer3d_editor_args_parse(int argc, char **argv, slayer3d_editor_args *args, FILE *stream)
{
    struct arg_str *project = arg_str0(NULL, "project", "<project>", "project directory or slayer3d.project.json");
    struct arg_str *input = arg_str0(NULL, "input", "<level.json>", "editable level fragment to open");
    struct arg_str *output = arg_str0(NULL, "output", "<level.json>", "editable level fragment save target");
    struct arg_str *texture_path =
        arg_str0(NULL, "texture-path", "<dir>", "authoritative texture directory for this editor session");
    struct arg_str *model_path =
        arg_str0(NULL, "model-path", "<dir>", "authoritative actor model directory for this editor session");
    struct arg_int *max_dynamic_lights =
        arg_int0(NULL, "max-dynamic-lights", "<n>", "dynamic/runtime light budget for lighting-plan");
    struct arg_int *max_static_lights =
        arg_int0(NULL, "max-static-lights", "<n>", "static/baked light budget for lighting-plan");
    struct arg_lit *preview_quality = arg_lit0(NULL, "preview", "use preview quality for lighting-plan");
    struct arg_lit *final_quality = arg_lit0(NULL, "final", "use final quality for lighting-plan");
    struct arg_lit *no_dynamic_preview =
        arg_lit0(NULL, "no-dynamic-preview", "do not count static lights as runtime previews in lighting-plan");
    struct arg_lit *manifest =
        arg_lit0(NULL, "manifest", "print planned static-light artifact manifest JSON for lighting-plan");
    struct arg_lit *static_artifact =
        arg_lit0(NULL, "static-artifact", "print deterministic static-light artifact JSON for lighting-plan");
    struct arg_lit *overwrite = arg_lit0(NULL, "overwrite", "allow new/open to replace an existing output path");
    struct arg_lit *help = arg_lit0("h", "help", "print this help and exit");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {project,
                        input,
                        output,
                        texture_path,
                        model_path,
                        max_dynamic_lights,
                        max_static_lights,
                        preview_quality,
                        final_quality,
                        no_dynamic_preview,
                        manifest,
                        static_artifact,
                        overwrite,
                        help,
                        end};
    const char *program = argc > 0 && argv != NULL && argv[0] != NULL ? argv[0] : "slayer3d_editor";
    FILE *out = stream != NULL ? stream : stderr;

    if (args == NULL || arg_nullcheck(argtable) != 0)
    {
        if (out != NULL)
            fprintf(out, "%s: insufficient memory\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    SDL_zero(*args);
    if (argc <= 1 || argv == NULL || argv[1] == NULL)
    {
        args->command = SLAYER3D_EDITOR_COMMAND_NEW;
        args->owned_project = editor_default_project_path();
        args->owned_output_path = editor_default_output_path();
        args->project = args->owned_project;
        args->output_path = args->owned_output_path;
        arg_freetable(argtable, SDL_arraysize(argtable));
        if (args->project == NULL || args->project[0] == '\0' || args->output_path == NULL ||
            args->output_path[0] == '\0')
        {
            slayer3d_editor_args_destroy(args);
            if (out != NULL)
                fprintf(out, "%s: failed to resolve default editor project or output path.\n", program);
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        return SLAYER3D_TOOL_CLI_OK;
    }
    if (SDL_strcmp(argv[1], "--help") == 0 || SDL_strcmp(argv[1], "-h") == 0)
    {
        slayer3d_editor_args_print_usage(program, out);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_HELP;
    }

    bool default_launch_with_options = false;
    const char *command_name = argv[1];
    int parse_argc = argc - 1;
    char **parse_argv = argv + 1;
    if (SDL_strcmp(argv[1], "new") == 0)
    {
        args->command = SLAYER3D_EDITOR_COMMAND_NEW;
    }
    else if (SDL_strcmp(argv[1], "open") == 0)
    {
        args->command = SLAYER3D_EDITOR_COMMAND_OPEN;
    }
    else if (SDL_strcmp(argv[1], "check") == 0)
    {
        args->command = SLAYER3D_EDITOR_COMMAND_CHECK;
    }
    else if (SDL_strcmp(argv[1], "lighting-plan") == 0)
    {
        args->command = SLAYER3D_EDITOR_COMMAND_LIGHTING_PLAN;
    }
    else if (SDL_strcmp(argv[1], "lighting-artifact-validate") == 0)
    {
        args->command = SLAYER3D_EDITOR_COMMAND_LIGHTING_ARTIFACT_VALIDATE;
    }
    else if (argv[1][0] == '-')
    {
        default_launch_with_options = true;
        command_name = "default";
        args->command = SLAYER3D_EDITOR_COMMAND_NEW;
        parse_argc = argc;
        parse_argv = argv;
    }
    else
    {
        fprintf(out, "%s: unknown editor command '%s'\n", program, argv[1]);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    if (args->command == SLAYER3D_EDITOR_COMMAND_CHECK)
    {
        fprintf(out, "%s: 'check' is not implemented yet; use 'new' or 'open'.\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    const int errors = arg_parse(parse_argc, parse_argv, argtable);
    if (help->count > 0)
    {
        slayer3d_editor_args_print_usage(program, out);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_HELP;
    }
    if (errors > 0)
    {
        arg_print_errors(out, end, program);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    args->project = project->count > 0 ? project->sval[0] : NULL;
    args->input_path = input->count > 0 ? input->sval[0] : NULL;
    args->output_path = output->count > 0 ? output->sval[0] : NULL;
    args->texture_path = texture_path->count > 0 ? texture_path->sval[0] : NULL;
    args->model_path = model_path->count > 0 ? model_path->sval[0] : NULL;
    args->overwrite = overwrite->count > 0;
    args->lighting_preview_quality = preview_quality->count > 0;
    args->lighting_final_quality = final_quality->count > 0;
    args->lighting_no_dynamic_preview = no_dynamic_preview->count > 0;
    args->lighting_manifest = manifest->count > 0;
    args->lighting_static_artifact = static_artifact->count > 0;
    args->max_dynamic_lights = max_dynamic_lights->count > 0 ? max_dynamic_lights->ival[0] : 0;
    args->max_static_lights = max_static_lights->count > 0 ? max_static_lights->ival[0] : 0;

    if (default_launch_with_options)
    {
        if (project->count > 0 || input->count > 0 || output->count > 0 || overwrite->count > 0)
        {
            fprintf(out,
                    "%s: default editor launch only accepts --texture-path and --model-path; use 'new' or 'open' for "
                    "map paths.\n",
                    program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->texture_path != NULL && args->texture_path[0] == '\0')
        {
            fprintf(out, "%s: --texture-path must be non-empty when present.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->model_path != NULL && args->model_path[0] == '\0')
        {
            fprintf(out, "%s: --model-path must be non-empty when present.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        args->owned_project = editor_default_project_path();
        args->owned_output_path = editor_default_output_path();
        args->project = args->owned_project;
        args->output_path = args->owned_output_path;
        arg_freetable(argtable, SDL_arraysize(argtable));
        if (args->project == NULL || args->project[0] == '\0' || args->output_path == NULL ||
            args->output_path[0] == '\0')
        {
            slayer3d_editor_args_destroy(args);
            if (out != NULL)
                fprintf(out, "%s: failed to resolve default editor project or output path.\n", program);
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        return SLAYER3D_TOOL_CLI_OK;
    }

    if (args->command == SLAYER3D_EDITOR_COMMAND_LIGHTING_PLAN)
    {
        if (args->input_path == NULL || args->input_path[0] == '\0')
        {
            fprintf(out, "%s: 'lighting-plan' requires --input <map.slayermap.json>.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (project->count > 0 || texture_path->count > 0 || model_path->count > 0 || overwrite->count > 0)
        {
            fprintf(out, "%s: 'lighting-plan' only accepts --input, --output, and lighting budget/quality options.\n",
                    program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->lighting_preview_quality && args->lighting_final_quality)
        {
            fprintf(out, "%s: 'lighting-plan' accepts only one of --preview or --final.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->lighting_manifest && args->lighting_static_artifact)
        {
            fprintf(out, "%s: 'lighting-plan' accepts only one of --manifest or --static-artifact.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->output_path != NULL && args->output_path[0] == '\0')
        {
            fprintf(out, "%s: --output must be non-empty when present.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->output_path != NULL && !args->lighting_manifest && !args->lighting_static_artifact)
        {
            fprintf(out, "%s: 'lighting-plan --output' requires --manifest or --static-artifact.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->max_dynamic_lights < 0 || args->max_static_lights < 0)
        {
            fprintf(out, "%s: lighting budgets must be non-negative.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_OK;
    }

    if (args->command == SLAYER3D_EDITOR_COMMAND_LIGHTING_ARTIFACT_VALIDATE)
    {
        if (args->input_path == NULL || args->input_path[0] == '\0')
        {
            fprintf(out, "%s: 'lighting-artifact-validate' requires --input <lighting-static.json>.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (project->count > 0 || output->count > 0 || texture_path->count > 0 || model_path->count > 0 ||
            max_dynamic_lights->count > 0 || max_static_lights->count > 0 || preview_quality->count > 0 ||
            final_quality->count > 0 || no_dynamic_preview->count > 0 || manifest->count > 0 ||
            static_artifact->count > 0 || overwrite->count > 0)
        {
            fprintf(out, "%s: 'lighting-artifact-validate' only accepts --input.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_OK;
    }

    if (args->project == NULL || args->project[0] == '\0')
    {
        fprintf(out, "%s: --project is required for '%s'.\n", program, command_name);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }
    if (args->command == SLAYER3D_EDITOR_COMMAND_NEW)
    {
        if (args->input_path != NULL)
        {
            fprintf(out, "%s: 'new' does not accept --input; use 'open' to edit an existing level.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->output_path == NULL || args->output_path[0] == '\0')
        {
            fprintf(out, "%s: 'new' requires --output <level.json>.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
    }
    if (args->command == SLAYER3D_EDITOR_COMMAND_OPEN)
    {
        if (args->input_path == NULL || args->input_path[0] == '\0')
        {
            fprintf(out, "%s: 'open' requires --input <level.json>.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        if (args->output_path != NULL && args->output_path[0] == '\0')
        {
            fprintf(out, "%s: --output must be non-empty when present.\n", program);
            arg_freetable(argtable, SDL_arraysize(argtable));
            return SLAYER3D_TOOL_CLI_ERROR;
        }
    }
    if (args->texture_path != NULL && args->texture_path[0] == '\0')
    {
        fprintf(out, "%s: --texture-path must be non-empty when present.\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }
    if (args->model_path != NULL && args->model_path[0] == '\0')
    {
        fprintf(out, "%s: --model-path must be non-empty when present.\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    arg_freetable(argtable, SDL_arraysize(argtable));
    return SLAYER3D_TOOL_CLI_OK;
}

void slayer3d_editor_args_destroy(slayer3d_editor_args *args)
{
    if (args == NULL)
        return;
    SDL_free(args->owned_project);
    SDL_free(args->owned_output_path);
    SDL_zero(*args);
}

static const char *lighting_quality_name(slayer3d_map_lighting_build_quality quality)
{
    switch (quality)
    {
    case SLAYER3D_MAP_LIGHTING_BUILD_PREVIEW:
        return "preview";
    case SLAYER3D_MAP_LIGHTING_BUILD_FINAL:
        return "final";
    case SLAYER3D_MAP_LIGHTING_BUILD_BALANCED:
    default:
        return "balanced";
    }
}

static bool editor_write_lighting_json_output(const slayer3d_editor_args *args, FILE *stream, const char *json,
                                              size_t json_size, const char *failure_label, char *error_buffer,
                                              int error_buffer_size)
{
    if (args != NULL && args->output_path != NULL && args->output_path[0] != '\0')
        return editor_write_output_bytes(args->output_path, json, json_size, error_buffer, error_buffer_size);

    FILE *out = stream != NULL ? stream : stdout;
    if (fwrite(json, 1u, json_size, out) == json_size)
        return true;
    editor_set_error(error_buffer, error_buffer_size, failure_label != NULL ? failure_label : "failed to write output");
    return false;
}

bool slayer3d_editor_run_lighting_plan(const slayer3d_editor_args *args, FILE *stream, char *error_buffer,
                                       int error_buffer_size)
{
    FILE *out = stream != NULL ? stream : stdout;
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
    if (args == NULL || args->command != SLAYER3D_EDITOR_COMMAND_LIGHTING_PLAN)
    {
        editor_set_error(error_buffer, error_buffer_size, "lighting-plan command arguments are required");
        return false;
    }
    if (args->input_path == NULL || args->input_path[0] == '\0')
    {
        editor_set_error(error_buffer, error_buffer_size, "lighting-plan requires --input");
        return false;
    }

    slayer3d_map_document *document = NULL;
    if (!slayer3d_map_load_file(args->input_path, NULL, &document, error_buffer, error_buffer_size))
        return false;

    slayer3d_map_lighting_build_options options;
    slayer3d_map_init_lighting_build_options(&options);
    if (args->lighting_preview_quality)
        options.quality = SLAYER3D_MAP_LIGHTING_BUILD_PREVIEW;
    if (args->lighting_final_quality)
        options.quality = SLAYER3D_MAP_LIGHTING_BUILD_FINAL;
    if (args->max_dynamic_lights > 0)
        options.max_dynamic_lights = (size_t)args->max_dynamic_lights;
    if (args->max_static_lights > 0)
        options.max_static_lights = (size_t)args->max_static_lights;
    options.include_dynamic_preview = !args->lighting_no_dynamic_preview;

    if (args->lighting_manifest)
    {
        char *json = NULL;
        size_t json_size = 0u;
        const bool ok = slayer3d_map_build_lighting_artifact_manifest_json(document, &options, &json, &json_size,
                                                                           error_buffer, error_buffer_size);
        slayer3d_map_destroy(document);
        if (!ok)
            return false;
        const bool wrote = editor_write_lighting_json_output(
            args, out, json, json_size, "failed to write lighting artifact manifest", error_buffer, error_buffer_size);
        slayer3d_map_free_string(json);
        if (!wrote)
            return false;
        return true;
    }
    if (args->lighting_static_artifact)
    {
        char *json = NULL;
        size_t json_size = 0u;
        const bool ok = slayer3d_map_build_static_lighting_artifact_json(document, &options, &json, &json_size,
                                                                         error_buffer, error_buffer_size);
        slayer3d_map_destroy(document);
        if (!ok)
            return false;
        const bool wrote = editor_write_lighting_json_output(
            args, out, json, json_size, "failed to write static lighting artifact", error_buffer, error_buffer_size);
        slayer3d_map_free_string(json);
        if (!wrote)
            return false;
        return true;
    }

    slayer3d_map_lighting_build_plan plan;
    const bool ok = slayer3d_map_build_lighting_plan(document, &options, &plan, error_buffer, error_buffer_size);
    slayer3d_map_destroy(document);
    if (!ok)
        return false;

    fprintf(out, "Slayer3D lighting plan: %s\n", args->input_path);
    fprintf(out, "  quality: %s\n", lighting_quality_name(plan.quality));
    fprintf(out, "  lights: total=%zu dynamic=%zu static=%zu area=%zu\n", plan.total_light_count,
            plan.dynamic_light_count, plan.static_light_count, plan.area_light_count);
    fprintf(out, "  runtime_preview_lights: %zu / %zu%s\n", plan.runtime_light_count, plan.max_dynamic_lights,
            plan.dynamic_light_budget_exceeded ? " (exceeded)" : "");
    fprintf(out, "  bake_lights: %zu / %zu%s\n", plan.bake_light_count, plan.max_static_lights,
            plan.static_light_budget_exceeded ? " (exceeded)" : "");
    fprintf(out, "  requires_static_bake: %s\n", plan.requires_static_bake ? "yes" : "no");
    fprintf(out, "  dynamic_preview: %s\n", plan.has_dynamic_preview ? "yes" : "no");
    return true;
}

bool slayer3d_editor_run_lighting_artifact_validate(const slayer3d_editor_args *args, FILE *stream, char *error_buffer,
                                                    int error_buffer_size)
{
    FILE *out = stream != NULL ? stream : stdout;
    if (error_buffer != NULL && error_buffer_size > 0)
        error_buffer[0] = '\0';
    if (args == NULL || args->command != SLAYER3D_EDITOR_COMMAND_LIGHTING_ARTIFACT_VALIDATE)
    {
        editor_set_error(error_buffer, error_buffer_size, "lighting-artifact-validate command arguments are required");
        return false;
    }
    if (args->input_path == NULL || args->input_path[0] == '\0')
    {
        editor_set_error(error_buffer, error_buffer_size, "lighting-artifact-validate requires --input");
        return false;
    }

    size_t size = 0u;
    char *json = (char *)SDL_LoadFile(args->input_path, &size);
    if (json == NULL)
    {
        editor_set_errorf(error_buffer, error_buffer_size, "failed to read lighting artifact '%s'", args->input_path);
        return false;
    }
    const bool ok = slayer3d_map_validate_static_lighting_artifact_json(json, size, error_buffer, error_buffer_size);
    SDL_free(json);
    if (!ok)
        return false;

    fprintf(out, "Static lighting artifact valid: %s\n", args->input_path);
    return true;
}

bool slayer3d_editor_project_load(const char *project_arg, slayer3d_editor_project *out_project, char *error_buffer,
                                  int error_buffer_size)
{
    if (out_project != NULL)
        SDL_zero(*out_project);
    if (out_project == NULL)
    {
        editor_set_error(error_buffer, error_buffer_size, "project output is required");
        return false;
    }

    if (SDL_strcmp(project_arg != NULL ? project_arg : "", SLAYER3D_EDITOR_EMBEDDED_PROJECT_URI) == 0)
    {
#if defined(SLAYER3D_EDITOR_EMBEDDED_ASSETS)
        out_project->project_dir = SDL_strdup(SLAYER3D_EDITOR_EMBEDDED_PROJECT_URI);
        out_project->data_root = SDL_strdup(SLAYER3D_EDITOR_EMBEDDED_PROJECT_URI);
        out_project->data_root_relative_path = SDL_strdup("embedded");
        out_project->editor_entry = SDL_strdup(SLAYER3D_EDITOR_EMBEDDED_DATA_ASSET);
#if defined(SLAYER3D_MEDIA_DIR)
        out_project->media_dir = SLAYER3D_MEDIA_DIR;
#endif
        out_project->embedded = true;
        const bool ok =
            out_project->project_dir != NULL && out_project->data_root != NULL &&
            out_project->data_root_relative_path != NULL && out_project->editor_entry != NULL &&
            editor_asset_source_init(&out_project->asset_sources.textures, "asset://textures", "textures", true) &&
            editor_asset_source_init(&out_project->asset_sources.models, "asset://models", "models", true) &&
            editor_asset_source_init(&out_project->asset_sources.sprites, "asset://sprites", "sprites", true) &&
            editor_asset_source_init(&out_project->asset_sources.skyboxes, "asset://skyboxes", "skyboxes", true) &&
            editor_asset_source_init(&out_project->asset_sources.effects, "asset://effects", "effects", true);
        if (!ok)
        {
            editor_set_error(error_buffer, error_buffer_size, "failed to allocate embedded editor project");
            slayer3d_editor_project_destroy(out_project);
            return false;
        }
        return true;
#else
        editor_set_error(error_buffer, error_buffer_size, "this editor build does not contain embedded assets");
        return false;
#endif
    }

    char *manifest = project_manifest_path(project_arg, error_buffer, error_buffer_size);
    if (manifest == NULL)
        return false;

    bool is_file = false;
    if (!file_exists_tool(manifest, &is_file) || !is_file)
    {
        editor_set_errorf(error_buffer, error_buffer_size, "project manifest '%s' does not exist", manifest);
        SDL_free(manifest);
        return false;
    }

    yyjson_read_err read_error;
    SDL_zero(read_error);
    yyjson_doc *doc = yyjson_read_file(manifest, 0, NULL, &read_error);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    const char *schema = manifest_string(root, "schema");
    const char *data_root = manifest_string(root, "data_root");
    const char *editor_entry = manifest_string(root, "editor_entry");
    const char *media_root = manifest_string(root, "media_root");
    const char *test_run_output = manifest_string(root, "test_run_output");
    if (doc == NULL || !yyjson_is_obj(root))
    {
        editor_set_errorf(error_buffer, error_buffer_size, "failed to parse project manifest '%s'", manifest);
        yyjson_doc_free(doc);
        SDL_free(manifest);
        return false;
    }
    if (SDL_strcmp(schema != NULL ? schema : "", "slayer3d.project.v0") != 0)
    {
        editor_set_error(error_buffer, error_buffer_size, "project manifest must use schema 'slayer3d.project.v0'");
        yyjson_doc_free(doc);
        SDL_free(manifest);
        return false;
    }
    if (data_root == NULL || data_root[0] == '\0' || editor_entry == NULL || editor_entry[0] == '\0')
    {
        editor_set_error(error_buffer, error_buffer_size,
                         "project manifest requires non-empty data_root and editor_entry fields");
        yyjson_doc_free(doc);
        SDL_free(manifest);
        return false;
    }

    char *project_dir = path_dirname_tool(manifest);
    char *resolved_data_root = path_join_tool(project_dir, data_root);
    char *resolved_media_root =
        media_root != NULL && media_root[0] != '\0' ? path_join_tool(project_dir, media_root) : NULL;
    char *resolved_test_run =
        test_run_output != NULL && test_run_output[0] != '\0' ? path_join_tool(project_dir, test_run_output) : NULL;
    char *entry_copy = SDL_strdup(editor_entry);
    char *data_root_copy = SDL_strdup(data_root);
    slayer3d_editor_asset_sources asset_sources;
    SDL_zero(asset_sources);
    if (project_dir == NULL || resolved_data_root == NULL || entry_copy == NULL || data_root_copy == NULL ||
        (media_root != NULL && media_root[0] != '\0' && resolved_media_root == NULL) ||
        (test_run_output != NULL && test_run_output[0] != '\0' && resolved_test_run == NULL))
    {
        editor_set_error(error_buffer, error_buffer_size, "failed to allocate project manifest paths");
        SDL_free(project_dir);
        SDL_free(resolved_data_root);
        SDL_free(resolved_media_root);
        SDL_free(resolved_test_run);
        SDL_free(entry_copy);
        SDL_free(data_root_copy);
        editor_asset_sources_destroy(&asset_sources);
        yyjson_doc_free(doc);
        SDL_free(manifest);
        return false;
    }
    if (!load_editor_asset_sources(root, project_dir, media_root, &asset_sources, error_buffer, error_buffer_size))
    {
        SDL_free(project_dir);
        SDL_free(resolved_data_root);
        SDL_free(resolved_media_root);
        SDL_free(resolved_test_run);
        SDL_free(entry_copy);
        SDL_free(data_root_copy);
        editor_asset_sources_destroy(&asset_sources);
        yyjson_doc_free(doc);
        SDL_free(manifest);
        return false;
    }

    out_project->project_dir = project_dir;
    out_project->data_root = resolved_data_root;
    out_project->data_root_relative_path = data_root_copy;
    out_project->editor_entry = entry_copy;
    out_project->owned_media_dir = resolved_media_root;
    out_project->media_dir = out_project->owned_media_dir;
    out_project->test_run_path = resolved_test_run;
    out_project->asset_sources = asset_sources;
    out_project->embedded = false;
    yyjson_doc_free(doc);
    SDL_free(manifest);
    return true;
}

void slayer3d_editor_project_destroy(slayer3d_editor_project *project)
{
    if (project == NULL)
        return;
    SDL_free(project->project_dir);
    SDL_free(project->data_root);
    SDL_free(project->data_root_relative_path);
    SDL_free(project->editor_entry);
    SDL_free(project->owned_media_dir);
    SDL_free(project->test_run_path);
    editor_asset_sources_destroy(&project->asset_sources);
    SDL_zero(*project);
}

static bool editor_override_asset_source(slayer3d_editor_asset_source *source, const char *path, const char *label,
                                         char *error_buffer, int error_buffer_size)
{
    if (source == NULL || path == NULL || path[0] == '\0')
        return true;
    char *absolute_path = path_make_absolute_tool(path);
    if (absolute_path == NULL)
    {
        editor_set_errorf(error_buffer, error_buffer_size, "failed to resolve %s path override", label);
        return false;
    }
    char *relative_path = SDL_strdup(path);
    if (relative_path == NULL)
    {
        SDL_free(absolute_path);
        editor_set_errorf(error_buffer, error_buffer_size, "failed to allocate %s path override", label);
        return false;
    }
    editor_asset_source_destroy(source);
    source->path = absolute_path;
    source->relative_path = relative_path;
    source->available = editor_asset_source_available(absolute_path);
    return true;
}

bool slayer3d_editor_prepare_launch(const slayer3d_editor_args *args, const slayer3d_editor_project *project,
                                    slayer3d_editor_launch *out_launch, char *error_buffer, int error_buffer_size)
{
    if (out_launch != NULL)
        SDL_zero(*out_launch);
    if (args == NULL || project == NULL || out_launch == NULL)
    {
        editor_set_error(error_buffer, error_buffer_size, "editor launch requires parsed args and project");
        return false;
    }
    const char *save_path = args->output_path;
    if (args->command == SLAYER3D_EDITOR_COMMAND_OPEN && (save_path == NULL || save_path[0] == '\0'))
        save_path = args->input_path;
    if (project->data_root == NULL || project->editor_entry == NULL || save_path == NULL || save_path[0] == '\0')
    {
        editor_set_error(error_buffer, error_buffer_size, "project and editor command did not resolve launch paths");
        return false;
    }

    out_launch->root = project->data_root;
    out_launch->data_asset_path = project->editor_entry;
    out_launch->media_dir = project->media_dir;
    out_launch->input_path = args->command == SLAYER3D_EDITOR_COMMAND_OPEN ? args->input_path : "";
    out_launch->save_path = save_path;
    out_launch->test_run_path = project->test_run_path;
    out_launch->project_dir = project->project_dir;
    out_launch->data_root_relative_path = project->data_root_relative_path;
    out_launch->asset_sources = &project->asset_sources;
    out_launch->embedded = project->embedded;
    const bool has_texture_override = args->texture_path != NULL && args->texture_path[0] != '\0';
    const bool has_model_override = args->model_path != NULL && args->model_path[0] != '\0';
    if (has_texture_override || has_model_override)
    {
        if (!editor_asset_sources_copy(&project->asset_sources, &out_launch->owned_asset_sources))
        {
            editor_set_error(error_buffer, error_buffer_size, "failed to allocate asset source overrides");
            slayer3d_editor_launch_destroy(out_launch);
            return false;
        }
        out_launch->asset_sources = &out_launch->owned_asset_sources;
        out_launch->owns_asset_sources = true;
        if (!editor_override_asset_source(&out_launch->owned_asset_sources.textures, args->texture_path, "texture",
                                          error_buffer, error_buffer_size) ||
            !editor_override_asset_source(&out_launch->owned_asset_sources.models, args->model_path, "model",
                                          error_buffer, error_buffer_size))
        {
            slayer3d_editor_launch_destroy(out_launch);
            return false;
        }
    }
    return true;
}

void slayer3d_editor_launch_destroy(slayer3d_editor_launch *launch)
{
    if (launch == NULL)
        return;
    if (launch->owns_asset_sources)
        editor_asset_sources_destroy(&launch->owned_asset_sources);
    SDL_zero(*launch);
}

bool slayer3d_editor_validate_paths(const slayer3d_editor_args *args, const slayer3d_editor_launch *launch,
                                    char *error_buffer, int error_buffer_size)
{
    if (args == NULL || launch == NULL)
    {
        editor_set_error(error_buffer, error_buffer_size, "editor path validation requires args and launch");
        return false;
    }
    if (args->texture_path != NULL && args->texture_path[0] != '\0' && !directory_exists_tool(args->texture_path))
    {
        editor_set_errorf(error_buffer, error_buffer_size, "texture path '%s' is not an existing directory",
                          args->texture_path);
        return false;
    }
    if (args->model_path != NULL && args->model_path[0] != '\0' && !directory_exists_tool(args->model_path))
    {
        editor_set_errorf(error_buffer, error_buffer_size, "model path '%s' is not an existing directory",
                          args->model_path);
        return false;
    }
    bool is_file = false;
    if (args->command == SLAYER3D_EDITOR_COMMAND_OPEN && (!file_exists_tool(args->input_path, &is_file) || !is_file))
    {
        editor_set_errorf(error_buffer, error_buffer_size, "input level '%s' does not exist", args->input_path);
        return false;
    }
    if (args->command == SLAYER3D_EDITOR_COMMAND_OPEN &&
        !validate_editable_level_fragment_file(args->input_path, error_buffer, error_buffer_size))
    {
        return false;
    }

    if (launch->save_path == NULL || launch->save_path[0] == '\0')
    {
        editor_set_error(error_buffer, error_buffer_size, "output save path is required");
        return false;
    }
    const bool output_exists = file_exists_tool(launch->save_path, &is_file);
    const bool saving_back_to_input = args->command == SLAYER3D_EDITOR_COMMAND_OPEN && args->input_path != NULL &&
                                      SDL_strcmp(args->input_path, launch->save_path) == 0;
    if (output_exists && !is_file)
    {
        editor_set_errorf(error_buffer, error_buffer_size, "output path '%s' is not a regular file", launch->save_path);
        return false;
    }
    if (output_exists && !saving_back_to_input && !args->overwrite)
    {
        editor_set_errorf(error_buffer, error_buffer_size,
                          "output path '%s' already exists; pass --overwrite to replace it", launch->save_path);
        return false;
    }
    return true;
}

static char *editor_state_assignment(const char *key, const char *value)
{
    const size_t key_len = SDL_strlen(key);
    const size_t value_len = SDL_strlen(value != NULL ? value : "");
    char *assignment = (char *)SDL_malloc(key_len + 1u + value_len + 1u);
    if (assignment == NULL)
        return NULL;
    SDL_memcpy(assignment, key, key_len);
    assignment[key_len] = '=';
    SDL_memcpy(assignment + key_len + 1u, value != NULL ? value : "", value_len);
    assignment[key_len + 1u + value_len] = '\0';
    return assignment;
}

static bool editor_asset_sources_any_missing(const slayer3d_editor_asset_sources *sources)
{
    if (sources == NULL)
        return true;
    return !sources->textures.available || !sources->models.available || !sources->sprites.available ||
           !sources->skyboxes.available || !sources->effects.available;
}

static bool editor_build_asset_source_assignment(char **assignments, size_t assignment_count, size_t *index,
                                                 const char *name, const slayer3d_editor_asset_source *source)
{
    if (assignments == NULL || index == NULL || name == NULL || source == NULL || *index + 3u > assignment_count)
        return false;

    char path_key[128];
    char relative_key[128];
    char available_key[128];
    SDL_snprintf(path_key, sizeof(path_key), "editor.asset_source.%s.path", name);
    SDL_snprintf(relative_key, sizeof(relative_key), "editor.asset_source.%s.relative", name);
    SDL_snprintf(available_key, sizeof(available_key), "editor.asset_source.%s.available", name);

    assignments[(*index)++] = editor_state_assignment(path_key, source->path);
    assignments[(*index)++] = editor_state_assignment(relative_key, source->relative_path);
    assignments[(*index)++] = editor_state_assignment(available_key, source->available ? "true" : "false");
    return assignments[*index - 3u] != NULL && assignments[*index - 2u] != NULL && assignments[*index - 1u] != NULL;
}

static bool editor_build_asset_source_assignments(slayer3d_editor_runner_invocation *invocation,
                                                  const slayer3d_editor_asset_sources *sources)
{
    if (invocation == NULL || sources == NULL)
        return false;
    size_t index = 0u;
    if (!editor_build_asset_source_assignment(invocation->owned_asset_source_assignments,
                                              SDL_arraysize(invocation->owned_asset_source_assignments), &index,
                                              "textures", &sources->textures) ||
        !editor_build_asset_source_assignment(invocation->owned_asset_source_assignments,
                                              SDL_arraysize(invocation->owned_asset_source_assignments), &index,
                                              "models", &sources->models) ||
        !editor_build_asset_source_assignment(invocation->owned_asset_source_assignments,
                                              SDL_arraysize(invocation->owned_asset_source_assignments), &index,
                                              "sprites", &sources->sprites) ||
        !editor_build_asset_source_assignment(invocation->owned_asset_source_assignments,
                                              SDL_arraysize(invocation->owned_asset_source_assignments), &index,
                                              "skyboxes", &sources->skyboxes) ||
        !editor_build_asset_source_assignment(invocation->owned_asset_source_assignments,
                                              SDL_arraysize(invocation->owned_asset_source_assignments), &index,
                                              "effects", &sources->effects))
    {
        return false;
    }
    if (index >= SDL_arraysize(invocation->owned_asset_source_assignments))
        return false;
    invocation->owned_asset_source_assignments[index++] = editor_state_assignment(
        "editor.asset_source.any_missing", editor_asset_sources_any_missing(sources) ? "true" : "false");
    return index == SDL_arraysize(invocation->owned_asset_source_assignments) &&
           invocation->owned_asset_source_assignments[index - 1u] != NULL;
}

static bool editor_add_arg(slayer3d_editor_runner_invocation *invocation, int *index, char *arg)
{
    if (invocation == NULL || index == NULL || *index >= invocation->argc)
        return false;
    invocation->argv[*index] = arg;
    ++(*index);
    return true;
}

bool slayer3d_editor_build_runner_invocation(const slayer3d_editor_launch *launch, const char *program,
                                             slayer3d_editor_runner_invocation *out_invocation)
{
    if (out_invocation == NULL)
        return false;
    SDL_zero(*out_invocation);
    if (launch == NULL || launch->root == NULL || launch->root[0] == '\0' || launch->data_asset_path == NULL ||
        launch->data_asset_path[0] == '\0' || launch->save_path == NULL || launch->save_path[0] == '\0' ||
        launch->project_dir == NULL || launch->asset_sources == NULL)
    {
        return false;
    }

    const int state_count = 4 + 2 + (int)SDL_arraysize(out_invocation->owned_asset_source_assignments);
    int argc = (launch->embedded ? 4 : 5) + (state_count * 2);
    if (launch->media_dir != NULL && launch->media_dir[0] != '\0')
        argc += 2;

    char **argv = (char **)SDL_calloc((size_t)argc + 1u, sizeof(*argv));
    if (argv == NULL)
        return false;

    out_invocation->argv = argv;
    out_invocation->argc = argc;
    out_invocation->owned_command_assignment = editor_state_assignment(
        "editor.command", launch->input_path != NULL && launch->input_path[0] != '\0' ? "open" : "new");
    out_invocation->owned_input_assignment = editor_state_assignment("editor.input.path", launch->input_path);
    out_invocation->owned_save_assignment = editor_state_assignment("editor.save.path", launch->save_path);
    out_invocation->owned_test_run_assignment =
        editor_state_assignment("editor.test_run.path", launch->test_run_path != NULL ? launch->test_run_path : "");
    out_invocation->owned_project_dir_assignment = editor_state_assignment("editor.project.dir", launch->project_dir);
    out_invocation->owned_project_data_root_assignment =
        editor_state_assignment("editor.project.data_root", launch->data_root_relative_path);
    if (out_invocation->owned_command_assignment == NULL || out_invocation->owned_input_assignment == NULL ||
        out_invocation->owned_save_assignment == NULL || out_invocation->owned_test_run_assignment == NULL ||
        out_invocation->owned_project_dir_assignment == NULL ||
        out_invocation->owned_project_data_root_assignment == NULL ||
        !editor_build_asset_source_assignments(out_invocation, launch->asset_sources))
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }

    int index = 0;
    if (!editor_add_arg(out_invocation, &index, (char *)(program != NULL ? program : "slayer3d_editor")))
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }
    if (launch->embedded)
    {
        if (!editor_add_arg(out_invocation, &index, "--embedded"))
        {
            slayer3d_editor_runner_invocation_destroy(out_invocation);
            return false;
        }
    }
    else if (!editor_add_arg(out_invocation, &index, "--root") ||
             !editor_add_arg(out_invocation, &index, (char *)launch->root))
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }
    if (!editor_add_arg(out_invocation, &index, "--data") ||
        !editor_add_arg(out_invocation, &index, (char *)launch->data_asset_path))
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }
    if (launch->media_dir != NULL && launch->media_dir[0] != '\0')
    {
        if (!editor_add_arg(out_invocation, &index, "--media") ||
            !editor_add_arg(out_invocation, &index, (char *)launch->media_dir))
        {
            slayer3d_editor_runner_invocation_destroy(out_invocation);
            return false;
        }
    }

    char *owned_state[] = {
        out_invocation->owned_command_assignment,     out_invocation->owned_input_assignment,
        out_invocation->owned_save_assignment,        out_invocation->owned_test_run_assignment,
        out_invocation->owned_project_dir_assignment, out_invocation->owned_project_data_root_assignment};
    for (size_t i = 0; i < SDL_arraysize(owned_state); ++i)
    {
        if (!editor_add_arg(out_invocation, &index, "--state") ||
            !editor_add_arg(out_invocation, &index, owned_state[i]))
        {
            slayer3d_editor_runner_invocation_destroy(out_invocation);
            return false;
        }
    }
    for (size_t i = 0; i < SDL_arraysize(out_invocation->owned_asset_source_assignments); ++i)
    {
        if (!editor_add_arg(out_invocation, &index, "--state") ||
            !editor_add_arg(out_invocation, &index, out_invocation->owned_asset_source_assignments[i]))
        {
            slayer3d_editor_runner_invocation_destroy(out_invocation);
            return false;
        }
    }
    if (index != argc)
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }
    return true;
}

void slayer3d_editor_runner_invocation_destroy(slayer3d_editor_runner_invocation *invocation)
{
    if (invocation == NULL)
        return;
    SDL_free(invocation->owned_command_assignment);
    SDL_free(invocation->owned_input_assignment);
    SDL_free(invocation->owned_save_assignment);
    SDL_free(invocation->owned_test_run_assignment);
    SDL_free(invocation->owned_project_dir_assignment);
    SDL_free(invocation->owned_project_data_root_assignment);
    for (size_t i = 0; i < SDL_arraysize(invocation->owned_asset_source_assignments); ++i)
        SDL_free(invocation->owned_asset_source_assignments[i]);
    SDL_free(invocation->argv);
    SDL_zero(*invocation);
}
