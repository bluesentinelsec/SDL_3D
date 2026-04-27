#include <gtest/gtest.h>

#include <string>
#include <vector>

extern "C"
{
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
}

namespace
{

struct AdapterCapture
{
    int calls = 0;
};

struct CapturedDiagnostic
{
    sdl3d_game_data_diagnostic_severity severity = SDL3D_GAME_DATA_DIAGNOSTIC_WARNING;
    std::string path;
    std::string message;
};

struct DiagnosticCapture
{
    std::vector<CapturedDiagnostic> diagnostics;
};

bool serve_adapter(void *userdata, sdl3d_game_data_runtime *runtime, const char *adapter_name,
                   sdl3d_registered_actor *target, const sdl3d_properties *payload)
{
    auto *capture = static_cast<AdapterCapture *>(userdata);
    EXPECT_STREQ(adapter_name, "adapter.pong.serve_random");
    EXPECT_NE(runtime, nullptr);
    EXPECT_NE(target, nullptr);
    EXPECT_EQ(payload, nullptr);
    sdl3d_properties_set_vec3(target->props, "velocity", sdl3d_vec3_make(3.0f, 1.0f, 0.0f));
    capture->calls++;
    return true;
}

void capture_diagnostic(void *userdata, sdl3d_game_data_diagnostic_severity severity, const char *json_path,
                        const char *message)
{
    auto *capture = static_cast<DiagnosticCapture *>(userdata);
    capture->diagnostics.push_back(
        {severity, json_path != nullptr ? json_path : "", message != nullptr ? message : ""});
}

std::string fixture_path(const char *filename)
{
    return std::string(SDL3D_GAME_DATA_FIXTURE_DIR) + "/" + filename;
}

} // namespace

TEST(GameDataRuntime, LoadsPongDataIntoGenericSessionServices)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    EXPECT_NE(sdl3d_game_data_find_actor(runtime, "entity.ball"), nullptr);
    EXPECT_GE(sdl3d_game_data_find_signal(runtime, "signal.ball.serve"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.paddle.up"), 0);
    EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.overhead");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, SignalBindingsResolveLuaAdaptersDeclaredInJson)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    const int serve_signal = sdl3d_game_data_find_signal(runtime, "signal.ball.serve");
    ASSERT_GE(serve_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), serve_signal, nullptr);

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(ball, nullptr);
    const sdl3d_vec3 velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(ball->position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(ball->position.y, 0.0f, 0.0001f);
    EXPECT_GT(SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y), 5.0f);
    EXPECT_TRUE(sdl3d_properties_get_bool(ball->props, "active_motion", false));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LuaAdapterReflectsBallFromPaddle)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    sdl3d_registered_actor *paddle = sdl3d_game_data_find_actor(runtime, "entity.paddle.player");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(paddle, nullptr);
    ball->position = sdl3d_vec3_make(paddle->position.x + 0.10f, paddle->position.y + 0.40f, 0.12f);
    sdl3d_properties_set_vec3(ball->props, "origin", ball->position);
    sdl3d_properties_set_vec3(ball->props, "velocity", sdl3d_vec3_make(-5.6f, 0.0f, 0.0f));

    sdl3d_properties *payload = sdl3d_properties_create();
    ASSERT_NE(payload, nullptr);
    sdl3d_properties_set_string(payload, "other_actor_name", "entity.paddle.player");
    const int hit_signal = sdl3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    ASSERT_GE(hit_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), hit_signal, payload);
    sdl3d_properties_destroy(payload);

    const sdl3d_vec3 velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(velocity.x, 0.0f);
    EXPECT_GT(SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y), 5.6f);
    EXPECT_GT(ball->position.x, paddle->position.x);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LuaControllerMovesCpuPaddleTowardBall)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    sdl3d_registered_actor *cpu = sdl3d_game_data_find_actor(runtime, "entity.paddle.cpu");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(cpu, nullptr);
    ball->position.y = 2.0f;
    cpu->position.y = 0.0f;
    sdl3d_properties_set_vec3(ball->props, "origin", ball->position);
    sdl3d_properties_set_vec3(cpu->props, "origin", cpu->position);

    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.1f));

    EXPECT_GT(cpu->position.y, 0.0f);
    EXPECT_LE(cpu->position.y, 0.55f);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RegisteredCAdaptersOverrideLuaAdapters)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    AdapterCapture capture{};
    ASSERT_TRUE(sdl3d_game_data_register_adapter(runtime, "adapter.pong.serve_random", serve_adapter, &capture));

    const int serve_signal = sdl3d_game_data_find_signal(runtime, "signal.ball.serve");
    ASSERT_GE(serve_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), serve_signal, nullptr);

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(ball, nullptr);
    const sdl3d_vec3 velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(capture.calls, 1);
    EXPECT_FLOAT_EQ(velocity.x, 3.0f);
    EXPECT_TRUE(sdl3d_properties_get_bool(ball->props, "active_motion", false));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LoadsLuaScriptDependenciesBeforeDependentAdapters)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    const std::string path = fixture_path("module_success.game.json");
    ASSERT_TRUE(sdl3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << error;

    const int run_signal = sdl3d_game_data_find_signal(runtime, "signal.run");
    ASSERT_GE(run_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), run_signal, nullptr);

    sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    const sdl3d_vec3 velocity = sdl3d_properties_get_vec3(target->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    const float speed_length = sdl3d_properties_get_float(target->props, "speed_length", 0.0f);
    const float random_value = sdl3d_properties_get_float(target->props, "random_value", -1.0f);
    EXPECT_FLOAT_EQ(target->position.x, 1.0f);
    EXPECT_FLOAT_EQ(target->position.y, 2.0f);
    EXPECT_FLOAT_EQ(target->position.z, 3.0f);
    EXPECT_FLOAT_EQ(velocity.x, 7.0f);
    EXPECT_FLOAT_EQ(velocity.y, 2.0f);
    EXPECT_NEAR(speed_length, SDL_sqrtf(53.0f), 0.0001f);
    EXPECT_TRUE(sdl3d_properties_get_bool(target->props, "ctx_ok", false));
    EXPECT_GE(random_value, 0.0f);
    EXPECT_LT(random_value, 1.0f);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);

    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << error;
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), sdl3d_game_data_find_signal(runtime, "signal.run"),
                      nullptr);
    target = sdl3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(target->props, "random_value", -1.0f), random_value);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ValidatesPongDataWithoutDiagnostics)
{
    DiagnosticCapture capture;
    sdl3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    EXPECT_TRUE(sdl3d_game_data_validate_file(SDL3D_PONG_DATA_PATH, &options, error, sizeof(error))) << error;
    EXPECT_TRUE(capture.diagnostics.empty());
    EXPECT_EQ(error[0], '\0');
}

TEST(GameDataRuntime, ValidationReportsJsonPathAndMissingReference)
{
    DiagnosticCapture capture;
    sdl3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    const std::string path = fixture_path("bad_reference.game.json");
    EXPECT_FALSE(sdl3d_game_data_validate_file(path.c_str(), &options, error, sizeof(error)));
    ASSERT_FALSE(capture.diagnostics.empty());
    EXPECT_EQ(capture.diagnostics[0].severity, SDL3D_GAME_DATA_DIAGNOSTIC_ERROR);
    EXPECT_NE(capture.diagnostics[0].path.find("$.logic.bindings[0].actions[0]"), std::string::npos);
    EXPECT_NE(capture.diagnostics[0].message.find("entity.missing"), std::string::npos);
    EXPECT_NE(std::string(error).find("$.logic.bindings[0].actions[0]"), std::string::npos);
}

TEST(GameDataRuntime, ValidationReportsWarningsWithoutFailingByDefault)
{
    DiagnosticCapture capture;
    sdl3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    const std::string path = fixture_path("warning_unsupported_component.game.json");
    EXPECT_TRUE(sdl3d_game_data_validate_file(path.c_str(), &options, error, sizeof(error))) << error;
    ASSERT_EQ(capture.diagnostics.size(), 1u);
    EXPECT_EQ(capture.diagnostics[0].severity, SDL3D_GAME_DATA_DIAGNOSTIC_WARNING);
    EXPECT_NE(capture.diagnostics[0].message.find("unsupported component type"), std::string::npos);
    EXPECT_EQ(error[0], '\0');

    options.treat_warnings_as_errors = true;
    EXPECT_FALSE(sdl3d_game_data_validate_file(path.c_str(), &options, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("unsupported component type"), std::string::npos);
}

TEST(GameDataRuntime, RejectsLuaScriptManifestErrorsBeforeGameplay)
{
    const char *bad_files[] = {
        "missing_dependency.game.json", "missing_file.game.json", "dependency_cycle.game.json",
        "missing_function.game.json",   "no_table.game.json",
    };

    for (const char *file : bad_files)
    {
        sdl3d_game_session *session = nullptr;
        ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

        char error[512]{};
        sdl3d_game_data_runtime *runtime = nullptr;
        const std::string path = fixture_path(file);
        EXPECT_FALSE(sdl3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << file;
        EXPECT_NE(error[0], '\0') << file;
        EXPECT_EQ(runtime, nullptr);

        sdl3d_game_data_destroy(runtime);
        sdl3d_game_session_destroy(session);
    }
}

TEST(GameDataRuntime, AuthoredGoalSensorDrivesScoreBinding)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    sdl3d_registered_actor *cpu_score = sdl3d_game_data_find_actor(runtime, "entity.score.cpu");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(cpu_score, nullptr);

    ball->position.x = -10.0f;
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 1.0f / 60.0f));

    EXPECT_EQ(sdl3d_properties_get_int(cpu_score->props, "value", 0), 1);
    EXPECT_FLOAT_EQ(ball->position.x, 0.0f);
    EXPECT_FALSE(sdl3d_properties_get_bool(ball->props, "active_motion", true));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}
