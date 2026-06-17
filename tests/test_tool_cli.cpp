#include <gtest/gtest.h>

#include <chrono>
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
    EXPECT_TRUE(loaded_project.asset_sources.textures.available);
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
    "effects": "content/effects"
  }
})json");

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(project_dir.string().c_str(), &loaded_project, error, sizeof(error)))
        << error;
    EXPECT_STREQ(loaded_project.asset_sources.textures.relative_path, "content/textures");
    EXPECT_STREQ(loaded_project.asset_sources.models.relative_path, "content/models");
    EXPECT_STREQ(loaded_project.asset_sources.textures.path, (project_dir / "content" / "textures").string().c_str());
    EXPECT_TRUE(loaded_project.asset_sources.textures.available);
    EXPECT_FALSE(loaded_project.asset_sources.models.available);

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

TEST(ToolCli, EditorTexturePathOverrideBecomesAuthoritativeTextureSource)
{
    const std::filesystem::path project_dir = unique_cli_test_dir("editor_texture_override");
    const std::filesystem::path override_dir = project_dir / "external_textures";
    std::filesystem::create_directories(project_dir / "data");
    std::filesystem::create_directories(project_dir / "media" / "textures");
    std::filesystem::create_directories(project_dir / "media" / "models");
    std::filesystem::create_directories(project_dir / "media" / "sprites");
    std::filesystem::create_directories(project_dir / "media" / "skyboxes");
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

TEST(ToolCli, EditorDefaultLaunchResolvesRelativeTexturePathOverride)
{
    const std::filesystem::path repo_root =
        std::filesystem::path(SLAYER3D_EDITOR_DEFAULT_PROJECT).parent_path().parent_path();
    ScopedCurrentPath cwd(repo_root);
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
    EXPECT_EQ(std::filesystem::path(args.project), std::filesystem::path(SLAYER3D_EDITOR_DEFAULT_PROJECT));
    EXPECT_NE(std::string(args.output_path).find(".slayermap.json"), std::string::npos);

    char error[512]{};
    slayer3d_editor_project loaded_project;
    ASSERT_TRUE(slayer3d_editor_project_load(args.project, &loaded_project, error, sizeof(error))) << error;
    slayer3d_editor_launch launch;
    ASSERT_TRUE(slayer3d_editor_prepare_launch(&args, &loaded_project, &launch, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_editor_validate_paths(&args, &launch, error, sizeof(error))) << error;
    EXPECT_STREQ(launch.input_path, "");
    EXPECT_STREQ(launch.save_path, args.output_path);

    slayer3d_editor_runner_invocation invocation;
    ASSERT_TRUE(slayer3d_editor_build_runner_invocation(&launch, "slayer3d_editor", &invocation));
    std::string joined;
    for (int i = 0; i < invocation.argc; ++i)
    {
        if (!joined.empty())
            joined += "\n";
        joined += invocation.argv[i];
    }
    EXPECT_NE(joined.find("editor.command=new"), std::string::npos);
    EXPECT_NE(joined.find(std::string("editor.save.path=") + args.output_path), std::string::npos);
    EXPECT_NE(joined.find("editor.project.dir=" + std::string(launch.project_dir)), std::string::npos);

    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&loaded_project);
    slayer3d_editor_args_destroy(&args);
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
