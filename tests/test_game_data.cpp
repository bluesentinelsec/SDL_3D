#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

extern "C"
{
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/asset.h"
#include "sdl3d/data_game.h"
#include "sdl3d/game.h"
#include "sdl3d/game_data.h"
#include "sdl3d/game_presentation.h"
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
#include "sdl3d/timer_pool.h"
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

struct CapturedLogMessage
{
    int category = -1;
    SDL_LogPriority priority = SDL_LOG_PRIORITY_INVALID;
    std::string message;
};

void SDLCALL capture_log_output(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    auto *capture = static_cast<CapturedLogMessage *>(userdata);
    capture->category = category;
    capture->priority = priority;
    capture->message = message != nullptr ? message : "";
}

class SDLLogOutputGuard
{
  public:
    SDLLogOutputGuard()
    {
        SDL_GetLogOutputFunction(&callback_, &userdata_);
        priority_ = SDL_GetLogPriority(SDL_LOG_CATEGORY_APPLICATION);
    }

    ~SDLLogOutputGuard()
    {
        SDL_SetLogOutputFunction(callback_, userdata_);
        SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, priority_);
    }

  private:
    SDL_LogOutputFunction callback_ = nullptr;
    void *userdata_ = nullptr;
    SDL_LogPriority priority_ = SDL_LOG_PRIORITY_INFO;
};

struct NetworkSignalCapture
{
    int calls = 0;
    int signal_id = -1;
    std::string network_control;
    std::string network_direction;
    int network_tick = 0;
};

struct RenderPrimitiveCapture
{
    int cubes = 0;
    int spheres = 0;
    bool saw_player_paddle = false;
    bool saw_ball = false;
    float ball_rotation_angle = 0.0f;
    bool saw_options_background = false;
    bool saw_options_glow = false;
};

void capture_signal_payload(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    auto *capture = static_cast<NetworkSignalCapture *>(userdata);
    if (capture == nullptr)
    {
        return;
    }

    capture->calls++;
    capture->signal_id = signal_id;
    capture->network_control = sdl3d_properties_get_string(payload, "network_control", "");
    capture->network_direction = sdl3d_properties_get_string(payload, "network_direction", "");
    capture->network_tick = sdl3d_properties_get_int(payload, "network_tick", 0);
}

struct UiTextCapture
{
    int count = 0;
    bool saw_score = false;
    bool saw_pause = false;
    bool saw_network_match_terminated = false;
};

struct UiImageCapture
{
    int count = 0;
    bool saw_splash_logo = false;
};

struct ParticleCapture
{
    int count = 0;
    bool saw_ambient = false;
    bool saw_options_flow = false;
};

struct EvaluatedPrimitiveCapture
{
    bool saw_border = false;
    bool saw_ball = false;
    bool saw_options_drift = false;
};

struct ScenePayloadCapture
{
    bool called = false;
    std::string from_scene;
    std::string to_scene;
    std::string selected_level;
};

struct SignalCapture
{
    int calls = 0;
};

struct SensorSignalCapture
{
    int calls = 0;
    std::string actor_name;
    std::string other_actor_name;
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

bool configure_play_input_adapter(void *, sdl3d_game_data_runtime *runtime, const char *adapter_name,
                                  sdl3d_registered_actor *, const sdl3d_properties *payload)
{
    EXPECT_STREQ(adapter_name, "adapter.pong.configure_play_input");
    EXPECT_NE(runtime, nullptr);
    if (payload != nullptr)
    {
        const char *match_mode = sdl3d_properties_get_string(payload, "match_mode", nullptr);
        const char *network_role = sdl3d_properties_get_string(payload, "network_role", nullptr);
        const char *network_flow = sdl3d_properties_get_string(payload, "network_flow", nullptr);
        if (match_mode != nullptr)
        {
            sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(runtime), "match_mode", match_mode);
        }
        if (network_role != nullptr)
        {
            sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(runtime), "network_role", network_role);
        }
        if (network_flow != nullptr)
        {
            sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(runtime), "network_flow", network_flow);
        }
    }
    return true;
}

bool reload_native_adapter(void *userdata, sdl3d_game_data_runtime *runtime, const char *adapter_name,
                           sdl3d_registered_actor *target, const sdl3d_properties *payload)
{
    auto *capture = static_cast<AdapterCapture *>(userdata);
    EXPECT_NE(runtime, nullptr);
    EXPECT_STREQ(adapter_name, "adapter.reload.run");
    EXPECT_NE(target, nullptr);
    EXPECT_EQ(payload, nullptr);
    sdl3d_properties_set_int(target->props, "value", 99);
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

void capture_scene_payload(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    auto *capture = static_cast<ScenePayloadCapture *>(userdata);
    (void)signal_id;
    capture->called = true;
    capture->from_scene = sdl3d_properties_get_string(payload, "from_scene", "");
    capture->to_scene = sdl3d_properties_get_string(payload, "to_scene", "");
    capture->selected_level = sdl3d_properties_get_string(payload, "selected_level", "");
}

void count_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    auto *capture = static_cast<SignalCapture *>(userdata);
    (void)signal_id;
    (void)payload;
    capture->calls++;
}

void capture_sensor_signal(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    auto *capture = static_cast<SensorSignalCapture *>(userdata);
    (void)signal_id;
    if (capture == nullptr)
    {
        return;
    }
    capture->calls++;
    capture->actor_name = sdl3d_properties_get_string(payload, "actor_name", "");
    capture->other_actor_name = sdl3d_properties_get_string(payload, "other_actor_name", "");
}

std::string fixture_path(const char *filename)
{
    return std::string(SDL3D_GAME_DATA_FIXTURE_DIR) + "/" + filename;
}

std::string read_fixture_file(const char *filename)
{
    std::ifstream in(fixture_path(filename), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string read_text(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path unique_test_dir(const char *name)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path();
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path dir = root / ("sdl3d_game_data_test_" + std::string(name) + "_" +
                                                  std::to_string(now) + "_" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directories(dir, error))
            return dir;
    }
    throw std::runtime_error("failed to create unique game data test directory");
}

void remove_test_dir(const std::filesystem::path &dir)
{
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void write_text(const std::filesystem::path &path, const char *text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string network_schema_game_json(const std::string &network_json, const char *metadata_name = "Network Schema")
{
    return std::string(R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": ")json") +
           metadata_name + R"json(", "id": "test.network_schema", "version": "0.1.0" },
  "world": { "name": "world.network_schema", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.paddle.player" },
    { "name": "entity.ball" },
    { "name": "entity.match", "properties": { "paused": { "type": "bool", "value": false } } }
  ],
  "signals": [
    "signal.network.start",
    "signal.network.pause"
  ],
  "input": {
    "contexts": [
      {
        "name": "gameplay",
        "actions": [
          { "name": "action.remote.up" },
          { "name": "action.remote.down" },
          { "name": "action.pause" }
        ]
      }
    ]
  },
  "network": )json" +
           network_json +
           R"json(
})json";
}

std::string valid_network_schema_json(const char *ball_velocity_type = "vec3")
{
    return std::string(R"json({
    "protocol": {
      "id": "sdl3d.test.network.v1",
      "version": 1,
      "transport": "udp",
      "tick_rate": 60
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.paddle.player",
            "fields": [
              "position",
              { "path": "properties.active", "type": "bool" }
            ]
          },
          {
            "entity": "entity.ball",
            "fields": [
              "position",
              { "path": "properties.velocity", "type": ")json") +
           ball_velocity_type + R"json(" }
            ]
          }
        ]
      },
      {
        "name": "client_input",
        "direction": "client_to_host",
        "rate": 60,
        "inputs": [
          { "action": "action.remote.up" },
          { "action": "action.remote.down" }
        ]
      }
    ],
    "control_messages": [
      { "name": "start_game", "direction": "host_to_client", "signal": "signal.network.start" },
      { "name": "pause", "direction": "bidirectional", "signal": "signal.network.pause" }
    ]
  })json";
}

std::string actor_pool_replication_game_json(int pool_capacity)
{
    return std::string(R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Actor Pool Replication", "id": "test.actor_pool_replication", "version": "0.1.0" },
  "world": { "name": "world.actor_pool_replication", "kind": "fixed_screen" },
  "entities": [],
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "tags": ["projectile"],
      "transform": { "position": [0.0, 0.0, 0.25] },
      "properties": {
        "damage": { "type": "int", "value": 1 },
        "velocity": { "type": "vec2", "value": [0.0, 0.0] }
      }
    }
  ],
  "actor_pools": [
    {
      "name": "pool.shots",
      "archetype": "archetype.shot",
      "capacity": )json") +
           std::to_string(pool_capacity) + R"json(,
      "scene": "scene.play",
      "initial_active": false,
      "on_exhausted": "fail"
    }
  ],
  "signals": [
    "signal.spawn",
    "signal.despawn"
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.spawn",
        "actions": [
          {
            "type": "actor.spawn",
            "pool": "pool.shots",
            "position": [2.0, 3.0, 4.0],
            "properties": { "damage": 7 }
          }
        ]
      },
      {
        "signal": "signal.despawn",
        "actions": [
          { "type": "actor.despawn", "target": "pool.shots.0" }
        ]
      }
    ]
  },
  "network": {
    "protocol": { "id": "sdl3d.test.pool.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "pool_state",
        "direction": "host_to_client",
        "rate": 60,
        "pools": [
          {
            "pool": "pool.shots",
            "fields": [
              "active",
              "position",
              { "path": "properties.damage", "type": "int32" }
            ]
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
}

std::array<Uint8, SDL3D_REPLICATION_SCHEMA_HASH_SIZE> load_network_schema_hash(const std::filesystem::path &path)
{
    sdl3d_game_session *session = nullptr;
    EXPECT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    EXPECT_TRUE(sdl3d_game_data_load_file(path.string().c_str(), session, &runtime, error, sizeof(error))) << error;
    EXPECT_NE(runtime, nullptr);
    EXPECT_TRUE(sdl3d_game_data_has_network_schema(runtime));

    std::array<Uint8, SDL3D_REPLICATION_SCHEMA_HASH_SIZE> hash{};
    EXPECT_TRUE(sdl3d_game_data_get_network_schema_hash(runtime, hash.data()));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    return hash;
}

void load_pong_runtime(sdl3d_game_session **out_session, sdl3d_game_data_runtime **out_runtime)
{
    ASSERT_NE(out_session, nullptr);
    ASSERT_NE(out_runtime, nullptr);
    *out_session = nullptr;
    *out_runtime = nullptr;

    ASSERT_TRUE(sdl3d_game_session_create(nullptr, out_session));
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, *out_session, out_runtime, error, sizeof(error)))
        << error;
    ASSERT_NE(*out_runtime, nullptr);
}

void destroy_runtime_session(sdl3d_game_session *session, sdl3d_game_data_runtime *runtime)
{
    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

void expect_vec3_near(sdl3d_vec3 actual, sdl3d_vec3 expected, float tolerance = 0.0001f)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

std::filesystem::path copy_pong_data_with_storage_overrides(const std::filesystem::path &dir,
                                                            const std::filesystem::path &user_root,
                                                            const std::filesystem::path &cache_root)
{
    const std::filesystem::path source = std::filesystem::path(SDL3D_PONG_DATA_PATH).parent_path();
    const std::filesystem::path dest = dir / "pong_data";
    std::filesystem::copy(source, dest,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);

    const std::filesystem::path game_path = dest / "pong.game.json";
    std::string game_json = read_text(game_path);
    const std::string marker = R"json("profile": "default")json";
    const std::string replacement = std::string(R"json("profile": "default",
    "user_root_override": ")json") + user_root.generic_string() +
                                    R"json(",
    "cache_root_override": ")json" + cache_root.generic_string() +
                                    R"json(")json";
    const size_t marker_pos = game_json.find(marker);
    if (marker_pos == std::string::npos)
        throw std::runtime_error("Pong storage profile marker not found");
    game_json.replace(marker_pos, marker.size(), replacement);
    write_text(game_path, game_json.c_str());
    return game_path;
}

void write_hot_reload_json(const std::filesystem::path &dir)
{
    write_text(dir / "reload.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Reload", "id": "test.reload", "version": "0.1.0" },
  "scripts": [
    { "id": "script.rules", "path": "scripts/rules.lua", "module": "reload.rules" }
  ],
  "world": { "name": "world.reload", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.target", "active": true }
  ],
  "signals": ["signal.run"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.run",
        "actions": [
          { "type": "adapter.invoke", "adapter": "adapter.reload.run", "target": "entity.target" }
        ]
      }
    ]
  },
  "adapters": [
    {
      "name": "adapter.reload.run",
      "kind": "action",
      "script": "script.rules",
      "function": "run"
    }
  ]
})json");
}

void write_timeline_json(const std::filesystem::path &dir)
{
    write_text(dir / "timeline.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Timeline", "id": "test.timeline", "version": "0.1.0" },
  "transitions": {
    "scene_out": { "type": "fade", "direction": "out", "color": [0, 0, 0, 255], "duration": 0.10 },
    "scene_in": { "type": "fade", "direction": "in", "color": [0, 0, 0, 255], "duration": 0.10 }
  },
  "entities": [
    {
      "name": "entity.flag",
      "active": true,
      "properties": {
        "ready": { "type": "bool", "value": false }
      }
    }
  ],
  "signals": ["signal.timeline"],
  "scenes": {
    "initial": "scene.intro",
    "files": [
      "scenes/intro.scene.json",
      "scenes/title.scene.json"
    ]
  }
})json");
    write_text(dir / "scenes" / "intro.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.intro",
  "updates_game": false,
  "renders_world": false,
  "entities": [],
  "transitions": { "exit": "scene_out" },
  "timeline": {
    "autoplay": true,
    "events": [
      {
        "time": 0.10,
        "action": { "type": "property.set", "target": "entity.flag", "key": "ready", "value": true }
      },
      {
        "time": 0.20,
        "action": { "type": "signal.emit", "signal": "signal.timeline" }
      },
      {
        "time": 0.30,
        "action": { "type": "scene.request", "scene": "scene.title" }
      }
    ]
  }
})json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false,
  "entities": [],
  "transitions": { "enter": "scene_in" }
})json");
}

void write_skip_policy_json(const std::filesystem::path &dir)
{
    write_text(dir / "skip.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Skip", "id": "test.skip", "version": "0.1.0" },
  "transitions": {
    "scene_out": { "type": "fade", "direction": "out", "color": [0, 0, 0, 255], "duration": 0.10 },
    "scene_in": { "type": "fade", "direction": "in", "color": [0, 0, 0, 255], "duration": 0.10 }
  },
  "input": {
    "contexts": [
      {
        "name": "input.main",
        "actions": [
          {
            "name": "action.skip",
            "bindings": [
              { "device": "keyboard", "key": "RETURN" },
              { "device": "gamepad", "button": "SOUTH" }
            ]
          },
          {
            "name": "action.menu.select",
            "bindings": [
              { "device": "keyboard", "key": "RETURN" },
              { "device": "gamepad", "button": "SOUTH" }
            ]
          },
          {
            "name": "action.menu.up",
            "bindings": [
              { "device": "keyboard", "key": "UP" }
            ]
          },
          {
            "name": "action.menu.down",
            "bindings": [
              { "device": "keyboard", "key": "DOWN" }
            ]
          }
        ]
      }
    ]
  },
  "scenes": {
    "initial": "scene.intro",
    "files": [
      "scenes/intro.scene.json",
      "scenes/title.scene.json",
      "scenes/play.scene.json"
    ]
  }
})json");
    write_text(dir / "scenes" / "intro.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.intro",
  "updates_game": false,
  "renders_world": false,
  "entities": [],
  "input": { "actions": ["action.skip"] },
  "transitions": { "exit": "scene_out" },
  "timeline": {
    "autoplay": true,
    "skip_policy": {
      "enabled": true,
      "input": "action",
      "action": "action.skip",
      "scene": "scene.title",
      "preserve_exit_transition": true,
      "consume_input": true
    },
    "events": []
  }
})json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false,
  "entities": [],
  "input": { "actions": ["action.menu.up", "action.menu.down", "action.menu.select"] },
  "transitions": { "enter": "scene_in", "exit": "scene_out" },
  "menus": [
    {
      "name": "menu.title",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "items": [
        { "label": "Play", "scene": "scene.play" }
      ]
    }
  ]
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true,
  "entities": [],
  "transitions": { "enter": "scene_in" }
})json");
}

void write_scene_flow_policy_json(const std::filesystem::path &dir, bool block_menus, bool block_scene_shortcuts)
{
    const char *block_menus_text = block_menus ? "true" : "false";
    const char *block_shortcuts_text = block_scene_shortcuts ? "true" : "false";
    const std::string game_json = R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Scene Flow Policy", "id": "test.scene_flow_policy", "version": "0.1.0" },
  "app": {
    "scene_shortcuts": [
      { "action": "action.scene.play", "scene": "scene.play" }
    ],
    "input_policy": {
      "global_actions": ["action.scene.play"]
    }
  },
  "input": {
    "contexts": [
      {
        "name": "input.main",
        "actions": [
          {
            "name": "action.menu.select",
            "bindings": [
              { "device": "keyboard", "key": "RETURN" }
            ]
          },
          {
            "name": "action.menu.up",
            "bindings": [
              { "device": "keyboard", "key": "UP" }
            ]
          },
          {
            "name": "action.menu.down",
            "bindings": [
              { "device": "keyboard", "key": "DOWN" }
            ]
          },
          {
            "name": "action.scene.play",
            "bindings": [
              { "device": "keyboard", "key": "3" }
            ]
          }
        ]
      }
    ]
  },
  "scenes": {
    "initial": "scene.intro",
    "files": [
      "scenes/intro.scene.json",
      "scenes/title.scene.json",
      "scenes/play.scene.json"
    ]
  }
})json";
    write_text(dir / "flow_policy.game.json", game_json.c_str());

    std::string intro_json = R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.intro",
  "updates_game": false,
  "renders_world": false,
  "entities": [],
  "input": { "actions": ["action.menu.select"] },
  "timeline": {
    "autoplay": true,
    "block_menus": )json";
    intro_json += block_menus_text;
    intro_json += R"json(,
    "block_scene_shortcuts": )json";
    intro_json += block_shortcuts_text;
    intro_json += R"json(,
    "events": [
      {
        "time": 1.0,
        "action": { "type": "scene.request", "scene": "scene.title" }
      }
    ]
  },
  "menus": [
    {
      "name": "menu.intro",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "items": [
        { "label": "Play", "scene": "scene.play" }
      ]
    }
  ]
})json";
    write_text(dir / "scenes" / "intro.scene.json", intro_json.c_str());
    write_text(dir / "scenes" / "title.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false,
  "entities": []
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true,
  "entities": []
})json");
}

void write_scene_activity_json(const std::filesystem::path &dir)
{
    write_text(dir / "activity.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Activity", "id": "test.activity", "version": "0.1.0" },
  "input": {
    "contexts": [
      {
        "name": "menu",
        "actions": [
          {
            "name": "action.menu.select",
            "bindings": [
              { "device": "keyboard", "key": "RETURN" }
            ]
          },
          {
            "name": "action.menu.up",
            "bindings": [
              { "device": "keyboard", "key": "UP" }
            ]
          },
          {
            "name": "action.menu.down",
            "bindings": [
              { "device": "keyboard", "key": "DOWN" }
            ]
          }
        ]
      }
    ]
  },
  "world": {
    "cameras": [
      {
        "name": "camera.overhead",
        "type": "orthographic",
        "position": [0.0, 0.0, 10.0],
        "target": [0.0, 0.0, 0.0],
        "up": [0.0, 1.0, 0.0],
        "size": 10.0,
        "active": true
      },
      {
        "name": "camera.close",
        "type": "orthographic",
        "position": [0.0, 0.0, 6.0],
        "target": [0.0, 0.0, 0.0],
        "up": [0.0, 1.0, 0.0],
        "size": 4.0,
        "active": false
      }
    ]
  },
  "entities": [
    {
      "name": "entity.state",
      "active": true,
      "properties": {
        "entered": { "type": "bool", "value": false },
        "idle": { "type": "bool", "value": false },
        "periodic": { "type": "int", "value": 0 }
      }
    },
    {
      "name": "entity.lamp",
      "active": true,
      "transform": { "position": [2.0, 0.0, 0.0] },
      "components": [
        { "type": "motion.oscillate", "origin": [2.0, 0.0, 0.0], "amplitude": [3.0, 0.0, 0.0], "rate": 1.0 }
      ]
    }
  ],
  "scenes": {
    "initial": "scene.title",
    "files": ["scenes/title.scene.json", "scenes/play.scene.json"]
  }
})json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false,
  "camera": "camera.overhead",
  "entities": ["entity.state", "entity.lamp"],
  "input": { "actions": ["action.menu.select", "action.menu.up", "action.menu.down"] },
  "activity": {
    "enabled": true,
    "input": "any",
    "consume_wake_input": true,
    "block_menus_on_wake": true,
    "block_scene_shortcuts_on_wake": true,
    "idle_after": 1.0,
    "on_enter": [
      { "type": "property.set", "target": "entity.state", "key": "entered", "value": true },
      { "type": "camera.set", "camera": "camera.close" }
    ],
    "on_idle": [
      { "type": "property.set", "target": "entity.state", "key": "idle", "value": true }
    ],
    "on_active": [
      { "type": "property.set", "target": "entity.state", "key": "idle", "value": false }
    ],
    "periodic": [
      {
        "interval": 2.0,
        "reset_idle": true,
        "actions": [
          { "type": "property.set", "target": "entity.state", "key": "idle", "value": false },
          { "type": "property.add", "target": "entity.state", "key": "periodic", "value": 1 }
        ]
      }
    ]
  },
  "menus": [
    {
      "name": "menu.title",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "items": [
        { "label": "Play", "scene": "scene.play" }
      ]
    }
  ]
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": false,
  "entities": []
})json");
}

void write_animation_json(const std::filesystem::path &dir)
{
    write_text(dir / "animation.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Animation", "id": "test.animation", "version": "0.1.0" },
  "entities": [
    {
      "name": "entity.box",
      "active": true,
      "properties": {
        "x": { "type": "float", "value": 0.0 },
        "ease": { "type": "float", "value": 0.0 },
        "loop": { "type": "float", "value": 0.0 },
        "ping": { "type": "float", "value": 0.0 }
      }
    }
  ],
  "signals": ["signal.property.done", "signal.ui.done"],
  "scenes": {
    "initial": "scene.intro",
    "files": [
      "scenes/intro.scene.json",
      "scenes/title.scene.json"
    ]
  }
})json");
    write_text(dir / "scenes" / "intro.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.intro",
  "updates_game": false,
  "renders_world": false,
  "entities": ["entity.box"],
  "ui": {
    "text": [
      {
        "name": "ui.logo",
        "text": "LOGO",
        "x": 0.5,
        "y": 0.5,
        "normalized": true,
        "align": "center",
        "color": [255, 255, 255, 255]
      }
    ]
  },
  "timeline": {
    "autoplay": true,
    "events": [
      {
        "time": 0.0,
        "action": {
          "type": "property.animate",
          "target": "entity.box",
          "key": "x",
          "from": 0.0,
          "to": 10.0,
          "duration": 1.0,
          "done_signal": "signal.property.done"
        }
      },
      {
        "time": 0.0,
        "action": {
          "type": "property.animate",
          "target": "entity.box",
          "key": "ease",
          "from": 0.0,
          "to": 1.0,
          "duration": 1.0,
          "easing": "out_quad"
        }
      },
      {
        "time": 0.0,
        "action": {
          "type": "property.animate",
          "target": "entity.box",
          "key": "loop",
          "from": 0.0,
          "to": 1.0,
          "duration": 1.0,
          "repeat": "loop"
        }
      },
      {
        "time": 0.0,
        "action": {
          "type": "property.animate",
          "target": "entity.box",
          "key": "ping",
          "from": 0.0,
          "to": 1.0,
          "duration": 1.0,
          "repeat": "ping_pong"
        }
      },
      {
        "time": 0.0,
        "action": {
          "type": "ui.animate",
          "target": "ui.logo",
          "property": "alpha",
          "from": 0.0,
          "to": 1.0,
          "duration": 1.0,
          "done_signal": "signal.ui.done"
        }
      },
      {
        "time": 0.0,
        "action": {
          "type": "ui.animate",
          "target": "ui.logo",
          "property": "scale",
          "from": 1.0,
          "to": 2.0,
          "duration": 1.0
        }
      }
    ]
  }
})json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false,
  "entities": []
})json");
}

void write_hot_reload_script(const std::filesystem::path &dir, int value)
{
    const std::string script = std::string("local rules = {}\n"
                                           "function rules.run(target)\n"
                                           "    target:set_int(\"value\", ") +
                               std::to_string(value) +
                               ")\n"
                               "    return true\n"
                               "end\n"
                               "return rules\n";
    write_text(dir / "scripts" / "rules.lua", script.c_str());
}

void emit_reload_signal(sdl3d_game_session *session, sdl3d_game_data_runtime *runtime)
{
    const int signal = sdl3d_game_data_find_signal(runtime, "signal.run");
    ASSERT_GE(signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), signal, nullptr);
}

bool capture_render_primitive(void *userdata, const sdl3d_game_data_render_primitive *primitive)
{
    auto *capture = static_cast<RenderPrimitiveCapture *>(userdata);
    if (primitive->type == SDL3D_GAME_DATA_RENDER_CUBE)
        capture->cubes++;
    else if (primitive->type == SDL3D_GAME_DATA_RENDER_SPHERE)
        capture->spheres++;

    if (std::string(primitive->entity_name) == "entity.paddle.player")
    {
        capture->saw_player_paddle = true;
        EXPECT_NEAR(primitive->position.x, -8.0f, 0.0001f);
        EXPECT_NEAR(primitive->size.x, 0.36f, 0.0001f);
        EXPECT_EQ(primitive->color.r, 205);
        EXPECT_EQ(primitive->color.g, 230);
        EXPECT_EQ(primitive->color.b, 255);
    }
    if (std::string(primitive->entity_name) == "entity.ball")
    {
        capture->saw_ball = true;
        capture->ball_rotation_angle = primitive->rotation_angle;
        EXPECT_NEAR(primitive->position.z, 0.12f, 0.0001f);
        EXPECT_NEAR(primitive->radius, 0.22f, 0.0001f);
        EXPECT_EQ(primitive->rings, 24);
        EXPECT_EQ(primitive->slices, 32);
        EXPECT_STREQ(primitive->texture_image, "image.ball.texture");
        EXPECT_NEAR(primitive->rotation_axis.x, 0.0f, 0.0001f);
        EXPECT_NEAR(primitive->rotation_axis.y, 0.0f, 0.0001f);
        EXPECT_NEAR(primitive->rotation_axis.z, 1.0f, 0.0001f);
        EXPECT_FALSE(primitive->emissive);
    }
    if (std::string(primitive->entity_name) == "entity.options.background.base")
    {
        capture->saw_options_background = true;
        EXPECT_EQ(primitive->type, SDL3D_GAME_DATA_RENDER_CUBE);
        EXPECT_NEAR(primitive->position.z, -0.65f, 0.0001f);
        EXPECT_NEAR(primitive->size.x, 22.0f, 0.0001f);
        EXPECT_EQ(primitive->color.r, 0);
        EXPECT_EQ(primitive->color.g, 0);
        EXPECT_EQ(primitive->color.b, 0);
        EXPECT_FALSE(primitive->lighting_enabled);
        EXPECT_FALSE(primitive->emissive);
        EXPECT_NEAR(primitive->emissive_color.x, 0.0f, 0.0001f);
    }
    if (std::string(primitive->entity_name) == "entity.options.glow.magenta")
    {
        capture->saw_options_glow = true;
        EXPECT_EQ(primitive->type, SDL3D_GAME_DATA_RENDER_SPHERE);
        EXPECT_NEAR(primitive->radius, 1.05f, 0.0001f);
        EXPECT_TRUE(primitive->emissive);
    }
    return true;
}

bool capture_ui_text(void *userdata, const sdl3d_game_data_ui_text *text)
{
    auto *capture = static_cast<UiTextCapture *>(userdata);
    capture->count++;
    if (std::string(text->name) == "ui.score")
    {
        capture->saw_score = true;
        EXPECT_TRUE(text->centered);
        EXPECT_TRUE(text->normalized);
        EXPECT_NEAR(text->x, 0.5f, 0.0001f);
        EXPECT_NEAR(text->y, 0.06f, 0.0001f);
    }
    if (std::string(text->name) == "ui.pause")
    {
        capture->saw_pause = true;
        EXPECT_STREQ(text->text, "PAUSED");
        EXPECT_TRUE(text->pulse_alpha);
    }
    if (std::string(text->name) == "ui.network.match_terminated")
    {
        capture->saw_network_match_terminated = true;
        EXPECT_TRUE(text->centered);
        EXPECT_TRUE(text->normalized);
    }
    return true;
}

bool capture_particle(void *userdata, const sdl3d_game_data_particle_emitter *emitter)
{
    auto *capture = static_cast<ParticleCapture *>(userdata);
    ++capture->count;
    if (std::string(emitter->entity_name) == "entity.effect.ambient_particles")
    {
        capture->saw_ambient = true;
        EXPECT_EQ(emitter->config.max_particles, 360);
        EXPECT_NEAR(emitter->draw_emissive.x, 0.8f, 0.0001f);
    }
    if (std::string(emitter->entity_name) == "entity.options.flow.magenta")
    {
        capture->saw_options_flow = true;
        EXPECT_EQ(emitter->config.max_particles, 130);
        EXPECT_NEAR(emitter->draw_emissive.x, 1.0f, 0.0001f);
        EXPECT_FALSE(emitter->config.depth_test);
        EXPECT_TRUE(emitter->config.additive_blend);
        EXPECT_LT(emitter->config.size_start, 0.04f);
    }
    return true;
}

bool capture_evaluated_primitive(void *userdata, const sdl3d_game_data_render_primitive *primitive)
{
    auto *capture = static_cast<EvaluatedPrimitiveCapture *>(userdata);
    if (std::string(primitive->entity_name) == "entity.field.border.top")
    {
        capture->saw_border = true;
        EXPECT_GT(primitive->color.r, 62);
        EXPECT_GT(primitive->size.y, 0.12f);
        EXPECT_GT(primitive->emissive_color.z, 0.9f);
    }
    if (std::string(primitive->entity_name) == "entity.ball")
    {
        capture->saw_ball = true;
        EXPECT_STREQ(primitive->texture_image, "image.ball.texture");
        EXPECT_FALSE(primitive->emissive);
    }
    if (std::string(primitive->entity_name) == "entity.options.glow.magenta")
    {
        capture->saw_options_drift = true;
        EXPECT_NE(primitive->position.x, -4.6f);
        EXPECT_GT(primitive->radius, 1.05f);
        EXPECT_GT(primitive->emissive_color.x, 0.2f);
    }
    return true;
}

void append_u16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
}

void append_u32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
}

void append_u64(std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
}

std::vector<std::uint8_t> make_pack(const std::vector<std::pair<std::string, std::string>> &entries)
{
    std::uint64_t table_size = 0;
    for (const auto &entry : entries)
        table_size += 18u + static_cast<std::uint64_t>(entry.first.size());

    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), {'S', '3', 'D', 'P', 'A', 'K', '1', '\0'});
    append_u32(bytes, 1);
    append_u32(bytes, static_cast<std::uint32_t>(entries.size()));
    append_u64(bytes, 24);

    std::uint64_t data_offset = 24u + table_size;
    for (const auto &entry : entries)
    {
        append_u16(bytes, static_cast<std::uint16_t>(entry.first.size()));
        append_u64(bytes, data_offset);
        append_u64(bytes, static_cast<std::uint64_t>(entry.second.size()));
        bytes.insert(bytes.end(), entry.first.begin(), entry.first.end());
        data_offset += static_cast<std::uint64_t>(entry.second.size());
    }

    for (const auto &entry : entries)
        bytes.insert(bytes.end(), entry.second.begin(), entry.second.end());
    return bytes;
}

bool mount_test_directory_assets(sdl3d_asset_resolver *assets, void *userdata, char *error_buffer,
                                 int error_buffer_size)
{
    return sdl3d_asset_resolver_mount_directory(assets, static_cast<const char *>(userdata), error_buffer,
                                                error_buffer_size);
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
    EXPECT_NE(sdl3d_game_data_find_actor_with_tag(runtime, "ball"), nullptr);
    const char *paddle_tags[] = {"paddle", "player"};
    EXPECT_NE(sdl3d_game_data_find_actor_with_tags(runtime, paddle_tags, 2), nullptr);
    EXPECT_GE(sdl3d_game_data_find_signal(runtime, "signal.ball.serve"), 0);
    EXPECT_GE(sdl3d_game_data_find_signal(runtime, "signal.multiplayer.lobby.start"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.paddle.up"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.paddle.local.up"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.paddle.local.down"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.scene.title"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.scene.options"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.scene.play"), 0);
    const char *network_scene_state_key = nullptr;
    ASSERT_TRUE(sdl3d_game_data_get_network_scene_state_key(runtime, "host", "status", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "multiplayer_host_status");
    ASSERT_TRUE(sdl3d_game_data_get_network_scene_state_key(runtime, "host", "connected", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "multiplayer_host_connected");
    ASSERT_TRUE(sdl3d_game_data_get_network_scene_state_key(runtime, "discovery", "count", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "multiplayer_discovery_count");
    ASSERT_TRUE(
        sdl3d_game_data_get_network_scene_state_key(runtime, "discovery", "result_0", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "session_0");
    EXPECT_FALSE(sdl3d_game_data_get_network_scene_state_key(runtime, "host", "missing", &network_scene_state_key));
    EXPECT_EQ(network_scene_state_key, nullptr);
    const char *network_session_value = nullptr;
    ASSERT_TRUE(sdl3d_game_data_get_network_session_scene(runtime, "play", &network_session_value));
    EXPECT_STREQ(network_session_value, "scene.play");
    ASSERT_TRUE(sdl3d_game_data_get_network_session_scene(runtime, "join", &network_session_value));
    EXPECT_STREQ(network_session_value, "scene.multiplayer.join");
    ASSERT_TRUE(sdl3d_game_data_get_network_session_state_key(runtime, "match_mode", &network_session_value));
    EXPECT_STREQ(network_session_value, "match_mode");
    ASSERT_TRUE(
        sdl3d_game_data_get_network_session_state_value(runtime, "match_mode", "network", &network_session_value));
    EXPECT_STREQ(network_session_value, "lan");
    ASSERT_TRUE(
        sdl3d_game_data_get_network_session_state_value(runtime, "network_role", "client", &network_session_value));
    EXPECT_STREQ(network_session_value, "client");
    ASSERT_TRUE(sdl3d_game_data_get_network_session_message(runtime, "disconnect_reasons", "host_exited",
                                                            &network_session_value));
    EXPECT_STREQ(network_session_value, "Host exited");
    ASSERT_TRUE(sdl3d_game_data_get_network_session_message(runtime, "disconnect_prompts", "match_terminated",
                                                            &network_session_value));
    EXPECT_STREQ(network_session_value, "Match terminated: {reason} - Press Enter to return to title screen.");
    EXPECT_FALSE(
        sdl3d_game_data_get_network_session_message(runtime, "disconnect_reasons", "missing", &network_session_value));
    EXPECT_EQ(network_session_value, nullptr);
    EXPECT_TRUE(sdl3d_game_data_network_managed_runtime_enabled(runtime));
    float managed_ack_delay = 0.0f;
    ASSERT_TRUE(sdl3d_game_data_get_network_managed_termination_ack_delay(runtime, &managed_ack_delay));
    EXPECT_FLOAT_EQ(managed_ack_delay, 3.0f);
    EXPECT_TRUE(sdl3d_game_data_network_managed_keep_alive_scene_matches(runtime, "host", "scene.multiplayer.lobby"));
    EXPECT_TRUE(sdl3d_game_data_network_managed_keep_alive_scene_matches(runtime, "host", "scene.play"));
    EXPECT_FALSE(sdl3d_game_data_network_managed_keep_alive_scene_matches(runtime, "host", "scene.title"));
    EXPECT_TRUE(sdl3d_game_data_network_managed_keep_alive_scene_matches(runtime, "direct_connect",
                                                                         "scene.multiplayer.discovery"));
    EXPECT_TRUE(sdl3d_game_data_network_managed_keep_alive_scene_matches(runtime, "direct_connect", "scene.play"));
    const char *network_runtime_value = nullptr;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_replication(runtime, "state_snapshot", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "play_state");
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_replication(runtime, "client_input", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "client_input");
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_control(runtime, "pause_request", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "pause_request");
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_control(runtime, "disconnect", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "disconnect");
    EXPECT_FALSE(sdl3d_game_data_get_network_runtime_control(runtime, "missing", &network_runtime_value));
    EXPECT_EQ(network_runtime_value, nullptr);
    int network_runtime_id = -1;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_action(runtime, "client_up", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, sdl3d_game_data_find_action(runtime, "action.paddle.local.up"));
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_action(runtime, "menu_back", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, sdl3d_game_data_find_action(runtime, "action.menu.back"));
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_signal(runtime, "lobby_start", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, sdl3d_game_data_find_signal(runtime, "signal.multiplayer.lobby.start"));
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_signal(runtime, "ui_select", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, sdl3d_game_data_find_signal(runtime, "signal.ui.menu.select"));
    int network_pause_action = -1;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_pause_action(runtime, &network_pause_action));
    EXPECT_EQ(network_pause_action, sdl3d_game_data_find_action(runtime, "action.pause"));
    bool network_paused = true;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_pause_state(runtime, &network_paused, error, sizeof(error)))
        << error;
    EXPECT_FALSE(network_paused);
    ASSERT_TRUE(sdl3d_game_data_set_network_runtime_pause_state(runtime, true, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_pause_state(runtime, &network_paused, error, sizeof(error)))
        << error;
    EXPECT_TRUE(network_paused);
    ASSERT_EQ(sdl3d_game_data_haptics_policy_count(runtime), 2);
    sdl3d_game_data_haptics_policy haptics_policy{};
    ASSERT_TRUE(sdl3d_game_data_get_haptics_policy_at(runtime, 0, &haptics_policy));
    EXPECT_STREQ(haptics_policy.name, "haptics.gamepad.vibration_test");
    EXPECT_EQ(haptics_policy.signal_id, sdl3d_game_data_find_signal(runtime, "signal.settings.vibration"));
    EXPECT_FLOAT_EQ(haptics_policy.low_frequency, 0.30f);
    EXPECT_FLOAT_EQ(haptics_policy.high_frequency, 0.70f);
    EXPECT_EQ(haptics_policy.duration_ms, 100U);
    ASSERT_TRUE(sdl3d_game_data_get_haptics_policy_at(runtime, 1, &haptics_policy));
    EXPECT_STREQ(haptics_policy.name, "haptics.gamepad.paddle_hit");
    EXPECT_EQ(haptics_policy.signal_id, sdl3d_game_data_find_signal(runtime, "signal.ball.hit_paddle"));

    sdl3d_properties *haptics_payload = sdl3d_properties_create();
    ASSERT_NE(haptics_payload, nullptr);
    const int paddle_hit_signal = sdl3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    sdl3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.player");
    EXPECT_TRUE(sdl3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    sdl3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.cpu");
    EXPECT_FALSE(sdl3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(runtime), "match_mode", "local");
    EXPECT_TRUE(sdl3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    sdl3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.attract_left");
    EXPECT_FALSE(sdl3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    sdl3d_registered_actor *settings_for_haptics = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings_for_haptics, nullptr);
    sdl3d_properties_set_bool(settings_for_haptics->props, "vibration", false);
    sdl3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.player");
    EXPECT_FALSE(sdl3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    sdl3d_properties_destroy(haptics_payload);
    EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.overhead");
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.splash");
    EXPECT_EQ(sdl3d_game_data_scene_count(runtime), 15);
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 0), "scene.splash");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 1), "scene.title");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 2), "scene.multiplayer");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 3), "scene.multiplayer.lan");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 4), "scene.multiplayer.lobby");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 5), "scene.multiplayer.join");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 6), "scene.multiplayer.direct_connect");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 7), "scene.multiplayer.discovery");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 8), "scene.options");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 9), "scene.options.display");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 10), "scene.options.keyboard");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 11), "scene.options.mouse");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 12), "scene.options.gamepad");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 13), "scene.options.audio");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 14), "scene.play");
    EXPECT_EQ(sdl3d_game_data_scene_name_at(runtime, -1), nullptr);
    EXPECT_EQ(sdl3d_game_data_scene_name_at(runtime, 15), nullptr);
    EXPECT_FALSE(sdl3d_game_data_active_scene_updates_game(runtime));
    EXPECT_FALSE(sdl3d_game_data_active_scene_renders_world(runtime));
    EXPECT_FALSE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.ball"));
    EXPECT_EQ(sdl3d_timer_pool_active_count(sdl3d_game_session_get_timer_pool(session)), 0);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DataGameRuntimeOwnsGenericPongLifecycle)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    const std::filesystem::path data_path = SDL3D_PONG_DATA_PATH;
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    sdl3d_data_game_runtime_desc desc{};
    sdl3d_data_game_runtime_desc_init(&desc);
    desc.session = session;
    desc.media_dir = SDL3D_MEDIA_DIR;
    desc.data_asset_path = asset_path.c_str();
    desc.mount_assets = mount_test_directory_assets;
    desc.mount_userdata = const_cast<char *>(root.c_str());

    char error[512]{};
    sdl3d_data_game_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error))) << error;
    ASSERT_NE(runtime, nullptr);
    ASSERT_NE(sdl3d_data_game_runtime_assets(runtime), nullptr);
    sdl3d_game_data_runtime *data = sdl3d_data_game_runtime_data(runtime);
    ASSERT_NE(data, nullptr);
    EXPECT_STREQ(sdl3d_game_data_active_scene(data), "scene.splash");
    EXPECT_NE(sdl3d_game_data_find_actor(data, "entity.ball"), nullptr);

    sdl3d_game_context ctx{};
    ctx.session = session;
    EXPECT_TRUE(sdl3d_data_game_runtime_update_frame(runtime, &ctx, 0.016f));
    sdl3d_data_game_runtime_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DataGameRuntimeRefreshesInputProfilesOnGamepadHotplug)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    if (sdl3d_input_gamepad_count(input) != 0)
    {
        sdl3d_game_session_destroy(session);
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }

    const std::filesystem::path data_path = SDL3D_PONG_DATA_PATH;
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    sdl3d_data_game_runtime_desc desc{};
    sdl3d_data_game_runtime_desc_init(&desc);
    desc.session = session;
    desc.media_dir = SDL3D_MEDIA_DIR;
    desc.data_asset_path = asset_path.c_str();
    desc.mount_assets = mount_test_directory_assets;
    desc.mount_userdata = const_cast<char *>(root.c_str());

    char error[512]{};
    sdl3d_data_game_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error))) << error;
    sdl3d_game_data_runtime *data = sdl3d_data_game_runtime_data(runtime);
    ASSERT_NE(data, nullptr);

    const int p1_up = sdl3d_game_data_find_action(data, "action.paddle.up");
    const int p2_up = sdl3d_game_data_find_action(data, "action.paddle.local.up");
    ASSERT_GE(p1_up, 0);
    ASSERT_GE(p2_up, 0);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "match_mode", "local");
    sdl3d_properties_set_string(scene_state, "network_role", "none");
    sdl3d_properties_set_string(scene_state, "network_flow", "none");

    sdl3d_game_context ctx{};
    ctx.session = session;
    ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(runtime, &ctx, 0.016f));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 0.0f);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7301;
    sdl3d_input_process_event(input, &event);
    ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(runtime, &ctx, 0.016f));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7301;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 3);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 1.0f);

    sdl3d_data_game_runtime_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DataGameRuntimeNetworkLoopReplicatesPongInputStateAndControls)
{
    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_session *client_session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &host_session));
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &client_session));

    const std::filesystem::path data_path = SDL3D_PONG_DATA_PATH;
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    sdl3d_data_game_runtime_desc host_desc{};
    sdl3d_data_game_runtime_desc_init(&host_desc);
    host_desc.session = host_session;
    host_desc.media_dir = SDL3D_MEDIA_DIR;
    host_desc.data_asset_path = asset_path.c_str();
    host_desc.mount_assets = mount_test_directory_assets;
    host_desc.mount_userdata = const_cast<char *>(root.c_str());

    sdl3d_data_game_runtime_desc client_desc = host_desc;
    client_desc.session = client_session;

    char error[512]{};
    sdl3d_data_game_runtime *host_runtime = nullptr;
    sdl3d_data_game_runtime *client_runtime = nullptr;
    ASSERT_TRUE(sdl3d_data_game_runtime_create(&host_desc, &host_runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_data_game_runtime_create(&client_desc, &client_runtime, error, sizeof(error))) << error;
    sdl3d_game_data_runtime *host_data = sdl3d_data_game_runtime_data(host_runtime);
    sdl3d_game_data_runtime *client_data = sdl3d_data_game_runtime_data(client_runtime);
    ASSERT_NE(host_data, nullptr);
    ASSERT_NE(client_data, nullptr);

    auto enter_play = [](sdl3d_game_data_runtime *runtime, const char *role) {
        sdl3d_properties *payload = sdl3d_properties_create();
        if (payload == nullptr)
            return false;
        sdl3d_properties_set_string(payload, "match_mode", "lan");
        sdl3d_properties_set_string(payload, "network_role", role);
        sdl3d_properties_set_string(payload, "network_flow", SDL_strcmp(role, "host") == 0 ? "host" : "direct");
        const bool ok = sdl3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload);
        sdl3d_properties_destroy(payload);
        return ok;
    };
    ASSERT_TRUE(enter_play(host_data, "host"));
    ASSERT_TRUE(enter_play(client_data, "client"));

    const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_name =
        test_info != nullptr ? std::string(test_info->test_suite_name()) + "." + test_info->name() : "runtime_net";
    const int port = 30000 + (int)(std::hash<std::string>{}(test_name) % 20000U);
    if (!sdl3d_game_data_network_host_start(host_data, "host", port, "SDL3D Test", "host_status", "host_endpoint",
                                            "host_peer", "host_connected"))
    {
        sdl3d_data_game_runtime_destroy(client_runtime);
        sdl3d_data_game_runtime_destroy(host_runtime);
        sdl3d_game_session_destroy(client_session);
        sdl3d_game_session_destroy(host_session);
        GTEST_SKIP() << "network host unavailable: " << SDL_GetError();
    }
    ASSERT_TRUE(sdl3d_game_data_network_direct_connect_start(client_data, "direct_connect", "127.0.0.1", port,
                                                             "direct_status", "direct_state", "direct_connected"));

    const sdl3d_data_game_network_bindings bindings = {"state_snapshot", "client_input",   "start_game",
                                                       "pause_request",  "resume_request", "disconnect"};
    sdl3d_game_context host_ctx{};
    host_ctx.session = host_session;
    sdl3d_game_context client_ctx{};
    client_ctx.session = client_session;
    sdl3d_data_game_network_loop_result host_result{};
    sdl3d_data_game_network_loop_result client_result{};

    sdl3d_network_session *host_net = sdl3d_game_data_get_network_host_session(host_data, "host");
    sdl3d_network_session *client_net =
        sdl3d_game_data_get_network_direct_connect_session(client_data, "direct_connect");
    ASSERT_NE(host_net, nullptr);
    ASSERT_NE(client_net, nullptr);
    bool connected = false;
    for (int i = 0; i < 1200 && !connected; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_network_host_session(
            host_runtime, &host_ctx, "host", &bindings, false, 0.01f, &host_result, error, sizeof(error)))
            << error;
        ASSERT_TRUE(sdl3d_data_game_runtime_update_network_client_session(client_runtime, &client_ctx, "direct_connect",
                                                                          &bindings, false, false, 0.01f,
                                                                          &client_result, error, sizeof(error)))
            << error;
        connected = sdl3d_network_session_is_connected(host_net) && sdl3d_network_session_is_connected(client_net);
    }
    ASSERT_TRUE(connected);

    sdl3d_input_manager *client_input = sdl3d_game_session_get_input(client_session);
    sdl3d_input_manager *host_input = sdl3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);
    const int client_up = sdl3d_game_data_find_action(client_data, "action.paddle.local.up");
    const int host_up = sdl3d_game_data_find_action(host_data, "action.paddle.local.up");
    ASSERT_GE(client_up, 0);
    ASSERT_GE(host_up, 0);
    sdl3d_input_set_action_override(client_input, client_up, 1.0f);
    ASSERT_NE(sdl3d_input_update(client_input, 101), nullptr);
    ASSERT_TRUE(sdl3d_data_game_runtime_update_network_client_session(client_runtime, &client_ctx, "direct_connect",
                                                                      &bindings, true, false, 0.016f, &client_result,
                                                                      error, sizeof(error)))
        << error;
    EXPECT_TRUE(client_result.sent_input);
    for (int i = 0; i < 120 && !host_result.applied_input; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_network_host_session(
            host_runtime, &host_ctx, "host", &bindings, true, 0.01f, &host_result, error, sizeof(error)))
            << error;
    }
    ASSERT_TRUE(host_result.applied_input);
    ASSERT_NE(sdl3d_input_update(host_input, 102), nullptr);
    EXPECT_NEAR(sdl3d_input_get_value(host_input, host_up), 1.0f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_send_network_runtime_control(client_data, client_net, "pause_request", 202U, error,
                                                             sizeof(error)))
        << error;
    host_ctx.paused = false;
    host_result = {};
    for (int i = 0; i < 120 && !host_result.received_pause_request; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_network_host_session(
            host_runtime, &host_ctx, "host", &bindings, true, 0.01f, &host_result, error, sizeof(error)))
            << error;
    }
    EXPECT_TRUE(host_result.received_pause_request);
    EXPECT_TRUE(host_ctx.paused);

    sdl3d_registered_actor *host_ball = sdl3d_game_data_find_actor(host_data, "entity.ball");
    sdl3d_registered_actor *client_ball = sdl3d_game_data_find_actor(client_data, "entity.ball");
    ASSERT_NE(host_ball, nullptr);
    ASSERT_NE(client_ball, nullptr);
    host_ball->position = sdl3d_vec3_make(3.0f, 2.0f, 0.0f);
    ASSERT_TRUE(sdl3d_data_game_runtime_publish_network_host_snapshot(host_runtime, &host_ctx, "host", &bindings,
                                                                      &host_result, error, sizeof(error)))
        << error;
    EXPECT_TRUE(host_result.sent_snapshot);
    client_ctx.paused = false;
    client_result = {};
    for (int i = 0; i < 120 && !client_result.applied_snapshot; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_network_client_session(client_runtime, &client_ctx, "direct_connect",
                                                                          &bindings, true, false, 0.01f, &client_result,
                                                                          error, sizeof(error)))
            << error;
    }
    ASSERT_TRUE(client_result.applied_snapshot);
    EXPECT_NEAR(client_ball->position.x, host_ball->position.x, 0.0001f);
    EXPECT_NEAR(client_ball->position.y, host_ball->position.y, 0.0001f);
    EXPECT_TRUE(client_ctx.paused);

    ASSERT_TRUE(
        sdl3d_game_data_send_network_runtime_control(host_data, host_net, "start_game", 303U, error, sizeof(error)))
        << error;
    client_result = {};
    for (int i = 0; i < 120 && !client_result.received_start_game; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_network_client_session(client_runtime, &client_ctx, "direct_connect",
                                                                          &bindings, true, false, 0.01f, &client_result,
                                                                          error, sizeof(error)))
            << error;
    }
    EXPECT_TRUE(client_result.received_start_game);

    sdl3d_data_game_runtime_destroy(client_runtime);
    sdl3d_data_game_runtime_destroy(host_runtime);
    sdl3d_game_session_destroy(client_session);
    sdl3d_game_session_destroy(host_session);
}

TEST(GameDataRuntime, ManagedNetworkRuntimeStartsPongMatchAndReplicatesState)
{
    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_session *client_session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &host_session));
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &client_session));

    const std::filesystem::path data_path = SDL3D_PONG_DATA_PATH;
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    sdl3d_data_game_runtime_desc host_desc{};
    sdl3d_data_game_runtime_desc_init(&host_desc);
    host_desc.session = host_session;
    host_desc.media_dir = SDL3D_MEDIA_DIR;
    host_desc.data_asset_path = asset_path.c_str();
    host_desc.mount_assets = mount_test_directory_assets;
    host_desc.mount_userdata = const_cast<char *>(root.c_str());
    host_desc.enable_managed_network = true;

    sdl3d_data_game_runtime_desc client_desc = host_desc;
    client_desc.session = client_session;

    char error[512]{};
    sdl3d_data_game_runtime *host_runtime = nullptr;
    sdl3d_data_game_runtime *client_runtime = nullptr;
    ASSERT_TRUE(sdl3d_data_game_runtime_create(&host_desc, &host_runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_data_game_runtime_create(&client_desc, &client_runtime, error, sizeof(error))) << error;
    sdl3d_game_data_runtime *host_data = sdl3d_data_game_runtime_data(host_runtime);
    sdl3d_game_data_runtime *client_data = sdl3d_data_game_runtime_data(client_runtime);
    ASSERT_NE(host_data, nullptr);
    ASSERT_NE(client_data, nullptr);

    auto enter_scene = [](sdl3d_game_data_runtime *runtime, const char *scene, const char *network_flow) {
        sdl3d_properties *payload = sdl3d_properties_create();
        if (payload == nullptr)
            return false;
        sdl3d_properties_set_string(payload, "network_flow", network_flow);
        const bool ok = sdl3d_game_data_set_active_scene_with_payload(runtime, scene, payload);
        sdl3d_properties_destroy(payload);
        return ok;
    };
    ASSERT_TRUE(enter_scene(host_data, "scene.multiplayer.lobby", "host"));
    ASSERT_TRUE(enter_scene(client_data, "scene.multiplayer.direct_connect", "direct"));

    const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_name =
        test_info != nullptr ? std::string(test_info->test_suite_name()) + "." + test_info->name() : "managed_net";
    const int port = 30000 + (int)(std::hash<std::string>{}(test_name) % 20000U);
    if (!sdl3d_game_data_network_host_start(host_data, "host", port, "SDL3D Test", "host_status", "host_endpoint",
                                            "host_peer", "host_connected"))
    {
        sdl3d_data_game_runtime_destroy(client_runtime);
        sdl3d_data_game_runtime_destroy(host_runtime);
        sdl3d_game_session_destroy(client_session);
        sdl3d_game_session_destroy(host_session);
        GTEST_SKIP() << "network host unavailable: " << SDL_GetError();
    }
    ASSERT_TRUE(sdl3d_game_data_network_direct_connect_start(client_data, "direct_connect", "127.0.0.1", port,
                                                             "direct_connect_status", "direct_connect_state",
                                                             "direct_connect_connected"));

    sdl3d_game_context host_ctx{};
    host_ctx.session = host_session;
    sdl3d_game_context client_ctx{};
    client_ctx.session = client_session;

    sdl3d_network_session *host_net = sdl3d_game_data_get_network_host_session(host_data, "host");
    sdl3d_network_session *client_net =
        sdl3d_game_data_get_network_direct_connect_session(client_data, "direct_connect");
    ASSERT_NE(host_net, nullptr);
    ASSERT_NE(client_net, nullptr);
    bool connected = false;
    for (int i = 0; i < 1200 && !connected; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(host_runtime, &host_ctx, 0.01f));
        ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.01f));
        connected = sdl3d_network_session_is_connected(host_net) && sdl3d_network_session_is_connected(client_net);
    }
    ASSERT_TRUE(connected);

    int lobby_start_signal = -1;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_signal(host_data, "lobby_start", &lobby_start_signal));
    ASSERT_GE(lobby_start_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(host_session), lobby_start_signal, nullptr);

    bool started = false;
    for (int i = 0; i < 240 && !started; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(host_runtime, &host_ctx, 0.016f));
        ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.016f));
        started = SDL_strcmp(sdl3d_game_data_active_scene(host_data), "scene.play") == 0 &&
                  SDL_strcmp(sdl3d_game_data_active_scene(client_data), "scene.play") == 0;
    }
    ASSERT_TRUE(started);
    const sdl3d_properties *host_scene_state = sdl3d_game_data_scene_state(host_data);
    const sdl3d_properties *client_scene_state = sdl3d_game_data_scene_state(client_data);
    ASSERT_NE(host_scene_state, nullptr);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(host_scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(sdl3d_properties_get_string(host_scene_state, "network_role", ""), "host");
    EXPECT_STREQ(sdl3d_properties_get_string(client_scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(sdl3d_properties_get_string(client_scene_state, "network_role", ""), "client");

    sdl3d_registered_actor *host_ball = sdl3d_game_data_find_actor(host_data, "entity.ball");
    sdl3d_registered_actor *client_ball = sdl3d_game_data_find_actor(client_data, "entity.ball");
    ASSERT_NE(host_ball, nullptr);
    ASSERT_NE(client_ball, nullptr);
    host_ctx.paused = true;
    client_ctx.paused = false;
    host_ball->position = sdl3d_vec3_make(4.0f, 1.5f, 0.0f);
    bool snapshot_applied = false;
    for (int i = 0; i < 240 && !snapshot_applied; ++i)
    {
        ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(host_runtime, &host_ctx, 0.016f));
        ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.016f));
        snapshot_applied = SDL_fabsf(client_ball->position.x - host_ball->position.x) < 0.0001f &&
                           SDL_fabsf(client_ball->position.y - host_ball->position.y) < 0.0001f;
    }
    EXPECT_TRUE(snapshot_applied);
    EXPECT_TRUE(client_ctx.paused);

    sdl3d_properties *termination_payload = sdl3d_properties_create();
    ASSERT_NE(termination_payload, nullptr);
    sdl3d_properties_set_string(termination_payload, "reason", "Test disconnect");
    ASSERT_TRUE(sdl3d_game_data_run_network_session_flow_event(client_data, &client_ctx, "client_match_terminated",
                                                               termination_payload, error, sizeof(error)))
        << error;
    sdl3d_properties_destroy(termination_payload);
    client_scene_state = sdl3d_game_data_scene_state(client_data);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_TRUE(sdl3d_properties_get_bool(client_scene_state, "network_match_termination_active", false));

    int select_action = -1;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_action(client_data, "menu_select", &select_action));
    sdl3d_input_manager *client_input = sdl3d_game_session_get_input(client_session);
    ASSERT_NE(client_input, nullptr);
    sdl3d_input_set_action_override(client_input, select_action, 1.0f);
    ASSERT_NE(sdl3d_input_update(client_input, 5000), nullptr);
    ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.25f));
    client_scene_state = sdl3d_game_data_scene_state(client_data);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_TRUE(sdl3d_properties_get_bool(client_scene_state, "network_match_termination_active", false));
    EXPECT_STREQ(sdl3d_game_data_active_scene(client_data), "scene.play");

    sdl3d_input_set_action_override(client_input, select_action, 0.0f);
    ASSERT_NE(sdl3d_input_update(client_input, 5001), nullptr);
    ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 2.74f));
    EXPECT_TRUE(sdl3d_properties_get_bool(client_scene_state, "network_match_termination_active", false));

    sdl3d_input_set_action_override(client_input, select_action, 1.0f);
    ASSERT_NE(sdl3d_input_update(client_input, 5002), nullptr);
    ASSERT_TRUE(sdl3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.02f));
    client_scene_state = sdl3d_game_data_scene_state(client_data);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_FALSE(sdl3d_properties_get_bool(client_scene_state, "network_match_termination_active", true));
    EXPECT_FALSE(client_ctx.paused);
    EXPECT_STREQ(sdl3d_game_data_active_scene(client_data), "scene.title");

    sdl3d_data_game_runtime_destroy(client_runtime);
    sdl3d_data_game_runtime_destroy(host_runtime);
    sdl3d_game_session_destroy(client_session);
    sdl3d_game_session_destroy(host_session);
}

TEST(GameDataRuntime, AuthoredNetworkSessionFlowEventsDriveSceneTransitions)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    const std::filesystem::path data_path = SDL3D_PONG_DATA_PATH;
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    sdl3d_data_game_runtime_desc desc{};
    sdl3d_data_game_runtime_desc_init(&desc);
    desc.session = session;
    desc.media_dir = SDL3D_MEDIA_DIR;
    desc.data_asset_path = asset_path.c_str();
    desc.mount_assets = mount_test_directory_assets;
    desc.mount_userdata = const_cast<char *>(root.c_str());

    char error[512]{};
    sdl3d_data_game_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error))) << error;
    sdl3d_game_data_runtime *data = sdl3d_data_game_runtime_data(runtime);
    ASSERT_NE(data, nullptr);

    sdl3d_game_context ctx{};
    ctx.session = session;

    ASSERT_TRUE(
        sdl3d_game_data_run_network_session_flow_event(data, &ctx, "client_start_game", nullptr, error, sizeof(error)))
        << error;
    EXPECT_STREQ(sdl3d_game_data_active_scene(data), "scene.play");
    const sdl3d_properties *scene_state = sdl3d_game_data_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "network_role", ""), "client");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "network_flow", ""), "direct");

    sdl3d_properties *payload = sdl3d_properties_create();
    ASSERT_NE(payload, nullptr);
    sdl3d_properties_set_string(payload, "reason", "Cable unplugged");
    ctx.paused = false;
    ASSERT_TRUE(sdl3d_game_data_run_network_session_flow_event(data, &ctx, "client_match_terminated", payload, error,
                                                               sizeof(error)))
        << error;
    sdl3d_properties_destroy(payload);
    scene_state = sdl3d_game_data_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_TRUE(ctx.paused);
    EXPECT_TRUE(sdl3d_properties_get_bool(scene_state, "network_match_termination_active", false));
    EXPECT_NE(SDL_strstr(sdl3d_properties_get_string(scene_state, "network_match_termination_message", ""),
                         "Cable unplugged"),
              nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""), "Cable unplugged");

    ASSERT_TRUE(sdl3d_game_data_run_network_session_flow_event(data, &ctx, "network_match_termination_ack", nullptr,
                                                               error, sizeof(error)))
        << error;
    scene_state = sdl3d_game_data_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_FALSE(ctx.paused);
    EXPECT_STREQ(sdl3d_game_data_active_scene(data), "scene.title");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "network_match_termination_active", true));
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "network_match_termination_message", "x"), "");

    sdl3d_data_game_runtime_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, NetworkSessionFlowPlaceholderMalformedBraceIsLiteral)
{
    const std::filesystem::path dir = unique_test_dir("network_flow_malformed_placeholder");
    const std::filesystem::path source = std::filesystem::path(SDL3D_PONG_DATA_PATH).parent_path();
    const std::filesystem::path dest = dir / "pong_data";
    std::filesystem::copy(source, dest,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);

    const std::filesystem::path game_path = dest / "pong.game.json";
    std::string game_json = read_text(game_path);
    const std::string marker = R"json("events": {)json";
    const size_t marker_pos = game_json.find(marker);
    ASSERT_NE(marker_pos, std::string::npos);
    game_json.insert(marker_pos + marker.size(), R"json(
        "malformed_placeholder": {
          "actions": [
            {
              "type": "scene_state.set",
              "key": "malformed_placeholder_result",
              "value": "literal {reason"
            }
          ]
        },
)json");
    write_text(game_path, game_json.c_str());

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);

    ASSERT_TRUE(sdl3d_game_data_run_network_session_flow_event(runtime, nullptr, "malformed_placeholder", nullptr,
                                                               error, sizeof(error)))
        << error;
    const sdl3d_properties *scene_state = sdl3d_game_data_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "malformed_placeholder_result", ""), "literal {reason");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReadsSpriteAssetMetadata)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(fixture_path("sprite_asset_fixture.game.json").c_str(), session, &runtime,
                                          error, sizeof(error)))
        << error;

    sdl3d_game_data_sprite_asset sprite{};
    ASSERT_TRUE(sdl3d_game_data_get_sprite_asset(runtime, "sprite.robot.walk", &sprite));
    EXPECT_STREQ(sprite.id, "sprite.robot.walk");
    EXPECT_STREQ(sprite.path, "asset://sprites/robot/walk.png");
    EXPECT_EQ(sprite.frame_width, 32);
    EXPECT_EQ(sprite.frame_height, 48);
    EXPECT_EQ(sprite.columns, 8);
    EXPECT_EQ(sprite.rows, 6);
    EXPECT_EQ(sprite.frame_count, 6);
    EXPECT_EQ(sprite.direction_count, 8);
    EXPECT_FLOAT_EQ(sprite.fps, 8.0f);
    EXPECT_TRUE(sprite.loop);
    EXPECT_TRUE(sprite.lighting);
    EXPECT_FALSE(sprite.emissive);
    EXPECT_FLOAT_EQ(sprite.visual_ground_offset, 0.125f);

    EXPECT_FALSE(sdl3d_game_data_get_sprite_asset(runtime, "sprite.missing", &sprite));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ExposesAuthoredPongPresentationData)
{
    const std::filesystem::path dir = unique_test_dir("pong_presentation");
    const std::filesystem::path user_root = dir / "user";
    const std::filesystem::path cache_root = dir / "cache";
    const std::filesystem::path game_path = copy_pong_data_with_storage_overrides(dir, user_root, cache_root);

    sdl3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(sdl3d_game_data_load_app_config_file(game_path.string().c_str(), &config, title, sizeof(title),
                                                     app_error, sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "SDL3D Pong");
    EXPECT_EQ(config.width, 0);
    EXPECT_EQ(config.height, 0);
    EXPECT_EQ(config.logical_width, 1280);
    EXPECT_EQ(config.logical_height, 720);
    EXPECT_EQ(config.backend, SDL3D_BACKEND_OPENGL);
    EXPECT_EQ(config.display_mode, SDL3D_WINDOW_MODE_WINDOWED);
    EXPECT_GT(config.vsync, 0);
    EXPECT_GT(config.maximized, 0);
    EXPECT_NEAR(config.tick_rate, 1.0f / 120.0f, 0.00001f);
    EXPECT_EQ(config.max_ticks_per_frame, 12);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    sdl3d_storage_config storage{};
    ASSERT_TRUE(sdl3d_game_data_get_storage_config(runtime, &storage));
    EXPECT_STREQ(storage.organization, "Blue Sentinel Security");
    EXPECT_STREQ(storage.application, "SDL3D Pong");
    EXPECT_STREQ(storage.profile, "default");
    const std::string user_root_text = user_root.generic_string();
    const std::string cache_root_text = cache_root.generic_string();
    EXPECT_STREQ(storage.user_root_override, user_root_text.c_str());
    EXPECT_STREQ(storage.cache_root_override, cache_root_text.c_str());
    char storage_root[256]{};
    ASSERT_TRUE(sdl3d_storage_build_root_path(&storage, SDL3D_STORAGE_PLATFORM_UNIX, SDL3D_STORAGE_ROOT_USER,
                                              "/home/player/.local/share", storage_root, sizeof(storage_root)));
    EXPECT_STREQ(storage_root, "/home/player/.local/share/Blue Sentinel Security/SDL3D Pong/profiles/default");

    sdl3d_camera3d camera{};
    ASSERT_TRUE(sdl3d_game_data_get_camera(runtime, "camera.overhead", &camera));
    EXPECT_EQ(camera.projection, SDL3D_CAMERA_ORTHOGRAPHIC);
    EXPECT_NEAR(camera.position.z, 16.0f, 0.0001f);
    EXPECT_NEAR(camera.fovy, 11.4f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_get_camera(runtime, "camera.ball_chase", &camera));
    EXPECT_EQ(camera.projection, SDL3D_CAMERA_PERSPECTIVE);
    EXPECT_NEAR(camera.fovy, 68.0f, 0.0001f);
    EXPECT_NEAR(camera.position.x, -2.6f, 0.0001f);
    EXPECT_NEAR(camera.position.z, 1.91f, 0.0001f);
    float chase_fovy = 0.0f;
    ASSERT_TRUE(sdl3d_game_data_get_camera_float(runtime, "camera.ball_chase", "fovy", &chase_fovy));
    EXPECT_NEAR(chase_fovy, 68.0f, 0.0001f);

    sdl3d_game_data_app_control app{};
    ASSERT_TRUE(sdl3d_game_data_get_app_control(runtime, &app));
    EXPECT_EQ(app.start_signal_id, -1);
    EXPECT_GE(app.quit_action_id, 0);
    EXPECT_GE(app.pause_action_id, 0);
    EXPECT_EQ(app.startup_transition, nullptr);
    EXPECT_STREQ(app.quit_transition, "quit");
    EXPECT_GE(app.quit_signal_id, 0);
    EXPECT_EQ(app.window_apply_signal_id, sdl3d_game_data_find_signal(runtime, "signal.settings.apply"));
    EXPECT_STREQ(app.window_settings_target, "entity.settings");
    EXPECT_STREQ(app.window_display_mode_key, "display_mode");
    EXPECT_STREQ(app.window_renderer_key, "renderer");
    EXPECT_STREQ(app.window_vsync_key, "vsync");

    sdl3d_registered_actor *match = sdl3d_game_data_find_actor(runtime, "entity.match");
    ASSERT_NE(match, nullptr);
    EXPECT_TRUE(sdl3d_game_data_app_pause_allowed(runtime, nullptr));
    sdl3d_properties_set_bool(match->props, "finished", true);
    EXPECT_FALSE(sdl3d_game_data_app_pause_allowed(runtime, nullptr));
    sdl3d_properties_set_bool(match->props, "finished", false);

    sdl3d_game_data_font_asset font{};
    ASSERT_TRUE(sdl3d_game_data_get_font_asset(runtime, "font.hud", &font));
    EXPECT_TRUE(font.builtin);
    EXPECT_EQ(font.builtin_id, SDL3D_BUILTIN_FONT_INTER);
    EXPECT_NEAR(font.size, 34.0f, 0.0001f);
    ASSERT_TRUE(sdl3d_game_data_get_font_asset(runtime, "font.title", &font));
    EXPECT_TRUE(font.builtin);
    EXPECT_NEAR(font.size, 96.0f, 0.0001f);

    float ambient[3]{};
    ASSERT_TRUE(sdl3d_game_data_get_world_ambient_light(runtime, ambient));
    EXPECT_NEAR(ambient[0], 0.015f, 0.0001f);
    EXPECT_NEAR(ambient[1], 0.018f, 0.0001f);
    EXPECT_NEAR(ambient[2], 0.026f, 0.0001f);

    EXPECT_EQ(sdl3d_game_data_world_light_count(runtime), 3);
    sdl3d_light red_light{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light(runtime, 0, &red_light));
    EXPECT_EQ(red_light.type, SDL3D_LIGHT_SPOT);
    EXPECT_NEAR(red_light.position.x, -8.15f, 0.0001f);
    EXPECT_NEAR(red_light.position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(red_light.direction.x, 0.0f, 0.0001f);
    EXPECT_NEAR(red_light.direction.z, -1.0f, 0.0001f);
    EXPECT_NEAR(red_light.color[0], 1.0f, 0.0001f);
    EXPECT_NEAR(red_light.color[1], 0.06f, 0.0001f);
    sdl3d_light red_eval{};
    sdl3d_game_data_render_eval red_light_eval{};
    red_light_eval.time = 1.0f;
    ASSERT_TRUE(sdl3d_game_data_get_world_light_evaluated(runtime, 0, &red_light_eval, &red_eval));
    EXPECT_GT(red_eval.color[1], red_light.color[1]);

    sdl3d_light blue_light{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light(runtime, 2, &blue_light));
    EXPECT_EQ(blue_light.type, SDL3D_LIGHT_SPOT);
    EXPECT_NEAR(blue_light.position.x, 8.15f, 0.0001f);
    EXPECT_NEAR(blue_light.position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(blue_light.position.z, 3.35f, 0.0001f);
    EXPECT_NEAR(blue_light.direction.x, 0.0f, 0.0001f);
    EXPECT_NEAR(blue_light.direction.z, -1.0f, 0.0001f);
    EXPECT_NEAR(blue_light.color[0], 0.08f, 0.0001f);
    EXPECT_NEAR(blue_light.color[1], 0.28f, 0.0001f);
    EXPECT_NEAR(blue_light.color[2], 1.0f, 0.0001f);
    sdl3d_light blue_eval{};
    sdl3d_game_data_render_eval blue_light_eval{};
    blue_light_eval.time = 1.0f;
    ASSERT_TRUE(sdl3d_game_data_get_world_light_evaluated(runtime, 2, &blue_light_eval, &blue_eval));
    EXPECT_GT(blue_eval.color[1], blue_light.color[1]);

    sdl3d_light lamp_light{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light(runtime, 1, &lamp_light));
    EXPECT_EQ(lamp_light.type, SDL3D_LIGHT_SPOT);
    EXPECT_NEAR(lamp_light.position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(lamp_light.position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(lamp_light.position.z, 2.92f, 0.0001f);
    EXPECT_NEAR(lamp_light.direction.z, -1.0f, 0.0001f);
    EXPECT_NEAR(lamp_light.range, 5.8f, 0.0001f);

    sdl3d_particle_config particles{};
    ASSERT_TRUE(sdl3d_game_data_get_particle_emitter(runtime, "entity.effect.ambient_particles", &particles));
    EXPECT_EQ(particles.shape, SDL3D_PARTICLE_EMITTER_BOX);
    EXPECT_EQ(particles.max_particles, 360);
    EXPECT_NEAR(particles.emit_rate, 95.0f, 0.0001f);
    EXPECT_EQ(particles.color_start.a, 105);
    sdl3d_vec3 particle_emissive{};
    ASSERT_TRUE(sdl3d_game_data_get_particle_emitter_draw_emissive(runtime, "entity.effect.ambient_particles",
                                                                   &particle_emissive));
    EXPECT_NEAR(particle_emissive.x, 0.8f, 0.0001f);

    ParticleCapture title_particles{};
    ASSERT_TRUE(sdl3d_game_data_for_each_particle_emitter(runtime, capture_particle, &title_particles));
    EXPECT_EQ(title_particles.count, 0);

    sdl3d_game_data_render_settings render{};
    ASSERT_TRUE(sdl3d_game_data_get_render_settings(runtime, &render));
    EXPECT_EQ(render.clear_color.r, 3);
    EXPECT_EQ(render.clear_color.g, 4);
    EXPECT_EQ(render.clear_color.b, 8);
    EXPECT_TRUE(render.lighting_enabled);
    EXPECT_TRUE(render.bloom_enabled);
    EXPECT_TRUE(render.ssao_enabled);
    EXPECT_EQ(render.tonemap, SDL3D_TONEMAP_ACES);

    sdl3d_game_data_transition_desc transition{};
    ASSERT_TRUE(sdl3d_game_data_get_transition(runtime, "quit", &transition));
    EXPECT_EQ(transition.type, SDL3D_TRANSITION_FADE);
    EXPECT_EQ(transition.direction, SDL3D_TRANSITION_OUT);
    EXPECT_NEAR(transition.duration, 0.45f, 0.0001f);
    EXPECT_GE(transition.done_signal_id, 0);

    RenderPrimitiveCapture title_capture{};
    ASSERT_TRUE(sdl3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &title_capture));
    EXPECT_EQ(title_capture.cubes, 0);
    EXPECT_EQ(title_capture.spheres, 0);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");
    EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.overhead");
    EXPECT_TRUE(sdl3d_game_data_active_scene_updates_game(runtime));
    EXPECT_TRUE(sdl3d_game_data_active_scene_renders_world(runtime));
    EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.ball"));
    EXPECT_EQ(sdl3d_timer_pool_active_count(sdl3d_game_session_get_timer_pool(session)), 1);

    ParticleCapture play_particles{};
    ASSERT_TRUE(sdl3d_game_data_for_each_particle_emitter(runtime, capture_particle, &play_particles));
    EXPECT_EQ(play_particles.count, 1);
    EXPECT_TRUE(play_particles.saw_ambient);

    sdl3d_game_data_particle_cache particle_cache{};
    sdl3d_game_data_particle_cache_init(&particle_cache);
    ASSERT_TRUE(sdl3d_game_data_update_particles(runtime, &particle_cache, 0.1f));
    EXPECT_EQ(particle_cache.count, 1);
    EXPECT_TRUE(particle_cache.entries[0].visible);
    sdl3d_game_data_particle_cache_free(&particle_cache);

    RenderPrimitiveCapture capture{};
    ASSERT_TRUE(sdl3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &capture));
    EXPECT_EQ(capture.cubes, 16);
    EXPECT_EQ(capture.spheres, 1);
    EXPECT_TRUE(capture.saw_player_paddle);
    EXPECT_TRUE(capture.saw_ball);
    EXPECT_NEAR(capture.ball_rotation_angle, 0.0f, 0.0001f);

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(ball, nullptr);
    sdl3d_properties_set_bool(ball->props, "active_motion", true);
    sdl3d_properties_set_float(ball->props, "spin_angle", 0.0f);
    sdl3d_game_context spin_context{};
    spin_context.session = session;
    sdl3d_game_data_frame_state spin_frame_state{};
    sdl3d_game_data_frame_state_init(&spin_frame_state);
    sdl3d_game_data_update_frame_desc spin_update{};
    spin_update.ctx = &spin_context;
    spin_update.runtime = runtime;
    spin_update.dt = 0.5f;
    ASSERT_TRUE(sdl3d_game_data_update_frame(&spin_frame_state, &spin_update));
    EXPECT_NEAR(sdl3d_properties_get_float(ball->props, "spin_angle", -1.0f), 2.7f, 0.0001f);

    RenderPrimitiveCapture spun_capture{};
    ASSERT_TRUE(sdl3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &spun_capture));
    EXPECT_TRUE(spun_capture.saw_ball);
    EXPECT_NEAR(spun_capture.ball_rotation_angle, 2.7f, 0.0001f);

    sdl3d_registered_actor *presentation = sdl3d_game_data_find_actor(runtime, "entity.presentation");
    ASSERT_NE(presentation, nullptr);
    EXPECT_NEAR(sdl3d_properties_get_float(presentation->props, "border_flash_decay", 0.0f), 2.8f, 0.0001f);
    sdl3d_properties_set_float(presentation->props, "border_flash", 1.0f);
    ASSERT_TRUE(sdl3d_game_data_update_property_effects(runtime, 0.25f));
    EXPECT_NEAR(sdl3d_properties_get_float(presentation->props, "border_flash", -1.0f), 0.3f, 0.0001f);
    sdl3d_properties_set_float(presentation->props, "border_flash", 1.0f);

    sdl3d_light base_light{};
    sdl3d_light flashed_light{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light(runtime, 1, &base_light));
    sdl3d_game_data_render_eval light_eval{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light_evaluated(runtime, 1, &light_eval, &flashed_light));
    EXPECT_GT(flashed_light.intensity, base_light.intensity);
    EXPECT_GT(flashed_light.range, base_light.range);
    sdl3d_game_data_render_eval render_eval{};
    render_eval.time = 0.25f;
    EvaluatedPrimitiveCapture evaluated{};
    ASSERT_TRUE(sdl3d_game_data_for_each_render_primitive_evaluated(runtime, &render_eval, capture_evaluated_primitive,
                                                                    &evaluated));
    EXPECT_TRUE(evaluated.saw_border);
    EXPECT_TRUE(evaluated.saw_ball);

    UiTextCapture ui{};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, capture_ui_text, &ui));
    EXPECT_EQ(ui.count, 8);
    EXPECT_TRUE(ui.saw_score);
    EXPECT_TRUE(ui.saw_pause);
    EXPECT_TRUE(ui.saw_network_match_terminated);

    sdl3d_game_data_ui_metrics metrics{};
    metrics.fps = 119.5f;
    metrics.frame = 42;
    char ui_buffer[128]{};
    auto format_score = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.score")
            return true;
        auto *args = static_cast<std::pair<sdl3d_game_data_runtime *, char *> *>(userdata);
        sdl3d_game_data_ui_metrics local_metrics{};
        EXPECT_TRUE(sdl3d_game_data_format_ui_text(args->first, text, &local_metrics, args->second, 128));
        return false;
    };
    std::pair<sdl3d_game_data_runtime *, char *> score_args{runtime, ui_buffer};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, format_score, &score_args));
    EXPECT_STREQ(ui_buffer, "00   00");
    auto find_pause_visible = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.pause")
            return true;
        auto *args =
            static_cast<std::tuple<sdl3d_game_data_runtime *, sdl3d_game_data_ui_metrics *, bool *> *>(userdata);
        *std::get<2>(*args) = sdl3d_game_data_ui_text_is_visible(std::get<0>(*args), text, std::get<1>(*args));
        return false;
    };
    bool pause_visible = false;
    metrics.paused = true;
    std::tuple<sdl3d_game_data_runtime *, sdl3d_game_data_ui_metrics *, bool *> pause_args{runtime, &metrics,
                                                                                           &pause_visible};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_pause_visible, &pause_args));
    EXPECT_TRUE(pause_visible);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_bool(scene_state, "network_match_termination_active", true);
    sdl3d_properties_set_string(scene_state, "network_match_termination_message",
                                "Match terminated: Client exited - Press Enter to return to title screen.");
    bool termination_visible = false;
    char termination_buffer[192]{};
    auto find_termination_visible = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.network.match_terminated")
            return true;
        auto *args = static_cast<std::tuple<sdl3d_game_data_runtime *, sdl3d_game_data_ui_metrics *, bool *, char *> *>(
            userdata);
        *std::get<2>(*args) = sdl3d_game_data_ui_text_is_visible(std::get<0>(*args), text, std::get<1>(*args));
        EXPECT_TRUE(
            sdl3d_game_data_format_ui_text(std::get<0>(*args), text, std::get<1>(*args), std::get<3>(*args), 192));
        return false;
    };
    std::tuple<sdl3d_game_data_runtime *, sdl3d_game_data_ui_metrics *, bool *, char *> termination_args{
        runtime, &metrics, &termination_visible, termination_buffer};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_termination_visible, &termination_args));
    EXPECT_TRUE(termination_visible);
    EXPECT_STREQ(termination_buffer, "Match terminated: Client exited - Press Enter to return to title screen.");

    bool pause_hidden = true;
    std::tuple<sdl3d_game_data_runtime *, sdl3d_game_data_ui_metrics *, bool *> pause_hidden_args{runtime, &metrics,
                                                                                                  &pause_hidden};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_pause_visible, &pause_hidden_args));
    EXPECT_FALSE(pause_hidden);

    sdl3d_properties_set_bool(scene_state, "network_match_termination_active", false);

    struct PauseMenuTextArgs
    {
        bool saw_resume = false;
        bool saw_options = false;
    } pause_menu_text;
    auto find_pause_menu_text = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *args = static_cast<PauseMenuTextArgs *>(userdata);
        if (std::string(text->name) != "ui.pause.menu")
            return true;
        const std::string value = text->text != nullptr ? text->text : "";
        args->saw_resume = args->saw_resume || value == "Resume";
        args->saw_options = args->saw_options || value == "Options";
        return !(args->saw_resume && args->saw_options);
    };
    ASSERT_TRUE(
        sdl3d_game_data_for_each_ui_text_for_metrics(runtime, &metrics, find_pause_menu_text, &pause_menu_text));
    EXPECT_TRUE(pause_menu_text.saw_resume);
    EXPECT_TRUE(pause_menu_text.saw_options);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ExposesDataDrivenScenesAndMenus)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_game_data_skip_policy skip{};
    ASSERT_TRUE(sdl3d_game_data_get_active_skip_policy(runtime, &skip));
    EXPECT_TRUE(skip.enabled);
    EXPECT_EQ(skip.input, SDL3D_GAME_DATA_SKIP_INPUT_ANY);
    EXPECT_STREQ(skip.scene, "scene.title");
    EXPECT_TRUE(skip.preserve_exit_transition);
    EXPECT_TRUE(skip.consume_input);

    sdl3d_game_data_image_asset image_asset{};
    ASSERT_TRUE(sdl3d_game_data_get_image_asset(runtime, "image.splash.logo", &image_asset));
    EXPECT_STREQ(image_asset.sprite, "sprite.splash.logo");
    EXPECT_EQ(image_asset.path, nullptr);

    sdl3d_game_data_image_asset ball_image_asset{};
    ASSERT_TRUE(sdl3d_game_data_get_image_asset(runtime, "image.ball.texture", &ball_image_asset));
    EXPECT_STREQ(ball_image_asset.path, "asset://images/ball-texture.png");
    EXPECT_EQ(ball_image_asset.sprite, nullptr);

    sdl3d_game_data_sprite_asset sprite_asset{};
    ASSERT_TRUE(sdl3d_game_data_get_sprite_asset(runtime, "sprite.splash.logo", &sprite_asset));
    EXPECT_STREQ(sprite_asset.path, "asset://images/splash-logo.jpg");
    EXPECT_EQ(sprite_asset.frame_width, 784);
    EXPECT_EQ(sprite_asset.frame_height, 1168);
    EXPECT_EQ(sprite_asset.columns, 1);
    EXPECT_EQ(sprite_asset.rows, 1);
    EXPECT_EQ(sprite_asset.frame_count, 1);
    EXPECT_EQ(sprite_asset.direction_count, 1);
    EXPECT_FALSE(sprite_asset.loop);
    EXPECT_FALSE(sprite_asset.lighting);
    EXPECT_STREQ(sprite_asset.effect, "melt");
    EXPECT_NEAR(sprite_asset.effect_delay, 1.0f, 0.0001f);
    EXPECT_NEAR(sprite_asset.effect_duration, 1.0f, 0.0001f);
    EXPECT_EQ(sprite_asset.shader_vertex_path, nullptr);
    EXPECT_STREQ(sprite_asset.shader_fragment_path, "asset://shaders/splash_logo_melt.frag.glsl");

    sdl3d_game_data_sound_asset sound_asset{};
    ASSERT_TRUE(sdl3d_game_data_get_sound_asset(runtime, "sound.pong.hit", &sound_asset));
    EXPECT_STREQ(sound_asset.path, "asset://audio/ui/click3.wav");
    EXPECT_EQ(sound_asset.bus, SDL3D_AUDIO_BUS_SOUND_EFFECTS);
    EXPECT_GT(sound_asset.volume, 0.0f);

    sdl3d_game_data_music_asset music_asset{};
    ASSERT_TRUE(sdl3d_game_data_get_music_asset(runtime, "music.title", &music_asset));
    EXPECT_STREQ(music_asset.path, "asset://audio/music/moonlight-sonata-allegretto.ogg");
    EXPECT_TRUE(music_asset.loop);
    EXPECT_GT(music_asset.volume, 0.0f);

    UiImageCapture images{};
    auto capture_image = [](void *userdata, const sdl3d_game_data_ui_image *image) -> bool {
        auto *capture = static_cast<UiImageCapture *>(userdata);
        capture->count++;
        if (image->name != nullptr && std::string(image->name) == "ui.splash.logo")
        {
            capture->saw_splash_logo = true;
            EXPECT_STREQ(image->image, "image.splash.logo");
            EXPECT_EQ(image->align, SDL3D_GAME_DATA_UI_ALIGN_CENTER);
            EXPECT_EQ(image->valign, SDL3D_GAME_DATA_UI_VALIGN_CENTER);
            EXPECT_TRUE(image->preserve_aspect);
        }
        return true;
    };
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_image(runtime, capture_image, &images));
    EXPECT_EQ(images.count, 1);
    EXPECT_TRUE(images.saw_splash_logo);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));
    EXPECT_TRUE(sdl3d_game_data_active_scene_updates_game(runtime));
    EXPECT_TRUE(sdl3d_game_data_active_scene_renders_world(runtime));
    EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.ball.attract"));
    EXPECT_FALSE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.ball"));

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.title");
    EXPECT_EQ(menu.item_count, 4);
    EXPECT_EQ(menu.selected_index, 0);
    EXPECT_GE(menu.up_action_id, 0);
    EXPECT_GE(menu.down_action_id, 0);
    EXPECT_GE(menu.select_action_id, 0);

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(sdl3d_game_data_active_menu_input_is_idle(runtime, input));
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);
    EXPECT_FALSE(sdl3d_game_data_active_menu_input_is_idle(runtime, input));
    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    EXPECT_TRUE(sdl3d_game_data_active_menu_input_is_idle(runtime, input));

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Single Player");
    EXPECT_STREQ(item.scene, "scene.play");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "single");

    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Multiplayer");
    EXPECT_STREQ(item.scene, "scene.multiplayer");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "multiplayer");

    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Options");
    EXPECT_STREQ(item.scene, "scene.options");
    EXPECT_FALSE(item.quit);

    ASSERT_TRUE(sdl3d_game_data_menu_move(runtime, menu.name, -1));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.selected_index, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, menu.selected_index, &item));
    EXPECT_STREQ(item.label, "Exit");
    EXPECT_TRUE(item.quit);

    ASSERT_EQ(sdl3d_game_data_scene_shortcut_count(runtime), 3);
    sdl3d_game_data_scene_shortcut shortcut{};
    ASSERT_TRUE(sdl3d_game_data_scene_shortcut_at(runtime, 2, &shortcut));
    EXPECT_STREQ(shortcut.action, "action.scene.play");
    EXPECT_STREQ(shortcut.scene, "scene.play");
    EXPECT_GE(shortcut.action_id, 0);

    sdl3d_game_data_transition_desc transition{};
    ASSERT_TRUE(sdl3d_game_data_get_scene_transition(runtime, "scene.title", "exit", &transition));
    EXPECT_EQ(transition.type, SDL3D_TRANSITION_FADE);
    EXPECT_EQ(transition.direction, SDL3D_TRANSITION_OUT);

    bool saw_title_cursor = false;
    bool saw_title_exit = false;
    auto find_title_menu_ui = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *flags = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.title.menu" && value == ">")
        {
            *flags->first = true;
        }
        if (name == "ui.title.menu" && value == "Exit")
        {
            *flags->second = true;
            EXPECT_FLOAT_EQ(text->x, 0.45f);
            EXPECT_EQ(text->align, SDL3D_GAME_DATA_UI_ALIGN_LEFT);
            EXPECT_TRUE(text->pulse_alpha);
        }
        if (*flags->first && *flags->second)
            return false;
        return true;
    };
    std::pair<bool *, bool *> title_menu_flags{&saw_title_cursor, &saw_title_exit};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_title_menu_ui, &title_menu_flags));
    EXPECT_TRUE(saw_title_cursor);
    EXPECT_TRUE(saw_title_exit);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer");
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Local");
    EXPECT_STREQ(item.scene, "scene.play");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "local");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "LAN");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "lan");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer.lan"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.lan");
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Create Match");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lobby");
    EXPECT_STREQ(item.scene_state_key, "network_flow");
    EXPECT_STREQ(item.scene_state_value, "host");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Join Match");
    EXPECT_STREQ(item.scene, "scene.multiplayer.join");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer.lobby"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.lobby.waiting");
    EXPECT_EQ(menu.item_count, 1);
    const int lobby_start_signal = sdl3d_game_data_find_signal(runtime, "signal.multiplayer.lobby.start");
    ASSERT_GE(lobby_start_signal, 0);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");
    sdl3d_properties_set_bool(sdl3d_game_data_mutable_scene_state(runtime), "multiplayer_host_connected", true);
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.lobby.connected");
    EXPECT_EQ(menu.item_count, 2);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Client 1");
    EXPECT_EQ(item.signal_id, lobby_start_signal);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer.join"));
    EXPECT_TRUE(sdl3d_game_data_active_scene_renders_world(runtime));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.join");
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Search for local matches");
    EXPECT_STREQ(item.scene, "scene.multiplayer.discovery");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Join match with IP address or hostname");
    EXPECT_STREQ(item.scene, "scene.multiplayer.direct_connect");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer.direct_connect"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.direct");
    EXPECT_EQ(menu.item_count, 5);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Host");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_TEXT);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Port");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_TEXT);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Connect");
    EXPECT_EQ(item.signal_id, sdl3d_game_data_find_signal(runtime, "signal.multiplayer.direct.connect"));
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 3, &item));
    EXPECT_STREQ(item.label, "Disconnect");
    EXPECT_EQ(item.signal_id, sdl3d_game_data_find_signal(runtime, "signal.multiplayer.direct.disconnect"));
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 4, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.join");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer.discovery"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.discovery");
    EXPECT_EQ(menu.item_count, 2);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_EQ(item.dynamic_list_index, -1);
    EXPECT_STREQ(item.label, "Searching local network...");
    EXPECT_EQ(item.signal_id, -1);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.join");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));
    EXPECT_FALSE(sdl3d_game_data_active_scene_updates_game(runtime));
    EXPECT_TRUE(sdl3d_game_data_active_scene_renders_world(runtime));
    EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.overhead");
    EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.options.background.base"));
    EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.options.flow.magenta"));
    EXPECT_FALSE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.ball"));

    RenderPrimitiveCapture options_render{};
    ASSERT_TRUE(sdl3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &options_render));
    EXPECT_EQ(options_render.cubes, 1);
    EXPECT_EQ(options_render.spheres, 3);
    EXPECT_TRUE(options_render.saw_options_background);
    EXPECT_TRUE(options_render.saw_options_glow);

    ParticleCapture options_particles{};
    ASSERT_TRUE(sdl3d_game_data_for_each_particle_emitter(runtime, capture_particle, &options_particles));
    EXPECT_EQ(options_particles.count, 3);
    EXPECT_TRUE(options_particles.saw_options_flow);

    sdl3d_game_data_render_eval options_eval{};
    options_eval.time = 1.0f;
    EvaluatedPrimitiveCapture options_evaluated{};
    ASSERT_TRUE(sdl3d_game_data_for_each_render_primitive_evaluated(runtime, &options_eval, capture_evaluated_primitive,
                                                                    &options_evaluated));
    EXPECT_TRUE(options_evaluated.saw_options_drift);

    const char *option_submenus[] = {"scene.options.display", "scene.options.keyboard", "scene.options.mouse",
                                     "scene.options.gamepad", "scene.options.audio"};
    for (const char *scene : option_submenus)
    {
        ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, scene));
        EXPECT_TRUE(sdl3d_game_data_active_scene_renders_world(runtime)) << scene;
        EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.overhead") << scene;
        EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.options.background.base")) << scene;
        EXPECT_FALSE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.ball")) << scene;
    }
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));

    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options");
    EXPECT_EQ(menu.item_count, 6);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Display");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "display");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Mouse");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "mouse");
    bool saw_options_display = false;
    bool saw_options_divider = false;
    auto find_options_display = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *flags = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.menu" && value == "Display")
        {
            *flags->first = true;
            EXPECT_FLOAT_EQ(text->x, 0.43f);
            EXPECT_FLOAT_EQ(text->y, 0.36f);
            EXPECT_EQ(text->align, SDL3D_GAME_DATA_UI_ALIGN_LEFT);
            EXPECT_TRUE(text->pulse_alpha);
        }
        if (name == "ui.options.title.divider" && value == "----------------")
        {
            *flags->second = true;
            EXPECT_FLOAT_EQ(text->x, 0.5f);
            EXPECT_FLOAT_EQ(text->y, 0.275f);
        }
        return !*flags->first || !*flags->second;
    };
    std::pair<bool *, bool *> options_display_flags{&saw_options_display, &saw_options_divider};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_options_display, &options_display_flags));
    EXPECT_TRUE(saw_options_display);
    EXPECT_TRUE(saw_options_divider);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.keyboard");
    EXPECT_EQ(menu.item_count, 9);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Up");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 2);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 5, &item));
    EXPECT_STREQ(item.label, "Cancel");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 1);

    bool saw_keyboard_up = false;
    bool saw_keyboard_cancel = false;
    auto find_keyboard_up = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *saw = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.keyboard.menu" && value == "Up: Up")
        {
            *saw->first = true;
            EXPECT_FLOAT_EQ(text->y, 0.29f);
        }
        if (name == "ui.options.keyboard.menu" && value == "Cancel: Backspace")
        {
            *saw->second = true;
        }
        return !*saw->first || !*saw->second;
    };
    std::pair<bool *, bool *> keyboard_binding_labels{&saw_keyboard_up, &saw_keyboard_cancel};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_keyboard_up, &keyboard_binding_labels));
    EXPECT_TRUE(saw_keyboard_up);
    EXPECT_TRUE(saw_keyboard_cancel);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.display"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.display");
    EXPECT_EQ(menu.item_count, 5);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Display Mode");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_CHOICE);
    EXPECT_STREQ(item.control_target, "entity.settings");
    EXPECT_STREQ(item.control_key, "display_mode");
    EXPECT_EQ(item.choice_count, 3);
    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "windowed");

    bool saw_options_value = false;
    auto find_options_value = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *saw = static_cast<bool *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.display.menu" && value == "Display Mode: Windowed")
        {
            *saw = true;
            EXPECT_FLOAT_EQ(text->x, 0.3f);
            EXPECT_EQ(text->align, SDL3D_GAME_DATA_UI_ALIGN_LEFT);
            return false;
        }
        return true;
    };
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_options_value, &saw_options_value));
    EXPECT_TRUE(saw_options_value);

    ASSERT_TRUE(sdl3d_game_data_apply_menu_item_control(runtime, &item));
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "fullscreen_exclusive");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 3, &item));
    EXPECT_STREQ(item.label, "Reset Settings");
    EXPECT_TRUE(sdl3d_game_data_app_signal_applies_window_settings(runtime, item.signal_id));
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 4, &item));
    EXPECT_STREQ(item.scene, "scene.options");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.audio"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.audio");
    EXPECT_EQ(menu.item_count, 4);
    EXPECT_GE(menu.left_action_id, 0);
    EXPECT_GE(menu.right_action_id, 0);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Sound Effects");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_RANGE);
    EXPECT_STREQ(item.control_target, "entity.settings");
    EXPECT_STREQ(item.control_key, "sfx_volume");
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "music_volume", 0), 7);

    bool saw_sfx_slider = false;
    bool saw_music_slider = false;
    auto find_audio_sliders = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *flags = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.audio.menu" && value == "Sound Effects  [########--] 8/10")
        {
            *flags->first = true;
            EXPECT_FLOAT_EQ(text->x, 0.34f);
            EXPECT_EQ(text->align, SDL3D_GAME_DATA_UI_ALIGN_LEFT);
        }
        if (name == "ui.options.audio.menu" && value == "Music  [#######---] 7/10")
            *flags->second = true;
        if (*flags->first && *flags->second)
            return false;
        return true;
    };
    std::pair<bool *, bool *> audio_slider_flags{&saw_sfx_slider, &saw_music_slider};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_audio_sliders, &audio_slider_flags));
    EXPECT_TRUE(saw_sfx_slider);
    EXPECT_TRUE(saw_music_slider);

    ScenePayloadCapture payload_capture{};
    const int start_signal = sdl3d_game_data_find_signal(runtime, "signal.game.start");
    ASSERT_GE(start_signal, 0);
    ASSERT_NE(sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(session), start_signal, capture_scene_payload,
                                   &payload_capture),
              0);

    sdl3d_properties *payload = sdl3d_properties_create();
    ASSERT_NE(payload, nullptr);
    sdl3d_properties_set_string(payload, "from_scene", "scene.options");
    sdl3d_properties_set_string(payload, "selected_level", "level.test");
    sdl3d_input_manager *play_input = sdl3d_game_session_get_input(session);
    ASSERT_NE(play_input, nullptr);
    const int remote_up_action = sdl3d_game_data_find_action(runtime, "action.paddle.local.up");
    ASSERT_GE(remote_up_action, 0);
    sdl3d_input_set_action_override(play_input, remote_up_action, 1.0f);
    ASSERT_NE(sdl3d_input_update(play_input, 10), nullptr);
    EXPECT_NEAR(sdl3d_input_get_value(play_input, remote_up_action), 1.0f, 0.0001f);
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));
    ASSERT_TRUE(sdl3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload));
    ASSERT_NE(sdl3d_input_update(play_input, 11), nullptr);
    EXPECT_NEAR(sdl3d_input_get_value(play_input, remote_up_action), 0.0f, 0.0001f);
    EXPECT_TRUE(payload_capture.called);
    EXPECT_EQ(payload_capture.from_scene, "scene.options");
    EXPECT_EQ(payload_capture.to_scene, "scene.play");
    EXPECT_EQ(payload_capture.selected_level, "level.test");
    sdl3d_properties_destroy(payload);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "match_mode", ""), "single");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "network_role", ""), "none");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "network_flow", ""), "none");

    sdl3d_properties *lan_payload = sdl3d_properties_create();
    ASSERT_NE(lan_payload, nullptr);
    sdl3d_properties_set_string(lan_payload, "match_mode", "lan");
    sdl3d_properties_set_string(lan_payload, "network_role", "client");
    sdl3d_properties_set_string(lan_payload, "network_flow", "direct");
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(sdl3d_game_data_set_active_scene_with_payload(runtime, "scene.play", lan_payload));
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "network_role", ""), "client");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "network_flow", ""), "direct");
    sdl3d_properties_destroy(lan_payload);

    sdl3d_properties_set_string(scene_state, "selected_level", "level.002");
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "selected_level", ""), "level.002");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ResolvesRuntimeUiStateForTextAndImages)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_game_data_ui_image logo{};
    bool saw_logo = false;
    auto find_logo = [](void *userdata, const sdl3d_game_data_ui_image *image) -> bool {
        auto *args = static_cast<std::pair<sdl3d_game_data_ui_image *, bool *> *>(userdata);
        if (image->name != nullptr && std::string(image->name) == "ui.splash.logo")
        {
            *args->first = *image;
            *args->second = true;
            return false;
        }
        return true;
    };
    std::pair<sdl3d_game_data_ui_image *, bool *> logo_args{&logo, &saw_logo};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_image(runtime, find_logo, &logo_args));
    ASSERT_TRUE(saw_logo);
    EXPECT_EQ(logo.effect, nullptr);
    EXPECT_NEAR(logo.effect_speed, 1.0f, 0.0001f);

    sdl3d_game_data_ui_state image_state{};
    sdl3d_game_data_ui_state_init(&image_state);
    image_state.flags = SDL3D_GAME_DATA_UI_STATE_OFFSET | SDL3D_GAME_DATA_UI_STATE_SCALE |
                        SDL3D_GAME_DATA_UI_STATE_ALPHA | SDL3D_GAME_DATA_UI_STATE_TINT;
    image_state.offset_x = 0.10f;
    image_state.offset_y = -0.05f;
    image_state.scale = 0.5f;
    image_state.alpha = 0.25f;
    image_state.tint = {128, 64, 255, 200};
    ASSERT_TRUE(sdl3d_game_data_set_ui_state(runtime, "ui.splash.logo", &image_state));

    sdl3d_game_data_ui_state stored_image_state{};
    ASSERT_TRUE(sdl3d_game_data_get_ui_state(runtime, "ui.splash.logo", &stored_image_state));
    EXPECT_EQ(stored_image_state.flags, image_state.flags);
    EXPECT_NEAR(stored_image_state.scale, 0.5f, 0.0001f);

    sdl3d_game_data_ui_image resolved_logo{};
    bool logo_visible = false;
    ASSERT_TRUE(sdl3d_game_data_resolve_ui_image(runtime, &logo, nullptr, &resolved_logo, &logo_visible));
    EXPECT_TRUE(logo_visible);
    EXPECT_NEAR(resolved_logo.x, 0.60f, 0.0001f);
    EXPECT_NEAR(resolved_logo.y, 0.45f, 0.0001f);
    EXPECT_NEAR(resolved_logo.scale, 0.5f, 0.0001f);
    EXPECT_EQ(resolved_logo.color.r, 128);
    EXPECT_EQ(resolved_logo.color.g, 64);
    EXPECT_EQ(resolved_logo.color.b, 255);
    EXPECT_EQ(resolved_logo.color.a, 50);
    EXPECT_EQ(resolved_logo.effect, nullptr);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));

    sdl3d_game_data_ui_text pause{};
    bool saw_pause = false;
    auto find_pause = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *args = static_cast<std::pair<sdl3d_game_data_ui_text *, bool *> *>(userdata);
        if (text->name != nullptr && std::string(text->name) == "ui.pause")
        {
            *args->first = *text;
            *args->second = true;
            return false;
        }
        return true;
    };
    std::pair<sdl3d_game_data_ui_text *, bool *> pause_args{&pause, &saw_pause};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_pause, &pause_args));
    ASSERT_TRUE(saw_pause);

    sdl3d_game_data_ui_metrics metrics{};
    metrics.paused = false;
    EXPECT_FALSE(sdl3d_game_data_ui_text_is_visible(runtime, &pause, &metrics));

    sdl3d_game_data_ui_state text_state{};
    sdl3d_game_data_ui_state_init(&text_state);
    text_state.flags = SDL3D_GAME_DATA_UI_STATE_VISIBLE | SDL3D_GAME_DATA_UI_STATE_OFFSET |
                       SDL3D_GAME_DATA_UI_STATE_SCALE | SDL3D_GAME_DATA_UI_STATE_ALPHA | SDL3D_GAME_DATA_UI_STATE_TINT;
    text_state.visible = true;
    text_state.offset_x = 0.02f;
    text_state.offset_y = -0.03f;
    text_state.scale = 2.0f;
    text_state.alpha = 0.5f;
    text_state.tint = {128, 255, 64, 128};
    ASSERT_TRUE(sdl3d_game_data_set_ui_state(runtime, "ui.pause", &text_state));

    sdl3d_game_data_ui_text resolved_pause{};
    bool pause_visible = false;
    ASSERT_TRUE(sdl3d_game_data_resolve_ui_text(runtime, &pause, &metrics, &resolved_pause, &pause_visible));
    EXPECT_TRUE(pause_visible);
    EXPECT_NEAR(resolved_pause.x, 0.52f, 0.0001f);
    EXPECT_NEAR(resolved_pause.y, 0.32f, 0.0001f);
    EXPECT_NEAR(resolved_pause.scale, 2.0f, 0.0001f);
    EXPECT_EQ(resolved_pause.color.r, 123);
    EXPECT_EQ(resolved_pause.color.g, 248);
    EXPECT_EQ(resolved_pause.color.b, 64);
    EXPECT_EQ(resolved_pause.color.a, 64);

    text_state.visible = false;
    ASSERT_TRUE(sdl3d_game_data_set_ui_state(runtime, "ui.pause", &text_state));
    EXPECT_FALSE(sdl3d_game_data_ui_text_is_visible(runtime, &pause, &metrics));

    EXPECT_TRUE(sdl3d_game_data_clear_ui_state(runtime, "ui.pause"));
    EXPECT_FALSE(sdl3d_game_data_get_ui_state(runtime, "ui.pause", &text_state));
    sdl3d_game_data_clear_ui_states(runtime);
    EXPECT_FALSE(sdl3d_game_data_get_ui_state(runtime, "ui.splash.logo", &image_state));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DataAuthoredInputPolicyUpdatePhasesAndPresentationClocks)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    const int pause = sdl3d_game_data_find_action(runtime, "action.pause");
    const int scene_play = sdl3d_game_data_find_action(runtime, "action.scene.play");
    ASSERT_GE(pause, 0);
    ASSERT_GE(scene_play, 0);
    EXPECT_FALSE(sdl3d_game_data_active_scene_allows_action(runtime, pause));
    EXPECT_TRUE(sdl3d_game_data_active_scene_allows_action(runtime, scene_play));

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));
    EXPECT_TRUE(sdl3d_game_data_active_scene_allows_action(runtime, pause));
    EXPECT_TRUE(sdl3d_game_data_active_scene_allows_action(
        runtime, sdl3d_game_data_find_action(runtime, "action.paddle.local.up")));
    EXPECT_TRUE(sdl3d_game_data_active_scene_allows_action(
        runtime, sdl3d_game_data_find_action(runtime, "action.paddle.local.down")));
    EXPECT_TRUE(sdl3d_game_data_active_scene_update_phase(runtime, "presentation", true));
    EXPECT_FALSE(sdl3d_game_data_active_scene_update_phase(runtime, "simulation", true));

    sdl3d_registered_actor *presentation = sdl3d_game_data_find_actor(runtime, "entity.presentation");
    ASSERT_NE(presentation, nullptr);
    sdl3d_game_context ctx{};
    ctx.session = session;

    sdl3d_game_data_frame_state frame_state{};
    sdl3d_game_data_frame_state_init(&frame_state);
    sdl3d_game_data_update_frame_desc update{};
    update.ctx = &ctx;
    update.runtime = runtime;
    update.dt = 0.25f;
    ASSERT_TRUE(sdl3d_game_data_update_frame(&frame_state, &update));
    EXPECT_NEAR(frame_state.time, 0.25f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(presentation->props, "pause_flash", -1.0f), 0.0f, 0.0001f);

    ctx.paused = true;
    update.dt = 0.1f;
    ASSERT_TRUE(sdl3d_game_data_update_frame(&frame_state, &update));
    EXPECT_NEAR(sdl3d_properties_get_float(presentation->props, "pause_flash", -1.0f), 0.3f, 0.0001f);
    EXPECT_NEAR(sdl3d_game_data_ui_pulse_phase(runtime, -1.0f), 0.3f, 0.0001f);

    sdl3d_game_data_scene_transition_policy policy{};
    ASSERT_TRUE(sdl3d_game_data_get_scene_transition_policy(runtime, &policy));
    EXPECT_FALSE(policy.allow_same_scene);
    EXPECT_FALSE(policy.allow_interrupt);
    EXPECT_TRUE(policy.reset_menu_input_on_request);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppliesAuthoredPongPlayInputProfiles)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int p1_up = sdl3d_game_data_find_action(runtime, "action.paddle.up");
    const int p2_up = sdl3d_game_data_find_action(runtime, "action.paddle.local.up");
    ASSERT_GE(p1_up, 0);
    ASSERT_GE(p2_up, 0);

    ASSERT_TRUE(sdl3d_game_data_apply_input_profile(runtime, input, "profile.pong.play.single", error, sizeof(error)))
        << error;
    error[0] = '\0';
    EXPECT_FALSE(
        sdl3d_game_data_apply_input_profile(runtime, input, "profile.pong.play.missing", error, sizeof(error)));
    EXPECT_NE(std::string(error).find("profile.pong.play.missing"), std::string::npos);
    error[0] = '\0';

    const char *profile_name = nullptr;
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    ASSERT_STREQ(profile_name, "profile.pong.play.single");

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 0.0f);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "match_mode", "lan");
    sdl3d_properties_set_string(scene_state, "network_role", "client");
    sdl3d_properties_set_string(scene_state, "network_flow", "direct");
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    ASSERT_STREQ(profile_name, "profile.pong.play.lan.client");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 1.0f);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 4);

    sdl3d_properties_set_string(scene_state, "match_mode", "local");
    sdl3d_properties_set_string(scene_state, "network_role", "none");
    sdl3d_properties_set_string(scene_state, "network_flow", "none");
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    ASSERT_STREQ(profile_name, "profile.pong.play.local.keyboard_only");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 5);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 0.0f);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 6);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppliesAuthoredPongGamepadAssignmentPolicies)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    if (sdl3d_input_gamepad_count(input) != 0)
    {
        sdl3d_game_data_destroy(runtime);
        sdl3d_game_session_destroy(session);
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }

    const int p1_up = sdl3d_game_data_find_action(runtime, "action.paddle.up");
    const int p2_up = sdl3d_game_data_find_action(runtime, "action.paddle.local.up");
    ASSERT_GE(p1_up, 0);
    ASSERT_GE(p2_up, 0);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "match_mode", "local");
    sdl3d_properties_set_string(scene_state, "network_role", "none");
    sdl3d_properties_set_string(scene_state, "network_flow", "none");

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7101;
    sdl3d_input_process_event(input, &event);

    const char *profile_name = nullptr;
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    EXPECT_STREQ(profile_name, "profile.pong.play.local.one_gamepad");

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7101;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 1.0f);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 2);

    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7102;
    sdl3d_input_process_event(input, &event);
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    EXPECT_STREQ(profile_name, "profile.pong.play.local.two_gamepads");

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7101;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 3);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 0.0f);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 4);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7102;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 5);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, p2_up), 1.0f);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RefreshesActiveInputProfileWhenGamepadCountChanges)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    if (sdl3d_input_gamepad_count(input) != 0)
    {
        sdl3d_game_data_destroy(runtime);
        sdl3d_game_session_destroy(session);
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "match_mode", "local");
    sdl3d_properties_set_string(scene_state, "network_role", "none");
    sdl3d_properties_set_string(scene_state, "network_flow", "none");

    sdl3d_game_data_input_profile_refresh_state refresh{};
    sdl3d_game_data_input_profile_refresh_state_init(&refresh);

    const char *profile_name = nullptr;
    bool applied = false;
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                            &applied, error, sizeof(error)))
        << error;
    EXPECT_TRUE(applied);
    EXPECT_STREQ(profile_name, "profile.pong.play.local.keyboard_only");
    EXPECT_EQ(refresh.gamepad_count, 0);

    profile_name = "unchanged";
    applied = true;
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                            &applied, error, sizeof(error)))
        << error;
    EXPECT_FALSE(applied);
    EXPECT_EQ(profile_name, nullptr);

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7201;
    sdl3d_input_process_event(input, &event);

    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                            &applied, error, sizeof(error)))
        << error;
    EXPECT_TRUE(applied);
    EXPECT_STREQ(profile_name, "profile.pong.play.local.one_gamepad");
    EXPECT_EQ(refresh.gamepad_count, 1);

    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7202;
    sdl3d_input_process_event(input, &event);

    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                            &applied, error, sizeof(error)))
        << error;
    EXPECT_TRUE(applied);
    EXPECT_STREQ(profile_name, "profile.pong.play.local.two_gamepads");
    EXPECT_EQ(refresh.gamepad_count, 2);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppliesFallbackAndMouseInputProfiles)
{
    const std::filesystem::path dir = unique_test_dir("input_profile_mouse");
    write_text(dir / "mouse_profile.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Mouse Profile", "id": "test.mouse_profile", "version": "0.1.0" },
  "world": { "name": "world.mouse_profile", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.gameplay",
        "actions": [
          { "name": "action.pointer.primary" }
        ]
      }
    ],
    "profiles": [
      {
        "name": "profile.requires_gamepad",
        "min_gamepads": 1,
        "bindings": [
          { "action": "action.pointer.primary", "device": "keyboard", "key": "SPACE" }
        ]
      },
      {
        "name": "profile.fallback.mouse",
        "unbind": [ "action.pointer.primary" ],
        "bindings": [
          { "action": "action.pointer.primary", "device": "mouse", "button": "LEFT" }
        ]
      }
    ]
  },
  "entities": []
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "mouse_profile.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int action = sdl3d_game_data_find_action(runtime, "action.pointer.primary");
    ASSERT_GE(action, 0);

    const char *profile_name = nullptr;
    ASSERT_TRUE(sdl3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    EXPECT_STREQ(profile_name, "profile.fallback.mouse");

    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_LEFT;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, action), 1.0f);

    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 2);
    EXPECT_FLOAT_EQ(sdl3d_input_get_value(input, action), 0.0f);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, MenuControllerConsumesAuthoredMenuInput)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    bool armed = false;
    sdl3d_game_data_menu_update_result result{};
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(armed);
    EXPECT_FALSE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_STREQ(result.menu, "menu.title");
    EXPECT_EQ(result.selected_index, 0);
    EXPECT_EQ(result.move_signal_id, -1);
    EXPECT_EQ(result.select_signal_id, -1);

    const int menu_move_signal = sdl3d_game_data_find_signal(runtime, "signal.ui.menu.move");
    const int menu_select_signal = sdl3d_game_data_find_signal(runtime, "signal.ui.menu.select");
    ASSERT_GE(menu_move_signal, 0);
    ASSERT_GE(menu_select_signal, 0);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_DOWN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_STREQ(result.menu, "menu.title");
    EXPECT_EQ(result.selected_index, 1);
    EXPECT_EQ(result.move_signal_id, menu_move_signal);
    EXPECT_EQ(result.select_signal_id, -1);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);

    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_FALSE(result.quit);
    EXPECT_STREQ(result.menu, "menu.title");
    EXPECT_EQ(result.selected_index, 1);
    EXPECT_STREQ(result.scene, "scene.multiplayer");
    EXPECT_EQ(result.signal_id, -1);
    EXPECT_EQ(result.move_signal_id, -1);
    EXPECT_EQ(result.select_signal_id, menu_select_signal);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, MenuTextEntryCapturesEditingInput)
{
    const std::filesystem::path dir = unique_test_dir("menu_text_entry");
    write_text(dir / "text_entry.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Text Entry", "id": "test.text_entry", "version": "0.1.0" },
  "world": { "name": "world.text_entry", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.ui",
        "actions": [
          { "name": "action.menu.select", "bindings": [{ "device": "keyboard", "key": "RETURN" }] },
          { "name": "action.menu.back", "bindings": [{ "device": "keyboard", "key": "ESCAPE" }] },
          { "name": "action.menu.up", "bindings": [{ "device": "keyboard", "key": "UP" }] },
          { "name": "action.menu.down", "bindings": [{ "device": "keyboard", "key": "DOWN" }] }
        ]
      }
    ]
  },
  "entities": [],
  "scenes": { "initial": "scene.form", "files": ["scenes/form.scene.json"] }
})json");
    write_text(dir / "scenes" / "form.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.form",
  "input": { "actions": ["action.menu.select", "action.menu.back", "action.menu.up", "action.menu.down"] },
  "menus": [
    {
      "name": "menu.form",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "back_action": "action.menu.back",
      "items": [
        {
          "label": "Host",
          "control": {
            "type": "text",
            "target": "scene_state",
            "key": "host",
            "default": "",
            "placeholder": "Host / IP",
            "charset": "hostname",
            "max_length": 32
          }
        },
        { "label": "Back", "return_scene": true }
      ]
    }
  ]
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "text_entry.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;
    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    ASSERT_STREQ(menu.name, "menu.form");
    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_TEXT);

    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 1);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_capture_started);
    EXPECT_TRUE(sdl3d_game_data_menu_text_entry_capture_active(runtime));

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "host local@1"
                      "\xC3"
                      "\xA9"
                      "\xF0"
                      "\x9F"
                      "\x98"
                      "\x80";
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_changed);
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "host", ""), "hostlocal1");

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DOWN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 4);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.selected_index, 0);

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 5);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_BACKSPACE;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 6);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "host", ""), "hostlocal");

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 7);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DELETE;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 8);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "host", ""), "hostloca");

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 9);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 10);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_canceled);
    EXPECT_FALSE(sdl3d_game_data_menu_text_entry_capture_active(runtime));
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "host", ""), "");

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 11);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 12);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_capture_started);

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 13);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "192.168.1.20:27183";
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 14);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 15);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_submitted);
    EXPECT_FALSE(sdl3d_game_data_menu_text_entry_capture_active(runtime));
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "host", ""), "192.168.1.20:27183");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidMenuTextControlSchema)
{
    const std::filesystem::path dir = unique_test_dir("menu_text_entry_validation");
    const auto write_case = [&](const char *name, const char *control_json) {
        write_text(dir / (std::string(name) + ".game.json"),
                   R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Text Entry Validation", "id": "test.text_entry_validation", "version": "0.1.0" },
  "world": { "name": "world.text_entry_validation", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.ui",
        "actions": [
          { "name": "action.menu.select", "bindings": [{ "device": "keyboard", "key": "RETURN" }] },
          { "name": "action.menu.up", "bindings": [{ "device": "keyboard", "key": "UP" }] },
          { "name": "action.menu.down", "bindings": [{ "device": "keyboard", "key": "DOWN" }] }
        ]
      }
    ]
  },
  "entities": [],
  "scenes": { "initial": "scene.form", "files": ["scenes/form.scene.json"] }
})json");
        write_text(dir / "scenes" / "form.scene.json", (std::string(R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.form",
  "input": { "actions": ["action.menu.select", "action.menu.up", "action.menu.down"] },
  "menus": [
    {
      "name": "menu.form",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "items": [
        { "label": "Host", "control": )json") + control_json +
                                                        R"json( }
      ]
    }
  ]
})json")
                                                           .c_str());
    };

    char error[512]{};
    write_case("alias", R"json({ "type": "text_entry", "target": "scene_state", "key": "host" })json");
    EXPECT_FALSE(
        sdl3d_game_data_validate_file((dir / "alias.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("menu item control requires type"), std::string::npos) << error;

    error[0] = '\0';
    write_case("too_long", R"json({ "type": "text", "target": "scene_state", "key": "host", "max_length": 256 })json");
    EXPECT_FALSE(
        sdl3d_game_data_validate_file((dir / "too_long.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("max_length must be 255 bytes or fewer"), std::string::npos) << error;

    remove_test_dir(dir);
}

TEST(GameDataRuntime, DynamicListMenuUsesIndexedSceneStateEntries)
{
    const std::filesystem::path dir = unique_test_dir("menu_dynamic_list");
    write_text(dir / "dynamic_list.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Dynamic List", "id": "test.dynamic_list", "version": "0.1.0" },
  "world": { "name": "world.dynamic_list", "kind": "fixed_screen" },
  "assets": { "fonts": [{ "id": "font.hud", "builtin": "Inter", "size": 18 }] },
  "input": {
    "contexts": [
      {
        "name": "input.ui",
        "actions": [
          { "name": "action.menu.select", "bindings": [{ "device": "keyboard", "key": "RETURN" }] },
          { "name": "action.menu.back", "bindings": [{ "device": "keyboard", "key": "ESCAPE" }] },
          { "name": "action.menu.up", "bindings": [{ "device": "keyboard", "key": "UP" }] },
          { "name": "action.menu.down", "bindings": [{ "device": "keyboard", "key": "DOWN" }] }
        ]
      }
    ]
  },
  "signals": ["signal.session.join"],
  "entities": [],
  "scenes": { "initial": "scene.browser", "files": ["scenes/browser.scene.json"] }
})json");
    write_text(dir / "scenes" / "browser.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.browser",
  "input": {
    "actions": ["action.menu.select", "action.menu.back", "action.menu.up", "action.menu.down"]
  },
  "menus": [
    {
      "name": "menu.sessions",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "back_action": "action.menu.back",
      "items": [
        {
          "type": "dynamic_list",
          "name": "list.sessions",
          "source": {
            "type": "scene_state_indexed",
            "count_key": "session_count",
            "label_key_format": "session_%d_label",
            "value_key_format": "session_%d_value"
          },
          "empty_label": "No sessions discovered",
          "label_format": "Join {label}",
          "selected_index_key": "selected_session_index",
          "selected_value_key": "selected_session_live",
          "scene_state": { "key": "selected_session", "value_from": "value" },
          "signal": "signal.session.join"
        },
        { "label": "Back", "return_scene": true }
      ]
    }
  ],
  "ui": {
    "menus": [
      {
        "name": "ui.sessions",
        "menu": "menu.sessions",
        "font": "font.hud",
        "x": 0.5,
        "y": 0.2,
        "gap": 0.1,
        "normalized": true,
        "visible_count": 2
      }
    ]
  }
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "dynamic_list.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.sessions");
    EXPECT_EQ(menu.item_count, 2);

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_EQ(item.dynamic_list_index, -1);
    EXPECT_STREQ(item.label, "No sessions discovered");
    EXPECT_EQ(item.signal_id, -1);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_int(scene_state, "session_count", 4);
    sdl3d_properties_set_string(scene_state, "session_0_label", "Alpha");
    sdl3d_properties_set_string(scene_state, "session_0_value", "10.0.0.1:27183");
    sdl3d_properties_set_string(scene_state, "session_1_label", "Beta");
    sdl3d_properties_set_string(scene_state, "session_1_value", "10.0.0.2:27183");
    sdl3d_properties_set_string(scene_state, "session_2_label", "Gamma");
    sdl3d_properties_set_string(scene_state, "session_2_value", "10.0.0.3:27183");
    sdl3d_properties_set_string(scene_state, "session_3_label", "Delta");
    sdl3d_properties_set_string(scene_state, "session_3_value", "10.0.0.4:27183");

    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 5);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_STREQ(item.dynamic_list_name, "list.sessions");
    EXPECT_EQ(item.dynamic_list_index, 1);
    EXPECT_STREQ(item.dynamic_list_value, "10.0.0.2:27183");
    EXPECT_STREQ(item.label, "Join Beta");
    sdl3d_game_data_menu_item first_dynamic_item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &first_dynamic_item));
    sdl3d_game_data_menu_item second_dynamic_item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &second_dynamic_item));
    EXPECT_STREQ(first_dynamic_item.label, "Join Alpha");
    EXPECT_STREQ(second_dynamic_item.label, "Join Beta");
    EXPECT_STREQ(first_dynamic_item.dynamic_list_value, "10.0.0.1:27183");
    EXPECT_STREQ(second_dynamic_item.dynamic_list_value, "10.0.0.2:27183");

    struct MenuLabels
    {
        std::vector<std::string> labels;
    } labels;
    auto collect_menu_labels = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *capture = static_cast<MenuLabels *>(userdata);
        if (std::string(text->name) == "ui.sessions")
            capture->labels.emplace_back(text->text);
        return true;
    };
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, collect_menu_labels, &labels));
    ASSERT_EQ(labels.labels.size(), 2U);
    EXPECT_EQ(labels.labels[0], "Join Alpha");
    EXPECT_EQ(labels.labels[1], "Join Beta");

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DOWN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 1);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_EQ(result.selected_index, 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "selected_session_index", -1), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "selected_session_live", ""), "10.0.0.2:27183");

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_EQ(result.signal_id, sdl3d_game_data_find_signal(runtime, "signal.session.join"));
    EXPECT_STREQ(result.scene_state_key, "selected_session");
    EXPECT_STREQ(result.scene_state_value, "10.0.0.2:27183");

    sdl3d_properties_set_int(scene_state, "session_count", 1);
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 2);
    EXPECT_EQ(menu.selected_index, 1);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, DynamicListMenuReadsRuntimeCollections)
{
    const std::filesystem::path dir = unique_test_dir("menu_dynamic_runtime_collection");
    write_text(dir / "dynamic_collection.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Dynamic Runtime Collection", "id": "test.dynamic_runtime_collection", "version": "0.1.0" },
  "world": { "name": "world.dynamic_runtime_collection", "kind": "fixed_screen" },
  "assets": { "fonts": [{ "id": "font.hud", "builtin": "Inter", "size": 18 }] },
  "input": {
    "contexts": [
      {
        "name": "input.ui",
        "actions": [
          { "name": "action.menu.select", "bindings": [{ "device": "keyboard", "key": "RETURN" }] },
          { "name": "action.menu.up", "bindings": [{ "device": "keyboard", "key": "UP" }] },
          { "name": "action.menu.down", "bindings": [{ "device": "keyboard", "key": "DOWN" }] }
        ]
      }
    ]
  },
  "signals": ["signal.session.inspect"],
  "entities": [],
  "scenes": { "initial": "scene.browser", "files": ["scenes/browser.scene.json"] }
})json");
    write_text(dir / "scenes" / "browser.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.browser",
  "input": { "actions": ["action.menu.select", "action.menu.up", "action.menu.down"] },
  "menus": [
    {
      "name": "menu.sessions",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "items": [
        {
          "type": "dynamic_list",
          "name": "list.sessions",
          "source": {
            "type": "runtime_collection",
            "collection": "local_matches",
            "label_field": "name",
            "value_field": "latency_ms"
          },
          "empty_label": "No runtime rows",
          "label_format": "{label}",
          "selected_index_key": "selected_runtime_index",
          "selected_value_key": "selected_runtime_latency",
          "scene_state": { "key": "selected_runtime_latency_on_accept", "value_from": "value" },
          "signal": "signal.session.inspect"
        },
        { "label": "Back", "return_scene": true }
      ]
    }
  ]
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "dynamic_collection.game.json").string().c_str(), session, &runtime,
                                          error, sizeof(error)))
        << error;

    EXPECT_FALSE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 2, "name", "Sparse"));
    EXPECT_EQ(sdl3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 2);

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_EQ(item.dynamic_list_index, -1);
    EXPECT_STREQ(item.label, "No runtime rows");

    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "name", "Alpha"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_int(runtime, "local_matches", 0, "latency_ms", 42));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 1, "name", "Beta"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_float(runtime, "local_matches", 1, "latency_ms", 19.5f));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_bool(runtime, "local_matches", 1, "secure", true));
    EXPECT_EQ(sdl3d_game_data_runtime_collection_count(runtime, "local_matches"), 2);

    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Alpha");
    EXPECT_STREQ(item.dynamic_list_value, "42");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Beta");
    EXPECT_STREQ(item.dynamic_list_value, "19.500");

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DOWN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 1);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_EQ(result.selected_index, 1);
    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "selected_runtime_index", -1), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "selected_runtime_latency", ""), "19.500");

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.selected);
    EXPECT_STREQ(result.scene_state_key, "selected_runtime_latency_on_accept");
    EXPECT_STREQ(result.scene_state_value, "19.500");

    EXPECT_TRUE(sdl3d_game_data_runtime_collection_clear(runtime, "local_matches"));
    EXPECT_EQ(sdl3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 2);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "No runtime rows");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidDynamicListMenuSchema)
{
    const std::filesystem::path dir = unique_test_dir("menu_dynamic_list_validation");
    const auto write_case = [&](const char *name, const char *item_json) {
        write_text(dir / (std::string(name) + ".game.json"),
                   R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Dynamic List Validation", "id": "test.dynamic_list_validation", "version": "0.1.0" },
  "world": { "name": "world.dynamic_list_validation", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.ui",
        "actions": [
          { "name": "action.menu.select", "bindings": [{ "device": "keyboard", "key": "RETURN" }] },
          { "name": "action.menu.up", "bindings": [{ "device": "keyboard", "key": "UP" }] },
          { "name": "action.menu.down", "bindings": [{ "device": "keyboard", "key": "DOWN" }] }
        ]
      }
    ]
  },
  "signals": ["signal.session.join"],
  "entities": [],
  "scenes": { "initial": "scene.browser", "files": ["scenes/browser.scene.json"] }
})json");
        write_text(dir / "scenes" / "browser.scene.json", (std::string(R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.browser",
  "input": { "actions": ["action.menu.select", "action.menu.up", "action.menu.down"] },
  "menus": [
    {
      "name": "menu.browser",
      "up_action": "action.menu.up",
      "down_action": "action.menu.down",
      "select_action": "action.menu.select",
      "items": [)json") + item_json +
                                                           R"json(]
    }
  ]
})json")
                                                              .c_str());
    };

    char error[512]{};
    write_case("missing_source", R"json({ "type": "dynamic_list", "name": "list.sessions" })json");
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "missing_source.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(std::string(error).find("requires a source object"), std::string::npos) << error;

    error[0] = '\0';
    write_case("bad_value_from",
               R"json({
      "type": "dynamic_list",
      "name": "list.sessions",
      "source": {
        "type": "scene_state_indexed",
        "count_key": "session_count",
        "label_key_format": "session_%d_label"
      },
      "scene_state": { "key": "selected_session", "value_from": "endpoint" }
    })json");
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "bad_value_from.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(std::string(error).find("value_from must be value, label, or index"), std::string::npos) << error;

    error[0] = '\0';
    write_case("bad_label_format",
               R"json({
      "type": "dynamic_list",
      "name": "list.sessions",
      "source": {
        "type": "scene_state_indexed",
        "count_key": "session_count",
        "label_key_format": "session_%s_label"
      }
    })json");
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "bad_label_format.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(std::string(error).find("label_key_format must contain exactly one %d token"), std::string::npos)
        << error;

    error[0] = '\0';
    write_case("bad_value_format",
               R"json({
      "type": "dynamic_list",
      "name": "list.sessions",
      "source": {
        "type": "scene_state_indexed",
        "count_key": "session_count",
        "label_key_format": "session_%d_label",
        "value_key_format": "session_%d_%d_value"
      }
    })json");
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "bad_value_format.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(std::string(error).find("value_key_format must contain exactly one %d token"), std::string::npos)
        << error;

    error[0] = '\0';
    write_case("missing_runtime_collection",
               R"json({
      "type": "dynamic_list",
      "name": "list.sessions",
      "source": {
        "type": "runtime_collection",
        "label_field": "name"
      }
    })json");
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "missing_runtime_collection.game.json").string().c_str(), nullptr,
                                               error, sizeof(error)));
    EXPECT_NE(std::string(error).find("runtime_collection source requires a non-empty collection"), std::string::npos)
        << error;

    error[0] = '\0';
    write_case("missing_runtime_label_field",
               R"json({
      "type": "dynamic_list",
      "name": "list.sessions",
      "source": {
        "type": "runtime_collection",
        "collection": "local_matches"
      }
    })json");
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "missing_runtime_label_field.game.json").string().c_str(),
                                               nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("runtime_collection source requires a non-empty label_field"), std::string::npos)
        << error;

    remove_test_dir(dir);
}

TEST(GameDataRuntime, PlaySceneMenusAreSelectedByAuthoredConditions)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));

    sdl3d_game_data_ui_metrics metrics{};
    sdl3d_game_data_menu menu{};
    EXPECT_FALSE(sdl3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));

    metrics.paused = true;
    ASSERT_TRUE(sdl3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    EXPECT_STREQ(menu.name, "menu.pause");
    EXPECT_EQ(menu.item_count, 3);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "match_mode", "lan");
    sdl3d_properties_set_string(scene_state, "network_role", "host");
    ASSERT_TRUE(sdl3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    EXPECT_STREQ(menu.name, "menu.pause.network");
    EXPECT_EQ(menu.item_count, 2);

    sdl3d_properties_set_bool(scene_state, "network_match_termination_active", true);
    EXPECT_FALSE(sdl3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    sdl3d_properties_set_bool(scene_state, "network_match_termination_active", false);

    sdl3d_registered_actor *match = sdl3d_game_data_find_actor(runtime, "entity.match");
    ASSERT_NE(match, nullptr);
    sdl3d_properties_set_bool(match->props, "finished", true);
    metrics.paused = false;
    ASSERT_TRUE(sdl3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    EXPECT_STREQ(menu.name, "menu.match_over");
    EXPECT_EQ(menu.item_count, 2);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PauseMenuResumeConsumesSharedEnterWithoutRepausing)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    sdl3d_game_context ctx{};
    ctx.session = session;
    ctx.paused = true;

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));

    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));
    flow.scene_input_armed = true;

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, OptionsMenuCanReturnToAuthoredScene)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "return_scene", "scene.play");
    sdl3d_properties_set_bool(scene_state, "return_paused", true);

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, "menu.options", 5, &item));
    EXPECT_TRUE(item.return_scene);
    EXPECT_STREQ(item.scene, "scene.title");

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    bool armed = true;
    sdl3d_game_data_ui_metrics metrics{};
    sdl3d_game_data_menu_update_result result{};

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);
    ASSERT_TRUE(sdl3d_game_data_update_menus_for_metrics(runtime, input, &armed, &metrics, &result));
    EXPECT_EQ(result.selected_index, 5);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus_for_metrics(runtime, input, &armed, &metrics, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_update_menus_for_metrics(runtime, input, &armed, &metrics, &result));
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.return_scene);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "return_scene", ""), "scene.play");
    EXPECT_TRUE(sdl3d_properties_get_bool(scene_state, "return_paused", false));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, OptionsSubmenusDoNotOverwriteCallerReturnScene)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "return_scene", "scene.title");
    sdl3d_properties_set_bool(scene_state, "return_paused", false);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));
    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, "menu.options", 0, &item));
    EXPECT_STREQ(item.label, "Display");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "display");
    EXPECT_EQ(item.return_to, nullptr);

    sdl3d_properties_set_string(scene_state, "options_menu", "display");
    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.display");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, "menu.options.display", 4, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "root");
    EXPECT_FALSE(item.return_scene);

    sdl3d_properties_set_string(scene_state, "options_menu", "root");
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, "menu.options", 5, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_TRUE(item.return_scene);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "return_scene", ""), "scene.title");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "return_paused", true));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DisplayOptionControlsApplyImmediately)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.display"));

    const int menu_select_signal = sdl3d_game_data_find_signal(runtime, "signal.ui.menu.select");
    ASSERT_GE(menu_select_signal, 0);

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.control_changed);
    EXPECT_EQ(result.select_signal_id, menu_select_signal);
    EXPECT_EQ(result.signal_id, sdl3d_game_data_find_signal(runtime, "signal.settings.apply"));
    EXPECT_TRUE(sdl3d_game_data_app_signal_applies_window_settings(runtime, result.signal_id));
    EXPECT_EQ(result.scene, nullptr);

    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "fullscreen_exclusive");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AudioOptionSlidersApplyImmediatelyWithLeftRightInput)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.audio"));

    const int menu_select_signal = sdl3d_game_data_find_signal(runtime, "signal.ui.menu.select");
    const int apply_audio_signal = sdl3d_game_data_find_signal(runtime, "signal.settings.apply_audio");
    ASSERT_GE(menu_select_signal, 0);
    ASSERT_GE(apply_audio_signal, 0);

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 8);

    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_LEFT;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.control_changed);
    EXPECT_EQ(result.select_signal_id, menu_select_signal);
    EXPECT_EQ(result.signal_id, apply_audio_signal);
    EXPECT_EQ(result.scene, nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 7);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RIGHT;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);

    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.control_changed);
    EXPECT_EQ(result.signal_id, apply_audio_signal);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 8);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 4);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    ASSERT_TRUE(sdl3d_game_data_menu_move(runtime, "menu.options.audio", 3));
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_LEFT;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 5);

    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_FALSE(result.control_changed);
    EXPECT_EQ(result.select_signal_id, -1);
    EXPECT_EQ(result.signal_id, -1);
    EXPECT_EQ(result.scene, nullptr);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, KeyboardOptionsCaptureAndApplyAuthoredInputBindings)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int paddle_up = sdl3d_game_data_find_action(runtime, "action.paddle.up");
    ASSERT_GE(paddle_up, 0);
    const int menu_up = sdl3d_game_data_find_action(runtime, "action.menu.up");
    ASSERT_GE(menu_up, 0);
    const int exit_action = sdl3d_game_data_find_action(runtime, "action.exit");
    ASSERT_GE(exit_action, 0);

    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.input_binding_capture_started);
    EXPECT_TRUE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_I;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.input_binding_changed);
    EXPECT_FALSE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 4);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_I;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 5);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(sdl3d_input_is_pressed(input, menu_up));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 6);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    ASSERT_TRUE(sdl3d_game_data_menu_move(runtime, "menu.options.keyboard", 1));
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 7);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 8);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_I;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 9);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.input_binding_conflict);
    EXPECT_TRUE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 10);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_ESCAPE;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 11);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_FALSE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    const int reset_keyboard = sdl3d_game_data_find_signal(runtime, "signal.settings.reset_keyboard");
    ASSERT_GE(reset_keyboard, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), reset_keyboard, nullptr);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 12);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 13);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(sdl3d_input_is_pressed(input, menu_up));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 14);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_BACKSPACE;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 15);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, exit_action));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, GamepadOptionsCaptureAndApplyAuthoredInputBindings)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.gamepad"));

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.gamepad");
    EXPECT_EQ(menu.item_count, 12);

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Button Icons");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_CHOICE);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Up");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 2);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 10, &item));
    EXPECT_STREQ(item.label, "Reset Settings");

    bool saw_button_icons = false;
    bool saw_ball_camera = false;
    auto find_gamepad_labels = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *flags = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.gamepad.menu" && value == "Button Icons: Xbox")
        {
            *flags->first = true;
            EXPECT_FLOAT_EQ(text->x, 0.34f);
            EXPECT_FLOAT_EQ(text->y, 0.24f);
            EXPECT_EQ(text->align, SDL3D_GAME_DATA_UI_ALIGN_LEFT);
        }
        if (name == "ui.options.gamepad.menu" && value.rfind("Ball Camera:", 0) == 0)
        {
            *flags->second = true;
            EXPECT_FLOAT_EQ(text->x, 0.34f);
            EXPECT_NEAR(text->y, 0.735f, 0.0001f);
            EXPECT_EQ(text->align, SDL3D_GAME_DATA_UI_ALIGN_LEFT);
        }
        return !*flags->first || !*flags->second;
    };
    std::pair<bool *, bool *> gamepad_label_flags{&saw_button_icons, &saw_ball_camera};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_gamepad_labels, &gamepad_label_flags));
    EXPECT_TRUE(saw_button_icons);
    EXPECT_TRUE(saw_ball_camera);

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int paddle_up = sdl3d_game_data_find_action(runtime, "action.paddle.up");
    ASSERT_GE(paddle_up, 0);
    const int menu_up = sdl3d_game_data_find_action(runtime, "action.menu.up");
    ASSERT_GE(menu_up, 0);
    const int exit_action = sdl3d_game_data_find_action(runtime, "action.exit");
    ASSERT_GE(exit_action, 0);

    ASSERT_TRUE(sdl3d_game_data_menu_move(runtime, "menu.options.gamepad", 2));
    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 1);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);
    EXPECT_TRUE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_changed);
    EXPECT_FALSE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 4);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 5);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(sdl3d_input_is_pressed(input, menu_up));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 6);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    ASSERT_TRUE(sdl3d_game_data_menu_move(runtime, "menu.options.gamepad", 1));
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 7);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 8);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 9);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_conflict);
    EXPECT_TRUE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 10);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 11);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_FALSE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    const int reset_gamepad = sdl3d_game_data_find_signal(runtime, "signal.settings.reset_gamepad");
    ASSERT_GE(reset_gamepad, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), reset_gamepad, nullptr);

    event.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 12);
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 13);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(sdl3d_input_is_pressed(input, menu_up));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 14);
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_BACK;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 15);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, exit_action));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, OptionsMenusUseGamepadAxesAndBack)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.display"));

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_GE(menu.back_action_id, 0);

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_CHOICE);

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 2041;
    sdl3d_input_process_event(input, &event);

    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = 2041;
    event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    event.gaxis.value = -30000;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 1);

    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.control_changed);
    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "fullscreen_borderless");

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 2041;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    sdl3d_input_process_event(input, &event);
    sdl3d_input_update(input, 2);

    SDL_zero(result);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_FALSE(result.return_scene);
    EXPECT_STREQ(result.scene, "scene.options");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, MouseOptionsCaptureAndApplyAuthoredInputBindings)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.mouse"));

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.mouse");
    EXPECT_EQ(menu.item_count, 3);

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Accept");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 1);

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int menu_select = sdl3d_game_data_find_action(runtime, "action.menu.select");
    ASSERT_GE(menu_select, 0);

    bool armed = true;
    sdl3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);
    EXPECT_TRUE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_MIDDLE;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_changed);
    EXPECT_FALSE(sdl3d_game_data_menu_input_binding_capture_active(runtime));

    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 4);
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &armed, &result));

    bool saw_mouse_label = false;
    auto find_mouse_accept = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *saw = static_cast<bool *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.mouse.menu" && value == "Accept: Middle Mouse")
        {
            *saw = true;
            return false;
        }
        return true;
    };
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_mouse_accept, &saw_mouse_label));
    EXPECT_TRUE(saw_mouse_label);

    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_LEFT;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 5);
    EXPECT_FALSE(sdl3d_input_is_pressed(input, menu_select));
    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 6);

    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_MIDDLE;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 7);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, menu_select));

    const int reset_mouse = sdl3d_game_data_find_signal(runtime, "signal.settings.reset_mouse");
    ASSERT_GE(reset_mouse, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), reset_mouse, nullptr);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 8);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_LEFT;
    sdl3d_input_process_event(input, &mouse);
    sdl3d_input_update(input, 9);
    EXPECT_TRUE(sdl3d_input_is_pressed(input, menu_select));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AuthoredSettingsResetRestoresSelectedDefaults)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    sdl3d_properties_set_string(settings->props, "display_mode", "fullscreen_exclusive");
    sdl3d_properties_set_bool(settings->props, "vsync", false);
    sdl3d_properties_set_string(settings->props, "renderer", "software");
    sdl3d_properties_set_int(settings->props, "sfx_volume", 2);
    sdl3d_properties_set_int(settings->props, "music_volume", 3);

    const int reset_display = sdl3d_game_data_find_signal(runtime, "signal.settings.reset_display");
    ASSERT_GE(reset_display, 0);
    EXPECT_TRUE(sdl3d_game_data_app_signal_applies_window_settings(runtime, reset_display));
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), reset_display, nullptr);

    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
    EXPECT_TRUE(sdl3d_properties_get_bool(settings->props, "vsync", false));
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "renderer", ""), "opengl");

    const int reset_audio = sdl3d_game_data_find_signal(runtime, "signal.settings.reset_audio");
    ASSERT_GE(reset_audio, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), reset_audio, nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "music_volume", 0), 7);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongStandardOptionsUseImmediateApplyContract)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
    EXPECT_TRUE(sdl3d_properties_get_bool(settings->props, "vsync", false));
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "renderer", ""), "opengl");
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "music_volume", 0), 7);
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "gamepad_icons", ""), "xbox");
    EXPECT_TRUE(sdl3d_properties_get_bool(settings->props, "vibration", false));

    EXPECT_LT(sdl3d_game_data_find_signal(runtime, "signal.settings.snapshot_display"), 0);
    EXPECT_LT(sdl3d_game_data_find_signal(runtime, "signal.settings.snapshot_audio"), 0);
    EXPECT_LT(sdl3d_game_data_find_signal(runtime, "signal.settings.snapshot_gamepad"), 0);
    EXPECT_LT(sdl3d_game_data_find_signal(runtime, "signal.settings.cancel_display"), 0);
    EXPECT_LT(sdl3d_game_data_find_signal(runtime, "signal.settings.cancel_audio"), 0);
    EXPECT_LT(sdl3d_game_data_find_signal(runtime, "signal.settings.cancel_gamepad"), 0);
    EXPECT_GE(sdl3d_game_data_find_signal(runtime, "signal.settings.vibration"), 0);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.display"));
    sdl3d_game_data_menu_item display_mode{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, "menu.options.display", 0, &display_mode));
    EXPECT_EQ(display_mode.control_type, SDL3D_GAME_DATA_MENU_CONTROL_CHOICE);
    EXPECT_STREQ(display_mode.control_target, "entity.settings");
    EXPECT_STREQ(display_mode.control_key, "display_mode");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));
    sdl3d_game_data_menu_item up_binding{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, "menu.options.keyboard", 0, &up_binding));
    EXPECT_EQ(up_binding.control_type, SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(up_binding.input_binding_count, 2);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppFlowConsumesAuthoredLifecycleAndSceneShortcutControls)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_game_context ctx{};
    ctx.session = session;

    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.8f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_FALSE(ctx.quit_requested);
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.splash");

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_F9;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.splash");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_3;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 4);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 5);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 6);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 7);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    sdl3d_registered_actor *match = sdl3d_game_data_find_actor(runtime, "entity.match");
    ASSERT_NE(match, nullptr);
    sdl3d_properties_set_bool(match->props, "finished", true);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 8);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 9);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 10);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_BACKSPACE;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 11);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_quit_pending(&flow));
    EXPECT_FALSE(ctx.quit_requested);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.5f));
    EXPECT_TRUE(ctx.quit_requested);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, SceneFlowRunsAuthoredExitAndEnterTransitions)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));

    sdl3d_game_data_scene_flow flow{};
    sdl3d_game_data_scene_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_scene_flow_request(&flow, runtime, "scene.play"));
    EXPECT_TRUE(sdl3d_game_data_scene_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");
    EXPECT_FALSE(sdl3d_game_data_scene_flow_request(&flow, runtime, "scene.options"));

    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(session);
    sdl3d_game_data_scene_flow_update(&flow, runtime, bus, 0.29f);
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");
    EXPECT_TRUE(sdl3d_game_data_scene_flow_is_transitioning(&flow));

    sdl3d_game_data_scene_flow_update(&flow, runtime, bus, 0.29f);
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");
    EXPECT_FALSE(sdl3d_game_data_scene_flow_is_transitioning(&flow));
    EXPECT_FALSE(sdl3d_game_data_scene_flow_request(&flow, runtime, "scene.play"));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppFlowRunsAuthoredSceneTimelineActions)
{
    const std::filesystem::path dir = unique_test_dir("timeline");
    write_timeline_json(dir);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "timeline.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    const int timeline_signal = sdl3d_game_data_find_signal(runtime, "signal.timeline");
    ASSERT_GE(timeline_signal, 0);
    SignalCapture signal_capture{};
    ASSERT_NE(sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(session), timeline_signal, count_signal,
                                   &signal_capture),
              0);

    sdl3d_registered_actor *flag = sdl3d_game_data_find_actor(runtime, "entity.flag");
    ASSERT_NE(flag, nullptr);
    EXPECT_FALSE(sdl3d_properties_get_bool(flag->props, "ready", true));

    sdl3d_game_context ctx{};
    ctx.session = session;
    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.05f));
    EXPECT_FALSE(sdl3d_properties_get_bool(flag->props, "ready", true));
    EXPECT_EQ(signal_capture.calls, 0);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.06f));
    EXPECT_TRUE(sdl3d_properties_get_bool(flag->props, "ready", false));
    EXPECT_EQ(signal_capture.calls, 0);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.09f));
    EXPECT_EQ(signal_capture.calls, 1);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.10f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_EQ(signal_capture.calls, 1);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, AppFlowAppliesAuthoredSkipPolicyWithoutMenuBleedThrough)
{
    const std::filesystem::path dir = unique_test_dir("skip_policy");
    write_skip_policy_json(dir);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        sdl3d_game_data_load_file((dir / "skip.game.json").string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    sdl3d_game_context ctx{};
    ctx.session = session;
    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.intro");

    sdl3d_game_data_skip_policy policy{};
    ASSERT_TRUE(sdl3d_game_data_get_active_skip_policy(runtime, &policy));
    EXPECT_EQ(policy.input, SDL3D_GAME_DATA_SKIP_INPUT_ACTION);
    EXPECT_STREQ(policy.action, "action.skip");
    EXPECT_STREQ(policy.scene, "scene.title");
    EXPECT_TRUE(policy.preserve_exit_transition);
    EXPECT_TRUE(policy.block_menus);
    EXPECT_TRUE(policy.block_scene_shortcuts);

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    sdl3d_input_update(input, 0);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.intro");

    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 4);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 5);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, TimelinePolicyCanBlockMenusAndSceneShortcuts)
{
    const std::filesystem::path dir = unique_test_dir("timeline_blocks_input");
    write_scene_flow_policy_json(dir, true, true);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "flow_policy.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_game_data_timeline_policy policy{};
    ASSERT_TRUE(sdl3d_game_data_get_active_timeline_policy(runtime, &policy));
    EXPECT_TRUE(policy.block_menus);
    EXPECT_TRUE(policy.block_scene_shortcuts);

    sdl3d_game_context ctx{};
    ctx.session = session;
    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    sdl3d_input_update(input, 0);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    key.key.scancode = SDL_SCANCODE_3;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 1.0f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, TimelinePolicyCanAllowInteractiveIntroMenus)
{
    const std::filesystem::path dir = unique_test_dir("timeline_allows_input");
    write_scene_flow_policy_json(dir, false, false);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "flow_policy.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_game_data_timeline_policy policy{};
    ASSERT_TRUE(sdl3d_game_data_get_active_timeline_policy(runtime, &policy));
    EXPECT_FALSE(policy.block_menus);
    EXPECT_FALSE(policy.block_scene_shortcuts);

    sdl3d_game_context ctx{};
    ctx.session = session;
    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    sdl3d_input_update(input, 0);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(flow.scene_input_armed);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    bool menu_armed = true;
    sdl3d_game_data_menu_update_result menu_result{};
    ASSERT_TRUE(sdl3d_game_data_update_menus(runtime, input, &menu_armed, &menu_result));
    EXPECT_TRUE(menu_result.selected);
    EXPECT_STREQ(menu_result.scene, "scene.play");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, SceneActivityDrivesIdleWakeAndPeriodicActions)
{
    const std::filesystem::path dir = unique_test_dir("scene_activity");
    write_scene_activity_json(dir);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "activity.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_registered_actor *state = sdl3d_game_data_find_actor(runtime, "entity.state");
    ASSERT_NE(state, nullptr);
    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.overhead");
    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 0.0f));
    EXPECT_TRUE(sdl3d_properties_get_bool(state->props, "entered", false));
    EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.close");

    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 0.75f));
    EXPECT_FALSE(sdl3d_properties_get_bool(state->props, "idle", false));
    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 0.30f));
    EXPECT_TRUE(sdl3d_properties_get_bool(state->props, "idle", false));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_SPACE;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);
    bool block_menus = false;
    bool block_shortcuts = false;
    EXPECT_TRUE(sdl3d_game_data_scene_activity_consumes_wake_input(runtime, input, &block_menus, &block_shortcuts));
    EXPECT_TRUE(block_menus);
    EXPECT_TRUE(block_shortcuts);
    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 0.0f));
    EXPECT_FALSE(sdl3d_properties_get_bool(state->props, "idle", true));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 2.1f));
    EXPECT_EQ(sdl3d_properties_get_int(state->props, "periodic", 0), 1);
    EXPECT_FALSE(sdl3d_properties_get_bool(state->props, "idle", true));

    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 1.1f));
    EXPECT_TRUE(sdl3d_properties_get_bool(state->props, "idle", false));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, AppFlowConsumesActivityWakeInputBeforeMenus)
{
    const std::filesystem::path dir = unique_test_dir("scene_activity_app_flow");
    write_scene_activity_json(dir);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "activity.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_game_context ctx{};
    ctx.session = session;
    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    sdl3d_registered_actor *state = sdl3d_game_data_find_actor(runtime, "entity.state");
    ASSERT_NE(state, nullptr);

    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 0.0f));
    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 1.1f));
    ASSERT_TRUE(sdl3d_properties_get_bool(state->props, "idle", false));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));

    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, input, 0.0f));
    EXPECT_FALSE(sdl3d_properties_get_bool(state->props, "idle", true));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, MotionOscillateMovesActiveSceneActors)
{
    const std::filesystem::path dir = unique_test_dir("motion_oscillate");
    write_scene_activity_json(dir);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "activity.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_registered_actor *lamp = sdl3d_game_data_find_actor(runtime, "entity.lamp");
    ASSERT_NE(lamp, nullptr);
    EXPECT_NEAR(lamp->position.x, 2.0f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_update(runtime, 1.0f));
    EXPECT_NEAR(lamp->position.x, 2.0f + 3.0f * SDL_sinf(1.0f), 0.0001f);
    EXPECT_NEAR(lamp->position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(lamp->position.z, 0.0f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_update(runtime, 1.0f));
    EXPECT_NEAR(lamp->position.x, 2.0f + 3.0f * SDL_sinf(2.0f), 0.0001f);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, AppFlowRunsAuthoredTweenActions)
{
    const std::filesystem::path dir = unique_test_dir("animation");
    write_animation_json(dir);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "animation.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    const int property_done = sdl3d_game_data_find_signal(runtime, "signal.property.done");
    const int ui_done = sdl3d_game_data_find_signal(runtime, "signal.ui.done");
    ASSERT_GE(property_done, 0);
    ASSERT_GE(ui_done, 0);
    SignalCapture property_capture{};
    SignalCapture ui_capture{};
    ASSERT_NE(sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(session), property_done, count_signal,
                                   &property_capture),
              0);
    ASSERT_NE(sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(session), ui_done, count_signal, &ui_capture), 0);

    sdl3d_game_context ctx{};
    ctx.session = session;
    sdl3d_game_data_app_flow flow{};
    sdl3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(sdl3d_game_data_app_flow_start(&flow, runtime));
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));

    sdl3d_registered_actor *box = sdl3d_game_data_find_actor(runtime, "entity.box");
    ASSERT_NE(box, nullptr);

    ASSERT_TRUE(sdl3d_game_data_update_animations(runtime, 0.5f));
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "x", -1.0f), 5.0f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "ease", -1.0f), 0.75f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "loop", -1.0f), 0.5f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "ping", -1.0f), 0.5f, 0.0001f);
    EXPECT_EQ(property_capture.calls, 0);
    EXPECT_EQ(ui_capture.calls, 0);

    sdl3d_game_data_ui_text logo{};
    bool saw_logo = false;
    auto find_logo = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *args = static_cast<std::pair<sdl3d_game_data_ui_text *, bool *> *>(userdata);
        if (text->name != nullptr && std::string(text->name) == "ui.logo")
        {
            *args->first = *text;
            *args->second = true;
            return false;
        }
        return true;
    };
    std::pair<sdl3d_game_data_ui_text *, bool *> logo_args{&logo, &saw_logo};
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_logo, &logo_args));
    ASSERT_TRUE(saw_logo);

    sdl3d_game_data_ui_text resolved_logo{};
    bool logo_visible = false;
    ASSERT_TRUE(sdl3d_game_data_resolve_ui_text(runtime, &logo, nullptr, &resolved_logo, &logo_visible));
    EXPECT_TRUE(logo_visible);
    EXPECT_EQ(resolved_logo.color.a, 128);
    EXPECT_NEAR(resolved_logo.scale, 1.5f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_update_animations(runtime, 0.5f));
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "x", -1.0f), 10.0f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "ease", -1.0f), 1.0f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "loop", -1.0f), 0.0f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "ping", -1.0f), 1.0f, 0.0001f);
    EXPECT_EQ(property_capture.calls, 1);
    EXPECT_EQ(ui_capture.calls, 1);

    ASSERT_TRUE(sdl3d_game_data_resolve_ui_text(runtime, &logo, nullptr, &resolved_logo, &logo_visible));
    EXPECT_TRUE(logo_visible);
    EXPECT_EQ(resolved_logo.color.a, 255);
    EXPECT_NEAR(resolved_logo.scale, 2.0f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_update_animations(runtime, 0.25f));
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "loop", -1.0f), 0.25f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "ping", -1.0f), 0.75f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(sdl3d_game_data_update_animations(runtime, 1.0f));
    EXPECT_NEAR(sdl3d_properties_get_float(box->props, "loop", -1.0f), 0.25f, 0.0001f);
    sdl3d_game_data_ui_state logo_state{};
    EXPECT_FALSE(sdl3d_game_data_get_ui_state(runtime, "ui.logo", &logo_state));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
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
    EXPECT_GT(SDL_fabsf(velocity.x), 4.8f);
    EXPECT_GT(SDL_fabsf(velocity.y), 0.70f);
    EXPECT_TRUE(sdl3d_properties_get_bool(ball->props, "active_motion", false));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongTitleAttractServeHasJitterAndMovesCpuPaddles)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(sdl3d_game_data_update_scene_activity(runtime, sdl3d_game_session_get_input(session), 0.0f));

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball.attract");
    sdl3d_registered_actor *left = sdl3d_game_data_find_actor(runtime, "entity.paddle.attract_left");
    sdl3d_registered_actor *right = sdl3d_game_data_find_actor(runtime, "entity.paddle.attract_right");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    const sdl3d_vec3 velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(sdl3d_properties_get_bool(ball->props, "active_motion", false));
    EXPECT_GT(SDL_fabsf(velocity.x), 4.8f);
    EXPECT_GT(SDL_fabsf(velocity.y), 1.0f);

    const float initial_left_y = left->position.y;
    const float initial_right_y = right->position.y;
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.25f));
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.25f));

    EXPECT_NE(left->position.y, initial_left_y);
    EXPECT_NE(right->position.y, initial_right_y);

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
    sdl3d_properties_set_string(payload, "actor_name", "entity.ball");
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

TEST(GameDataRuntime, LuaAdapterAddsJitterAfterRepeatedFlatPaddleReflects)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    sdl3d_registered_actor *left = sdl3d_game_data_find_actor(runtime, "entity.paddle.player");
    sdl3d_registered_actor *right = sdl3d_game_data_find_actor(runtime, "entity.paddle.cpu");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    const int hit_signal = sdl3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    ASSERT_GE(hit_signal, 0);

    auto emit_center_hit = [&](const char *paddle_name, float ball_x) {
        ball->position = sdl3d_vec3_make(ball_x, 0.0f, 0.12f);
        sdl3d_properties_set_vec3(ball->props, "origin", ball->position);

        sdl3d_properties *payload = sdl3d_properties_create();
        ASSERT_NE(payload, nullptr);
        sdl3d_properties_set_string(payload, "actor_name", "entity.ball");
        sdl3d_properties_set_string(payload, "other_actor_name", paddle_name);
        sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), hit_signal, payload);
        sdl3d_properties_destroy(payload);
    };

    emit_center_hit("entity.paddle.player", left->position.x + 0.10f);
    sdl3d_vec3 velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(velocity.y, 0.0f, 0.0001f);
    EXPECT_EQ(sdl3d_properties_get_int(ball->props, "stagnant_reflect_count", -1), 1);

    emit_center_hit("entity.paddle.cpu", right->position.x - 0.10f);
    velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(SDL_fabsf(velocity.y), 1.0f);
    EXPECT_EQ(sdl3d_properties_get_int(ball->props, "stagnant_reflect_count", -1), 0);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AttractBallReflectsApplyAuthoredRandomJitter)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball.attract");
    sdl3d_registered_actor *left = sdl3d_game_data_find_actor(runtime, "entity.paddle.attract_left");
    sdl3d_registered_actor *right = sdl3d_game_data_find_actor(runtime, "entity.paddle.attract_right");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    const int hit_signal = sdl3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    ASSERT_GE(hit_signal, 0);

    auto emit_center_attract_hit = [&](const char *paddle_name, float ball_x) {
        ball->position = sdl3d_vec3_make(ball_x, 0.0f, 0.12f);
        sdl3d_properties_set_vec3(ball->props, "origin", ball->position);

        sdl3d_properties *payload = sdl3d_properties_create();
        ASSERT_NE(payload, nullptr);
        sdl3d_properties_set_string(payload, "actor_name", "entity.ball.attract");
        sdl3d_properties_set_string(payload, "other_actor_name", paddle_name);
        sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), hit_signal, payload);
        sdl3d_properties_destroy(payload);
    };

    emit_center_attract_hit("entity.paddle.attract_left", left->position.x + 0.10f);
    sdl3d_vec3 velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(velocity.x, 0.0f);
    EXPECT_GT(SDL_fabsf(velocity.y), 0.55f);

    emit_center_attract_hit("entity.paddle.attract_right", right->position.x - 0.10f);
    velocity = sdl3d_properties_get_vec3(ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_LT(velocity.x, 0.0f);
    EXPECT_GT(SDL_fabsf(velocity.y), 0.55f);

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
    sdl3d_properties *payload = sdl3d_properties_create();
    ASSERT_NE(payload, nullptr);
    sdl3d_properties_set_string(payload, "match_mode", "single");
    ASSERT_TRUE(sdl3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload));
    sdl3d_properties_destroy(payload);
    sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(runtime), "match_mode", "single");
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.1f));

    EXPECT_GT(cpu->position.y, 0.0f);
    EXPECT_LE(cpu->position.y, 0.55f);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongClientDoesNotStartServeTimer)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_register_adapter(runtime, "adapter.pong.configure_play_input",
                                                 configure_play_input_adapter, nullptr));

    sdl3d_properties *payload = sdl3d_properties_create();
    ASSERT_NE(payload, nullptr);
    sdl3d_properties_set_string(payload, "match_mode", "lan");
    sdl3d_properties_set_string(payload, "network_role", "client");
    ASSERT_TRUE(sdl3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload));
    sdl3d_properties_destroy(payload);

    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "network_role", ""), "client");
    EXPECT_FALSE(sdl3d_game_data_active_scene_update_phase(runtime, "simulation", false));
    EXPECT_EQ(sdl3d_timer_pool_active_count(sdl3d_game_session_get_timer_pool(session)), 0);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongMatchStateAndRestartAreAuthoredLogic)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_registered_actor *player_score = sdl3d_game_data_find_actor(runtime, "entity.score.player");
    sdl3d_registered_actor *cpu_score = sdl3d_game_data_find_actor(runtime, "entity.score.cpu");
    sdl3d_registered_actor *match = sdl3d_game_data_find_actor(runtime, "entity.match");
    sdl3d_registered_actor *presentation = sdl3d_game_data_find_actor(runtime, "entity.presentation");
    sdl3d_registered_actor *ball = sdl3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(player_score, nullptr);
    ASSERT_NE(cpu_score, nullptr);
    ASSERT_NE(match, nullptr);
    ASSERT_NE(presentation, nullptr);
    ASSERT_NE(ball, nullptr);

    sdl3d_properties_set_int(player_score->props, "value", 9);
    const int player_score_signal = sdl3d_game_data_find_signal(runtime, "signal.score.player");
    ASSERT_GE(player_score_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), player_score_signal, nullptr);

    EXPECT_EQ(sdl3d_properties_get_int(player_score->props, "value", 0), 10);
    EXPECT_TRUE(sdl3d_properties_get_bool(match->props, "finished", false));
    EXPECT_STREQ(sdl3d_properties_get_string(match->props, "winner", ""), "player");
    EXPECT_FALSE(ball->active);
    EXPECT_FALSE(sdl3d_properties_get_bool(ball->props, "active_motion", true));

    sdl3d_properties_set_float(presentation->props, "border_flash", 1.0f);
    sdl3d_properties_set_float(presentation->props, "paddle_flash", 1.0f);
    const int restart_signal = sdl3d_game_data_find_signal(runtime, "signal.match.restart");
    ASSERT_GE(restart_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), restart_signal, nullptr);

    EXPECT_EQ(sdl3d_properties_get_int(player_score->props, "value", -1), 0);
    EXPECT_EQ(sdl3d_properties_get_int(cpu_score->props, "value", -1), 0);
    EXPECT_FALSE(sdl3d_properties_get_bool(match->props, "finished", true));
    EXPECT_STREQ(sdl3d_properties_get_string(match->props, "winner", ""), "none");
    EXPECT_TRUE(ball->active);
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(presentation->props, "border_flash", -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(sdl3d_properties_get_float(presentation->props, "paddle_flash", -1.0f), 0.0f);

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
    EXPECT_TRUE(sdl3d_properties_get_bool(target->props, "state_ok", false));
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "last_adapter", ""),
                 "adapter.test.run");
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

TEST(GameDataRuntime, ActorPoolsSpawnDespawnAndResetActors)
{
    const std::filesystem::path dir = unique_test_dir("actor_pools");
    write_text(dir / "actor_pools.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Actor Pools", "id": "test.actor_pools", "version": "0.1.0" },
  "world": { "name": "world.actor_pools", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.player", "transform": { "position": [1.0, 2.0, 3.0] } }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.player_shot",
      "tags": ["projectile", "player_projectile"],
      "transform": { "position": [0.0, 0.0, 0.25] },
      "properties": {
        "damage": { "type": "int", "value": 1 },
        "velocity": { "type": "vec2", "value": [0.0, 12.0] }
      },
      "components": [
        { "type": "render.sprite", "sprite": "sprite.player_shot" },
        { "type": "collision.circle", "radius": 0.08 }
      ]
    }
  ],
  "actor_pools": [
    {
      "name": "pool.player_shots",
      "archetype": "archetype.player_shot",
      "capacity": 2,
      "scene": "scene.play",
      "initial_active": false,
      "on_exhausted": "fail"
    },
    {
      "name": "pool.reusable_shots",
      "archetype": "archetype.player_shot",
      "capacity": 2,
      "scene": "scene.play",
      "initial_active": false,
      "on_exhausted": "reuse_oldest"
    }
  ],
  "signals": [
    "signal.spawn",
    "signal.spawn.second",
    "signal.spawn.reuse",
    "signal.spawn.reuse.second",
    "signal.despawn.reuse.first",
    "signal.spawn.reuse.again",
    "signal.spawn.reuse.exhausted",
    "signal.despawn.first",
    "signal.despawn.projectiles"
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.spawn",
        "actions": [
          {
            "type": "actor.spawn",
            "pool": "pool.player_shots",
            "from": "entity.player",
            "offset": [0.5, 0.0, 0.0],
            "properties": { "damage": 7 },
            "output_actor_key": "last_actor",
            "output_id_key": "last_actor_id",
            "output_pool_index_key": "last_actor_pool_index"
          }
        ]
      },
      {
        "signal": "signal.spawn.second",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.player_shots", "position": [4.0, 5.0, 6.0] }
        ]
      },
      {
        "signal": "signal.spawn.reuse",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.reusable_shots", "position": [7.0, 8.0, 9.0] }
        ]
      },
      {
        "signal": "signal.spawn.reuse.second",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.reusable_shots", "position": [8.0, 9.0, 10.0] }
        ]
      },
      {
        "signal": "signal.despawn.reuse.first",
        "actions": [
          { "type": "actor.despawn", "target": "pool.reusable_shots.0" }
        ]
      },
      {
        "signal": "signal.spawn.reuse.again",
        "actions": [
          {
            "type": "actor.spawn",
            "pool": "pool.reusable_shots",
            "position": [10.0, 11.0, 12.0],
            "properties": { "damage": 99 }
          }
        ]
      },
      {
        "signal": "signal.spawn.reuse.exhausted",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.reusable_shots", "position": [13.0, 14.0, 15.0] }
        ]
      },
      {
        "signal": "signal.despawn.first",
        "actions": [
          { "type": "actor.despawn", "target": "pool.player_shots.0" }
        ]
      },
      {
        "signal": "signal.despawn.projectiles",
        "actions": [
          { "type": "actor.despawn_by_tag", "tag": "player_projectile" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "actor_pools.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    sdl3d_registered_actor *shot0 = sdl3d_game_data_find_actor(runtime, "pool.player_shots.0");
    sdl3d_registered_actor *shot1 = sdl3d_game_data_find_actor(runtime, "pool.player_shots.1");
    sdl3d_registered_actor *reusable_shot0 = sdl3d_game_data_find_actor(runtime, "pool.reusable_shots.0");
    sdl3d_registered_actor *reusable_shot1 = sdl3d_game_data_find_actor(runtime, "pool.reusable_shots.1");
    ASSERT_NE(shot0, nullptr);
    ASSERT_NE(shot1, nullptr);
    ASSERT_NE(reusable_shot0, nullptr);
    ASSERT_NE(reusable_shot1, nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(shot1->active);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "damage", 0), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(shot0->props, "pool", ""), "pool.player_shots");
    EXPECT_STREQ(sdl3d_properties_get_string(shot0->props, "pool_scene", ""), "scene.play");
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "pool_index", -1), 0);

    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn"), nullptr);
    EXPECT_TRUE(shot0->active);
    EXPECT_FALSE(shot1->active);
    expect_vec3_near(shot0->position, sdl3d_vec3_make(1.5f, 2.0f, 3.0f));
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "damage", 0), 7);
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "last_actor", ""),
                 "pool.player_shots.0");
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "last_actor_id", -1), shot0->id);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "last_actor_pool_index", -1), 0);

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn.second"), nullptr);
    EXPECT_TRUE(shot1->active);
    expect_vec3_near(shot1->position, sdl3d_vec3_make(4.0f, 5.0f, 6.0f));

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn.reuse"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);
    expect_vec3_near(reusable_shot0->position, sdl3d_vec3_make(7.0f, 8.0f, 9.0f));

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn.reuse.second"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);
    expect_vec3_near(reusable_shot1->position, sdl3d_vec3_make(8.0f, 9.0f, 10.0f));

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.despawn.reuse.first"), nullptr);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn.reuse.again"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);
    expect_vec3_near(reusable_shot0->position, sdl3d_vec3_make(10.0f, 11.0f, 12.0f));
    EXPECT_EQ(sdl3d_properties_get_int(reusable_shot0->props, "damage", 0), 99);

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn.reuse.exhausted"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);
    expect_vec3_near(reusable_shot0->position, sdl3d_vec3_make(10.0f, 11.0f, 12.0f));
    expect_vec3_near(reusable_shot1->position, sdl3d_vec3_make(13.0f, 14.0f, 15.0f));

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.despawn.first"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_TRUE(shot1->active);
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "damage", 0), 1);
    expect_vec3_near(shot0->position, sdl3d_vec3_make(0.0f, 0.0f, 0.25f));

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn.second"), nullptr);
    EXPECT_TRUE(shot0->active);
    expect_vec3_near(shot0->position, sdl3d_vec3_make(4.0f, 5.0f, 6.0f));

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.despawn.projectiles"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(shot1->active);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.despawn.projectiles"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(shot1->active);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ActorPoolsApplySceneExitPolicies)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_scene_policy");
    write_text(dir / "actor_pool_scene_policy.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Actor Pool Scene Policy", "id": "test.actor_pool_scene_policy", "version": "0.1.0" },
  "world": { "name": "world.actor_pool_scene_policy", "kind": "fixed_screen" },
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "damage": { "type": "int", "value": 1 }
      }
    }
  ],
  "actor_pools": [
    {
      "name": "pool.reset_shots",
      "archetype": "archetype.shot",
      "capacity": 1,
      "scene": "scene.play",
      "on_scene_exit": "reset"
    },
    {
      "name": "pool.preserved_shots",
      "archetype": "archetype.shot",
      "capacity": 1,
      "scene": "scene.play",
      "on_scene_exit": "preserve"
    },
    {
      "name": "pool.shared_shots",
      "archetype": "archetype.shot",
      "capacity": 1,
      "scenes": ["scene.play", "scene.shop"],
      "on_scene_exit": "reset"
    },
    {
      "name": "pool.despawned_shots",
      "archetype": "archetype.shot",
      "capacity": 1,
      "scene": "scene.play",
      "on_scene_exit": "despawn"
    }
  ],
  "signals": ["signal.spawn"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.spawn",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.reset_shots", "position": [1.0, 0.0, 0.0], "properties": { "damage": 7 } },
          { "type": "actor.spawn", "pool": "pool.preserved_shots", "position": [2.0, 0.0, 0.0], "properties": { "damage": 9 } },
          { "type": "actor.spawn", "pool": "pool.shared_shots", "position": [3.0, 0.0, 0.0], "properties": { "damage": 11 } },
          { "type": "actor.spawn", "pool": "pool.despawned_shots", "position": [4.0, 0.0, 0.0], "properties": { "damage": 13 } }
        ]
      }
    ]
  },
  "scenes": {
    "initial": "scene.play",
    "files": ["scenes/play.scene.json", "scenes/shop.scene.json", "scenes/title.scene.json"]
  }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "sdl3d.scene.v0", "name": "scene.play", "entities": [] })json");
    write_text(dir / "scenes" / "shop.scene.json",
               R"json({ "schema": "sdl3d.scene.v0", "name": "scene.shop", "entities": [] })json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({ "schema": "sdl3d.scene.v0", "name": "scene.title", "entities": [] })json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "actor_pool_scene_policy.game.json").string().c_str(), session,
                                          &runtime, error, sizeof(error)))
        << error;

    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), sdl3d_game_data_find_signal(runtime, "signal.spawn"),
                      nullptr);
    sdl3d_registered_actor *reset = sdl3d_game_data_find_actor(runtime, "pool.reset_shots.0");
    sdl3d_registered_actor *preserved = sdl3d_game_data_find_actor(runtime, "pool.preserved_shots.0");
    sdl3d_registered_actor *shared = sdl3d_game_data_find_actor(runtime, "pool.shared_shots.0");
    sdl3d_registered_actor *despawned = sdl3d_game_data_find_actor(runtime, "pool.despawned_shots.0");
    ASSERT_NE(reset, nullptr);
    ASSERT_NE(preserved, nullptr);
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(despawned, nullptr);
    EXPECT_TRUE(reset->active);
    EXPECT_TRUE(preserved->active);
    EXPECT_TRUE(shared->active);
    EXPECT_TRUE(despawned->active);
    EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "pool.shared_shots.0"));

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.shop"));
    EXPECT_FALSE(reset->active);
    EXPECT_EQ(sdl3d_properties_get_int(reset->props, "damage", 0), 1);
    expect_vec3_near(reset->position, sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(preserved->active);
    EXPECT_EQ(sdl3d_properties_get_int(preserved->props, "damage", 0), 9);
    expect_vec3_near(preserved->position, sdl3d_vec3_make(2.0f, 0.0f, 0.0f));
    EXPECT_TRUE(shared->active);
    EXPECT_EQ(sdl3d_properties_get_int(shared->props, "damage", 0), 11);
    EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "pool.shared_shots.0"));
    EXPECT_FALSE(despawned->active);
    EXPECT_EQ(sdl3d_properties_get_int(despawned->props, "damage", 0), 1);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));
    EXPECT_TRUE(preserved->active);
    EXPECT_EQ(sdl3d_properties_get_int(preserved->props, "damage", 0), 9);
    EXPECT_FALSE(shared->active);
    EXPECT_EQ(sdl3d_properties_get_int(shared->props, "damage", 0), 1);
    EXPECT_FALSE(sdl3d_game_data_active_scene_has_entity(runtime, "pool.shared_shots.0"));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ContactSensorsMatchActivePooledActorTags)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_tag_sensors");
    write_text(dir / "actor_pool_tag_sensors.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Actor Pool Tag Sensors", "id": "test.actor_pool_tag_sensors", "version": "0.1.0" },
  "world": { "name": "world.actor_pool_tag_sensors", "kind": "fixed_screen" },
  "actor_archetypes": [
    {
      "name": "archetype.player_shot",
      "tags": ["projectile", "player_projectile"],
      "properties": {
        "radius": { "type": "float", "value": 0.25 }
      }
    },
    {
      "name": "archetype.invader",
      "tags": ["enemy", "invader"],
      "properties": {
        "half_width": { "type": "float", "value": 0.5 },
        "half_height": { "type": "float", "value": 0.5 }
      }
    }
  ],
  "actor_pools": [
    {
      "name": "pool.player_shots",
      "archetype": "archetype.player_shot",
      "capacity": 2,
      "scene": "scene.play"
    },
    {
      "name": "pool.invaders",
      "archetype": "archetype.invader",
      "capacity": 1,
      "scene": "scene.play"
    }
  ],
  "signals": ["signal.spawn.first", "signal.spawn.second", "signal.hit"],
  "logic": {
    "sensors": [
      {
        "name": "sensor.projectile_enemy_hit",
        "type": "sensor.contact_2d",
        "a_tag": "player_projectile",
        "b_tag": "enemy",
        "on_enter": "signal.hit"
      }
    ],
    "bindings": [
      {
        "signal": "signal.spawn.first",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.player_shots", "position": [0.0, 0.0, 0.0] },
          { "type": "actor.spawn", "pool": "pool.invaders", "position": [0.0, 0.0, 0.0] }
        ]
      },
      {
        "signal": "signal.spawn.second",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.player_shots", "position": [0.1, 0.1, 0.0] }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json", "scenes.title.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "sdl3d.scene.v0", "name": "scene.play", "entities": [] })json");
    write_text(dir / "scenes.title.scene.json",
               R"json({ "schema": "sdl3d.scene.v0", "name": "scene.title", "entities": [] })json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "actor_pool_tag_sensors.game.json").string().c_str(), session,
                                          &runtime, error, sizeof(error)))
        << error;

    SensorSignalCapture capture{};
    ASSERT_NE(sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(session),
                                   sdl3d_game_data_find_signal(runtime, "signal.hit"), capture_sensor_signal, &capture),
              0);

    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session),
                      sdl3d_game_data_find_signal(runtime, "signal.spawn.first"), nullptr);
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(capture.actor_name, "pool.player_shots.0");
    EXPECT_EQ(capture.other_actor_name, "pool.invaders.0");

    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 1);

    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session),
                      sdl3d_game_data_find_signal(runtime, "signal.spawn.second"), nullptr);
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 2);
    EXPECT_EQ(capture.actor_name, "pool.player_shots.1");
    EXPECT_EQ(capture.other_actor_name, "pool.invaders.0");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 2);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidActorPoolsAndSpawnActions)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_validation");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play"
})json");

    struct Case
    {
        const char *name;
        const char *json;
        const char *message;
    };

    const Case cases[] = {
        {
            "missing_archetype",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_pools": [
    { "name": "pool.bad", "archetype": "archetype.missing", "capacity": 1 }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown actor archetype",
        },
        {
            "bad_capacity",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_archetypes": [
    { "name": "archetype.shot" }
  ],
  "actor_pools": [
    { "name": "pool.bad", "archetype": "archetype.shot", "capacity": 0 }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "capacity",
        },
        {
            "bad_spawn_pool",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "signals": ["signal.spawn"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.spawn",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.missing" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown actor pool",
        },
        {
            "bad_spawn_from",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_archetypes": [
    { "name": "archetype.shot" }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1 }
  ],
  "signals": ["signal.spawn"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.spawn",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.shots", "from": "entity.missing" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown actor.spawn from actor",
        },
        {
            "bad_despawn_target",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "signals": ["signal.despawn"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.despawn",
        "actions": [
          { "type": "actor.despawn", "target": "entity.missing" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown actor.despawn target",
        },
        {
            "pool_actor_collision",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "entities": [
    { "name": "pool.shots.0" }
  ],
  "actor_archetypes": [
    { "name": "archetype.shot" }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1 }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "collides with entity",
        },
        {
            "bad_pool_scenes",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_archetypes": [
    { "name": "archetype.shot" }
  ],
  "actor_pools": [
    {
      "name": "pool.bad",
      "archetype": "archetype.shot",
      "capacity": 1,
      "scenes": ["scene.missing"]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown scene",
        },
        {
            "bad_scene_exit_policy",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_archetypes": [
    { "name": "archetype.shot" }
  ],
  "actor_pools": [
    {
      "name": "pool.bad",
      "archetype": "archetype.shot",
      "capacity": 1,
      "scene": "scene.play",
      "on_scene_exit": "hide"
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "on_scene_exit",
        },
        {
            "bad_contact_sensor_endpoint",
            R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_archetypes": [
    { "name": "archetype.shot", "tags": ["projectile"] }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1, "scene": "scene.play" }
  ],
  "signals": ["signal.hit"],
  "logic": {
    "sensors": [
      {
        "type": "sensor.contact_2d",
        "a": "pool.shots.0",
        "a_tag": "projectile",
        "b_tag": "enemy",
        "on_enter": "signal.hit"
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "exactly one of a or a_tag",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path path = dir / (std::string(test_case.name) + ".game.json");
        write_text(path, test_case.json);
        char error[512]{};
        EXPECT_FALSE(sdl3d_game_data_validate_file(path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.message), std::string::npos) << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, LuaCanSpawnIterateAndDespawnPooledActors)
{
    const std::filesystem::path dir = unique_test_dir("lua_actor_pools");
    write_text(dir / "scripts" / "rules.lua",
               R"lua(
local rules = {}

function rules.spawn_projectile(_, _, ctx)
  local player = ctx:actor("entity.player")
  local shot, actor_id, pool_index = ctx:spawn("pool.projectiles", {
    from = player,
    offset = Vec3(0.25, 0.5, 0.0),
    properties = {
      damage = 5,
      critical = true,
      owner = "player",
      velocity = Vec3(1.0, 2.0, 0.0)
    }
  })

  ctx:state_set("spawn_name", shot and shot.name or "")
  ctx:state_set("spawn_actor_id", actor_id or -1)
  ctx:state_set("spawn_pool_index", pool_index or -1)
  ctx:state_set("spawn_active", shot and shot.active or false)
  ctx:state_set("pool_capacity", ctx:pool_capacity("pool.projectiles"))
  ctx:state_set("pool_active", ctx:pool_active_count("pool.projectiles"))
  ctx:state_set("pool_available", ctx:pool_available_count("pool.projectiles"))

  local active = ctx:active_actors_with_tags("projectile")
  ctx:state_set("active_projectiles", #active)
  if active[1] ~= nil then
    active[1]:set_int("touched", 1)
  end
  return true
end

function rules.despawn_first(_, _, ctx)
  local active = ctx:active_actors_with_tags({ "projectile" })
  ctx:state_set("before_despawn", #active)
  if active[1] ~= nil then
    ctx:despawn(active[1])
    ctx:state_set("during_despawn_active", active[1].active)
    ctx:state_set("during_despawn_damage", active[1]:get_int("damage", -1))
    ctx:state_set("during_despawn_lifecycle", active[1]:get_string("pool_lifecycle", ""))
    ctx:state_set("during_despawn_available", ctx:pool_available_count("pool.projectiles"))
  end
  ctx:state_set("after_despawn", #ctx:active_actors_with_tags("projectile"))
  return true
end

function rules.inspect_sensor_despawn(_, _, ctx)
  local shot = ctx:actor("pool.projectiles.0")
  local active = true
  if shot ~= nil then
    active = shot.active
  end
  ctx:state_set("sensor_during_active", active)
  ctx:state_set("sensor_during_damage", shot and shot:get_int("damage", -1) or -1)
  ctx:state_set("sensor_during_lifecycle", shot and shot:get_string("pool_lifecycle", "") or "")
  ctx:state_set("sensor_during_available", ctx:pool_available_count("pool.projectiles"))
  return true
end

function rules.despawn_all(_, _, ctx)
  ctx:spawn("pool.projectiles", { position = Vec3(4.0, 5.0, 6.0) })
  ctx:state_set("despawned_count", ctx:despawn_by_tag("projectile"))
  return true
end

return rules
)lua");
    write_text(dir / "lua_actor_pools.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Lua Actor Pools", "id": "test.lua_actor_pools", "version": "0.1.0" },
  "world": { "name": "world.lua_actor_pools", "kind": "fixed_screen" },
  "scripts": [
    { "id": "script.rules", "path": "scripts/rules.lua", "module": "test.actor_pools" }
  ],
  "entities": [
    { "name": "entity.player", "transform": { "position": [1.0, 2.0, 3.0] } }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.projectile",
      "tags": ["projectile", "player_projectile"],
      "transform": { "position": [0.0, 0.0, 0.25] },
      "properties": {
        "damage": { "type": "int", "value": 1 },
        "critical": { "type": "bool", "value": false },
        "owner": { "type": "string", "value": "none" },
        "velocity": { "type": "vec2", "value": [0.0, 10.0] },
        "touched": { "type": "int", "value": 0 }
      }
    }
  ],
  "actor_pools": [
    {
      "name": "pool.projectiles",
      "archetype": "archetype.projectile",
      "capacity": 2,
      "scene": "scene.play"
    }
  ],
  "signals": [
    "signal.spawn",
    "signal.despawn.first",
    "signal.despawn.all",
    "signal.pool.exit"
  ],
  "adapters": [
    { "name": "adapter.spawn_projectile", "kind": "action", "script": "script.rules", "function": "spawn_projectile" },
    { "name": "adapter.despawn_first", "kind": "action", "script": "script.rules", "function": "despawn_first" },
    { "name": "adapter.inspect_sensor_despawn", "kind": "action", "script": "script.rules", "function": "inspect_sensor_despawn" },
    { "name": "adapter.despawn_all", "kind": "action", "script": "script.rules", "function": "despawn_all" }
  ],
  "logic": {
    "sensors": [
      { "type": "sensor.bounds_exit", "entity": "pool.projectiles.0", "axis": "y", "side": "max", "threshold": 2.0, "on_enter": "signal.pool.exit" }
    ],
    "bindings": [
      {
        "signal": "signal.spawn",
        "actions": [
          { "type": "adapter.invoke", "adapter": "adapter.spawn_projectile" }
        ]
      },
      {
        "signal": "signal.despawn.first",
        "actions": [
          { "type": "adapter.invoke", "adapter": "adapter.despawn_first" }
        ]
      },
      {
        "signal": "signal.despawn.all",
        "actions": [
          { "type": "adapter.invoke", "adapter": "adapter.despawn_all" }
        ]
      },
      {
        "signal": "signal.pool.exit",
        "actions": [
          { "type": "actor.despawn", "target": "pool.projectiles.0" },
          { "type": "adapter.invoke", "adapter": "adapter.inspect_sensor_despawn" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play"
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "lua_actor_pools.game.json").string().c_str(), session, &runtime,
                                          error, sizeof(error)))
        << error;

    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn"), nullptr);

    sdl3d_registered_actor *shot0 = sdl3d_game_data_find_actor(runtime, "pool.projectiles.0");
    ASSERT_NE(shot0, nullptr);
    EXPECT_TRUE(shot0->active);
    expect_vec3_near(shot0->position, sdl3d_vec3_make(1.25f, 2.5f, 3.0f));
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "damage", 0), 5);
    EXPECT_TRUE(sdl3d_properties_get_bool(shot0->props, "critical", false));
    EXPECT_STREQ(sdl3d_properties_get_string(shot0->props, "owner", ""), "player");
    expect_vec3_near(sdl3d_properties_get_vec3(shot0->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f)),
                     sdl3d_vec3_make(1.0f, 2.0f, 0.0f));
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "touched", 0), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "spawn_name", ""),
                 "pool.projectiles.0");
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "spawn_actor_id", -1), shot0->id);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "spawn_pool_index", -1), 0);
    EXPECT_TRUE(sdl3d_properties_get_bool(sdl3d_game_data_scene_state(runtime), "spawn_active", false));
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "pool_capacity", -1), 2);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "pool_active", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "pool_available", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "active_projectiles", -1), 1);

    EXPECT_TRUE(sdl3d_game_data_update(runtime, 0.016f));
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(sdl3d_properties_get_bool(sdl3d_game_data_scene_state(runtime), "sensor_during_active", true));
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "sensor_during_damage", -1), 5);
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "sensor_during_lifecycle", ""),
                 "despawning");
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "sensor_during_available", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "damage", 0), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(shot0->props, "pool_lifecycle", ""), "inactive");

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.spawn"), nullptr);
    EXPECT_TRUE(shot0->active);
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "damage", 0), 5);

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.despawn.first"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "before_despawn", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "after_despawn", -1), 0);
    EXPECT_FALSE(sdl3d_properties_get_bool(sdl3d_game_data_scene_state(runtime), "during_despawn_active", true));
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "during_despawn_damage", -1), 5);
    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "during_despawn_lifecycle", ""),
                 "despawning");
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "during_despawn_available", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "damage", 0), 1);
    EXPECT_EQ(sdl3d_properties_get_int(shot0->props, "touched", -1), 0);
    EXPECT_STREQ(sdl3d_properties_get_string(shot0->props, "pool_lifecycle", ""), "inactive");

    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.despawn.all"), nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_scene_state(runtime), "despawned_count", -1), 1);
    EXPECT_FALSE(shot0->active);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ActorPoolDiagnosticsTrackUsageAndExhaustion)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_diagnostics");
    write_text(dir / "scripts" / "rules.lua",
               R"lua(
local rules = {}

function rules.run(_, _, ctx)
  local first, _, first_index = ctx:spawn("pool.fail", { position = Vec3(1.0, 2.0, 0.0) })
  local second, err = ctx:spawn("pool.fail", { position = Vec3(3.0, 4.0, 0.0) })
  ctx:state_set("fail_first_index", first_index or -1)
  ctx:state_set("fail_second_missing", second == nil)
  ctx:state_set("fail_error", err or "")
  ctx:state_set("fail_attempts", ctx:pool_spawn_attempt_count("pool.fail"))
  ctx:state_set("fail_success", ctx:pool_spawn_success_count("pool.fail"))
  ctx:state_set("fail_failures", ctx:pool_spawn_failure_count("pool.fail"))
  ctx:state_set("fail_exhaustion", ctx:pool_exhaustion_count("pool.fail"))
  ctx:state_set("fail_peak", ctx:pool_peak_active_count("pool.fail"))
  ctx:state_set("fail_last_failure", ctx:pool_last_spawn_failure_reason("pool.fail"))
  ctx:state_set("fail_active", ctx:pool_active_count("pool.fail"))
  ctx:state_set("fail_available", ctx:pool_available_count("pool.fail"))
  if first ~= nil then
    ctx:despawn(first, "hit_enemy")
  end
  ctx:state_set("fail_despawns", ctx:pool_despawn_count("pool.fail"))
  ctx:state_set("fail_last_despawn", ctx:pool_last_despawn_reason("pool.fail"))

  ctx:spawn("pool.reuse", { position = Vec3(1.0, 0.0, 0.0), properties = { generation = 1 } })
  ctx:spawn("pool.reuse", { position = Vec3(2.0, 0.0, 0.0), properties = { generation = 2 } })
  ctx:state_set("reuse_attempts", ctx:pool_spawn_attempt_count("pool.reuse"))
  ctx:state_set("reuse_success", ctx:pool_spawn_success_count("pool.reuse"))
  ctx:state_set("reuse_failures", ctx:pool_spawn_failure_count("pool.reuse"))
  ctx:state_set("reuse_exhaustion", ctx:pool_exhaustion_count("pool.reuse"))
  ctx:state_set("reuse_count", ctx:pool_reuse_count("pool.reuse"))
  ctx:state_set("reuse_despawns", ctx:pool_despawn_count("pool.reuse"))
  ctx:state_set("reuse_last_despawn", ctx:pool_last_despawn_reason("pool.reuse"))
  ctx:state_set("reuse_peak", ctx:pool_peak_active_count("pool.reuse"))
  ctx:state_set("reuse_active", ctx:pool_active_count("pool.reuse"))
  return true
end

return rules
)lua");
    write_text(dir / "actor_pool_diagnostics.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Actor Pool Diagnostics", "id": "test.actor_pool_diagnostics", "version": "0.1.0" },
  "world": { "name": "world.actor_pool_diagnostics", "kind": "fixed_screen" },
  "scripts": [
    { "id": "script.rules", "path": "scripts/rules.lua", "module": "test.actor_pool_diagnostics" }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.projectile",
      "tags": ["projectile"],
      "properties": {
        "generation": { "type": "int", "value": 0 }
      }
    }
  ],
  "actor_pools": [
    {
      "name": "pool.fail",
      "archetype": "archetype.projectile",
      "capacity": 1,
      "scene": "scene.play",
      "on_exhausted": "fail"
    },
    {
      "name": "pool.reuse",
      "archetype": "archetype.projectile",
      "capacity": 1,
      "scene": "scene.play",
      "on_exhausted": "reuse_oldest"
    }
  ],
  "signals": ["signal.run"],
  "adapters": [
    { "name": "adapter.run", "kind": "action", "script": "script.rules", "function": "run" }
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.run",
        "actions": [
          { "type": "adapter.invoke", "adapter": "adapter.run" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play"
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "actor_pool_diagnostics.game.json").string().c_str(), session,
                                          &runtime, error, sizeof(error)))
        << error;

    SDLLogOutputGuard log_guard;
    CapturedLogMessage captured_log;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetLogOutputFunction(capture_log_output, &captured_log);

    sdl3d_signal_bus *bus = sdl3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    sdl3d_signal_emit(bus, sdl3d_game_data_find_signal(runtime, "signal.run"), nullptr);

    const sdl3d_properties *scene_state = sdl3d_game_data_scene_state(runtime);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_first_index", -1), 0);
    EXPECT_TRUE(sdl3d_properties_get_bool(scene_state, "fail_second_missing", false));
    EXPECT_NE(std::string(sdl3d_properties_get_string(scene_state, "fail_error", "")).find("exhausted"),
              std::string::npos);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_attempts", -1), 2);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_success", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_failures", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_exhaustion", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_peak", -1), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "fail_last_failure", ""), "exhausted");
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_active", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_available", -1), 0);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "fail_despawns", -1), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "fail_last_despawn", ""), "hit_enemy");

    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_attempts", -1), 2);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_success", -1), 2);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_failures", -1), 0);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_exhaustion", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_count", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_despawns", -1), 1);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "reuse_last_despawn", ""), "reuse_oldest");
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_peak", -1), 1);
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "reuse_active", -1), 1);
    sdl3d_registered_actor *reused = sdl3d_game_data_find_actor(runtime, "pool.reuse.0");
    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(sdl3d_properties_get_int(reused->props, "generation", -1), 2);

    EXPECT_EQ(captured_log.category, SDL_LOG_CATEGORY_APPLICATION);
    EXPECT_EQ(captured_log.priority, SDL_LOG_PRIORITY_WARN);
    EXPECT_NE(captured_log.message.find("SDL3D actor pool exhausted"), std::string::npos);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, LoadsLuaBackedGameDataFromMemoryPack)
{
    const std::string game_json = read_fixture_file("module_success.game.json");
    const std::string shared_lua = read_fixture_file("scripts/shared.lua");
    const std::string rules_lua = read_fixture_file("scripts/rules.lua");
    ASSERT_FALSE(game_json.empty());
    ASSERT_FALSE(shared_lua.empty());
    ASSERT_FALSE(rules_lua.empty());

    const std::vector<std::uint8_t> pack = make_pack({
        {"module_success.game.json", game_json},
        {"scripts/shared.lua", shared_lua},
        {"scripts/rules.lua", rules_lua},
    });

    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    char error[512]{};
    ASSERT_TRUE(sdl3d_asset_resolver_mount_memory_pack(assets, pack.data(), pack.size(), "module-fixture", error,
                                                       sizeof(error)))
        << error;
    EXPECT_TRUE(
        sdl3d_game_data_validate_asset(assets, "asset://module_success.game.json", nullptr, error, sizeof(error)))
        << error;

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        sdl3d_game_data_load_asset(assets, "asset://module_success.game.json", session, &runtime, error, sizeof(error)))
        << error;

    const int run_signal = sdl3d_game_data_find_signal(runtime, "signal.run");
    ASSERT_GE(run_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), run_signal, nullptr);

    sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    EXPECT_FLOAT_EQ(target->position.x, 1.0f);
    EXPECT_FLOAT_EQ(target->position.y, 2.0f);
    EXPECT_TRUE(sdl3d_properties_get_bool(target->props, "ctx_ok", false));
    EXPECT_TRUE(sdl3d_properties_get_bool(target->props, "state_ok", false));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    sdl3d_asset_resolver_destroy(assets);
}

TEST(GameDataRuntime, ReloadScriptsCommitsUpdatedLuaAdapters)
{
    const std::filesystem::path dir = unique_test_dir("reload_success");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 1);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        sdl3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(sdl3d_properties_get_int(target->props, "value", 0), 1);

    write_hot_reload_script(dir, 2);
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(sdl3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_reload_scripts(runtime, assets, error, sizeof(error))) << error;

    sdl3d_properties_set_int(target->props, "value", 0);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(sdl3d_properties_get_int(target->props, "value", 0), 2);

    sdl3d_asset_resolver_destroy(assets);
    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReloadScriptsPreservesLastGoodAdapterOnSyntaxFailure)
{
    const std::filesystem::path dir = unique_test_dir("reload_syntax_failure");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 7);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        sdl3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(sdl3d_properties_get_int(target->props, "value", 0), 7);

    write_text(dir / "scripts" / "rules.lua", "local rules = \n");
    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(sdl3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    EXPECT_FALSE(sdl3d_game_data_reload_scripts(runtime, assets, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("script.rules"), std::string::npos);

    sdl3d_properties_set_int(target->props, "value", 0);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(sdl3d_properties_get_int(target->props, "value", 0), 7);

    sdl3d_asset_resolver_destroy(assets);
    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReloadScriptsPreservesLastGoodAdapterOnMissingFunction)
{
    const std::filesystem::path dir = unique_test_dir("reload_missing_function");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 3);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        sdl3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    write_text(dir / "scripts" / "rules.lua", "return {}\n");

    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(sdl3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    EXPECT_FALSE(sdl3d_game_data_reload_scripts(runtime, assets, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("adapter.reload.run"), std::string::npos);

    sdl3d_properties_set_int(target->props, "value", 0);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(sdl3d_properties_get_int(target->props, "value", 0), 3);

    sdl3d_asset_resolver_destroy(assets);
    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReloadScriptsKeepsNativeAdapterOverrides)
{
    const std::filesystem::path dir = unique_test_dir("reload_native_override");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 1);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        sdl3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    AdapterCapture capture{};
    ASSERT_TRUE(sdl3d_game_data_register_adapter(runtime, "adapter.reload.run", reload_native_adapter, &capture));
    write_hot_reload_script(dir, 2);

    sdl3d_asset_resolver *assets = sdl3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(sdl3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    ASSERT_TRUE(sdl3d_game_data_reload_scripts(runtime, assets, error, sizeof(error))) << error;

    sdl3d_registered_actor *target = sdl3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(sdl3d_properties_get_int(target->props, "value", 0), 99);

    sdl3d_asset_resolver_destroy(assets);
    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
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

TEST(GameDataRuntime, RejectsInvalidHapticsPolicies)
{
    const std::filesystem::path dir = unique_test_dir("haptics_policy_validation");
    write_text(dir / "bad_haptics.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Bad Haptics", "id": "test.bad_haptics", "version": "0.1.0" },
  "world": { "name": "world.bad_haptics", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.player", "tags": ["player"] }
  ],
  "signals": [
    "signal.hit"
  ],
  "haptics": {
    "policies": [
      {
        "name": "haptics.bad",
        "signal": "signal.hit",
        "low_frequency": 1.5,
        "high_frequency": 0.5,
        "duration_ms": 100,
        "payload_actor_filters": [
          { "key": "other_actor_name", "tags": [] }
        ]
      }
    ]
  }
})json");

    char error[512]{};
    EXPECT_FALSE(
        sdl3d_game_data_validate_file((dir / "bad_haptics.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("haptics low_frequency must be a number from 0 to 1"), std::string::npos)
        << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidInputProfiles)
{
    const std::filesystem::path dir = unique_test_dir("input_profiles");
    write_text(dir / "bad_input_profile.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Bad Input Profile", "id": "test.bad_input_profile", "version": "0.1.0" },
  "world": { "name": "world.bad_input_profile", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.gameplay",
        "actions": [
          { "name": "action.up" }
        ]
      }
    ],
    "profiles": [
      {
        "name": "profile.bad",
        "min_gamepads": 2,
        "max_gamepads": 1,
        "bindings": [
          { "action": "action.up", "device": "gamepad", "button": "DPAD_UP", "slot": -2 }
        ]
      }
    ]
  },
  "entities": []
})json");

    char error[512]{};
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "bad_input_profile.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0]"), std::string::npos);

    write_text(dir / "bad_input_assignment.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Bad Input Assignment", "id": "test.bad_input_assignment", "version": "0.1.0" },
  "world": { "name": "world.bad_input_assignment", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.gameplay",
        "actions": [
          { "name": "action.up" }
        ]
      }
    ],
    "device_assignment_sets": [
      {
        "name": "assignment.bad",
        "device": "gamepad",
        "bindings": [
          { "semantic": "up", "button": "NOT_A_BUTTON" }
        ]
      }
    ],
    "profiles": [
      {
        "name": "profile.bad",
        "assignments": [
          { "set": "assignment.bad", "actions": { "up": "action.up" } }
        ]
      }
    ]
  },
  "entities": []
})json");

    error[0] = '\0';
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "bad_input_assignment.game.json").string().c_str(), nullptr,
                                               error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.device_assignment_sets[0].bindings[0]"), std::string::npos);

    write_text(dir / "mixed_input_profile.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Mixed Input Profile", "id": "test.mixed_input_profile", "version": "0.1.0" },
  "world": { "name": "world.mixed_input_profile", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.gameplay",
        "actions": [
          { "name": "action.up" }
        ]
      }
    ],
    "device_assignment_sets": [
      {
        "name": "assignment.keyboard",
        "device": "keyboard",
        "bindings": [
          { "semantic": "up", "key": "UP" }
        ]
      }
    ],
    "profiles": [
      {
        "name": "profile.mixed",
        "bindings": [
          { "action": "action.up", "device": "keyboard", "key": "UP" }
        ],
        "assignments": [
          { "set": "assignment.keyboard", "actions": { "up": "action.up" } }
        ]
      }
    ]
  },
  "entities": []
})json");

    error[0] = '\0';
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "mixed_input_profile.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0]"), std::string::npos);

    write_text(dir / "extra_assignment_semantic.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Extra Assignment Semantic", "id": "test.extra_assignment_semantic", "version": "0.1.0" },
  "world": { "name": "world.extra_assignment_semantic", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.gameplay",
        "actions": [
          { "name": "action.up" }
        ]
      }
    ],
    "device_assignment_sets": [
      {
        "name": "assignment.keyboard",
        "device": "keyboard",
        "bindings": [
          { "semantic": "up", "key": "UP" }
        ]
      }
    ],
    "profiles": [
      {
        "name": "profile.extra",
        "assignments": [
          { "set": "assignment.keyboard", "actions": { "up": "action.up", "fire": "action.up" } }
        ]
      }
    ]
  },
  "entities": []
})json");

    error[0] = '\0';
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "extra_assignment_semantic.game.json").string().c_str(), nullptr,
                                               error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0].assignments[0].actions.fire"), std::string::npos);

    write_text(dir / "nongamepad_assignment_slot.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Nongamepad Assignment Slot", "id": "test.nongamepad_assignment_slot", "version": "0.1.0" },
  "world": { "name": "world.nongamepad_assignment_slot", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      {
        "name": "input.gameplay",
        "actions": [
          { "name": "action.up" }
        ]
      }
    ],
    "device_assignment_sets": [
      {
        "name": "assignment.keyboard",
        "device": "keyboard",
        "bindings": [
          { "semantic": "up", "key": "UP" }
        ]
      }
    ],
    "profiles": [
      {
        "name": "profile.slot",
        "assignments": [
          { "set": "assignment.keyboard", "slot": 0, "actions": { "up": "action.up" } }
        ]
      }
    ]
  },
  "entities": []
})json");

    error[0] = '\0';
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "nongamepad_assignment_slot.game.json").string().c_str(), nullptr,
                                               error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0].assignments[0]"), std::string::npos);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, StandardOptionsAdoptionFixtureLoadsReusablePackage)
{
    const std::string path = fixture_path("standard_options_minimal.game.json");
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_validate_file(path.c_str(), nullptr, error, sizeof(error))) << error;

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << error;

    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");
    EXPECT_EQ(sdl3d_game_data_scene_count(runtime), 7);
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 0), "scene.title");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 1), "scene.options");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 2), "scene.options.display");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 3), "scene.options.keyboard");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 4), "scene.options.mouse");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 5), "scene.options.gamepad");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 6), "scene.options.audio");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));
    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options");
    EXPECT_EQ(menu.item_count, 6);

    sdl3d_game_data_menu_item item{};
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Display");
    EXPECT_STREQ(item.scene, "scene.options.display");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Mouse");
    EXPECT_STREQ(item.scene, "scene.options.mouse");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.keyboard");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Up");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 2);

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options.audio"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.audio");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Sound Effects");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_RANGE);
    EXPECT_STREQ(item.control_target, "entity.settings");
    EXPECT_STREQ(item.control_key, "sfx_volume");

    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(sdl3d_properties_get_int(settings->props, "music_volume", 0), 7);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
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

TEST(GameDataRuntime, ValidatesAuthoredStorageConfig)
{
    const std::filesystem::path dir = unique_test_dir("storage_validation");
    write_text(dir / "bad_storage.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Bad Storage", "id": "test.bad_storage", "version": "0.1.0" },
  "storage": { "organization": "Bad/Org", "application": "Pong" },
  "world": { "name": "world.bad_storage", "kind": "fixed_screen" },
  "entities": []
})json");

    char error[512]{};
    EXPECT_FALSE(
        sdl3d_game_data_validate_file((dir / "bad_storage.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.storage.organization"), std::string::npos);

    write_text(dir / "good_storage.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Good Storage", "id": "test.good_storage", "version": "0.1.0" },
  "storage": {
    "organization": "Blue Sentinel Security",
    "application": "Storage Test",
    "profile": "dev"
  },
  "world": { "name": "world.good_storage", "kind": "fixed_screen" },
  "entities": []
})json");
    error[0] = '\0';
    EXPECT_TRUE(
        sdl3d_game_data_validate_file((dir / "good_storage.game.json").string().c_str(), nullptr, error, sizeof(error)))
        << error;
    EXPECT_EQ(error[0], '\0');
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ValidatesNetworkReplicationSchemaAndComputesStableHash)
{
    const std::filesystem::path dir = unique_test_dir("network_schema");
    const std::string network_json = valid_network_schema_json();
    std::string network_with_session_flow = network_json;
    const size_t session_flow_insert = network_with_session_flow.rfind("\n  }");
    ASSERT_NE(session_flow_insert, std::string::npos);
    network_with_session_flow.insert(session_flow_insert, R"json(,
    "session_flow": {
      "state_keys": {
        "match_mode": "match_mode"
      },
      "state_values": {
        "match_mode": {
          "network": "lan"
        }
      },
      "messages": {
        "disconnect_reasons": {
          "host_exited": "Host exited"
        },
        "disconnect_prompts": {
          "match_terminated": "Match terminated: {reason}"
        }
      }
    })json");
    std::string network_with_runtime_bindings = network_json;
    const size_t runtime_bindings_insert = network_with_runtime_bindings.rfind("\n  }");
    ASSERT_NE(runtime_bindings_insert, std::string::npos);
    network_with_runtime_bindings.insert(runtime_bindings_insert, R"json(,
    "runtime_bindings": {
      "replication": {
        "state_snapshot": "play_state",
        "client_input": "client_input"
      },
      "controls": {
        "start_game": "start_game",
        "pause_request": "pause"
      },
      "actions": {
        "menu_select": "action.pause"
      },
      "signals": {
        "pause_changed": "signal.network.pause"
      },
      "pause": {
        "action": "action.pause",
        "state": { "actor": "entity.match", "property": "paused" }
      }
    })json");
    std::string network_with_diagnostics = network_json;
    const size_t diagnostics_insert = network_with_diagnostics.rfind("\n  }");
    ASSERT_NE(diagnostics_insert, std::string::npos);
    network_with_diagnostics.insert(diagnostics_insert, R"json(,
    "diagnostics": {
      "snapshots": [
        {
          "name": "multiplayer_state",
          "replication": "play_state",
          "enabled": true,
          "level": "debug",
          "cadence_seconds": 0.5,
          "include_session_state": true,
          "message": "{event} {description}"
        }
      ]
    })json");

    write_text(dir / "network_a.game.json", network_schema_game_json(network_json, "Network Schema A").c_str());
    write_text(dir / "network_b.game.json", network_schema_game_json(network_json, "Different Metadata").c_str());
    write_text(dir / "network_session_flow.game.json",
               network_schema_game_json(network_with_session_flow, "Network Schema A").c_str());
    write_text(dir / "network_runtime_bindings.game.json",
               network_schema_game_json(network_with_runtime_bindings, "Network Schema A").c_str());
    write_text(dir / "network_diagnostics.game.json",
               network_schema_game_json(network_with_diagnostics, "Network Schema A").c_str());
    write_text(dir / "network_changed.game.json",
               network_schema_game_json(valid_network_schema_json("vec2"), "Network Schema A").c_str());

    char error[512]{};
    ASSERT_TRUE(
        sdl3d_game_data_validate_file((dir / "network_a.game.json").string().c_str(), nullptr, error, sizeof(error)))
        << error;

    const auto hash_a = load_network_schema_hash(dir / "network_a.game.json");
    const auto hash_b = load_network_schema_hash(dir / "network_b.game.json");
    const auto hash_with_session_flow = load_network_schema_hash(dir / "network_session_flow.game.json");
    const auto hash_with_runtime_bindings = load_network_schema_hash(dir / "network_runtime_bindings.game.json");
    const auto hash_with_diagnostics = load_network_schema_hash(dir / "network_diagnostics.game.json");
    const auto hash_changed = load_network_schema_hash(dir / "network_changed.game.json");

    EXPECT_EQ(hash_a, hash_b);
    EXPECT_EQ(hash_a, hash_with_session_flow);
    EXPECT_EQ(hash_a, hash_with_runtime_bindings);
    EXPECT_EQ(hash_a, hash_with_diagnostics);
    EXPECT_NE(hash_a, hash_changed);

    remove_test_dir(dir);
}

TEST(GameDataRuntime, LocalOnlyGameHasNoNetworkSchemaHash)
{
    const std::filesystem::path dir = unique_test_dir("no_network_schema");
    write_text(dir / "local_only.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Local Only", "id": "test.local_only", "version": "0.1.0" },
  "world": { "name": "world.local_only", "kind": "fixed_screen" },
  "entities": []
})json");

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "local_only.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);

    std::array<Uint8, SDL3D_REPLICATION_SCHEMA_HASH_SIZE> hash{};
    EXPECT_FALSE(sdl3d_game_data_has_network_schema(runtime));
    EXPECT_FALSE(sdl3d_game_data_get_network_schema_hash(runtime, hash.data()));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EncodesAndAppliesPongNetworkSnapshotFromAuthoredSchema)
{
    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(host));

    sdl3d_game_session *client_session = nullptr;
    sdl3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(client));

    sdl3d_registered_actor *host_player = sdl3d_game_data_find_actor(host, "entity.paddle.player");
    sdl3d_registered_actor *host_cpu = sdl3d_game_data_find_actor(host, "entity.paddle.cpu");
    sdl3d_registered_actor *host_ball = sdl3d_game_data_find_actor(host, "entity.ball");
    sdl3d_registered_actor *host_player_score = sdl3d_game_data_find_actor(host, "entity.score.player");
    sdl3d_registered_actor *host_cpu_score = sdl3d_game_data_find_actor(host, "entity.score.cpu");
    sdl3d_registered_actor *host_match = sdl3d_game_data_find_actor(host, "entity.match");
    sdl3d_registered_actor *host_presentation = sdl3d_game_data_find_actor(host, "entity.presentation");
    ASSERT_NE(host_player, nullptr);
    ASSERT_NE(host_cpu, nullptr);
    ASSERT_NE(host_ball, nullptr);
    ASSERT_NE(host_player_score, nullptr);
    ASSERT_NE(host_cpu_score, nullptr);
    ASSERT_NE(host_match, nullptr);
    ASSERT_NE(host_presentation, nullptr);

    host_player->position = {-7.5f, 1.25f, 0.0f};
    host_cpu->position = {7.5f, -2.0f, 0.0f};
    host_ball->position = {1.5f, 2.5f, 0.12f};
    sdl3d_properties_set_vec3(host_ball->props, "velocity", {3.25f, -1.75f, 9.0f});
    sdl3d_properties_set_bool(host_ball->props, "active_motion", true);
    sdl3d_properties_set_bool(host_ball->props, "has_last_reflect_y", true);
    sdl3d_properties_set_float(host_ball->props, "last_reflect_y", 1.5f);
    sdl3d_properties_set_int(host_ball->props, "stagnant_reflect_count", 2);
    sdl3d_properties_set_int(host_player_score->props, "value", 4);
    sdl3d_properties_set_int(host_cpu_score->props, "value", 6);
    sdl3d_properties_set_bool(host_match->props, "finished", true);
    sdl3d_properties_set_int(host_match->props, "winner_id", 2);
    sdl3d_properties_set_bool(host_match->props, "paused", true);
    sdl3d_properties_set_float(host_presentation->props, "border_flash", 0.75f);
    sdl3d_properties_set_float(host_presentation->props, "paddle_flash", 0.5f);

    sdl3d_registered_actor *client_ball = sdl3d_game_data_find_actor(client, "entity.ball");
    ASSERT_NE(client_ball, nullptr);
    sdl3d_properties_set_vec3(client_ball->props, "velocity", {0.0f, 0.0f, 42.0f});

    std::array<Uint8, 512> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_encode_network_snapshot(host, "play_state", 12345U, packet.data(), packet.size(),
                                                        &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 0U);

    Uint32 tick = 0U;
    ASSERT_TRUE(sdl3d_game_data_apply_network_snapshot(client, packet.data(), packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 12345U);

    expect_vec3_near(sdl3d_game_data_find_actor(client, "entity.paddle.player")->position, host_player->position);
    expect_vec3_near(sdl3d_game_data_find_actor(client, "entity.paddle.cpu")->position, host_cpu->position);
    expect_vec3_near(client_ball->position, host_ball->position);
    const sdl3d_vec3 client_velocity =
        sdl3d_properties_get_vec3(client_ball->props, "velocity", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(client_velocity.x, 3.25f, 0.0001f);
    EXPECT_NEAR(client_velocity.y, -1.75f, 0.0001f);
    EXPECT_NEAR(client_velocity.z, 42.0f, 0.0001f);
    EXPECT_TRUE(sdl3d_properties_get_bool(client_ball->props, "active_motion", false));
    EXPECT_TRUE(sdl3d_properties_get_bool(client_ball->props, "has_last_reflect_y", false));
    EXPECT_NEAR(sdl3d_properties_get_float(client_ball->props, "last_reflect_y", 0.0f), 1.5f, 0.0001f);
    EXPECT_EQ(sdl3d_properties_get_int(client_ball->props, "stagnant_reflect_count", 0), 2);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_find_actor(client, "entity.score.player")->props, "value", 0),
              4);
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_find_actor(client, "entity.score.cpu")->props, "value", 0), 6);
    EXPECT_TRUE(
        sdl3d_properties_get_bool(sdl3d_game_data_find_actor(client, "entity.match")->props, "finished", false));
    EXPECT_EQ(sdl3d_properties_get_int(sdl3d_game_data_find_actor(client, "entity.match")->props, "winner_id", 0), 2);
    EXPECT_TRUE(sdl3d_properties_get_bool(sdl3d_game_data_find_actor(client, "entity.match")->props, "paused", false));
    EXPECT_NEAR(sdl3d_properties_get_float(sdl3d_game_data_find_actor(client, "entity.presentation")->props,
                                           "border_flash", 0.0f),
                0.75f, 0.0001f);
    EXPECT_NEAR(sdl3d_properties_get_float(sdl3d_game_data_find_actor(client, "entity.presentation")->props,
                                           "paddle_flash", 0.0f),
                0.5f, 0.0001f);

    sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(host), "match_mode", "lan");
    sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(host), "network_role", "host");
    sdl3d_properties_set_string(sdl3d_game_data_mutable_scene_state(host), "network_flow", "direct");
    char description[4096]{};
    ASSERT_TRUE(sdl3d_game_data_describe_network_snapshot(host, "play_state", 12345U, description, sizeof(description),
                                                          error, sizeof(error)))
        << error;
    const std::string text(description);
    EXPECT_NE(text.find("tick=12345"), std::string::npos);
    EXPECT_NE(text.find("scene=scene.splash"), std::string::npos);
    EXPECT_NE(text.find("match_mode=lan"), std::string::npos);
    EXPECT_NE(text.find("network_role=host"), std::string::npos);
    EXPECT_NE(text.find("network_flow=direct"), std::string::npos);
    EXPECT_NE(text.find("entity.paddle.player.position=(-7.500,1.250,0.000)"), std::string::npos);
    EXPECT_NE(text.find("entity.ball.properties.velocity=(3.250,-1.750)"), std::string::npos);
    EXPECT_NE(text.find("entity.match.properties.paused=true"), std::string::npos);

    char tiny_description[16]{};
    EXPECT_FALSE(sdl3d_game_data_describe_network_snapshot(host, "play_state", 12345U, tiny_description,
                                                           sizeof(tiny_description), error, sizeof(error)));
    EXPECT_NE(std::string(error).find("buffer is too small"), std::string::npos);

    SDLLogOutputGuard log_guard;
    CapturedLogMessage captured_log;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetLogOutputFunction(capture_log_output, &captured_log);

    bool logged = false;
    ASSERT_TRUE(sdl3d_game_data_log_network_snapshot_diagnostic(host, "multiplayer_state", 12346U, "test_event",
                                                                "first", &logged, error, sizeof(error)))
        << error;
    EXPECT_TRUE(logged);
    EXPECT_EQ(captured_log.category, SDL_LOG_CATEGORY_APPLICATION);
    EXPECT_EQ(captured_log.priority, SDL_LOG_PRIORITY_INFO);
    EXPECT_NE(captured_log.message.find("network test_event"), std::string::npos);
    EXPECT_NE(captured_log.message.find("tick=12346"), std::string::npos);
    EXPECT_NE(captured_log.message.find("entity.ball.properties.velocity=(3.250,-1.750)"), std::string::npos);
    EXPECT_NE(captured_log.message.find("first"), std::string::npos);

    captured_log = {};
    logged = true;
    ASSERT_TRUE(sdl3d_game_data_log_network_snapshot_diagnostic(host, "multiplayer_state", 12347U, "test_event",
                                                                "second", &logged, error, sizeof(error)))
        << error;
    EXPECT_FALSE(logged);
    EXPECT_TRUE(captured_log.message.empty());

    destroy_runtime_session(host_session, host);
    destroy_runtime_session(client_session, client);
}

TEST(GameDataRuntime, RejectsPongNetworkSnapshotsWithMismatchedSchemaOrTruncation)
{
    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    sdl3d_game_session *client_session = nullptr;
    sdl3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);

    std::array<Uint8, 512> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_encode_network_snapshot(host, "play_state", 10U, packet.data(), packet.size(),
                                                        &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 24U);

    std::array<Uint8, 8> too_small{};
    size_t too_small_size = 0U;
    EXPECT_FALSE(sdl3d_game_data_encode_network_snapshot(host, "play_state", 10U, too_small.data(), too_small.size(),
                                                         &too_small_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;
    EXPECT_EQ(too_small_size, 0U);

    std::array<Uint8, 512> corrupted = packet;
    corrupted[16] ^= 0xffU;
    EXPECT_FALSE(
        sdl3d_game_data_apply_network_snapshot(client, corrupted.data(), packet_size, nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("schema hash"), std::string::npos) << error;

    EXPECT_FALSE(
        sdl3d_game_data_apply_network_snapshot(client, packet.data(), packet_size - 1U, nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("field data"), std::string::npos) << error;

    destroy_runtime_session(host_session, host);
    destroy_runtime_session(client_session, client);
}

TEST(GameDataRuntime, EncodesAndAppliesPooledActorNetworkSnapshots)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_replication");
    write_text(dir / "pool.game.json", actor_pool_replication_game_json(2).c_str());
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    sdl3d_game_session *host_session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &host_session));
    sdl3d_game_data_runtime *host = nullptr;
    char error[512]{};
    ASSERT_TRUE(
        sdl3d_game_data_load_file((dir / "pool.game.json").string().c_str(), host_session, &host, error, sizeof(error)))
        << error;

    sdl3d_game_session *client_session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &client_session));
    sdl3d_game_data_runtime *client = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "pool.game.json").string().c_str(), client_session, &client, error,
                                          sizeof(error)))
        << error;

    sdl3d_registered_actor *host_shot0 = sdl3d_game_data_find_actor(host, "pool.shots.0");
    sdl3d_registered_actor *host_shot1 = sdl3d_game_data_find_actor(host, "pool.shots.1");
    sdl3d_registered_actor *client_shot0 = sdl3d_game_data_find_actor(client, "pool.shots.0");
    sdl3d_registered_actor *client_shot1 = sdl3d_game_data_find_actor(client, "pool.shots.1");
    ASSERT_NE(host_shot0, nullptr);
    ASSERT_NE(host_shot1, nullptr);
    ASSERT_NE(client_shot0, nullptr);
    ASSERT_NE(client_shot1, nullptr);
    EXPECT_FALSE(host_shot0->active);
    EXPECT_FALSE(client_shot0->active);

    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(host_session),
                      sdl3d_game_data_find_signal(host, "signal.spawn"), nullptr);
    ASSERT_TRUE(host_shot0->active);
    EXPECT_FALSE(host_shot1->active);
    host_shot0->position = sdl3d_vec3_make(5.0f, 6.0f, 7.0f);
    sdl3d_properties_set_int(host_shot0->props, "damage", 11);

    std::array<Uint8, 256> packet{};
    size_t packet_size = 0U;
    ASSERT_TRUE(sdl3d_game_data_encode_network_snapshot(host, "pool_state", 55U, packet.data(), packet.size(),
                                                        &packet_size, error, sizeof(error)))
        << error;
    Uint32 tick = 0U;
    ASSERT_TRUE(sdl3d_game_data_apply_network_snapshot(client, packet.data(), packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 55U);
    EXPECT_TRUE(client_shot0->active);
    EXPECT_FALSE(client_shot1->active);
    expect_vec3_near(client_shot0->position, host_shot0->position);
    EXPECT_EQ(sdl3d_properties_get_int(client_shot0->props, "damage", 0), 11);
    EXPECT_STREQ(sdl3d_properties_get_string(client_shot0->props, "pool_lifecycle", ""), "active");

    char description[1024]{};
    ASSERT_TRUE(sdl3d_game_data_describe_network_snapshot(host, "pool_state", 56U, description, sizeof(description),
                                                          error, sizeof(error)))
        << error;
    EXPECT_NE(std::string(description).find("pool.shots.0.active=true"), std::string::npos);
    EXPECT_NE(std::string(description).find("pool.shots.0.properties.damage=11"), std::string::npos);

    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(host_session),
                      sdl3d_game_data_find_signal(host, "signal.despawn"), nullptr);
    ASSERT_FALSE(host_shot0->active);
    ASSERT_TRUE(sdl3d_game_data_encode_network_snapshot(host, "pool_state", 56U, packet.data(), packet.size(),
                                                        &packet_size, error, sizeof(error)))
        << error;
    ASSERT_TRUE(sdl3d_game_data_apply_network_snapshot(client, packet.data(), packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_FALSE(client_shot0->active);
    EXPECT_STREQ(sdl3d_properties_get_string(client_shot0->props, "pool_lifecycle", ""), "inactive");
    EXPECT_EQ(sdl3d_properties_get_int(client_shot0->props, "damage", 0), 1);
    expect_vec3_near(client_shot0->position, sdl3d_vec3_make(0.0f, 0.0f, 0.25f));

    destroy_runtime_session(host_session, host);
    destroy_runtime_session(client_session, client);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, PooledActorNetworkSchemaHashIncludesPoolCapacity)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_replication_hash");
    write_text(dir / "capacity_2.game.json", actor_pool_replication_game_json(2).c_str());
    write_text(dir / "capacity_3.game.json", actor_pool_replication_game_json(3).c_str());
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    char error[512]{};
    ASSERT_TRUE(
        sdl3d_game_data_validate_file((dir / "capacity_2.game.json").string().c_str(), nullptr, error, sizeof(error)))
        << error;
    ASSERT_TRUE(
        sdl3d_game_data_validate_file((dir / "capacity_3.game.json").string().c_str(), nullptr, error, sizeof(error)))
        << error;

    EXPECT_NE(load_network_schema_hash(dir / "capacity_2.game.json"),
              load_network_schema_hash(dir / "capacity_3.game.json"));
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsUnknownPooledActorNetworkReplicationRefs)
{
    const std::filesystem::path dir = unique_test_dir("bad_actor_pool_replication");
    const std::string network_json = R"json({
    "protocol": { "id": "sdl3d.test.pool.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "pool_state",
        "direction": "host_to_client",
        "rate": 60,
        "pools": [
          {
            "pool": "pool.missing",
            "fields": ["active"]
          }
        ]
      }
    ]
  })json";
    write_text(dir / "bad_pool_ref.game.json", network_schema_game_json(network_json).c_str());

    char error[512]{};
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "bad_pool_ref.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(std::string(error).find("actor pool"), std::string::npos) << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EncodesAndAppliesPongNetworkInputFromAuthoredSchema)
{
    sdl3d_game_session *client_session = nullptr;
    sdl3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(client));

    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(host));

    sdl3d_input_manager *client_input = sdl3d_game_session_get_input(client_session);
    sdl3d_input_manager *host_input = sdl3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);

    const int up_action = sdl3d_game_data_find_action(client, "action.paddle.local.up");
    const int down_action = sdl3d_game_data_find_action(client, "action.paddle.local.down");
    ASSERT_GE(up_action, 0);
    ASSERT_GE(down_action, 0);

    sdl3d_input_set_action_override(client_input, up_action, 0.75f);
    sdl3d_input_set_action_override(client_input, down_action, -0.25f);
    ASSERT_NE(sdl3d_input_update(client_input, 44), nullptr);

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_encode_network_input(client, "client_input", client_input, 44U, packet.data(),
                                                     packet.size(), &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 0U);

    Uint32 tick = 0U;
    ASSERT_TRUE(
        sdl3d_game_data_apply_network_input(host, host_input, packet.data(), packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 44U);

    ASSERT_NE(sdl3d_input_update(host_input, 45), nullptr);
    const int host_up_action = sdl3d_game_data_find_action(host, "action.paddle.local.up");
    const int host_down_action = sdl3d_game_data_find_action(host, "action.paddle.local.down");
    ASSERT_GE(host_up_action, 0);
    ASSERT_GE(host_down_action, 0);
    EXPECT_NEAR(sdl3d_input_get_value(host_input, host_up_action), 0.75f, 0.0001f);
    EXPECT_NEAR(sdl3d_input_get_value(host_input, host_down_action), -0.25f, 0.0001f);

    ASSERT_TRUE(sdl3d_game_data_clear_network_input_overrides(host, "client_input", host_input, error, sizeof(error)))
        << error;
    ASSERT_NE(sdl3d_input_update(host_input, 46), nullptr);
    EXPECT_NEAR(sdl3d_input_get_value(host_input, host_up_action), 0.0f, 0.0001f);
    EXPECT_NEAR(sdl3d_input_get_value(host_input, host_down_action), 0.0f, 0.0001f);

    destroy_runtime_session(client_session, client);
    destroy_runtime_session(host_session, host);
}

TEST(GameDataRuntime, RuntimeReplicationBindingsEncodeAndApplyPongPackets)
{
    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(host));

    sdl3d_game_session *client_session = nullptr;
    sdl3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(client));

    sdl3d_registered_actor *host_ball = sdl3d_game_data_find_actor(host, "entity.ball");
    sdl3d_registered_actor *client_ball = sdl3d_game_data_find_actor(client, "entity.ball");
    ASSERT_NE(host_ball, nullptr);
    ASSERT_NE(client_ball, nullptr);
    host_ball->position = {2.0f, 3.0f, 0.25f};

    std::array<Uint8, 512> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_encode_network_runtime_snapshot(host, "state_snapshot", 123U, packet.data(),
                                                                packet.size(), &packet_size, error, sizeof(error)))
        << error;

    Uint32 tick = 0U;
    ASSERT_TRUE(sdl3d_game_data_apply_network_runtime_snapshot(client, "state_snapshot", packet.data(), packet_size,
                                                               &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 123U);
    expect_vec3_near(client_ball->position, host_ball->position);

    EXPECT_FALSE(sdl3d_game_data_apply_network_runtime_input(host, "client_input",
                                                             sdl3d_game_session_get_input(host_session), packet.data(),
                                                             packet_size, nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("unsupported header"), std::string::npos) << error;

    sdl3d_input_manager *client_input = sdl3d_game_session_get_input(client_session);
    sdl3d_input_manager *host_input = sdl3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);
    const int client_up_action = sdl3d_game_data_find_action(client, "action.paddle.local.up");
    const int host_up_action = sdl3d_game_data_find_action(host, "action.paddle.local.up");
    ASSERT_GE(client_up_action, 0);
    ASSERT_GE(host_up_action, 0);
    sdl3d_input_set_action_override(client_input, client_up_action, 0.5f);
    ASSERT_NE(sdl3d_input_update(client_input, 321), nullptr);

    ASSERT_TRUE(sdl3d_game_data_encode_network_runtime_input(client, "client_input", client_input, 321U, packet.data(),
                                                             packet.size(), &packet_size, error, sizeof(error)))
        << error;
    ASSERT_TRUE(sdl3d_game_data_apply_network_runtime_input(host, "client_input", host_input, packet.data(),
                                                            packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 321U);
    ASSERT_NE(sdl3d_input_update(host_input, 322), nullptr);
    EXPECT_NEAR(sdl3d_input_get_value(host_input, host_up_action), 0.5f, 0.0001f);

    EXPECT_FALSE(sdl3d_game_data_apply_network_runtime_snapshot(client, "state_snapshot", packet.data(), packet_size,
                                                                nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("unsupported header"), std::string::npos) << error;

    destroy_runtime_session(host_session, host);
    destroy_runtime_session(client_session, client);
}

TEST(GameDataRuntime, RejectsPongNetworkInputWithMismatchedSchemaOrTruncation)
{
    sdl3d_game_session *client_session = nullptr;
    sdl3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    sdl3d_game_session *host_session = nullptr;
    sdl3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);

    sdl3d_input_manager *client_input = sdl3d_game_session_get_input(client_session);
    sdl3d_input_manager *host_input = sdl3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);
    ASSERT_NE(sdl3d_input_update(client_input, 10), nullptr);

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_encode_network_input(client, "client_input", client_input, 10U, packet.data(),
                                                     packet.size(), &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 24U);

    std::array<Uint8, 8> too_small{};
    size_t too_small_size = 0U;
    EXPECT_FALSE(sdl3d_game_data_encode_network_input(client, "client_input", client_input, 10U, too_small.data(),
                                                      too_small.size(), &too_small_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;
    EXPECT_EQ(too_small_size, 0U);

    std::array<Uint8, 128> corrupted = packet;
    corrupted[16] ^= 0xffU;
    EXPECT_FALSE(sdl3d_game_data_apply_network_input(host, host_input, corrupted.data(), packet_size, nullptr, error,
                                                     sizeof(error)));
    EXPECT_NE(std::string(error).find("schema hash"), std::string::npos) << error;

    EXPECT_FALSE(sdl3d_game_data_apply_network_input(host, host_input, packet.data(), packet_size - 1U, nullptr, error,
                                                     sizeof(error)));
    EXPECT_NE(std::string(error).find("action data"), std::string::npos) << error;

    destroy_runtime_session(client_session, client);
    destroy_runtime_session(host_session, host);
}

TEST(GameDataRuntime, EncodesDecodesAndAppliesPongNetworkControlFromAuthoredSchema)
{
    sdl3d_game_session *sender_session = nullptr;
    sdl3d_game_data_runtime *sender = nullptr;
    load_pong_runtime(&sender_session, &sender);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(sender));

    sdl3d_game_session *receiver_session = nullptr;
    sdl3d_game_data_runtime *receiver = nullptr;
    load_pong_runtime(&receiver_session, &receiver);
    ASSERT_TRUE(sdl3d_game_data_has_network_schema(receiver));

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_encode_network_control(sender, "pause_request", 77U, packet.data(), packet.size(),
                                                       &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 0U);

    sdl3d_game_data_network_control control{};
    ASSERT_TRUE(
        sdl3d_game_data_decode_network_control(receiver, packet.data(), packet_size, &control, error, sizeof(error)))
        << error;
    EXPECT_STREQ(control.name, "pause_request");
    EXPECT_EQ(control.direction, SDL3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL);
    EXPECT_EQ(control.signal_id, sdl3d_game_data_find_signal(receiver, "signal.network.pause_changed"));
    EXPECT_EQ(control.tick, 77U);

    NetworkSignalCapture capture;
    const int connection = sdl3d_signal_connect(sdl3d_game_session_get_signal_bus(receiver_session), control.signal_id,
                                                capture_signal_payload, &capture);
    ASSERT_GT(connection, 0);
    sdl3d_game_data_network_control applied{};
    ASSERT_TRUE(
        sdl3d_game_data_apply_network_control(receiver, packet.data(), packet_size, &applied, error, sizeof(error)))
        << error;
    EXPECT_STREQ(applied.name, "pause_request");
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(capture.signal_id, control.signal_id);
    EXPECT_EQ(capture.network_control, "pause_request");
    EXPECT_EQ(capture.network_direction, "bidirectional");
    EXPECT_EQ(capture.network_tick, 77);
    sdl3d_signal_disconnect(sdl3d_game_session_get_signal_bus(receiver_session), connection);

    destroy_runtime_session(sender_session, sender);
    destroy_runtime_session(receiver_session, receiver);
}

TEST(GameDataRuntime, ResolvesRuntimeControlBindingsForGenericNetworkLoops)
{
    const std::filesystem::path dir = unique_test_dir("network_runtime_control_bindings");
    std::string network_json = valid_network_schema_json();
    const size_t insert = network_json.rfind("\n  }");
    ASSERT_NE(insert, std::string::npos);
    network_json.insert(insert, R"json(,
    "runtime_bindings": {
      "controls": {
        "semantic_pause": "pause"
      }
    })json");
    write_text(dir / "network_runtime_controls.game.json", network_schema_game_json(network_json).c_str());

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));
    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "network_runtime_controls.game.json").string().c_str(), session,
                                          &runtime, error, sizeof(error)))
        << error;

    const char *control_name = nullptr;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_control(runtime, "semantic_pause", &control_name));
    EXPECT_STREQ(control_name, "pause");

    const char *binding_name = nullptr;
    ASSERT_TRUE(sdl3d_game_data_get_network_runtime_control_binding(runtime, "pause", &binding_name));
    EXPECT_STREQ(binding_name, "semantic_pause");

    std::array<Uint8, SDL3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE> packet{};
    size_t packet_size = 0U;
    ASSERT_TRUE(sdl3d_game_data_encode_network_runtime_control(runtime, "semantic_pause", 99U, packet.data(),
                                                               packet.size(), &packet_size, error, sizeof(error)))
        << error;
    EXPECT_EQ(packet_size, SDL3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE);

    sdl3d_game_data_network_control control{};
    const char *decoded_binding = nullptr;
    ASSERT_TRUE(sdl3d_game_data_decode_network_runtime_control(runtime, packet.data(), packet_size, &decoded_binding,
                                                               &control, error, sizeof(error)))
        << error;
    EXPECT_STREQ(decoded_binding, "semantic_pause");
    EXPECT_STREQ(control.name, "pause");
    EXPECT_EQ(control.tick, 99U);

    EXPECT_FALSE(sdl3d_game_data_encode_network_runtime_control(runtime, "missing", 1U, packet.data(), packet.size(),
                                                                &packet_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("network runtime control binding 'missing' not found"), std::string::npos)
        << error;

    ASSERT_TRUE(sdl3d_game_data_encode_network_control(runtime, "start_game", 100U, packet.data(), packet.size(),
                                                       &packet_size, error, sizeof(error)))
        << error;
    EXPECT_FALSE(sdl3d_game_data_decode_network_runtime_control(runtime, packet.data(), packet_size, &decoded_binding,
                                                                &control, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("network runtime control binding for 'start_game' not found"), std::string::npos)
        << error;

    destroy_runtime_session(session, runtime);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsPongNetworkControlWithMismatchedSchemaOrBadSize)
{
    sdl3d_game_session *sender_session = nullptr;
    sdl3d_game_data_runtime *sender = nullptr;
    load_pong_runtime(&sender_session, &sender);
    sdl3d_game_session *receiver_session = nullptr;
    sdl3d_game_data_runtime *receiver = nullptr;
    load_pong_runtime(&receiver_session, &receiver);

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(sdl3d_game_data_encode_network_control(sender, "disconnect", 88U, packet.data(), packet.size(),
                                                       &packet_size, error, sizeof(error)))
        << error;
    ASSERT_EQ(packet_size, SDL3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE);
    ASSERT_GT(packet_size, 32U);

    std::array<Uint8, 8> too_small{};
    size_t too_small_size = 0U;
    EXPECT_FALSE(sdl3d_game_data_encode_network_control(sender, "disconnect", 88U, too_small.data(), too_small.size(),
                                                        &too_small_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;
    EXPECT_EQ(too_small_size, 0U);

    std::array<Uint8, 128> corrupted = packet;
    corrupted[16] ^= 0xffU;
    EXPECT_FALSE(
        sdl3d_game_data_decode_network_control(receiver, corrupted.data(), packet_size, nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("schema hash"), std::string::npos) << error;

    EXPECT_FALSE(sdl3d_game_data_decode_network_control(receiver, packet.data(), packet_size - 1U, nullptr, error,
                                                        sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;

    destroy_runtime_session(sender_session, sender);
    destroy_runtime_session(receiver_session, receiver);
}

TEST(GameDataRuntime, AuthoredDirectConnectActionsUpdateSceneState)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    sdl3d_properties_set_string(scene_state, "direct_connect_host", "");
    sdl3d_properties_set_string(scene_state, "direct_connect_port", "27183");
    sdl3d_properties_set_string(scene_state, "direct_connect_status", "Ready");
    sdl3d_properties_set_string(scene_state, "direct_connect_state", "disconnected");
    sdl3d_properties_set_bool(scene_state, "direct_connect_connected", true);

    const int connect_signal = sdl3d_game_data_find_signal(runtime, "signal.multiplayer.direct.connect");
    ASSERT_GE(connect_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(sdl3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""), "Invalid host");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    sdl3d_properties_set_string(scene_state, "direct_connect_host", "127.0.0.1");
    sdl3d_properties_set_string(scene_state, "direct_connect_port", "0");
    sdl3d_properties_set_bool(scene_state, "direct_connect_connected", true);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(sdl3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""), "Invalid port");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    const int disconnect_signal = sdl3d_game_data_find_signal(runtime, "signal.multiplayer.direct.disconnect");
    ASSERT_GE(disconnect_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), disconnect_signal, nullptr);

    EXPECT_EQ(sdl3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""), "Disconnected");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_state", ""), "disconnected");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RuntimeOwnedHostSessionPublishesSceneState)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);

    const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_name =
        test_info != nullptr ? std::string(test_info->test_suite_name()) + "." + test_info->name() : "host";
    const int port = 30000 + (int)(std::hash<std::string>{}(test_name) % 20000U);
    if (!sdl3d_game_data_network_host_start(runtime, "test_host", port, "SDL3D Test", "host_status", "host_endpoint",
                                            "host_peer", "host_connected"))
    {
        sdl3d_game_data_destroy(runtime);
        sdl3d_game_session_destroy(session);
        GTEST_SKIP() << "network host session unavailable: " << SDL_GetError();
    }

    sdl3d_network_session *host = sdl3d_game_data_get_network_host_session(runtime, "test_host");
    ASSERT_NE(host, nullptr);
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "host_connected", true));
    EXPECT_NE(std::string(sdl3d_properties_get_string(scene_state, "host_endpoint", "")).find("UDP "),
              std::string::npos);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "host_peer", ""), "Waiting for client");

    ASSERT_TRUE(sdl3d_game_data_network_host_publish_status(runtime, "test_host", "host_status", "host_endpoint",
                                                            "host_peer", "host_connected"));
    ASSERT_TRUE(sdl3d_game_data_network_host_cancel(runtime, "test_host", "host_status", "host_endpoint", "host_peer",
                                                    "host_connected", "Not hosting"));
    EXPECT_EQ(sdl3d_game_data_get_network_host_session(runtime, "test_host"), nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "host_status", ""), "Not hosting");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "host_connected", true));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AuthoredDiscoveryConnectActionsUpdateSceneState)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    const int connect_signal = sdl3d_game_data_find_signal(runtime, "signal.multiplayer.discovery.connect");
    ASSERT_GE(connect_signal, 0);

    sdl3d_properties_set_int(scene_state, "local_match_index", 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(sdl3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""), "No session selected");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "label", "Local Pong Host"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "name", "Local Pong Host"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "host", "127.0.0.1"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_int(runtime, "local_matches", 0, "port", 0));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "endpoint", "127.0.0.1:0"));
    EXPECT_EQ(sdl3d_game_data_runtime_collection_count(runtime, "local_matches"), 1);

    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(sdl3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_host", ""), "127.0.0.1");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_port", ""), "0");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""), "Invalid port");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(sdl3d_properties_get_bool(scene_state, "direct_connect_connected", true));
    EXPECT_EQ(sdl3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);

    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "label", "Valid Pong Host"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "name", "Valid Pong Host"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "host", "127.0.0.1"));
    ASSERT_TRUE(sdl3d_game_data_runtime_collection_set_int(runtime, "local_matches", 0, "port", 65535));
    ASSERT_TRUE(
        sdl3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "endpoint", "127.0.0.1:65535"));
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_host", ""), "127.0.0.1");
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_port", ""), "65535");
    EXPECT_EQ(sdl3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);
    if (sdl3d_game_data_get_network_direct_connect_session(runtime, "direct_connect") != nullptr)
    {
        EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""),
                     "Match found. Connecting...");
    }
    else
    {
        EXPECT_NE(std::string(sdl3d_properties_get_string(scene_state, "direct_connect_status", ""))
                      .find("networking is disabled"),
                  std::string::npos);
    }

    const int cancel_signal = sdl3d_game_data_find_signal(runtime, "signal.multiplayer.discovery.cancel");
    ASSERT_GE(cancel_signal, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), cancel_signal, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(scene_state, "local_match_status", ""), "Discovery canceled");
    EXPECT_EQ(sdl3d_properties_get_int(scene_state, "local_match_count", -1), 0);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RejectsInvalidDirectConnectActions)
{
    const std::filesystem::path dir = unique_test_dir("bad_direct_connect_actions");
    write_text(dir / "bad_direct_connect.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Bad Direct Connect", "id": "test.bad_direct_connect", "version": "0.1.0" },
  "world": { "name": "world.bad_direct_connect", "kind": "fixed_screen" },
  "entities": [],
  "signals": ["signal.connect"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.connect",
        "actions": [
          { "type": "network.direct_connect.start", "name": "direct", "host": "127.0.0.1", "port": 70000 }
        ]
      }
    ]
  }
})json");

    char error[512]{};
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "bad_direct_connect.game.json").string().c_str(), nullptr, error,
                                               sizeof(error)));
    EXPECT_NE(
        std::string(error).find("network.direct_connect.start port must be a non-empty string or integer 1..65535"),
        std::string::npos)
        << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidHostActions)
{
    const std::filesystem::path dir = unique_test_dir("bad_host_actions");
    write_text(dir / "bad_host.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Bad Host", "id": "test.bad_host", "version": "0.1.0" },
  "world": { "name": "world.bad_host", "kind": "fixed_screen" },
  "entities": [],
  "signals": ["signal.host"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.host",
        "actions": [
          { "type": "network.host.start", "name": "host", "port": 70000 }
        ]
      }
    ]
  }
})json");

    char error[512]{};
    EXPECT_FALSE(
        sdl3d_game_data_validate_file((dir / "bad_host.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("network.host.start port must be a non-empty string or integer 1..65535"),
              std::string::npos)
        << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidDiscoveryActions)
{
    struct Case
    {
        const char *name;
        const char *action_json;
        const char *expected_error;
    };

    const Case cases[] = {
        {
            "bad_port",
            R"json({ "type": "network.discovery.start", "name": "local", "collection": "matches", "port": 70000 })json",
            "network.discovery port must be a non-empty string or integer 1..65535",
        },
        {
            "missing_collection",
            R"json({ "type": "network.discovery.observe", "name": "local" })json",
            "network.discovery.observe requires a non-empty collection",
        },
        {
            "missing_selection",
            R"json({ "type": "network.discovery.connect_selected", "name": "local", "collection": "matches", "direct_connect_name": "direct" })json",
            "network.discovery.connect_selected requires selected_index_key or selected_index",
        },
    };

    const std::filesystem::path dir = unique_test_dir("bad_discovery_actions");
    for (const Case &test_case : cases)
    {
        const std::string json = std::string(R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Bad Discovery", "id": "test.bad_discovery", "version": "0.1.0" },
  "world": { "name": "world.bad_discovery", "kind": "fixed_screen" },
  "entities": [],
  "signals": ["signal.discovery"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.discovery",
        "actions": [
)json") + test_case.action_json +
                                 R"json(
        ]
      }
    ]
  }
})json";
        write_text(dir / (std::string(test_case.name) + ".game.json"), json.c_str());

        char error[512]{};
        EXPECT_FALSE(sdl3d_game_data_validate_file(
            (dir / (std::string(test_case.name) + ".game.json")).string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidNetworkReplicationSchemas)
{
    struct Case
    {
        const char *name;
        std::string network_json;
        const char *expected_error;
    };

    const Case cases[] = {
        {
            "unknown_entity",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          { "entity": "entity.missing", "fields": [ "position" ] }
        ]
      }
    ]
  })json",
            "unknown entity reference",
        },
        {
            "duplicate_field",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": [
              { "path": "properties.velocity", "type": "vec3" },
              { "path": "properties.velocity", "type": "vec3" }
            ]
          }
        ]
      }
    ]
  })json",
            "duplicate network actor field",
        },
        {
            "unsupported_field_type",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": [
              { "path": "properties.transform", "type": "mat4" }
            ]
          }
        ]
      }
    ]
  })json",
            "network actor field must",
        },
        {
            "number_alias_is_not_a_network_field_type",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": [
              { "path": "properties.speed", "type": "number" }
            ]
          }
        ]
      }
    ]
  })json",
            "network actor field must",
        },
        {
            "unknown_action",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "client_input",
        "direction": "client_to_host",
        "rate": 60,
        "inputs": [
          { "action": "action.missing" }
        ]
      }
    ]
  })json",
            "unknown input action reference",
        },
        {
            "bad_direction",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "sideways",
        "rate": 60,
        "actors": [
          { "entity": "entity.ball", "fields": [ "position" ] }
        ]
      }
    ]
  })json",
            "network replication direction",
        },
        {
            "duplicate_replication_channel",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          { "entity": "entity.ball", "fields": [ "position" ] }
        ]
      },
      {
        "name": "play_state",
        "direction": "client_to_host",
        "rate": 60,
        "inputs": [
          { "action": "action.remote.up" }
        ]
      }
    ]
  })json",
            "duplicate network replication",
        },
        {
            "duplicate_control_message",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          { "entity": "entity.ball", "fields": [ "position" ] }
        ]
      }
    ],
    "control_messages": [
      { "name": "pause", "direction": "bidirectional", "signal": "signal.network.pause" },
      { "name": "pause", "direction": "host_to_client", "signal": "signal.network.start" }
    ]
  })json",
            "duplicate network control message",
        },
        {
            "host_to_client_missing_actors",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60
      }
    ]
  })json",
            "host_to_client network replication must declare actors",
        },
        {
            "client_to_host_missing_inputs",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "replication": [
      {
        "name": "client_input",
        "direction": "client_to_host",
        "rate": 60
      }
    ]
  })json",
            "client_to_host network replication must declare inputs",
        },
        {
            "bad_scene_state_key",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "scene_state": {
      "host": {
        "status": ""
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "network scene_state key value must be a non-empty string",
        },
        {
            "bad_session_flow_state_key",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "session_flow": {
      "state_keys": {
        "match_mode": ""
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "network session_flow state_keys value must be a non-empty string",
        },
        {
            "bad_session_flow_message",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "session_flow": {
      "messages": {
        "disconnect_reasons": {
          "host_exited": ""
        }
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "network session_flow messages value must be a non-empty string",
        },
        {
            "bad_session_flow_event",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "session_flow": {
      "events": {
        "disconnect": {
          "pause": true,
          "actions": [
            { "type": "scene_state.set", "value": true }
          ]
        }
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "scene_state.set requires a non-empty key",
        },
        {
            "bad_managed_network_missing_scene_semantic",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "session_flow": {
      "managed_runtime": {
        "enabled": true,
        "termination_ack_delay_seconds": 3.0
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "managed network requires session scene 'play'",
        },
        {
            "bad_managed_network_keep_alive_scene",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "session_flow": {
      "managed_runtime": {
        "enabled": false,
        "keep_alive_scenes": {
          "host": ["missing"]
        }
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "managed network keep-alive scene must reference session_flow.scenes",
        },
        {
            "bad_runtime_replication_binding",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "replication": {
        "state_snapshot": "missing_channel"
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "unknown network replication reference",
        },
        {
            "bad_snapshot_diagnostic_replication",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "diagnostics": {
      "snapshots": [
        {
          "name": "multiplayer_state",
          "replication": "missing_channel"
        }
      ]
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "unknown network replication reference",
        },
        {
            "bad_snapshot_diagnostic_level",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "diagnostics": {
      "snapshots": [
        {
          "name": "multiplayer_state",
          "replication": "play_state",
          "level": "chatty"
        }
      ]
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "network snapshot diagnostic level is unsupported",
        },
        {
            "bad_runtime_control_binding",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "controls": {
        "pause_request": "missing_control"
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ],
    "control_messages": [
      { "name": "pause", "direction": "bidirectional", "signal": "signal.network.pause" }
    ]
  })json",
            "unknown network control message reference",
        },
        {
            "duplicate_runtime_control_binding_value",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "controls": {
        "pause_request": "pause",
        "resume_request": "pause"
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ],
    "control_messages": [
      { "name": "pause", "direction": "bidirectional", "signal": "signal.network.pause" }
    ]
  })json",
            "duplicate network runtime binding value 'pause'",
        },
        {
            "bad_runtime_action_binding",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "actions": {
        "menu_select": "action.missing"
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "unknown input action reference",
        },
        {
            "bad_runtime_signal_binding",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "signals": {
        "ui_select": "signal.missing"
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "unknown signal reference",
        },
        {
            "bad_runtime_pause_action_binding",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "pause": {
        "action": "action.missing.pause",
        "state": { "actor": "entity.match", "property": "paused" }
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "unknown input action reference",
        },
        {
            "bad_runtime_pause_state_actor_binding",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "pause": {
        "action": "action.pause",
        "state": { "actor": "entity.missing.match", "property": "paused" }
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "unknown entity reference",
        },
        {
            "bad_runtime_pause_state_property_binding",
            R"json({
    "protocol": { "id": "sdl3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
    "runtime_bindings": {
      "pause": {
        "action": "action.pause",
        "state": { "actor": "entity.match", "property": "" }
      }
    },
    "replication": [
      {
        "name": "play_state",
        "direction": "host_to_client",
        "rate": 60,
        "actors": [
          {
            "entity": "entity.ball",
            "fields": ["position"]
          }
        ]
      }
    ]
  })json",
            "network runtime_bindings pause state property must be a non-empty string",
        },
    };

    const std::filesystem::path dir = unique_test_dir("bad_network_schema");
    for (const Case &test_case : cases)
    {
        const std::filesystem::path path = dir / (std::string(test_case.name) + ".game.json");
        write_text(path, network_schema_game_json(test_case.network_json, test_case.name).c_str());
        char error[512]{};
        EXPECT_FALSE(sdl3d_game_data_validate_file(path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }
    remove_test_dir(dir);
}

TEST(GameDataRuntime, MaterializesAudioAssetsThroughAuthoredCacheStorage)
{
    const std::filesystem::path dir = unique_test_dir("audio_cache_storage");
    const std::filesystem::path user_root = dir / "user";
    const std::filesystem::path cache_root = dir / "cache";
    write_text(dir / "tone.wav", "audio bytes");

    const std::string game_json = std::string(R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Audio Cache", "id": "test.audio_cache", "version": "0.1.0" },
  "storage": {
    "organization": "Blue Sentinel Security",
    "application": "Audio Cache Test",
    "user_root_override": ")json") +
                                  user_root.generic_string() + R"json(",
    "cache_root_override": ")json" +
                                  cache_root.generic_string() + R"json("
  },
  "world": { "name": "world.audio_cache", "kind": "fixed_screen" },
  "entities": []
})json";
    write_text(dir / "audio_cache.game.json", game_json.c_str());

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "audio_cache.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    char materialized_path[4096]{};
    ASSERT_TRUE(
        sdl3d_game_data_prepare_audio_file(runtime, "asset://tone.wav", materialized_path, sizeof(materialized_path)));
    const std::filesystem::path materialized(materialized_path);
    EXPECT_TRUE(std::filesystem::exists(materialized));
    EXPECT_EQ(materialized.parent_path().filename().string(), "audio");

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::equivalent(materialized.parent_path().parent_path(), cache_root, ec));

    std::ifstream in(materialized, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "audio bytes");

    char cached_path[4096]{};
    ASSERT_TRUE(sdl3d_game_data_prepare_audio_file(runtime, "asset://tone.wav", cached_path, sizeof(cached_path)));
    EXPECT_STREQ(cached_path, materialized_path);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, LuaStorageBindingsUseSafeVirtualRoots)
{
    const std::filesystem::path dir = unique_test_dir("lua_storage");
    const std::filesystem::path user_root = dir / "user";
    const std::filesystem::path cache_root = dir / "cache";

    write_text(dir / "scripts" / "storage.lua",
               R"lua(local storage = {}

function storage.roundtrip(_, _, ctx)
    local unsafe_ok = ctx.storage.write("user://../escape.txt", "no")
    if unsafe_ok then
        return false
    end

    local ghost = { _name = "entity.missing" }
    local gx, gy, gz = sdl3d.get_vec3(ghost, "velocity")
    if gx ~= nil or gy ~= nil or gz ~= nil then
        return false
    end

    local ok = ctx.storage.write("user://settings/options.json", "{\"difficulty\":\"hard\"}")
    if not ok or not ctx.storage.exists("user://settings/options.json") then
        return false
    end

    local data = ctx.storage.read("user://settings/options.json")
    local decoded, decode_error = sdl3d.json.decode(data)
    if decoded == nil or decode_error ~= nil or decoded.difficulty ~= "hard" then
        return false
    end
    local encoded = sdl3d.json.encode({ difficulty = decoded.difficulty, enabled = true, values = { 1, 2, 3 } })
    local roundtrip = sdl3d.json.decode(encoded)
    if roundtrip == nil or roundtrip.enabled ~= true or roundtrip.values[2] ~= 2 then
        return false
    end

    if not ctx.storage.mkdir("cache://script") then
        return false
    end
    if not ctx.storage.write("cache://script/status.txt", "cached") then
        return false
    end

    ctx:state_set("loaded_options", roundtrip.difficulty .. ":" .. tostring(roundtrip.values[2]) .. ":" .. tostring(roundtrip.enabled))
    return true
end

return storage
)lua");

    const std::string game_json = std::string(R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Lua Storage", "id": "test.lua_storage", "version": "0.1.0" },
  "storage": {
    "organization": "Blue Sentinel Security",
    "application": "Lua Storage Test",
    "user_root_override": ")json") +
                                  user_root.generic_string() + R"json(",
    "cache_root_override": ")json" +
                                  cache_root.generic_string() + R"json("
  },
  "scripts": [
    { "id": "script.storage", "path": "scripts/storage.lua", "module": "test.storage" }
  ],
  "world": { "name": "world.lua_storage", "kind": "fixed_screen" },
  "entities": [],
  "signals": [ "signal.storage.roundtrip" ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.storage.roundtrip",
        "actions": [
          { "type": "adapter.invoke", "adapter": "adapter.storage.roundtrip" }
        ]
      }
    ]
  },
  "adapters": [
    {
      "name": "adapter.storage.roundtrip",
      "kind": "action",
      "script": "script.storage",
      "function": "roundtrip"
    }
  ]
})json";
    write_text(dir / "lua_storage.game.json", game_json.c_str());

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file((dir / "lua_storage.game.json").string().c_str(), session, &runtime, error,
                                          sizeof(error)))
        << error;

    const int signal_id = sdl3d_game_data_find_signal(runtime, "signal.storage.roundtrip");
    ASSERT_GE(signal_id, 0);
    sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), signal_id, nullptr);

    EXPECT_STREQ(sdl3d_properties_get_string(sdl3d_game_data_scene_state(runtime), "loaded_options", ""),
                 "hard:2:true");
    EXPECT_TRUE(std::filesystem::exists(user_root / "settings" / "options.json"));
    EXPECT_TRUE(std::filesystem::exists(cache_root / "script" / "status.txt"));
    EXPECT_FALSE(std::filesystem::exists(dir / "escape.txt"));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, GenericPersistenceSavesOptionsAndPongLuaLoadsHighScores)
{
    const std::filesystem::path dir = unique_test_dir("pong_persistence");
    const std::filesystem::path user_root = dir / "user";
    const std::filesystem::path cache_root = dir / "cache";
    const std::filesystem::path game_path = copy_pong_data_with_storage_overrides(dir, user_root, cache_root);

    auto emit = [](sdl3d_game_session *session, sdl3d_game_data_runtime *runtime, const char *signal) {
        const int signal_id = sdl3d_game_data_find_signal(runtime, signal);
        EXPECT_GE(signal_id, 0) << signal;
        if (signal_id >= 0)
            sdl3d_signal_emit(sdl3d_game_session_get_signal_bus(session), signal_id, nullptr);
    };

    {
        sdl3d_game_session *session = nullptr;
        ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

        char error[512]{};
        sdl3d_game_data_runtime *runtime = nullptr;
        ASSERT_TRUE(sdl3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
            << error;

        sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
        ASSERT_NE(settings, nullptr);
        EXPECT_FALSE(sdl3d_properties_get_bool(settings->props, "options_persistence_enabled", true));
        EXPECT_TRUE(sdl3d_properties_get_bool(settings->props, "score_persistence_enabled", false));

        emit(session, runtime, "signal.persistence.save_options");
        EXPECT_FALSE(std::filesystem::exists(user_root / "settings" / "options.json"));

        emit(session, runtime, "signal.persistence.load");
        EXPECT_FALSE(sdl3d_properties_get_bool(settings->props, "options_persistence_enabled", true));

        sdl3d_properties_set_string(settings->props, "display_mode", "windowed");
        sdl3d_properties_set_bool(settings->props, "vsync", false);
        sdl3d_properties_set_string(settings->props, "renderer", "opengl");
        sdl3d_properties_set_string(settings->props, "gamepad_icons", "playstation");
        sdl3d_properties_set_bool(settings->props, "vibration", false);
        sdl3d_properties_set_int(settings->props, "sfx_volume", 4);
        sdl3d_properties_set_int(settings->props, "music_volume", 7);
        sdl3d_properties_set_bool(settings->props, "options_persistence_enabled", true);
        emit(session, runtime, "signal.persistence.save_options");

        emit(session, runtime, "signal.match.player_won");
        sdl3d_registered_actor *scores = sdl3d_game_data_find_actor(runtime, "entity.high_scores");
        ASSERT_NE(scores, nullptr);
        EXPECT_EQ(sdl3d_properties_get_int(scores->props, "player_wins", 0), 1);
        EXPECT_EQ(sdl3d_properties_get_int(scores->props, "matches_played", 0), 1);
        EXPECT_STREQ(sdl3d_properties_get_string(scores->props, "latest_winner", ""), "player");

        sdl3d_game_data_destroy(runtime);
        sdl3d_game_session_destroy(session);
    }

    EXPECT_TRUE(std::filesystem::exists(user_root / "settings" / "options.json"));
    EXPECT_TRUE(std::filesystem::exists(user_root / "scores" / "pong_scores.json"));
    const std::string options_text = read_text(user_root / "settings" / "options.json");
    EXPECT_NE(options_text.find("\"schema\": \"sdl3d.options.v1\""), std::string::npos);
    EXPECT_NE(options_text.find("\"version\": 1"), std::string::npos);
    EXPECT_NE(options_text.find("\"display_mode\": \"windowed\""), std::string::npos);
    EXPECT_NE(options_text.find("\"gamepad_icons\": \"playstation\""), std::string::npos);

    sdl3d_game_config persisted_config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(sdl3d_game_data_load_app_config_file(game_path.string().c_str(), &persisted_config, title,
                                                     sizeof(title), app_error, sizeof(app_error)))
        << app_error;
    EXPECT_EQ(persisted_config.display_mode, SDL3D_WINDOW_MODE_WINDOWED);
    EXPECT_EQ(persisted_config.backend, SDL3D_BACKEND_OPENGL);
    EXPECT_GT(persisted_config.vsync, 0);

    {
        sdl3d_game_session *session = nullptr;
        ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

        char error[512]{};
        sdl3d_game_data_runtime *runtime = nullptr;
        ASSERT_TRUE(sdl3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
            << error;

        emit(session, runtime, "signal.persistence.load");

        sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
        ASSERT_NE(settings, nullptr);
        EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
        EXPECT_TRUE(sdl3d_properties_get_bool(settings->props, "vsync", false));
        EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "renderer", ""), "opengl");
        EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "gamepad_icons", ""), "xbox");
        EXPECT_TRUE(sdl3d_properties_get_bool(settings->props, "vibration", false));
        EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
        EXPECT_EQ(sdl3d_properties_get_int(settings->props, "music_volume", 0), 7);

        sdl3d_properties_set_bool(settings->props, "options_persistence_enabled", true);
        emit(session, runtime, "signal.persistence.load");
        EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
        EXPECT_FALSE(sdl3d_properties_get_bool(settings->props, "vsync", true));
        EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "renderer", ""), "opengl");
        EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "gamepad_icons", ""), "playstation");
        EXPECT_FALSE(sdl3d_properties_get_bool(settings->props, "vibration", true));
        EXPECT_EQ(sdl3d_properties_get_int(settings->props, "sfx_volume", 0), 4);
        EXPECT_EQ(sdl3d_properties_get_int(settings->props, "music_volume", 0), 7);

        sdl3d_registered_actor *scores = sdl3d_game_data_find_actor(runtime, "entity.high_scores");
        ASSERT_NE(scores, nullptr);
        EXPECT_EQ(sdl3d_properties_get_int(scores->props, "player_wins", 0), 1);
        EXPECT_EQ(sdl3d_properties_get_int(scores->props, "cpu_wins", -1), 0);
        EXPECT_EQ(sdl3d_properties_get_int(scores->props, "matches_played", 0), 1);
        EXPECT_STREQ(sdl3d_properties_get_string(scores->props, "latest_winner", ""), "player");

        emit(session, runtime, "signal.match.cpu_won");
        EXPECT_EQ(sdl3d_properties_get_int(scores->props, "cpu_wins", 0), 1);
        EXPECT_EQ(sdl3d_properties_get_int(scores->props, "matches_played", 0), 2);
        EXPECT_STREQ(sdl3d_properties_get_string(scores->props, "latest_winner", ""), "cpu");

        sdl3d_game_data_destroy(runtime);
        sdl3d_game_session_destroy(session);
    }

    remove_test_dir(dir);
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

TEST(GameDataRuntime, RejectsLegacySplashSceneBlock)
{
    const std::filesystem::path dir = unique_test_dir("legacy_splash");
    write_text(dir / "legacy_splash.game.json",
               R"json({
  "schema": "sdl3d.game.v0",
  "metadata": { "name": "Legacy Splash", "id": "test.legacy_splash", "version": "0.1.0" },
  "scenes": {
    "initial": "scene.splash",
    "files": [
      "scenes/splash.scene.json",
      "scenes/title.scene.json"
    ]
  }
})json");
    write_text(dir / "scenes" / "splash.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.splash",
  "updates_game": false,
  "renders_world": false,
  "splash": {
    "next_scene": "scene.title",
    "hold_seconds": 1.0,
    "skip_on_input": true
  }
})json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false
})json");

    DiagnosticCapture capture;
    sdl3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    EXPECT_FALSE(sdl3d_game_data_validate_file((dir / "legacy_splash.game.json").string().c_str(), &options, error,
                                               sizeof(error)));
    ASSERT_FALSE(capture.diagnostics.empty());
    EXPECT_EQ(capture.diagnostics[0].severity, SDL3D_GAME_DATA_DIAGNOSTIC_ERROR);
    EXPECT_NE(capture.diagnostics[0].message.find("scene.timeline"), std::string::npos);
    EXPECT_NE(std::string(error).find("scene splash is no longer supported"), std::string::npos);

    remove_test_dir(dir);
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
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 1.0f / 60.0f));

    EXPECT_EQ(sdl3d_properties_get_int(cpu_score->props, "value", 0), 1);
    EXPECT_FLOAT_EQ(ball->position.x, 0.0f);
    EXPECT_FALSE(sdl3d_properties_get_bool(ball->props, "active_motion", true));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}
