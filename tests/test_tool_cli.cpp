#include <gtest/gtest.h>

#include <string>
#include <vector>

extern "C"
{
#include "sdl3d_pack_cli.h"
#include "sdl3d_runner_cli.h"
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
