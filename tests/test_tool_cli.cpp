#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C"
{
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d_pack_cli.h"
#include "sdl3d_runner_cli.h"
#include "sdl3d_runner_state.h"
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
           ("sdl3d_tool_cli_" + std::string(name) + "_" + std::to_string(stamp) + ".json");
}

void write_text(const std::filesystem::path &path, const char *text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}
} // namespace

TEST(ToolCli, RunnerParsesDirectoryMount)
{
    std::vector<char *> argv =
        argv_from({"sdl3d_runner", "--root", "game/data", "--data", "asset://game.game.json", "--media", "media"});
    sdl3d_runner_args args;
    ASSERT_EQ(sdl3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_OK);
    EXPECT_EQ(args.mount_kind, SDL3D_RUNNER_MOUNT_DIRECTORY);
    EXPECT_STREQ(args.mount_path, "game/data");
    EXPECT_STREQ(args.data_asset_path, "asset://game.game.json");
    EXPECT_STREQ(args.media_dir, "media");
}

TEST(ToolCli, RunnerRejectsMultipleMounts)
{
    std::vector<char *> argv =
        argv_from({"sdl3d_runner", "--root", "game/data", "--pack", "game.sdl3dpak", "--data", "asset://game.json"});
    sdl3d_runner_args args;
    EXPECT_EQ(sdl3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_ERROR);
}

TEST(ToolCli, RunnerRejectsEmptyMountPath)
{
    std::vector<char *> argv = argv_from({"sdl3d_runner", "--root", "", "--data", "asset://game.json"});
    sdl3d_runner_args args;
    EXPECT_EQ(sdl3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_ERROR);
}

TEST(ToolCli, RunnerHelpDoesNotRequireMountOrData)
{
    std::vector<char *> argv = argv_from({"sdl3d_runner", "--help"});
    sdl3d_runner_args args;
    EXPECT_EQ(sdl3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_HELP);
}

TEST(ToolCli, RunnerParsesDirectStartStateInputs)
{
    std::vector<char *> argv = argv_from({"sdl3d_runner", "--root", "game/data", "--data", "asset://game.game.json",
                                          "--scene", "scene.level1", "--state-file", "dev/level1.json", "--state-json",
                                          "{\"lives\":3}", "--state", "checkpoint=midboss", "--state", "debug=true"});
    sdl3d_runner_args args;
    ASSERT_EQ(sdl3d_runner_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.scene, "scene.level1");
    ASSERT_EQ(args.state_file_count, 1);
    EXPECT_STREQ(args.state_files[0], "dev/level1.json");
    ASSERT_EQ(args.state_json_count, 1);
    EXPECT_STREQ(args.state_json_values[0], "{\"lives\":3}");
    ASSERT_EQ(args.state_assignment_count, 2);
    EXPECT_STREQ(args.state_assignments[0], "checkpoint=midboss");
    EXPECT_STREQ(args.state_assignments[1], "debug=true");
    sdl3d_runner_args_destroy(&args);
}

TEST(ToolCli, PackParsesRepeatedFiles)
{
    std::vector<char *> argv = argv_from({"sdl3d_pack", "--output", "game.sdl3dpak", "--root", "game/data", "--file",
                                          "game.game.json", "--file", "scenes/play.scene.json"});
    sdl3d_pack_args args;
    ASSERT_EQ(sdl3d_pack_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_OK);
    EXPECT_STREQ(args.output, "game.sdl3dpak");
    EXPECT_STREQ(args.root, "game/data");
    ASSERT_EQ(args.file_count, 2);
    EXPECT_STREQ(args.files[0], "game.game.json");
    EXPECT_STREQ(args.files[1], "scenes/play.scene.json");
    sdl3d_pack_args_destroy(&args);
}

TEST(ToolCli, PackRejectsMissingFile)
{
    std::vector<char *> argv = argv_from({"sdl3d_pack", "--output", "game.sdl3dpak", "--root", "game/data"});
    sdl3d_pack_args args;
    EXPECT_EQ(sdl3d_pack_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_ERROR);
    sdl3d_pack_args_destroy(&args);
}

TEST(ToolCli, PackRejectsEmptyFile)
{
    std::vector<char *> argv =
        argv_from({"sdl3d_pack", "--output", "game.sdl3dpak", "--root", "game/data", "--file", ""});
    sdl3d_pack_args args;
    EXPECT_EQ(sdl3d_pack_args_parse((int)argv.size(), argv.data(), &args, nullptr), SDL3D_TOOL_CLI_ERROR);
    sdl3d_pack_args_destroy(&args);
}

TEST(ToolCli, RunnerStateInputsMergeWithExpectedPrecedenceAndTypes)
{
    const std::filesystem::path path = unique_cli_test_path("state");
    write_text(path, R"json({"lives":1,"checkpoint":"from_file","debug":false})json");

    sdl3d_properties *props = sdl3d_properties_create();
    ASSERT_NE(props, nullptr);
    char error[256]{};
    ASSERT_TRUE(sdl3d_runner_apply_state_json_file(props, path.string().c_str(), error, sizeof(error))) << error;
    const char state_json[] = R"json({"lives":2,"speed":1.5,"spawn":[1,2,3]})json";
    ASSERT_TRUE(sdl3d_runner_apply_state_json_object(props, state_json, std::strlen(state_json), "--state-json", error,
                                                     sizeof(error)))
        << error;
    ASSERT_TRUE(sdl3d_runner_apply_state_assignment(props, "lives=3", error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_runner_apply_state_assignment(props, "checkpoint=midboss", error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_runner_apply_state_assignment(props, "debug=true", error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_runner_apply_state_assignment(props, "label=not_json", error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_runner_apply_state_assignment(props, "quoted=\"hello\"", error, sizeof(error))) << error;

    EXPECT_EQ(sdl3d_properties_get_int(props, "lives", 0), 3);
    EXPECT_STREQ(sdl3d_properties_get_string(props, "checkpoint", ""), "midboss");
    EXPECT_TRUE(sdl3d_properties_get_bool(props, "debug", false));
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(props, "speed", 0.0f), 1.5f);
    const sdl3d_vec3 spawn = sdl3d_properties_get_vec3(props, "spawn", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(spawn.x, 1.0f);
    EXPECT_FLOAT_EQ(spawn.y, 2.0f);
    EXPECT_FLOAT_EQ(spawn.z, 3.0f);
    EXPECT_STREQ(sdl3d_properties_get_string(props, "label", ""), "not_json");
    EXPECT_STREQ(sdl3d_properties_get_string(props, "quoted", ""), "hello");

    sdl3d_properties_destroy(props);
    std::filesystem::remove(path);
}

TEST(ToolCli, RunnerStateInputsRejectMalformedValues)
{
    sdl3d_properties *props = sdl3d_properties_create();
    ASSERT_NE(props, nullptr);
    char error[256]{};
    EXPECT_FALSE(sdl3d_runner_apply_state_assignment(props, "missing_equals", error, sizeof(error)));
    const char array_json[] = "[1,2,3]";
    const char nested_json[] = R"json({"nested":{"bad":true}})json";
    EXPECT_FALSE(sdl3d_runner_apply_state_json_object(props, array_json, std::strlen(array_json), "--state-json", error,
                                                      sizeof(error)));
    EXPECT_FALSE(sdl3d_runner_apply_state_json_object(props, nested_json, std::strlen(nested_json), "--state-json",
                                                      error, sizeof(error)));
    sdl3d_properties_destroy(props);
}
