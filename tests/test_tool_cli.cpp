#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C"
{
#include "slayer3d/math.h"
#include "slayer3d/properties.h"
#include "slayer3d_bundle_cli.h"
#include "slayer3d_editor_cli.h"
#include "slayer3d_fused.h"
#include "slayer3d_pack_cli.h"
#include "slayer3d_runner_cli.h"
#include "slayer3d_runner_manifest.h"
#include "slayer3d_runner_state.h"
}

namespace
{
std::vector<char *> argv_from(std::initializer_list<const char *> args)
{
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (const char *arg : args)
        argv.push_back(const_cast<char *>(arg));
    return argv;
}

std::filesystem::path unique_cli_test_path(const char *name)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("slayer3d_tool_cli_" + std::string(name) + "_" + std::to_string(stamp) + ".json");
}

void write_text(const std::filesystem::path &path, const char *text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_text_file(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string read_temp_file(FILE *file)
{
    if (file == nullptr)
        return {};
    std::fflush(file);
    std::fseek(file, 0, SEEK_SET);
    std::string text;
    char buffer[256];
    while (const size_t read = std::fread(buffer, 1, sizeof(buffer), file))
        text.append(buffer, read);
    return text;
}

std::filesystem::path unique_cli_test_dir(const char *name)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("slayer3d_tool_cli_" + std::string(name) + "_" + std::to_string(stamp));
}

class ScopedCurrentPath
{
  public:
    explicit ScopedCurrentPath(const std::filesystem::path &path) : previous_(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath()
    {
        std::filesystem::current_path(previous_);
    }

    ScopedCurrentPath(const ScopedCurrentPath &) = delete;
    ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete;

  private:
    std::filesystem::path previous_;
};
} // namespace

TEST(ToolCli, RunnerParsesDirectoryMount)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_runner", "--root", "game/data", "--data", "asset://game.game.json", "--media", "media"});
    slayer3d_runner_args args;
    ASSERT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.mount_kind, SLAYER3D_RUNNER_MOUNT_DIRECTORY);
    EXPECT_STREQ(args.mount_path, "game/data");
    EXPECT_STREQ(args.data_asset_path, "asset://game.game.json");
    EXPECT_STREQ(args.media_dir, "media");
}

TEST(ToolCli, RunnerParsesSlayerMapMode)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_runner", "--map", "/tmp/map.json", "--media", "media", "--state", "debug=true"});
    slayer3d_runner_args args;
    ASSERT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.mount_kind, SLAYER3D_RUNNER_MOUNT_NONE);
    EXPECT_STREQ(args.map_path, "/tmp/map.json");
    EXPECT_EQ(args.data_asset_path, nullptr);
    EXPECT_STREQ(args.media_dir, "media");
    ASSERT_EQ(args.state_assignment_count, 1);
    EXPECT_STREQ(args.state_assignments[0], "debug=true");
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerRejectsSlayerMapWithExplicitGameDataMount)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_runner", "--map", "/tmp/map.json", "--root", "game/data", "--data", "asset://game.json"});
    slayer3d_runner_args args;
    EXPECT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerRejectsMultipleMounts)
{
    std::vector<char *> argv = argv_from(
        {"slayer3d_runner", "--root", "game/data", "--pack", "game.slayer3dpak", "--data", "asset://game.json"});
    slayer3d_runner_args args;
    EXPECT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
}

TEST(ToolCli, RunnerRejectsEmptyMountPath)
{
    std::vector<char *> argv = argv_from({"slayer3d_runner", "--root", "", "--data", "asset://game.json"});
    slayer3d_runner_args args;
    EXPECT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
}

TEST(ToolCli, RunnerHelpDoesNotRequireMountOrData)
{
    std::vector<char *> argv = argv_from({"slayer3d_runner", "--help"});
    slayer3d_runner_args args;
    EXPECT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_HELP);
}

TEST(ToolCli, RunnerAllowsFusedExecutableWithoutExplicitMount)
{
    std::vector<char *> argv = argv_from({"slayer3d_runner"});
    slayer3d_runner_args args;
    ASSERT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.mount_kind, SLAYER3D_RUNNER_MOUNT_NONE);
    EXPECT_EQ(args.data_asset_path, nullptr);
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerParsesDirectStartStateInputs)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_runner", "--root", "game/data", "--data", "asset://game.game.json", "--scene",
                   "scene.level1", "--player-start", "player_start.level1", "--state-file", "dev/level1.json",
                   "--state-json", "{\"lives\":3}", "--state", "checkpoint=midboss", "--state", "debug=true"});
    slayer3d_runner_args args;
    ASSERT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.scene, "scene.level1");
    EXPECT_STREQ(args.player_start, "player_start.level1");
    ASSERT_EQ(args.state_file_count, 1);
    EXPECT_STREQ(args.state_files[0], "dev/level1.json");
    ASSERT_EQ(args.state_json_count, 1);
    EXPECT_STREQ(args.state_json_values[0], "{\"lives\":3}");
    ASSERT_EQ(args.state_assignment_count, 2);
    EXPECT_STREQ(args.state_assignments[0], "checkpoint=midboss");
    EXPECT_STREQ(args.state_assignments[1], "debug=true");
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerParsesTestRunManifestWithoutData)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_runner", "--root", "game/data", "--test-run-manifest", "asset://editor/run.json"});
    slayer3d_runner_args args;
    ASSERT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.mount_kind, SLAYER3D_RUNNER_MOUNT_DIRECTORY);
    EXPECT_STREQ(args.mount_path, "game/data");
    EXPECT_EQ(args.data_asset_path, nullptr);
    EXPECT_FALSE(args.data_asset_path_explicit);
    EXPECT_STREQ(args.test_run_manifest_path, "asset://editor/run.json");
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerRejectsEmptyPlayerStart)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_runner", "--root", "game/data", "--data", "asset://game.game.json", "--player-start", ""});
    slayer3d_runner_args args;
    EXPECT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerRejectsEmptyTestRunManifestPath)
{
    std::vector<char *> argv = argv_from({"slayer3d_runner", "--root", "game/data", "--test-run-manifest", ""});
    slayer3d_runner_args args;
    EXPECT_EQ(slayer3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, EditorNewLoadsProjectAndBuildsRunnerInvocation)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_project");
    std::filesystem::create_directories(project_dir / "data");
    std::filesystem::create_directories(project_dir / "media" / "textures");
    std::filesystem::create_directories(project_dir / "media" / "models");
    std::filesystem::create_directories(project_dir / "media" / "sprites");
    std::filesystem::create_directories(project_dir / "media" / "skyboxes");
    std::filesystem::create_directories(project_dir / "media" / "liquids");
    std::filesystem::create_directories(project_dir / "media" / "effects");
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json",
  "media_root": "media",
  "test_run_output": "build/test-run.json"
})json");
    const std::string project = project_dir.string();
    const std::string output = (project_dir / "levels" / "new.json").string();
    std::vector<char *> argv =
        argv_from({"slayer3d_editor", "new", "--project", project.c_str(), "--output", output.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.command, SLAYER3D_EDITOR_COMMAND_NEW);

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    EXPECT_STREQ(loaded_project.asset_sources.textures.relative_path, "media/textures");
    EXPECT_STREQ(loaded_project.asset_sources.models.relative_path, "media/models");
    EXPECT_STREQ(loaded_project.asset_sources.liquids.relative_path, "media/liquids");
    EXPECT_TRUE(loaded_project.asset_sources.textures.available);
    EXPECT_TRUE(loaded_project.asset_sources.liquids.available);
    EXPECT_TRUE(loaded_project.asset_sources.effects.available);
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;

    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    ASSERT_GE(invocation.argc, 15);
    EXPECT_STREQ(invocation.argv[0], "slayer3d_editor");
    EXPECT_STREQ(invocation.argv[1], "--root");
    EXPECT_STREQ(invocation.argv[2], (project_dir / "data").string().c_str());
    EXPECT_STREQ(invocation.argv[3], "--data");
    EXPECT_STREQ(invocation.argv[4], "asset://editor.game.json");
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("editor.command=new"), std::string::npos);
    EXPECT_NE(joined.find("editor.input.path="), std::string::npos);
    EXPECT_NE(joined.find("editor.save.path=" + output), std::string::npos);
    EXPECT_NE(joined.find("editor.test_run.path=" + (project_dir / "build" / "test-run.json").string()),
              std::string::npos);
    EXPECT_NE(joined.find("editor.project.dir=" + project_dir.string()), std::string::npos);
    EXPECT_NE(joined.find("editor.project.data_root=data"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.textures.relative=media/textures"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.textures.available=true"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.liquids.relative=media/liquids"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.liquids.available=true"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.any_missing=false"), std::string::npos);
    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorProjectAssetSourcesAllowConfiguredMissingDirectories)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_project_assets");
    std::filesystem::create_directories(project_dir / "data");
    std::filesystem::create_directories(project_dir / "content" / "textures");
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json",
  "asset_sources": {
    "textures": "content/textures",
    "models": "content/models",
    "sprites": "content/sprites",
    "skyboxes": "content/skyboxes",
    "liquids": "content/liquids",
    "effects": "content/effects"
  }
})json");

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(project_dir.string().c_str(), &loaded_project, error, sizeof(error)))
        << error;
    EXPECT_STREQ(loaded_project.asset_sources.textures.relative_path, "content/textures");
    EXPECT_STREQ(loaded_project.asset_sources.models.relative_path, "content/models");
    EXPECT_STREQ(loaded_project.asset_sources.liquids.relative_path, "content/liquids");
    EXPECT_STREQ(loaded_project.asset_sources.textures.path, (project_dir / "content" / "textures").string().c_str());
    EXPECT_TRUE(loaded_project.asset_sources.textures.available);
    EXPECT_FALSE(loaded_project.asset_sources.models.available);
    EXPECT_FALSE(loaded_project.asset_sources.liquids.available);

    slayer3d_editor_args args{};
    args.command = SLAYER3D_EDITOR_COMMAND_NEW;
    const std::string output = (project_dir / "level.slayermap.json").string();
    args.output_path = output.c_str();
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("editor.asset_source.textures.available=true"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.models.available=false"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.any_missing=true"), std::string::npos);

    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorProjectMediaRootPreservesManifestRelativePath)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_project_media_root");
    std::filesystem::create_directories(project_dir / "data");
    std::filesystem::create_directories(project_dir / "content" / "textures");
    std::filesystem::create_directories(project_dir / "content" / "models");
    std::filesystem::create_directories(project_dir / "content" / "sprites");
    std::filesystem::create_directories(project_dir / "content" / "skyboxes");
    std::filesystem::create_directories(project_dir / "content" / "liquids");
    std::filesystem::create_directories(project_dir / "content" / "effects");
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json",
  "media_root": "content"
})json");

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(project_dir.string().c_str(), &loaded_project, error, sizeof(error)))
        << error;
    ASSERT_STREQ(loaded_project.media_root_relative_path, "content");
    ASSERT_STREQ(loaded_project.asset_sources.textures.relative_path, "content/textures");

    slayer3d_editor_args args{};
    args.command = SLAYER3D_EDITOR_COMMAND_NEW;
    const std::string output = (project_dir / "level.slayermap.json").string();
    args.output_path = output.c_str();
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_STREQ(launch.media_relative_path, "content");
    ASSERT_STREQ(launch.asset_sources->textures.relative_path, "content/textures");
    ASSERT_STREQ(launch.asset_sources->liquids.relative_path, "content/liquids");

    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("editor.media.relative=content"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.textures.relative=content/textures"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.liquids.relative=content/liquids"), std::string::npos);

    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorDefaultLaunchUsesCompileTimeMediaWhenCwdMediaIsMissing)
{
    const std::filesystem::path cwd_root = unique_cli_test_dir("editor_embedded_media_fallback");
    std::filesystem::create_directories(cwd_root);
    ASSERT_FALSE(std::filesystem::exists(cwd_root / "media"));

    {
        ScopedCurrentPath cwd(cwd_root);
        std::vector<char *> argv = argv_from({"slayer3d_editor"});
        slayer3d_editor_args args;
        ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);

        char error[512]{};
        slayer3d_editor_project loaded_project;
        ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
        slayer3d_editor_launch launch;
        ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
        ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;

        ASSERT_NE(launch.media_dir, nullptr);
        ASSERT_NE(launch.asset_sources, nullptr);
        EXPECT_NE(std::filesystem::path(launch.media_dir), cwd_root / "media");
        EXPECT_TRUE(launch.asset_sources->textures.available);
        EXPECT_TRUE(launch.asset_sources->models.available);
        EXPECT_TRUE(launch.asset_sources->liquids.available);
        EXPECT_STREQ(launch.media_relative_path, "media");
        EXPECT_STREQ(launch.asset_sources->textures.relative_path, "media/textures");
        EXPECT_STREQ(launch.asset_sources->liquids.relative_path, "media/liquids");

        slayer3d_editor_launch_destroy(&launch);
        slayer3d_editor_project_destroy(&loaded_project);
        slayer3d_editor_args_destroy(&args);
    }
    std::filesystem::remove_all(cwd_root);
}

TEST(ToolCli, EditorMediaPathBecomesAuthoritativeAssetSourceRoot)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_media_path");
    const std::filesystem::path media_dir = project_dir / "external_media";
    std::filesystem::create_directories(project_dir / "data");
    std::filesystem::create_directories(media_dir / "textures");
    std::filesystem::create_directories(media_dir / "models");
    std::filesystem::create_directories(media_dir / "sprites");
    std::filesystem::create_directories(media_dir / "skyboxes");
    std::filesystem::create_directories(media_dir / "liquids");
    std::filesystem::create_directories(media_dir / "effects");
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json"
})json");

    const std::string project = project_dir.string();
    const std::string media = media_dir.string();
    const std::string output = (project_dir / "level.slayermap.json").string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "new", "--project", project.c_str(), "--output",
                                          output.c_str(), "--media-path", media.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.media_path, media.c_str());

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
    ASSERT_NE(launch.asset_sources, nullptr);
    EXPECT_STREQ(launch.media_dir, media.c_str());
    EXPECT_STREQ(launch.asset_sources->textures.path, (media_dir / "textures").string().c_str());
    EXPECT_STREQ(launch.asset_sources->models.path, (media_dir / "models").string().c_str());
    EXPECT_STREQ(launch.asset_sources->liquids.path, (media_dir / "liquids").string().c_str());
    EXPECT_TRUE(launch.asset_sources->textures.available);
    EXPECT_TRUE(launch.asset_sources->liquids.available);

    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("editor.media.path=" + media), std::string::npos);
    EXPECT_NE(joined.find("editor.media.available=true"), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.textures.path=" + (media_dir / "textures").string()), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.liquids.path=" + (media_dir / "liquids").string()), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.any_missing=false"), std::string::npos);

    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorMediaPathCannotBeCombinedWithPerToolOverrides)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_editor", "--media-path", "media", "--texture-path", "media/textures"});
    slayer3d_editor_args args;
    EXPECT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
    slayer3d_editor_args_destroy(&args);
}

TEST(ToolCli, EditorTexturePathOverrideBecomesAuthoritativeTextureSource)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_texture_override");
    const std::filesystem::path override_dir = project_dir / "external_textures";
    std::filesystem::create_directories(project_dir / "data");
    std::filesystem::create_directories(project_dir / "media" / "textures");
    std::filesystem::create_directories(project_dir / "media" / "models");
    std::filesystem::create_directories(project_dir / "media" / "sprites");
    std::filesystem::create_directories(project_dir / "media" / "skyboxes");
    std::filesystem::create_directories(project_dir / "media" / "liquids");
    std::filesystem::create_directories(project_dir / "media" / "effects");
    std::filesystem::create_directories(override_dir);
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json",
  "media_root": "media"
})json");

    const std::string project = project_dir.string();
    const std::string textures = override_dir.string();
    const std::string output = (project_dir / "level.slayermap.json").string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "new", "--project", project.c_str(), "--output",
                                          output.c_str(), "--texture-path", textures.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.texture_path, textures.c_str());

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;

    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("editor.asset_source.textures.path=" + textures), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.textures.relative=" + textures), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.textures.available=true"), std::string::npos);
    EXPECT_EQ(joined.find("editor.asset_source.textures.path=" + (project_dir / "media" / "textures").string()),
              std::string::npos);

    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorDefaultLaunchAcceptsTexturePathOverride)
{
    const std::filesystem::path textures_dir = unique_cli_test_dir("editor_default_texture_override");
    std::filesystem::create_directories(textures_dir);

    const std::string textures = textures_dir.string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "--texture-path", textures.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.command, SLAYER3D_EDITOR_COMMAND_NEW);
    ASSERT_NE(args.project, nullptr);
    ASSERT_NE(args.output_path, nullptr);
    EXPECT_STREQ(args.texture_path, textures.c_str());

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
    ASSERT_NE(launch.asset_sources, nullptr);
    ASSERT_STREQ(launch.asset_sources->textures.path, textures.c_str());

    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(textures_dir);
}

TEST(ToolCli, EditorDefaultLaunchAcceptsSkyboxPathOverride)
{
    const std::filesystem::path skyboxes_dir = unique_cli_test_dir("editor_default_skybox_override");
    std::filesystem::create_directories(skyboxes_dir);

    const std::string skyboxes = skyboxes_dir.string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "--skybox-path", skyboxes.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.command, SLAYER3D_EDITOR_COMMAND_NEW);
    ASSERT_NE(args.project, nullptr);
    ASSERT_NE(args.output_path, nullptr);
    EXPECT_STREQ(args.skybox_path, skyboxes.c_str());

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
    ASSERT_NE(launch.asset_sources, nullptr);
    ASSERT_STREQ(launch.asset_sources->skyboxes.path, skyboxes.c_str());

    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(skyboxes_dir);
}

TEST(ToolCli, EditorSkyboxPathOverrideRequiresExistingDirectory)
{
    std::vector<char *> argv = argv_from({"slayer3d_editor", "--skybox-path", "/does/not/exist/skyboxes"});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    EXPECT_FALSE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("skybox path"), std::string::npos);

    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
}

TEST(ToolCli, EditorModelPathOverrideBecomesAuthoritativeModelSource)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_model_override");
    const std::filesystem::path override_dir = project_dir / "external_models";
    std::filesystem::create_directories(project_dir / "data");
    std::filesystem::create_directories(project_dir / "media" / "textures");
    std::filesystem::create_directories(project_dir / "media" / "models");
    std::filesystem::create_directories(project_dir / "media" / "sprites");
    std::filesystem::create_directories(project_dir / "media" / "skyboxes");
    std::filesystem::create_directories(project_dir / "media" / "liquids");
    std::filesystem::create_directories(project_dir / "media" / "effects");
    std::filesystem::create_directories(override_dir);
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json",
  "media_root": "media"
})json");

    const std::string project = project_dir.string();
    const std::string models = override_dir.string();
    const std::string output = (project_dir / "level.slayermap.json").string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "new", "--project", project.c_str(), "--output",
                                          output.c_str(), "--model-path", models.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.model_path, models.c_str());

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;

    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("editor.asset_source.models.path=" + models), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.models.relative=" + models), std::string::npos);
    EXPECT_NE(joined.find("editor.asset_source.models.available=true"), std::string::npos);
    EXPECT_EQ(joined.find("editor.asset_source.models.path=" + (project_dir / "media" / "models").string()),
              std::string::npos);

    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorDefaultLaunchAcceptsModelPathOverride)
{
    const std::filesystem::path models_dir = unique_cli_test_dir("editor_default_model_override");
    std::filesystem::create_directories(models_dir);

    const std::string models = models_dir.string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "--model-path", models.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.command, SLAYER3D_EDITOR_COMMAND_NEW);
    ASSERT_NE(args.project, nullptr);
    ASSERT_NE(args.output_path, nullptr);
    EXPECT_STREQ(args.model_path, models.c_str());

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
    ASSERT_NE(launch.asset_sources, nullptr);
    EXPECT_TRUE(std::filesystem::path(launch.asset_sources->models.path).is_absolute());
    EXPECT_TRUE(std::filesystem::is_directory(launch.asset_sources->models.path));
    EXPECT_STREQ(launch.asset_sources->models.relative_path, models.c_str());

    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(models_dir);
}

TEST(ToolCli, EditorDefaultLaunchResolvesRelativeTexturePathOverride)
{
    const std::filesystem::path cwd_root = unique_cli_test_dir("editor_relative_texture_override");
    std::filesystem::create_directories(cwd_root / "media" / "textures");
    {
        ScopedCurrentPath cwd(cwd_root);
        ASSERT_TRUE(std::filesystem::is_directory("media/textures"));

        std::vector<char *> argv = argv_from({"slayer3d_editor", "--texture-path", "media/textures"});
        slayer3d_editor_args args;
        ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
        EXPECT_STREQ(args.texture_path, "media/textures");

        char error[512]{};
        slayer3d_editor_project loaded_project;
        ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
        slayer3d_editor_launch launch;
        ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
        ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
        ASSERT_NE(launch.asset_sources, nullptr);
        EXPECT_TRUE(std::filesystem::path(launch.asset_sources->textures.path).is_absolute());
        EXPECT_TRUE(std::filesystem::is_directory(launch.asset_sources->textures.path));
        EXPECT_STREQ(launch.asset_sources->textures.relative_path, "media/textures");

        slayer3d_editor_launch_destroy(&launch);
        slayer3d_editor_project_destroy(&loaded_project);
        slayer3d_editor_args_destroy(&args);
    }
    std::filesystem::remove_all(cwd_root);
}

TEST(ToolCli, EditorTexturePathOverrideRequiresExistingDirectory)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_texture_override_missing");
    std::filesystem::create_directories(project_dir / "data");
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json"
})json");

    const std::string project = project_dir.string();
    const std::string textures = (project_dir / "missing_textures").string();
    const std::string output = (project_dir / "level.slayermap.json").string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "new", "--project", project.c_str(), "--output",
                                          output.c_str(), "--texture-path", textures.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    EXPECT_FALSE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("texture path"), std::string::npos);

    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorOpenDefaultsOutputToInput)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_open_project");
    std::filesystem::create_directories(project_dir / "data");
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json"
})json");
    const std::filesystem::path level_path = project_dir / "level.json";
    write_text(level_path, R"json({ "schema": "slayer3d.fragment.v0", "brush_worlds": [] })json");
    const std::string project = project_dir.string();
    const std::string input = level_path.string();
    std::vector<char *> argv =
        argv_from({"slayer3d_editor", "open", "--project", project.c_str(), "--input", input.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
    EXPECT_STREQ(launch.input_path, input.c_str());
    EXPECT_STREQ(launch.save_path, input.c_str());

    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, EditorNoArgsLaunchesDefaultUntitledMap)
{
    std::vector<char *> argv = argv_from({"slayer3d_editor"});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.command, SLAYER3D_EDITOR_COMMAND_NEW);
    ASSERT_NE(args.project, nullptr);
    ASSERT_NE(args.output_path, nullptr);
    EXPECT_STREQ(args.project, "embedded://slayer3d_editor");
    EXPECT_NE(std::string(args.output_path).find(".slayermap.json"), std::string::npos);

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
    EXPECT_STREQ(launch.input_path, "");
    EXPECT_STREQ(launch.save_path, args.output_path);
    EXPECT_TRUE(launch.embedded);

    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("--embedded"), std::string::npos);
    EXPECT_EQ(joined.find("--root"), std::string::npos);
    EXPECT_NE(joined.find("asset://slayer3d_editor.game.json"), std::string::npos);
    EXPECT_NE(joined.find("editor.command=new"), std::string::npos);
    EXPECT_NE(joined.find(std::string("editor.save.path=") + args.output_path), std::string::npos);
    EXPECT_NE(joined.find("editor.project.dir=" + std::string(launch.project_dir)), std::string::npos);

    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
}

TEST(ToolCli, EditorLightingPlanPrintsSharedMapLightingSummary)
{
    const std::filesystem::path map_path = unique_cli_test_path("lighting_plan");
    write_text(map_path,
               R"json({
  "format": "slayer3d.map",
  "version": 1,
  "lights": [
    { "id": "light.dynamic", "kind": "dynamic", "type": "point" },
    { "id": "light.static", "kind": "baked", "type": "directional" },
    { "id": "light.area", "kind": "baked", "type": "area_rect", "width": 2.0, "height": 1.0 }
  ]
})json");
    const std::string input = map_path.string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "lighting-plan", "--input", input.c_str(), "--final",
                                          "--max-dynamic-lights", "1", "--max-static-lights", "1"});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.command, SLAYER3D_EDITOR_COMMAND_LIGHTING_PLAN);
    EXPECT_STREQ(args.input_path, input.c_str());
    EXPECT_TRUE(args.lighting_final_quality);
    EXPECT_EQ(args.max_dynamic_lights, 1);
    EXPECT_EQ(args.max_static_lights, 1);

    FILE *output = std::tmpfile();
    ASSERT_NE(output, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_editor_run_lighting_plan(&args, output, error, sizeof(error))) << error;
    const std::string text = read_temp_file(output);
    std::fclose(output);

    EXPECT_NE(text.find("quality: final"), std::string::npos);
    EXPECT_NE(text.find("lights: total=3 dynamic=1 static=2 area=1"), std::string::npos);
    EXPECT_NE(text.find("runtime_preview_lights: 3 / 1 (exceeded)"), std::string::npos);
    EXPECT_NE(text.find("bake_lights: 2 / 1 (exceeded)"), std::string::npos);
    EXPECT_NE(text.find("requires_static_bake: yes"), std::string::npos);

    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove(map_path);
}

TEST(ToolCli, EditorLightingPlanCanPrintArtifactManifest)
{
    const std::filesystem::path map_path = unique_cli_test_path("lighting_manifest");
    write_text(map_path,
               R"json({
  "format": "slayer3d.map",
  "version": 1,
  "metadata": { "id": "map.test.lighting_manifest", "name": "Lighting Manifest Test" },
  "brushes": [
    { "id": "brush.floor", "geometry": { "kind": "box", "min": [0, 0, 0], "max": [4, 0.25, 4] } }
  ],
  "lights": [
    { "id": "light.static", "kind": "baked", "type": "directional" },
    { "id": "light.area", "kind": "baked", "type": "area_rect", "width": 2.0, "height": 1.0 }
  ]
})json");
    const std::string input = map_path.string();
    std::vector<char *> argv =
        argv_from({"slayer3d_editor", "lighting-plan", "--input", input.c_str(), "--manifest", "--final"});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_TRUE(args.lighting_manifest);
    EXPECT_TRUE(args.lighting_final_quality);

    FILE *output = std::tmpfile();
    ASSERT_NE(output, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_editor_run_lighting_plan(&args, output, error, sizeof(error))) << error;
    const std::string text = read_temp_file(output);
    std::fclose(output);

    EXPECT_NE(text.find("\"schema\": \"slayer3d.lighting_artifact_manifest.v0\""), std::string::npos);
    EXPECT_NE(text.find("\"quality\": \"final\""), std::string::npos);
    EXPECT_NE(text.find("\"id\": \"map.test.lighting_manifest\""), std::string::npos);
    EXPECT_NE(text.find("\"id\": \"lighting.static.default\""), std::string::npos);
    EXPECT_NE(text.find("\"storage\": \"embedded_json\""), std::string::npos);
    EXPECT_NE(text.find("\"status\": \"planned\""), std::string::npos);
    EXPECT_NE(text.find("\"light_count\": 2"), std::string::npos);

    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove(map_path);
}

TEST(ToolCli, EditorLightingPlanCanPrintStaticArtifact)
{
    const std::filesystem::path map_path = unique_cli_test_path("lighting_static_artifact");
    write_text(map_path,
               R"json({
  "format": "slayer3d.map",
  "version": 1,
  "metadata": { "id": "map.test.lighting_static_artifact", "name": "Lighting Static Artifact Test" },
  "global": { "ambient_light": [8, 8, 8, 255] },
  "brushes": [
    { "id": "brush.floor", "geometry": { "kind": "box", "min": [0, 0, 0], "max": [4, 0.25, 4] } }
  ],
  "lights": [
    { "id": "light.static", "kind": "baked", "type": "directional", "direction": [0, -1, 0] }
  ]
})json");
    const std::string input = map_path.string();
    std::vector<char *> argv =
        argv_from({"slayer3d_editor", "lighting-plan", "--input", input.c_str(), "--static-artifact", "--final"});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_TRUE(args.lighting_static_artifact);
    EXPECT_TRUE(args.lighting_final_quality);

    FILE *output = std::tmpfile();
    ASSERT_NE(output, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_editor_run_lighting_plan(&args, output, error, sizeof(error))) << error;
    const std::string text = read_temp_file(output);
    std::fclose(output);

    EXPECT_NE(text.find("\"schema\": \"slayer3d.lighting_static.v0\""), std::string::npos);
    EXPECT_NE(text.find("\"quality\": \"final\""), std::string::npos);
    EXPECT_NE(text.find("\"id\": \"map.test.lighting_static_artifact\""), std::string::npos);
    EXPECT_NE(text.find("\"sample_model\": \"box_face_irradiance_preview\""), std::string::npos);
    EXPECT_NE(text.find("\"bake_lights\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"samples\": 6"), std::string::npos);
    EXPECT_NE(text.find("\"face\": \"positive_y\""), std::string::npos);

    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove(map_path);
}

TEST(ToolCli, EditorLightingPlanCanWriteStaticArtifactOutputFile)
{
    const std::filesystem::path map_path = unique_cli_test_path("lighting_static_artifact_file");
    const std::filesystem::path output_dir = unique_cli_test_dir("lighting_static_artifact_output");
    const std::filesystem::path output_path = output_dir / "nested" / "static-lighting.json";
    write_text(map_path,
               R"json({
  "format": "slayer3d.map",
  "version": 1,
  "metadata": { "id": "map.test.lighting_static_artifact_file" },
  "brushes": [
    { "id": "brush.floor", "geometry": { "kind": "box", "min": [0, 0, 0], "max": [1, 1, 1] } }
  ],
  "lights": [
    { "id": "light.static", "kind": "baked", "type": "directional", "direction": [0, -1, 0] }
  ]
})json");
    const std::string input = map_path.string();
    const std::string output = output_path.string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "lighting-plan", "--input", input.c_str(),
                                          "--static-artifact", "--output", output.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.output_path, output.c_str());

    FILE *stdout_capture = std::tmpfile();
    ASSERT_NE(stdout_capture, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_editor_run_lighting_plan(&args, stdout_capture, error, sizeof(error))) << error;
    EXPECT_TRUE(read_temp_file(stdout_capture).empty());
    std::fclose(stdout_capture);

    ASSERT_TRUE(std::filesystem::exists(output_path));
    const std::string text = read_text_file(output_path);
    EXPECT_NE(text.find("\"schema\": \"slayer3d.lighting_static.v0\""), std::string::npos);
    EXPECT_NE(text.find("\"id\": \"map.test.lighting_static_artifact_file\""), std::string::npos);
    EXPECT_NE(text.find("\"samples\": 6"), std::string::npos);

    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove(map_path);
    std::filesystem::remove_all(output_dir);
}

TEST(ToolCli, EditorLightingArtifactValidateAcceptsStaticArtifact)
{
    const std::filesystem::path artifact_path = unique_cli_test_path("lighting_static_artifact_validate");
    write_text(artifact_path,
               R"json({
  "schema": "slayer3d.lighting_static.v0",
  "quality": "preview",
  "bake_group": "default",
  "self_contained": true,
  "sample_model": "box_face_irradiance_preview",
  "counts": { "samples": 1 },
  "samples": [
    {
      "brush": "brush.floor",
      "face": "positive_y",
      "position": [0, 1, 0],
      "normal": [0, 1, 0],
      "color": [0.25, 0.5, 1.0],
      "intensity": 1.0
    }
  ]
})json");
    const std::string input = artifact_path.string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "lighting-artifact-validate", "--input", input.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.command, SLAYER3D_EDITOR_COMMAND_LIGHTING_ARTIFACT_VALIDATE);

    FILE *output = std::tmpfile();
    ASSERT_NE(output, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_editor_run_lighting_artifact_validate(&args, output, error, sizeof(error))) << error;
    const std::string text = read_temp_file(output);
    std::fclose(output);
    EXPECT_NE(text.find("Static lighting artifact valid:"), std::string::npos);

    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove(artifact_path);
}

TEST(ToolCli, EditorLightingArtifactValidateRejectsInvalidStaticArtifact)
{
    const std::filesystem::path artifact_path = unique_cli_test_path("lighting_static_artifact_validate_bad");
    write_text(artifact_path,
               R"json({
  "schema": "slayer3d.lighting_static.v0",
  "counts": { "samples": 1 },
  "samples": [
    {
      "brush": "brush.floor",
      "face": "bogus",
      "position": [0, 1, 0],
      "normal": [0, 1, 0],
      "color": [1, 1, 1],
      "intensity": 1
    }
  ]
})json");
    const std::string input = artifact_path.string();
    std::vector<char *> argv = argv_from({"slayer3d_editor", "lighting-artifact-validate", "--input", input.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);

    FILE *output = std::tmpfile();
    ASSERT_NE(output, nullptr);
    char error[512]{};
    EXPECT_FALSE(slayer3d_editor_run_lighting_artifact_validate(&args, output, error, sizeof(error)));
    std::fclose(output);
    EXPECT_NE(std::string(error).find("$.samples[0].face"), std::string::npos) << error;

    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove(artifact_path);
}

TEST(ToolCli, EditorLightingPlanRejectsOutputForSummaryMode)
{
    const std::filesystem::path map_path = unique_cli_test_path("lighting_summary_output_rejected");
    write_text(map_path,
               R"json({
  "format": "slayer3d.map",
  "version": 1
})json");
    const std::string input = map_path.string();
    std::vector<char *> argv =
        argv_from({"slayer3d_editor", "lighting-plan", "--input", input.c_str(), "--output", "/tmp/lighting.txt"});
    slayer3d_editor_args args;
    EXPECT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove(map_path);
}

TEST(ToolCli, EditorRejectsUnknownSubcommand)
{
    std::vector<char *> argv = argv_from({"slayer3d_editor", "bogus"});
    slayer3d_editor_args args;
    EXPECT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
}

TEST(ToolCli, EditorRejectsNewWithoutOutput)
{
    std::vector<char *> argv = argv_from({"slayer3d_editor", "new", "--project", "project"});
    slayer3d_editor_args args;
    EXPECT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
}

TEST(ToolCli, EditorRejectsMalformedOpenInputBeforeLaunch)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_bad_open_project");
    std::filesystem::create_directories(project_dir / "data");
    write_text(project_dir / "slayer3d.project.json",
               R"json({
  "schema": "slayer3d.project.v0",
  "data_root": "data",
  "editor_entry": "asset://editor.game.json"
})json");
    const std::filesystem::path level_path = project_dir / "level.json";
    write_text(level_path, R"json({ "schema": "slayer3d.game.v0" })json");
    const std::string project = project_dir.string();
    const std::string input = level_path.string();
    std::vector<char *> argv =
        argv_from({"slayer3d_editor", "open", "--project", project.c_str(), "--input", input.c_str()});
    slayer3d_editor_args args;
    ASSERT_EQ(slayer3d_editor_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    EXPECT_FALSE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("slayer3d.fragment.v0"), std::string::npos) << error;

    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
    std::filesystem::remove_all(project_dir);
}

TEST(ToolCli, PackParsesRepeatedFiles)
{
    std::vector<char *> argv = argv_from({"slayer3d_pack", "--output", "game.slayer3dpak", "--root", "game/data",
                                          "--file", "game.game.json", "--file", "scenes/play.scene.json"});
    slayer3d_pack_args args;
    ASSERT_EQ(slayer3d_pack_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.output, "game.slayer3dpak");
    EXPECT_STREQ(args.root, "game/data");
    ASSERT_EQ(args.file_count, 2);
    EXPECT_STREQ(args.files[0], "game.game.json");
    EXPECT_STREQ(args.files[1], "scenes/play.scene.json");
    slayer3d_pack_args_destroy(&args);
}

TEST(ToolCli, PackRejectsMissingFile)
{
    std::vector<char *> argv = argv_from({"slayer3d_pack", "--output", "game.slayer3dpak", "--root", "game/data"});
    slayer3d_pack_args args;
    EXPECT_EQ(slayer3d_pack_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
    slayer3d_pack_args_destroy(&args);
}

TEST(ToolCli, PackRejectsEmptyFile)
{
    std::vector<char *> argv =
        argv_from({"slayer3d_pack", "--output", "game.slayer3dpak", "--root", "game/data", "--file", ""});
    slayer3d_pack_args args;
    EXPECT_EQ(slayer3d_pack_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
    slayer3d_pack_args_destroy(&args);
}

TEST(ToolCli, BundleParsesRequiredArgumentsAndOptionalFiles)
{
    std::vector<char *> argv = argv_from({"slayer3d_bundle", "--runner", "slayer3d_runner", "--root", "game/data",
                                          "--data", "asset://game.game.json", "--output", "Game", "--file",
                                          "game.game.json", "--file", "scripts/main.lua"});
    slayer3d_bundle_args args;
    ASSERT_EQ(slayer3d_bundle_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.runner, "slayer3d_runner");
    EXPECT_STREQ(args.root, "game/data");
    EXPECT_STREQ(args.data_asset_path, "asset://game.game.json");
    EXPECT_STREQ(args.output, "Game");
    ASSERT_EQ(args.file_count, 2);
    EXPECT_STREQ(args.files[0], "game.game.json");
    EXPECT_STREQ(args.files[1], "scripts/main.lua");
    slayer3d_bundle_args_destroy(&args);
}

TEST(ToolCli, BundleAllowsOmittingFilesForRecursiveRootPackaging)
{
    std::vector<char *> argv = argv_from({"slayer3d_bundle", "--runner", "slayer3d_runner", "--root", "game/data",
                                          "--data", "asset://game.game.json", "--output", "Game"});
    slayer3d_bundle_args args;
    ASSERT_EQ(slayer3d_bundle_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_OK);
    EXPECT_EQ(args.file_count, 0);
    EXPECT_EQ(args.files, nullptr);
    slayer3d_bundle_args_destroy(&args);
}

TEST(ToolCli, BundleRejectsMissingRequiredArguments)
{
    std::vector<char *> argv = argv_from({"slayer3d_bundle", "--runner", "slayer3d_runner", "--root", "game/data"});
    slayer3d_bundle_args args;
    EXPECT_EQ(slayer3d_bundle_args_parse((int)argv.size(), argv.data(), &args, nullptr), SLAYER3D_TOOL_CLI_ERROR);
}

TEST(ToolCli, FusedFooterRoundTripsAndRejectsPlainFiles)
{
    const std::filesystem::path path = unique_cli_test_path("fused");
    write_text(path, "runner-bytes");

    char error[256]{};
    slayer3d_fused_pack missing{};
    EXPECT_FALSE(slayer3d_fused_read_footer(path.string().c_str(), &missing, error, sizeof(error)));

    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << "pack-bytes";
    out.close();
    ASSERT_TRUE(
        slayer3d_fused_append_footer(path.string().c_str(), 12, 10, "asset://game.game.json", error, sizeof(error)))
        << error;

    slayer3d_fused_pack pack{};
    ASSERT_TRUE(slayer3d_fused_read_footer(path.string().c_str(), &pack, error, sizeof(error))) << error;
    EXPECT_EQ(pack.pack_offset, 12u);
    EXPECT_EQ(pack.pack_size, 10u);
    EXPECT_STREQ(pack.data_asset_path, "asset://game.game.json");

    void *bytes = nullptr;
    size_t size = 0;
    ASSERT_TRUE(slayer3d_fused_read_pack_bytes(path.string().c_str(), &pack, &bytes, &size, error, sizeof(error)))
        << error;
    ASSERT_EQ(size, 10u);
    EXPECT_EQ(std::memcmp(bytes, "pack-bytes", 10), 0);
    SDL_free(bytes);

    std::filesystem::remove(path);
}

TEST(ToolCli, FusedFooterRejectsInvalidPackRange)
{
    const std::filesystem::path path = unique_cli_test_path("fused_bad_range");
    write_text(path, "runner");

    char error[256]{};
    ASSERT_TRUE(
        slayer3d_fused_append_footer(path.string().c_str(), 128, 64, "asset://game.game.json", error, sizeof(error)))
        << error;

    slayer3d_fused_pack pack{};
    EXPECT_FALSE(slayer3d_fused_read_footer(path.string().c_str(), &pack, error, sizeof(error)));
    std::filesystem::remove(path);
}

TEST(ToolCli, FusedPackMountsAppendedAssetPack)
{
    const std::filesystem::path dir = unique_cli_test_dir("fused_mount");
    std::filesystem::create_directories(dir / "root" / "scripts");
    write_text(dir / "root" / "game.game.json", R"json({"schema":"slayer3d.game.v0"})json");
    write_text(dir / "root" / "scripts" / "main.lua", "return 42\n");

    const std::filesystem::path pack_path = dir / "game.slayer3dpak";
    const std::string game_json_path = (dir / "root" / "game.game.json").string();
    const std::string script_path = (dir / "root" / "scripts" / "main.lua").string();
    slayer3d_asset_pack_source sources[2] = {
        {"game.game.json", game_json_path.c_str()},
        {"scripts/main.lua", script_path.c_str()},
    };
    char error[256]{};
    ASSERT_TRUE(slayer3d_asset_pack_write_file(pack_path.string().c_str(), sources, 2, error, sizeof(error))) << error;

    const std::filesystem::path executable = dir / "Game";
    write_text(executable, "runner");
    const std::string pack_bytes = [&pack_path]() {
        std::ifstream in(pack_path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }();
    {
        std::ofstream out(executable, std::ios::binary | std::ios::app);
        out.write(pack_bytes.data(), (std::streamsize)pack_bytes.size());
    }
    ASSERT_TRUE(slayer3d_fused_append_footer(executable.string().c_str(), 6, pack_bytes.size(),
                                             "asset://game.game.json", error, sizeof(error)))
        << error;

    slayer3d_fused_pack fused{};
    ASSERT_TRUE(slayer3d_fused_read_footer(executable.string().c_str(), &fused, error, sizeof(error))) << error;
    slayer3d_asset_resolver *resolver = slayer3d_asset_resolver_create();
    ASSERT_NE(resolver, nullptr);
    ASSERT_TRUE(slayer3d_fused_mount_pack(resolver, executable.string().c_str(), &fused, error, sizeof(error)))
        << error;

    slayer3d_asset_buffer buffer{};
    ASSERT_TRUE(slayer3d_asset_resolver_read_file(resolver, "asset://scripts/main.lua", &buffer, error, sizeof(error)))
        << error;
    ASSERT_EQ(buffer.size, 10u);
    EXPECT_EQ(std::memcmp(buffer.data, "return 42\n", 10), 0);
    slayer3d_asset_buffer_free(&buffer);
    slayer3d_asset_resolver_destroy(resolver);
    std::filesystem::remove_all(dir);
}

TEST(ToolCli, RunnerStateInputsMergeWithExpectedPrecedenceAndTypes)
{
    const std::filesystem::path path = unique_cli_test_path("state");
    write_text(path, R"json({"lives":1,"checkpoint":"from_file","debug":false})json");

    slayer3d_properties *props = slayer3d_properties_create();
    ASSERT_NE(props, nullptr);
    char error[256]{};
    ASSERT_TRUE(slayer3d_runner_apply_state_json_file(props, path.string().c_str(), error, sizeof(error))) << error;
    const char state_json[] = R"json({"lives":2,"speed":1.5,"spawn":[1,2,3]})json";
    ASSERT_TRUE(slayer3d_runner_apply_state_json_object(props, state_json, std::strlen(state_json), "--state-json",
                                                        error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_runner_apply_state_assignment(props, "lives=3", error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_runner_apply_state_assignment(props, "checkpoint=midboss", error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_runner_apply_state_assignment(props, "debug=true", error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_runner_apply_state_assignment(props, "label=not_json", error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_runner_apply_state_assignment(props, "quoted=\"hello\"", error, sizeof(error))) << error;

    EXPECT_EQ(slayer3d_properties_get_int(props, "lives", 0), 3);
    EXPECT_STREQ(slayer3d_properties_get_string(props, "checkpoint", ""), "midboss");
    EXPECT_TRUE(slayer3d_properties_get_bool(props, "debug", false));
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(props, "speed", 0.0f), 1.5f);
    const slayer3d_vec3 spawn = slayer3d_properties_get_vec3(props, "spawn", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(spawn.x, 1.0f);
    EXPECT_FLOAT_EQ(spawn.y, 2.0f);
    EXPECT_FLOAT_EQ(spawn.z, 3.0f);
    EXPECT_STREQ(slayer3d_properties_get_string(props, "label", ""), "not_json");
    EXPECT_STREQ(slayer3d_properties_get_string(props, "quoted", ""), "hello");

    slayer3d_properties_destroy(props);
    std::filesystem::remove(path);
}

TEST(ToolCli, RunnerTestRunManifestAppliesCanonicalDirectStartArgs)
{
    const char manifest[] = R"json({
  "schema": "slayer3d.editor_test_run.v0",
  "data": "asset://game.game.json",
  "scene": "scene.level1",
  "player_start": "player_start.level1",
  "target": "entity.player",
  "args": ["--data", "asset://game.game.json", "--scene", "scene.level1", "--player-start", "player_start.level1"]
})json";

    slayer3d_runner_args args{};
    char error[256]{};
    ASSERT_TRUE(slayer3d_runner_apply_test_run_manifest_json(&args, manifest, std::strlen(manifest), "manifest", error,
                                                             sizeof(error)))
        << error;
    EXPECT_STREQ(args.data_asset_path, "asset://game.game.json");
    EXPECT_STREQ(args.scene, "scene.level1");
    EXPECT_STREQ(args.player_start, "player_start.level1");
    EXPECT_NE(args.owned_data_asset_path, nullptr);
    EXPECT_NE(args.owned_scene, nullptr);
    EXPECT_NE(args.owned_player_start, nullptr);
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerTestRunManifestRejectsConflictingExplicitArgs)
{
    const char manifest[] = R"json({
  "schema": "slayer3d.editor_test_run.v0",
  "data": "asset://game.game.json",
  "scene": "scene.level1"
})json";

    slayer3d_runner_args args{};
    args.data_asset_path = "asset://other.game.json";
    args.data_asset_path_explicit = true;
    char error[256]{};
    EXPECT_FALSE(slayer3d_runner_apply_test_run_manifest_json(&args, manifest, std::strlen(manifest), "manifest", error,
                                                              sizeof(error)));
    EXPECT_NE(std::string(error).find("conflicts"), std::string::npos) << error;
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerTestRunManifestRejectsInvalidSchema)
{
    const char manifest[] =
        R"json({"schema":"slayer3d.game.v0","data":"asset://game.game.json","scene":"scene.level1"})json";

    slayer3d_runner_args args{};
    char error[256]{};
    EXPECT_FALSE(slayer3d_runner_apply_test_run_manifest_json(&args, manifest, std::strlen(manifest), "manifest", error,
                                                              sizeof(error)));
    EXPECT_NE(std::string(error).find("schema"), std::string::npos) << error;
    slayer3d_runner_args_destroy(&args);
}

TEST(ToolCli, RunnerStateInputsRejectMalformedValues)
{
    slayer3d_properties *props = slayer3d_properties_create();
    ASSERT_NE(props, nullptr);
    char error[256]{};
    EXPECT_FALSE(slayer3d_runner_apply_state_assignment(props, "missing_equals", error, sizeof(error)));
    const char array_json[] = "[1,2,3]";
    const char nested_json[] = R"json({"nested":{"bad":true}})json";
    EXPECT_FALSE(slayer3d_runner_apply_state_json_object(props, array_json, std::strlen(array_json), "--state-json",
                                                         error, sizeof(error)));
    EXPECT_FALSE(slayer3d_runner_apply_state_json_object(props, nested_json, std::strlen(nested_json), "--state-json",
                                                         error, sizeof(error)));
    slayer3d_properties_destroy(props);
}
