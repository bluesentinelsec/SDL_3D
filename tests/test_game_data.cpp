#include <gtest/gtest.h>

#include <array>
#include <cfloat>
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
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/asset.h"
#include "slayer3d/data_game.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data.h"
#include "slayer3d/game_presentation.h"
#include "slayer3d/image.h"
#include "slayer3d/math.h"
#include "slayer3d/model.h"
#include "slayer3d/properties.h"
#include "slayer3d/signal_bus.h"
#include "slayer3d/timer_pool.h"
}

namespace
{

struct AdapterCapture
{
    int calls = 0;
};

struct CapturedDiagnostic
{
    slayer3d_game_data_diagnostic_severity severity = SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING;
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
    bool saw_pooled_cube = false;
    bool saw_pooled_sphere = false;
    bool saw_pickup_batch = false;
    int pickup_batch_instances = 0;
    int sprites = 0;
    bool saw_doom_robot_sprite = false;
    bool saw_doom_health_sprite = false;
    bool saw_doom_crate = false;
    bool saw_doom_dragon_model = false;
    int doom_robot_sprites = 0;
    int doom_health_sprites = 0;
    int doom_crates = 0;
    int doom_textured_crates = 0;
    int doom_model_primitives = 0;
    int doom_presentation_cubes = 0;
    int doom_projectile_spheres = 0;
    int brush_mesh_primitives = 0;
    int brush_sprites = 0;
    int brush_projectile_spheres = 0;
    bool saw_brush_spinning_cube = false;
    bool saw_brush_robot_sprite = false;
    bool saw_brush_health_sprite = false;
    int brush_player_capsules = 0;
};

struct SectorLevelInstanceCapture
{
    int count = 0;
    std::string level_name;
    std::string variant_name;
    slayer3d_game_data_sector_level_variant variant = static_cast<slayer3d_game_data_sector_level_variant>(0);
    const slayer3d_level *level = nullptr;
    slayer3d_vec3 position{};
    bool portal_culling = true;
    bool sector_lighting_enabled = true;
};

struct BrushWorldInstanceCapture
{
    int count = 0;
    std::string world_name;
    const slayer3d_game_data_brush_world *world = nullptr;
    slayer3d_vec3 position{};
    bool acceleration_enabled = true;
    bool lighting_enabled = true;
    bool debug_wireframe = false;
};

struct WorldModelInstanceCapture
{
    int count = 0;
    int sectors = 0;
    int brushes = 0;
    bool saw_sector_bounds = false;
    bool saw_brush_bounds = false;
    slayer3d_bounding_box sector_bounds{};
    slayer3d_bounding_box brush_bounds{};
};

struct SectorDoorRenderCapture
{
    int door_primitives = 0;
    int textured_door_primitives = 0;
    slayer3d_vec3 first_position{};
};

struct DoorPrefixRenderCapture
{
    const char *prefix = nullptr;
    int door_primitives = 0;
    int textured_door_primitives = 0;
};

void capture_signal_payload(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    auto *capture = static_cast<NetworkSignalCapture *>(userdata);
    if (capture == nullptr)
    {
        return;
    }

    capture->calls++;
    capture->signal_id = signal_id;
    capture->network_control = slayer3d_properties_get_string(payload, "network_control", "");
    capture->network_direction = slayer3d_properties_get_string(payload, "network_direction", "");
    capture->network_tick = slayer3d_properties_get_int(payload, "network_tick", 0);
}

struct UiTextCapture
{
    int count = 0;
    bool saw_score = false;
    bool saw_pause = false;
    bool saw_network_match_terminated = false;
    bool saw_doom_reticle = false;
    bool saw_doom_profile = false;
    bool saw_doom_fps = false;
};

struct UiImageCapture
{
    int count = 0;
    bool saw_splash_logo = false;
};

struct UiRectCapture
{
    int count = 0;
    bool saw_doom_damage_feedback = false;
    slayer3d_game_data_ui_rect damage_rect{};
};

struct ParticleCapture
{
    int count = 0;
    bool saw_ambient = false;
    bool saw_options_flow = false;
    bool saw_pooled_emitter = false;
    bool saw_nukage_vapor = false;
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

struct CombatSignalCapture
{
    int calls = 0;
    std::string actor_name;
    std::string source_actor_name;
    float amount = 0.0f;
    float armor_delta = 0.0f;
    float health_delta = 0.0f;
    float health = 0.0f;
    float armor = 0.0f;
    bool alive = true;
};

bool serve_adapter(void *userdata, slayer3d_game_data_runtime *runtime, const char *adapter_name,
                   slayer3d_registered_actor *target, const slayer3d_properties *payload)
{
    auto *capture = static_cast<AdapterCapture *>(userdata);
    EXPECT_STREQ(adapter_name, "adapter.pong.serve_random");
    EXPECT_NE(runtime, nullptr);
    EXPECT_NE(target, nullptr);
    EXPECT_EQ(payload, nullptr);
    slayer3d_properties_set_vec3(target->props, "velocity", slayer3d_vec3_make(3.0f, 1.0f, 0.0f));
    capture->calls++;
    return true;
}

bool configure_play_input_adapter(void *, slayer3d_game_data_runtime *runtime, const char *adapter_name,
                                  slayer3d_registered_actor *, const slayer3d_properties *payload)
{
    EXPECT_STREQ(adapter_name, "adapter.pong.configure_play_input");
    EXPECT_NE(runtime, nullptr);
    if (payload != nullptr)
    {
        const char *match_mode = slayer3d_properties_get_string(payload, "match_mode", nullptr);
        const char *network_role = slayer3d_properties_get_string(payload, "network_role", nullptr);
        const char *network_flow = slayer3d_properties_get_string(payload, "network_flow", nullptr);
        if (match_mode != nullptr)
        {
            slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(runtime), "match_mode", match_mode);
        }
        if (network_role != nullptr)
        {
            slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(runtime), "network_role",
                                           network_role);
        }
        if (network_flow != nullptr)
        {
            slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(runtime), "network_flow",
                                           network_flow);
        }
    }
    return true;
}

bool reload_native_adapter(void *userdata, slayer3d_game_data_runtime *runtime, const char *adapter_name,
                           slayer3d_registered_actor *target, const slayer3d_properties *payload)
{
    auto *capture = static_cast<AdapterCapture *>(userdata);
    EXPECT_NE(runtime, nullptr);
    EXPECT_STREQ(adapter_name, "adapter.reload.run");
    EXPECT_NE(target, nullptr);
    EXPECT_EQ(payload, nullptr);
    slayer3d_properties_set_int(target->props, "value", 99);
    capture->calls++;
    return true;
}

void capture_diagnostic(void *userdata, slayer3d_game_data_diagnostic_severity severity, const char *json_path,
                        const char *message)
{
    auto *capture = static_cast<DiagnosticCapture *>(userdata);
    capture->diagnostics.push_back(
        {severity, json_path != nullptr ? json_path : "", message != nullptr ? message : ""});
}

void capture_scene_payload(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    auto *capture = static_cast<ScenePayloadCapture *>(userdata);
    (void)signal_id;
    capture->called = true;
    capture->from_scene = slayer3d_properties_get_string(payload, "from_scene", "");
    capture->to_scene = slayer3d_properties_get_string(payload, "to_scene", "");
    capture->selected_level = slayer3d_properties_get_string(payload, "selected_level", "");
}

void count_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    auto *capture = static_cast<SignalCapture *>(userdata);
    (void)signal_id;
    (void)payload;
    capture->calls++;
}

void capture_sensor_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    auto *capture = static_cast<SensorSignalCapture *>(userdata);
    (void)signal_id;
    if (capture == nullptr)
    {
        return;
    }
    capture->calls++;
    capture->actor_name = slayer3d_properties_get_string(payload, "actor_name", "");
    capture->other_actor_name = slayer3d_properties_get_string(payload, "other_actor_name", "");
}

void capture_combat_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    auto *capture = static_cast<CombatSignalCapture *>(userdata);
    (void)signal_id;
    if (capture == nullptr)
    {
        return;
    }
    capture->calls++;
    capture->actor_name = slayer3d_properties_get_string(payload, "actor_name", "");
    capture->source_actor_name = slayer3d_properties_get_string(payload, "source_actor_name", "");
    capture->amount = slayer3d_properties_get_float(payload, "amount", 0.0f);
    capture->armor_delta = slayer3d_properties_get_float(payload, "armor_delta", 0.0f);
    capture->health_delta = slayer3d_properties_get_float(payload, "health_delta", 0.0f);
    capture->health = slayer3d_properties_get_float(payload, "health", 0.0f);
    capture->armor = slayer3d_properties_get_float(payload, "armor", 0.0f);
    capture->alive = slayer3d_properties_get_bool(payload, "alive", true);
}

std::string fixture_path(const char *filename)
{
    return std::string(SLAYER3D_GAME_DATA_FIXTURE_DIR) + "/" + filename;
}

std::filesystem::path demo_data_path(const char *demo_name, const char *data_file)
{
    return std::filesystem::path(SLAYER3D_DEMOS_ROOT) / demo_name / "data" / data_file;
}

std::filesystem::path pong_data_path()
{
    return demo_data_path("pong", "pong.game.json");
}

std::filesystem::path pacman_data_path()
{
    return demo_data_path("pacman", "pacman.game.json");
}

std::filesystem::path doom_level_data_path()
{
    return demo_data_path("doom_level", "doom_level.game.json");
}

std::filesystem::path fps_mechanics_dojo_data_path()
{
    return demo_data_path("fps_mechanics_dojo", "fps_mechanics_dojo.game.json");
}

std::filesystem::path mesh_primitives_dojo_data_path()
{
    return demo_data_path("mesh_primitives_dojo", "mesh_primitives_dojo.game.json");
}

std::filesystem::path lighting_dojo_data_path()
{
    return demo_data_path("lighting_dojo", "lighting_dojo.game.json");
}

std::filesystem::path brush_geometry_dojo_data_path()
{
    return demo_data_path("brush_geometry_dojo", "brush_geometry_dojo.game.json");
}

std::filesystem::path editor_shell_dojo_data_path()
{
    return demo_data_path("editor_shell_dojo", "editor_shell_dojo.game.json");
}

std::filesystem::path fps_template_data_path()
{
    return demo_data_path("templates/fps", "fps_template.game.json");
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
        const std::filesystem::path dir = root / ("slayer3d_game_data_test_" + std::string(name) + "_" +
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
  "schema": "slayer3d.game.v0",
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
      "id": "slayer3d.test.network.v1",
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
  "schema": "slayer3d.game.v0",
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
    "protocol": { "id": "slayer3d.test.pool.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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

std::array<Uint8, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE> load_network_schema_hash(const std::filesystem::path &path)
{
    slayer3d_game_session *session = nullptr;
    EXPECT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_TRUE(slayer3d_game_data_load_file(path.string().c_str(), session, &runtime, error, sizeof(error))) << error;
    EXPECT_NE(runtime, nullptr);
    EXPECT_TRUE(slayer3d_game_data_has_network_schema(runtime));

    std::array<Uint8, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE> hash{};
    EXPECT_TRUE(slayer3d_game_data_get_network_schema_hash(runtime, hash.data()));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    return hash;
}

void load_pong_runtime(slayer3d_game_session **out_session, slayer3d_game_data_runtime **out_runtime)
{
    ASSERT_NE(out_session, nullptr);
    ASSERT_NE(out_runtime, nullptr);
    *out_session = nullptr;
    *out_runtime = nullptr;

    ASSERT_TRUE(slayer3d_game_session_create(nullptr, out_session));
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_file(pong_data_path().string().c_str(), *out_session, out_runtime, error,
                                             sizeof(error)))
        << error;
    ASSERT_NE(*out_runtime, nullptr);
}

void destroy_runtime_session(slayer3d_game_session *session, slayer3d_game_data_runtime *runtime)
{
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

void expect_vec3_near(slayer3d_vec3 actual, slayer3d_vec3 expected, float tolerance = 0.0001f)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

std::filesystem::path copy_pong_data_with_storage_overrides(const std::filesystem::path &dir,
                                                            const std::filesystem::path &user_root,
                                                            const std::filesystem::path &cache_root)
{
    const std::filesystem::path source = std::filesystem::path(pong_data_path()).parent_path();
    const std::filesystem::path dest = dir / "pong_data";
    std::filesystem::copy(source, dest,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);

    const std::filesystem::path game_path = dest / "pong.game.json";
    const std::filesystem::path persistence_path = dest / "fragments" / "persistence.json";
    std::string persistence_json = read_text(persistence_path);
    const std::string marker = R"json("profile": "default")json";
    const std::string replacement = std::string(R"json("profile": "default",
    "user_root_override": ")json") + user_root.generic_string() +
                                    R"json(",
    "cache_root_override": ")json" + cache_root.generic_string() +
                                    R"json(")json";
    const size_t marker_pos = persistence_json.find(marker);
    if (marker_pos == std::string::npos)
        throw std::runtime_error("Pong storage profile marker not found");
    persistence_json.replace(marker_pos, marker.size(), replacement);
    write_text(persistence_path, persistence_json.c_str());
    return game_path;
}

void write_hot_reload_json(const std::filesystem::path &dir)
{
    write_text(dir / "reload.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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

void write_direct_start_json(const std::filesystem::path &dir)
{
    write_text(dir / "direct_start.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Direct Start", "id": "test.direct_start", "version": "0.1.0" },
  "world": { "name": "world.direct_start", "kind": "fixed_screen" },
  "signals": ["signal.intro.enter", "signal.level.enter"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.intro.enter",
        "actions": [
          { "type": "scene_state.set", "key": "intro_entered", "value": true }
        ]
      },
      {
        "signal": "signal.level.enter",
        "actions": [
          { "type": "scene_state.set", "key": "level_entered", "value": true },
          { "type": "scene_state.set", "key": "payload_level", "value": "{selected_level}" },
          { "type": "scene_state.set", "key": "payload_from_scene", "value": "{from_scene}" },
          { "type": "scene_state.set", "key": "payload_to_scene", "value": "{to_scene}" },
          {
            "type": "branch",
            "if": { "type": "scene_state.compare", "key": "checkpoint", "op": "==", "value": "midboss" },
            "then": [
              { "type": "scene_state.set", "key": "checkpoint_visible_on_enter", "value": true }
            ],
            "else": [
              { "type": "scene_state.set", "key": "checkpoint_visible_on_enter", "value": false }
            ]
          }
        ]
      }
    ]
  },
  "scenes": {
    "initial": "scene.intro",
    "files": ["scenes/intro.scene.json", "scenes/level1.scene.json"]
  }
})json");
    write_text(dir / "scenes" / "intro.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.intro",
  "on_enter_signal": "signal.intro.enter"
})json");
    write_text(dir / "scenes" / "level1.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.level1",
  "on_enter_signal": "signal.level.enter"
})json");
}

void write_timeline_json(const std::filesystem::path &dir)
{
    write_text(dir / "timeline.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false,
  "entities": []
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.scene.v0",
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

void emit_reload_signal(slayer3d_game_session *session, slayer3d_game_data_runtime *runtime)
{
    const int signal = slayer3d_game_data_find_signal(runtime, "signal.run");
    ASSERT_GE(signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), signal, nullptr);
}

bool capture_render_primitive(void *userdata, const slayer3d_game_data_render_primitive *primitive)
{
    auto *capture = static_cast<RenderPrimitiveCapture *>(userdata);
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
        capture->cubes++;
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE)
        capture->spheres++;
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
        capture->spheres++;
    else if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPRITE)
        capture->sprites++;

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
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_CUBE);
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
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_SPHERE);
        EXPECT_NEAR(primitive->radius, 1.05f, 0.0001f);
        EXPECT_TRUE(primitive->emissive);
    }
    if (std::string(primitive->entity_name) == "pool.renderables.0" &&
        primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
    {
        capture->saw_pooled_cube = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_CUBE);
        EXPECT_NEAR(primitive->position.x, 2.0f, 0.0001f);
        EXPECT_NEAR(primitive->size.x, 0.5f, 0.0001f);
    }
    if (std::string(primitive->entity_name) == "pool.renderables.0" &&
        primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE)
    {
        capture->saw_pooled_sphere = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_SPHERE);
        EXPECT_NEAR(primitive->position.y, 3.0f, 0.0001f);
        EXPECT_NEAR(primitive->radius, 0.2f, 0.0001f);
    }
    if (std::string(primitive->entity_name).rfind("pickup.", 0) == 0 &&
        primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        capture->saw_pickup_batch = true;
        capture->pickup_batch_instances += primitive->instance_count;
        EXPECT_GT(primitive->instance_count, 0);
        EXPECT_NE(primitive->instances, nullptr);
    }
    const std::string entity_name = primitive->entity_name != nullptr ? primitive->entity_name : "";
    if (entity_name.rfind("entity.doom.robot.", 0) == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_SPRITE)
        capture->doom_robot_sprites++;
    if (entity_name.rfind("entity.doom.health.", 0) == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_SPRITE)
        capture->doom_health_sprites++;
    if (entity_name.rfind("entity.doom.crate.", 0) == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
    {
        capture->doom_crates++;
        if (primitive->texture_image != nullptr &&
            std::string(primitive->texture_image) == "image.doom.radioactive_crate")
            capture->doom_textured_crates++;
    }
    if (entity_name.rfind("entity.doom.dragon", 0) == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_MODEL)
        capture->doom_model_primitives++;
    if (entity_name.rfind("entity.doom.presentation.", 0) == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_CUBE)
        capture->doom_presentation_cubes++;
    if (entity_name.rfind("pool.doom.projectiles.", 0) == 0 && primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE)
        capture->doom_projectile_spheres++;
    if (entity_name.rfind("entity.brush_geometry.mesh.", 0) == 0 &&
        primitive->type == SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
    {
        capture->brush_mesh_primitives++;
    }
    if (entity_name == "entity.brush_geometry.player" && primitive->type == SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
    {
        capture->brush_player_capsules++;
        EXPECT_EQ(primitive->mesh_primitive, SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE);
        EXPECT_EQ(primitive->color.r, 60);
        EXPECT_EQ(primitive->color.g, 255);
        EXPECT_EQ(primitive->color.b, 70);
        EXPECT_TRUE(primitive->emissive);
    }
    if (entity_name.rfind("pool.brush_geometry.projectiles.", 0) == 0 &&
        primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE)
    {
        capture->brush_projectile_spheres++;
    }
    if (entity_name == "entity.brush_geometry.mesh.cube")
    {
        capture->saw_brush_spinning_cube = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE);
        EXPECT_EQ(primitive->mesh_primitive, SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE);
        EXPECT_NEAR(primitive->rotation_axis.x, 0.0f, 0.0001f);
        EXPECT_NEAR(primitive->rotation_axis.y, 1.0f, 0.0001f);
        EXPECT_NEAR(primitive->rotation_axis.z, 0.0f, 0.0001f);
        EXPECT_GT(primitive->rotation_angle, 0.0f);
        EXPECT_TRUE(primitive->lighting_enabled);
    }
    if (entity_name == "entity.brush_geometry.sprite.robot")
    {
        capture->brush_sprites++;
        capture->saw_brush_robot_sprite = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_SPRITE);
        EXPECT_STREQ(primitive->sprite_asset, "sprite.brush_geometry.robot.walk");
        EXPECT_NEAR(primitive->sprite_size.x, 3.4f, 0.0001f);
        EXPECT_NEAR(primitive->sprite_size.y, 5.2f, 0.0001f);
        EXPECT_TRUE(primitive->lighting_enabled);
    }
    if (entity_name == "entity.brush_geometry.sprite.health")
    {
        capture->brush_sprites++;
        capture->saw_brush_health_sprite = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_SPRITE);
        EXPECT_STREQ(primitive->sprite_asset, "sprite.brush_geometry.health_pack");
        EXPECT_NEAR(primitive->sprite_size.x, 1.2f, 0.0001f);
        EXPECT_NEAR(primitive->sprite_size.y, 1.2f, 0.0001f);
        EXPECT_TRUE(primitive->lighting_enabled);
    }
    if (entity_name == "entity.doom.robot.entry")
    {
        capture->saw_doom_robot_sprite = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_SPRITE);
        EXPECT_STREQ(primitive->sprite_asset, "sprite.doom.robot.walk");
        EXPECT_NEAR(primitive->sprite_size.x, 3.4f, 0.0001f);
        EXPECT_NEAR(primitive->sprite_size.y, 5.2f, 0.0001f);
        EXPECT_TRUE(primitive->lighting_enabled);
    }
    if (entity_name == "entity.doom.health.entry")
    {
        capture->saw_doom_health_sprite = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_SPRITE);
        EXPECT_STREQ(primitive->sprite_asset, "sprite.doom.health_pack");
        EXPECT_NEAR(primitive->sprite_size.x, 1.0f, 0.0001f);
        EXPECT_NEAR(primitive->sprite_size.y, 1.0f, 0.0001f);
    }
    if (entity_name == "entity.doom.crate.nukage")
    {
        capture->saw_doom_crate = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_CUBE);
        EXPECT_NEAR(primitive->size.x, 0.9f, 0.0001f);
        EXPECT_EQ(primitive->color.g, 170);
        EXPECT_STREQ(primitive->texture_image, "image.doom.radioactive_crate");
    }
    if (entity_name == "entity.doom.dragon")
    {
        capture->saw_doom_dragon_model = true;
        EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_MODEL);
        EXPECT_STREQ(primitive->model_asset, "model.doom.black_dragon");
        EXPECT_NEAR(primitive->position.x, 24.0f, 0.0001f);
        EXPECT_NEAR(primitive->position.z, 74.0f, 0.0001f);
        EXPECT_NEAR(primitive->model_scale.x, 2.0f, 0.0001f);
        EXPECT_NEAR(primitive->rotation_axis.y, 1.0f, 0.0001f);
        EXPECT_NEAR(primitive->rotation_angle, 3.1415927f, 0.0001f);
        EXPECT_EQ(primitive->animation_clip, 0);
        EXPECT_TRUE(primitive->animation_loop);
    }
    return true;
}

bool capture_sector_door_render_primitive(void *userdata, const slayer3d_game_data_render_primitive *primitive)
{
    auto *capture = static_cast<SectorDoorRenderCapture *>(userdata);
    if (capture == nullptr || primitive == nullptr || primitive->entity_name == nullptr ||
        std::string(primitive->entity_name) != "door.test")
    {
        return true;
    }
    if (capture->door_primitives == 0)
        capture->first_position = primitive->position;
    capture->door_primitives++;
    if (primitive->texture_image != nullptr && std::string(primitive->texture_image) == "image.doom.door_hatch")
        capture->textured_door_primitives++;
    EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_CUBE);
    EXPECT_NEAR(primitive->size.x, 0.4f, 0.001f);
    EXPECT_NEAR(primitive->size.y, 2.0f, 0.001f);
    EXPECT_NEAR(primitive->size.z, 0.4f, 0.001f);
    return true;
}

bool capture_door_prefix_render_primitive(void *userdata, const slayer3d_game_data_render_primitive *primitive)
{
    auto *capture = static_cast<DoorPrefixRenderCapture *>(userdata);
    if (capture == nullptr || primitive == nullptr || primitive->entity_name == nullptr || capture->prefix == nullptr ||
        std::string(primitive->entity_name).rfind(capture->prefix, 0) != 0)
    {
        return true;
    }
    capture->door_primitives++;
    if (primitive->texture_image != nullptr && std::string(primitive->texture_image) == "image.doom.door_hatch")
        capture->textured_door_primitives++;
    EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_CUBE);
    EXPECT_GT(primitive->size.x, 0.0f);
    EXPECT_GT(primitive->size.y, 0.0f);
    EXPECT_GT(primitive->size.z, 0.0f);
    return true;
}

bool capture_sector_level_instance(void *userdata, const slayer3d_game_data_sector_level_instance *instance)
{
    auto *capture = static_cast<SectorLevelInstanceCapture *>(userdata);
    capture->count++;
    capture->level_name = instance->level_name != nullptr ? instance->level_name : "";
    capture->variant_name = instance->variant_name != nullptr ? instance->variant_name : "";
    capture->variant = instance->variant;
    capture->level = instance->level;
    capture->position = instance->position;
    capture->portal_culling = instance->portal_culling;
    capture->sector_lighting_enabled = instance->sector_lighting_enabled;
    return true;
}

bool capture_brush_world_instance(void *userdata, const slayer3d_game_data_brush_world_instance *instance)
{
    auto *capture = static_cast<BrushWorldInstanceCapture *>(userdata);
    capture->count++;
    capture->world_name = instance->world_name != nullptr ? instance->world_name : "";
    capture->world = instance->world;
    capture->position = instance->position;
    capture->acceleration_enabled = instance->acceleration_enabled;
    capture->lighting_enabled = instance->lighting_enabled;
    capture->debug_wireframe = instance->debug_wireframe;
    return true;
}

bool capture_world_model_instance(void *userdata, const slayer3d_game_data_world_model_instance *instance)
{
    auto *capture = static_cast<WorldModelInstanceCapture *>(userdata);
    capture->count++;
    if (instance->type == SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL)
    {
        capture->sectors++;
        EXPECT_STREQ(instance->name, "sector.world_model");
        EXPECT_STREQ(instance->variant_name, "lightmapped");
        EXPECT_NE(instance->sector_level, nullptr);
        EXPECT_NE(instance->sectors, nullptr);
        EXPECT_EQ(instance->sector_count, 1);
        capture->saw_sector_bounds = instance->has_bounds;
        capture->sector_bounds = instance->bounds;
    }
    else if (instance->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
    {
        capture->brushes++;
        EXPECT_STREQ(instance->name, "brush.world_model");
        EXPECT_NE(instance->brush_world, nullptr);
        capture->saw_brush_bounds = instance->has_bounds;
        capture->brush_bounds = instance->bounds;
    }
    return true;
}

bool capture_ui_text(void *userdata, const slayer3d_game_data_ui_text *text)
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
    if (std::string(text->name) == "ui.doom_level.reticle")
    {
        capture->saw_doom_reticle = true;
        EXPECT_STREQ(text->font, "font.doom.hud");
        EXPECT_STREQ(text->text, "+");
        EXPECT_TRUE(text->centered);
        EXPECT_TRUE(text->normalized);
        EXPECT_NEAR(text->x, 0.5f, 0.0001f);
        EXPECT_NEAR(text->y, 0.5f, 0.0001f);
    }
    if (std::string(text->name) == "ui.doom_level.fps")
    {
        capture->saw_doom_fps = true;
        EXPECT_STREQ(text->font, "font.doom.hud");
        EXPECT_STREQ(text->format, "FPS %.0f");
        EXPECT_TRUE(text->normalized);
        EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT);
        EXPECT_NEAR(text->x, 0.985f, 0.0001f);
    }
    if (std::string(text->name) == "ui.doom_level.profile")
    {
        capture->saw_doom_profile = true;
        EXPECT_STREQ(text->font, "font.doom.hud");
        EXPECT_STREQ(text->format, "PROFILE %s");
        EXPECT_TRUE(text->normalized);
        EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
    }
    return true;
}

bool capture_ui_rect(void *userdata, const slayer3d_game_data_ui_rect *rect)
{
    auto *capture = static_cast<UiRectCapture *>(userdata);
    capture->count++;
    if (std::string(rect->name) == "ui.doom_level.damage_feedback.top")
    {
        capture->saw_doom_damage_feedback = true;
        capture->damage_rect = *rect;
        EXPECT_TRUE(rect->normalized);
        EXPECT_TRUE(rect->pulse_alpha);
        EXPECT_STREQ(rect->alpha_source_target, "entity.player");
        EXPECT_STREQ(rect->alpha_source_key, "last_damage_per_second");
        EXPECT_GT(rect->alpha_source_scale, 0.0f);
    }
    return true;
}

bool capture_particle(void *userdata, const slayer3d_game_data_particle_emitter *emitter)
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
    if (std::string(emitter->entity_name) == "pool.renderables.0")
    {
        capture->saw_pooled_emitter = true;
        EXPECT_EQ(emitter->config.max_particles, 12);
        EXPECT_NEAR(emitter->config.position.x, 2.0f, 0.0001f);
        EXPECT_NEAR(emitter->draw_emissive.y, 0.8f, 0.0001f);
    }
    if (std::string(emitter->entity_name) == "entity.effect.nukage.vapor")
    {
        capture->saw_nukage_vapor = true;
        EXPECT_EQ(emitter->config.max_particles, 360);
        EXPECT_TRUE(emitter->config.additive_blend);
        EXPECT_NEAR(emitter->draw_emissive.y, 0.95f, 0.0001f);
    }
    return true;
}

bool capture_evaluated_primitive(void *userdata, const slayer3d_game_data_render_primitive *primitive)
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

bool mount_test_directory_assets(slayer3d_asset_resolver *assets, void *userdata, char *error_buffer,
                                 int error_buffer_size)
{
    return slayer3d_asset_resolver_mount_directory(assets, static_cast<const char *>(userdata), error_buffer,
                                                   error_buffer_size);
}

} // namespace

TEST(GameDataRuntime, LoadsPongDataIntoGenericSessionServices)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.ball"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor_with_tag(runtime, "ball"), nullptr);
    const char *paddle_tags[] = {"paddle", "player"};
    EXPECT_NE(slayer3d_game_data_find_actor_with_tags(runtime, paddle_tags, 2), nullptr);
    EXPECT_GE(slayer3d_game_data_find_signal(runtime, "signal.ball.serve"), 0);
    EXPECT_GE(slayer3d_game_data_find_signal(runtime, "signal.multiplayer.lobby.start"), 0);
    EXPECT_GE(slayer3d_game_data_find_action(runtime, "action.paddle.up"), 0);
    EXPECT_GE(slayer3d_game_data_find_action(runtime, "action.paddle.local.up"), 0);
    EXPECT_GE(slayer3d_game_data_find_action(runtime, "action.paddle.local.down"), 0);
    EXPECT_GE(slayer3d_game_data_find_action(runtime, "action.scene.title"), 0);
    EXPECT_GE(slayer3d_game_data_find_action(runtime, "action.scene.options"), 0);
    EXPECT_GE(slayer3d_game_data_find_action(runtime, "action.scene.play"), 0);
    const char *network_scene_state_key = nullptr;
    ASSERT_TRUE(slayer3d_game_data_get_network_scene_state_key(runtime, "host", "status", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "multiplayer_host_status");
    ASSERT_TRUE(slayer3d_game_data_get_network_scene_state_key(runtime, "host", "connected", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "multiplayer_host_connected");
    ASSERT_TRUE(
        slayer3d_game_data_get_network_scene_state_key(runtime, "discovery", "count", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "multiplayer_discovery_count");
    ASSERT_TRUE(
        slayer3d_game_data_get_network_scene_state_key(runtime, "discovery", "result_0", &network_scene_state_key));
    EXPECT_STREQ(network_scene_state_key, "session_0");
    EXPECT_FALSE(slayer3d_game_data_get_network_scene_state_key(runtime, "host", "missing", &network_scene_state_key));
    EXPECT_EQ(network_scene_state_key, nullptr);
    const char *network_session_value = nullptr;
    ASSERT_TRUE(slayer3d_game_data_get_network_session_scene(runtime, "play", &network_session_value));
    EXPECT_STREQ(network_session_value, "scene.play");
    ASSERT_TRUE(slayer3d_game_data_get_network_session_scene(runtime, "join", &network_session_value));
    EXPECT_STREQ(network_session_value, "scene.multiplayer.join");
    ASSERT_TRUE(slayer3d_game_data_get_network_session_state_key(runtime, "match_mode", &network_session_value));
    EXPECT_STREQ(network_session_value, "match_mode");
    ASSERT_TRUE(
        slayer3d_game_data_get_network_session_state_value(runtime, "match_mode", "network", &network_session_value));
    EXPECT_STREQ(network_session_value, "lan");
    ASSERT_TRUE(
        slayer3d_game_data_get_network_session_state_value(runtime, "network_role", "client", &network_session_value));
    EXPECT_STREQ(network_session_value, "client");
    ASSERT_TRUE(slayer3d_game_data_get_network_session_message(runtime, "disconnect_reasons", "host_exited",
                                                               &network_session_value));
    EXPECT_STREQ(network_session_value, "Host exited");
    ASSERT_TRUE(slayer3d_game_data_get_network_session_message(runtime, "disconnect_prompts", "match_terminated",
                                                               &network_session_value));
    EXPECT_STREQ(network_session_value, "Match terminated: {reason} - Press Enter to return to title screen.");
    EXPECT_FALSE(slayer3d_game_data_get_network_session_message(runtime, "disconnect_reasons", "missing",
                                                                &network_session_value));
    EXPECT_EQ(network_session_value, nullptr);
    EXPECT_TRUE(slayer3d_game_data_network_managed_runtime_enabled(runtime));
    float managed_ack_delay = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_get_network_managed_termination_ack_delay(runtime, &managed_ack_delay));
    EXPECT_FLOAT_EQ(managed_ack_delay, 3.0f);
    EXPECT_TRUE(
        slayer3d_game_data_network_managed_keep_alive_scene_matches(runtime, "host", "scene.multiplayer.lobby"));
    EXPECT_TRUE(slayer3d_game_data_network_managed_keep_alive_scene_matches(runtime, "host", "scene.play"));
    EXPECT_FALSE(slayer3d_game_data_network_managed_keep_alive_scene_matches(runtime, "host", "scene.title"));
    EXPECT_TRUE(slayer3d_game_data_network_managed_keep_alive_scene_matches(runtime, "direct_connect",
                                                                            "scene.multiplayer.discovery"));
    EXPECT_TRUE(slayer3d_game_data_network_managed_keep_alive_scene_matches(runtime, "direct_connect", "scene.play"));
    const char *network_runtime_value = nullptr;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_replication(runtime, "state_snapshot", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "play_state");
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_replication(runtime, "client_input", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "client_input");
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_control(runtime, "pause_request", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "pause_request");
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_control(runtime, "disconnect", &network_runtime_value));
    EXPECT_STREQ(network_runtime_value, "disconnect");
    EXPECT_FALSE(slayer3d_game_data_get_network_runtime_control(runtime, "missing", &network_runtime_value));
    EXPECT_EQ(network_runtime_value, nullptr);
    int network_runtime_id = -1;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_action(runtime, "client_up", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, slayer3d_game_data_find_action(runtime, "action.paddle.local.up"));
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_action(runtime, "menu_back", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, slayer3d_game_data_find_action(runtime, "action.menu.back"));
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_signal(runtime, "lobby_start", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, slayer3d_game_data_find_signal(runtime, "signal.multiplayer.lobby.start"));
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_signal(runtime, "ui_select", &network_runtime_id));
    EXPECT_EQ(network_runtime_id, slayer3d_game_data_find_signal(runtime, "signal.ui.menu.select"));
    int network_pause_action = -1;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_pause_action(runtime, &network_pause_action));
    EXPECT_EQ(network_pause_action, slayer3d_game_data_find_action(runtime, "action.pause"));
    bool network_paused = true;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_pause_state(runtime, &network_paused, error, sizeof(error)))
        << error;
    EXPECT_FALSE(network_paused);
    ASSERT_TRUE(slayer3d_game_data_set_network_runtime_pause_state(runtime, true, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_pause_state(runtime, &network_paused, error, sizeof(error)))
        << error;
    EXPECT_TRUE(network_paused);
    ASSERT_EQ(slayer3d_game_data_haptics_policy_count(runtime), 2);
    slayer3d_game_data_haptics_policy haptics_policy{};
    ASSERT_TRUE(slayer3d_game_data_get_haptics_policy_at(runtime, 0, &haptics_policy));
    EXPECT_STREQ(haptics_policy.name, "haptics.gamepad.vibration_test");
    EXPECT_EQ(haptics_policy.signal_id, slayer3d_game_data_find_signal(runtime, "signal.settings.vibration"));
    EXPECT_FLOAT_EQ(haptics_policy.low_frequency, 0.30f);
    EXPECT_FLOAT_EQ(haptics_policy.high_frequency, 0.70f);
    EXPECT_EQ(haptics_policy.duration_ms, 100U);
    ASSERT_TRUE(slayer3d_game_data_get_haptics_policy_at(runtime, 1, &haptics_policy));
    EXPECT_STREQ(haptics_policy.name, "haptics.gamepad.paddle_hit");
    EXPECT_EQ(haptics_policy.signal_id, slayer3d_game_data_find_signal(runtime, "signal.ball.hit_paddle"));

    slayer3d_properties *haptics_payload = slayer3d_properties_create();
    ASSERT_NE(haptics_payload, nullptr);
    const int paddle_hit_signal = slayer3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    slayer3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.player");
    EXPECT_TRUE(
        slayer3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    slayer3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.cpu");
    EXPECT_FALSE(
        slayer3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(runtime), "match_mode", "local");
    EXPECT_TRUE(
        slayer3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    slayer3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.attract_left");
    EXPECT_FALSE(
        slayer3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    slayer3d_registered_actor *settings_for_haptics = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings_for_haptics, nullptr);
    slayer3d_properties_set_bool(settings_for_haptics->props, "vibration", false);
    slayer3d_properties_set_string(haptics_payload, "other_actor_name", "entity.paddle.player");
    EXPECT_FALSE(
        slayer3d_game_data_match_haptics_policy(runtime, 1, paddle_hit_signal, haptics_payload, &haptics_policy));
    slayer3d_properties_destroy(haptics_payload);
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.overhead");
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.splash");
    EXPECT_EQ(slayer3d_game_data_scene_count(runtime), 15);
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 0), "scene.splash");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 1), "scene.title");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 2), "scene.multiplayer");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 3), "scene.multiplayer.lan");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 4), "scene.multiplayer.lobby");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 5), "scene.multiplayer.join");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 6), "scene.multiplayer.direct_connect");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 7), "scene.multiplayer.discovery");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 8), "scene.options");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 9), "scene.options.display");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 10), "scene.options.keyboard");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 11), "scene.options.mouse");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 12), "scene.options.gamepad");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 13), "scene.options.audio");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 14), "scene.play");
    EXPECT_EQ(slayer3d_game_data_scene_name_at(runtime, -1), nullptr);
    EXPECT_EQ(slayer3d_game_data_scene_name_at(runtime, 15), nullptr);
    EXPECT_FALSE(slayer3d_game_data_active_scene_updates_game(runtime));
    EXPECT_FALSE(slayer3d_game_data_active_scene_renders_world(runtime));
    EXPECT_FALSE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.ball"));
    EXPECT_EQ(slayer3d_timer_pool_active_count(slayer3d_game_session_get_timer_pool(session)), 0);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DataGameRuntimeOwnsGenericPongLifecycle)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    const std::filesystem::path data_path = pong_data_path();
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    slayer3d_data_game_runtime_desc desc{};
    slayer3d_data_game_runtime_desc_init(&desc);
    desc.session = session;
    desc.media_dir = SLAYER3D_MEDIA_DIR;
    desc.data_asset_path = asset_path.c_str();
    desc.mount_assets = mount_test_directory_assets;
    desc.mount_userdata = const_cast<char *>(root.c_str());

    char error[512]{};
    slayer3d_data_game_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error))) << error;
    ASSERT_NE(runtime, nullptr);
    ASSERT_NE(slayer3d_data_game_runtime_assets(runtime), nullptr);
    slayer3d_game_data_runtime *data = slayer3d_data_game_runtime_data(runtime);
    ASSERT_NE(data, nullptr);
    EXPECT_STREQ(slayer3d_game_data_active_scene(data), "scene.splash");
    EXPECT_NE(slayer3d_game_data_find_actor(data, "entity.ball"), nullptr);

    slayer3d_game_context ctx{};
    ctx.session = session;
    EXPECT_TRUE(slayer3d_data_game_runtime_update_frame(runtime, &ctx, 0.016f));
    slayer3d_data_game_runtime_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DirectStartEntersRequestedSceneBeforeInitialSceneRuns)
{
    const std::filesystem::path dir = unique_test_dir("direct_start");
    write_direct_start_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;

    slayer3d_properties *initial_state = slayer3d_properties_create();
    slayer3d_properties *initial_payload = slayer3d_properties_create();
    ASSERT_NE(initial_state, nullptr);
    ASSERT_NE(initial_payload, nullptr);
    slayer3d_properties_set_string(initial_state, "checkpoint", "midboss");
    slayer3d_properties_set_int(initial_state, "lives", 3);
    slayer3d_properties_set_string(initial_payload, "selected_level", "level1");

    slayer3d_game_data_load_options options{};
    options.session = session;
    options.initial_scene_override = "scene.level1";
    options.initial_scene_state = initial_state;
    options.initial_scene_payload = initial_payload;

    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_asset_with_options(assets, "asset://direct_start.game.json", &options, &runtime,
                                                           error, sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.level1");

    const slayer3d_properties *scene_state = slayer3d_game_data_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "intro_entered", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "level_entered", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "checkpoint_visible_on_enter", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "checkpoint", ""), "midboss");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "lives", 0), 3);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "payload_level", ""), "level1");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "payload_from_scene", "missing"), "");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "payload_to_scene", ""), "scene.level1");

    slayer3d_game_data_destroy(runtime);
    slayer3d_properties_destroy(initial_payload);
    slayer3d_properties_destroy(initial_state);
    slayer3d_asset_resolver_destroy(assets);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, DirectStartRejectsUnknownScene)
{
    const std::filesystem::path dir = unique_test_dir("direct_start_bad_scene");
    write_direct_start_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;

    slayer3d_game_data_load_options options{};
    options.session = session;
    options.initial_scene_override = "scene.missing";

    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_asset_with_options(assets, "asset://direct_start.game.json", &options,
                                                            &runtime, error, sizeof(error)));
    EXPECT_EQ(runtime, nullptr);
    EXPECT_NE(std::string(error).find("initial scene override"), std::string::npos);

    slayer3d_asset_resolver_destroy(assets);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, DataGameRuntimeDirectStartPassesSceneState)
{
    const std::filesystem::path dir = unique_test_dir("data_runtime_direct_start");
    write_direct_start_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    slayer3d_properties *initial_state = slayer3d_properties_create();
    ASSERT_NE(initial_state, nullptr);
    slayer3d_properties_set_string(initial_state, "checkpoint", "midboss");

    slayer3d_data_game_runtime_desc desc{};
    const std::string root = dir.string();
    slayer3d_data_game_runtime_desc_init(&desc);
    desc.session = session;
    desc.data_asset_path = "asset://direct_start.game.json";
    desc.mount_assets = mount_test_directory_assets;
    desc.mount_userdata = const_cast<char *>(root.c_str());
    desc.initial_scene_override = "scene.level1";
    desc.initial_scene_state = initial_state;
    desc.skip_app_flow_startup = true;

    char error[512]{};
    slayer3d_data_game_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error))) << error;
    slayer3d_game_data_runtime *data = slayer3d_data_game_runtime_data(runtime);
    ASSERT_NE(data, nullptr);
    EXPECT_STREQ(slayer3d_game_data_active_scene(data), "scene.level1");
    EXPECT_TRUE(
        slayer3d_properties_get_bool(slayer3d_game_data_scene_state(data), "checkpoint_visible_on_enter", false));

    slayer3d_data_game_runtime_destroy(runtime);
    slayer3d_properties_destroy(initial_state);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, DataGameRuntimeDirectStartsFocusedFpsDojoScenes)
{
    const std::filesystem::path data_path = fps_mechanics_dojo_data_path();
    ASSERT_TRUE(std::filesystem::exists(data_path)) << data_path;
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    struct DojoDirectStartCase
    {
        const char *scene;
        const char *scene_key;
        slayer3d_vec3 expected_position;
        const char *ui_title;
    };
    const DojoDirectStartCase scenes[] = {
        {"scene.dojo.movement", "movement", slayer3d_vec3_make(3.0f, 1.6f, 4.0f), "ui.dojo.movement.title"},
        {"scene.dojo.combat_resources", "combat_resources", slayer3d_vec3_make(21.0f, 1.6f, 5.0f),
         "ui.dojo.combat_resources.title"},
        {"scene.dojo.hazards", "hazards", slayer3d_vec3_make(16.0f, 1.6f, 5.0f), "ui.dojo.hazards.title"},
        {"scene.dojo.navigation", "navigation", slayer3d_vec3_make(4.0f, 1.6f, 14.0f), "ui.dojo.navigation.title"},
    };

    for (const DojoDirectStartCase &scene : scenes)
    {
        slayer3d_game_session *session = nullptr;
        ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

        slayer3d_data_game_runtime_desc desc{};
        slayer3d_data_game_runtime_desc_init(&desc);
        desc.session = session;
        desc.media_dir = SLAYER3D_MEDIA_DIR;
        desc.data_asset_path = asset_path.c_str();
        desc.mount_assets = mount_test_directory_assets;
        desc.mount_userdata = const_cast<char *>(root.c_str());
        desc.initial_scene_override = scene.scene;
        desc.skip_app_flow_startup = true;

        char error[512]{};
        slayer3d_data_game_runtime *runtime = nullptr;
        ASSERT_TRUE(slayer3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error)))
            << scene.scene << ": " << error;
        slayer3d_game_data_runtime *data = slayer3d_data_game_runtime_data(runtime);
        ASSERT_NE(data, nullptr);
        EXPECT_STREQ(slayer3d_game_data_active_scene(data), scene.scene);

        slayer3d_registered_actor *player = slayer3d_game_data_find_actor(data, "entity.player");
        ASSERT_NE(player, nullptr);
        EXPECT_STREQ(slayer3d_properties_get_string(player->props, "dojo_scene", ""), scene.scene_key);
        expect_vec3_near(player->position, scene.expected_position);

        bool saw_title = false;
        auto find_direct_start_dojo_title = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
            auto *args = static_cast<std::pair<const char *, bool *> *>(userdata);
            if (std::string(text->name) == args->first)
            {
                *args->second = true;
                return false;
            }
            return true;
        };
        std::pair<const char *, bool *> title_args{scene.ui_title, &saw_title};
        ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(data, find_direct_start_dojo_title, &title_args));
        EXPECT_TRUE(saw_title) << scene.ui_title;

        slayer3d_game_context ctx{};
        ctx.session = session;
        EXPECT_TRUE(slayer3d_data_game_runtime_update_frame(runtime, &ctx, 0.016f));

        slayer3d_data_game_runtime_destroy(runtime);
        slayer3d_game_session_destroy(session);
    }
}

TEST(GameDataRuntime, DataGameRuntimeRefreshesInputProfilesOnGamepadHotplug)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    if (slayer3d_input_gamepad_count(input) != 0)
    {
        slayer3d_game_session_destroy(session);
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }

    const std::filesystem::path data_path = pong_data_path();
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    slayer3d_data_game_runtime_desc desc{};
    slayer3d_data_game_runtime_desc_init(&desc);
    desc.session = session;
    desc.media_dir = SLAYER3D_MEDIA_DIR;
    desc.data_asset_path = asset_path.c_str();
    desc.mount_assets = mount_test_directory_assets;
    desc.mount_userdata = const_cast<char *>(root.c_str());

    char error[512]{};
    slayer3d_data_game_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error))) << error;
    slayer3d_game_data_runtime *data = slayer3d_data_game_runtime_data(runtime);
    ASSERT_NE(data, nullptr);

    const int p1_up = slayer3d_game_data_find_action(data, "action.paddle.up");
    const int p2_up = slayer3d_game_data_find_action(data, "action.paddle.local.up");
    ASSERT_GE(p1_up, 0);
    ASSERT_GE(p2_up, 0);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "match_mode", "local");
    slayer3d_properties_set_string(scene_state, "network_role", "none");
    slayer3d_properties_set_string(scene_state, "network_flow", "none");

    slayer3d_game_context ctx{};
    ctx.session = session;
    ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(runtime, &ctx, 0.016f));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 0.0f);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7301;
    slayer3d_input_process_event(input, &event);
    ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(runtime, &ctx, 0.016f));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7301;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 3);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 1.0f);

    slayer3d_data_game_runtime_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DataGameRuntimeNetworkLoopReplicatesPongInputStateAndControls)
{
    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_session *client_session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &host_session));
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &client_session));

    const std::filesystem::path data_path = pong_data_path();
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    slayer3d_data_game_runtime_desc host_desc{};
    slayer3d_data_game_runtime_desc_init(&host_desc);
    host_desc.session = host_session;
    host_desc.media_dir = SLAYER3D_MEDIA_DIR;
    host_desc.data_asset_path = asset_path.c_str();
    host_desc.mount_assets = mount_test_directory_assets;
    host_desc.mount_userdata = const_cast<char *>(root.c_str());

    slayer3d_data_game_runtime_desc client_desc = host_desc;
    client_desc.session = client_session;

    char error[512]{};
    slayer3d_data_game_runtime *host_runtime = nullptr;
    slayer3d_data_game_runtime *client_runtime = nullptr;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&host_desc, &host_runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&client_desc, &client_runtime, error, sizeof(error))) << error;
    slayer3d_game_data_runtime *host_data = slayer3d_data_game_runtime_data(host_runtime);
    slayer3d_game_data_runtime *client_data = slayer3d_data_game_runtime_data(client_runtime);
    ASSERT_NE(host_data, nullptr);
    ASSERT_NE(client_data, nullptr);

    auto enter_play = [](slayer3d_game_data_runtime *runtime, const char *role) {
        slayer3d_properties *payload = slayer3d_properties_create();
        if (payload == nullptr)
            return false;
        slayer3d_properties_set_string(payload, "match_mode", "lan");
        slayer3d_properties_set_string(payload, "network_role", role);
        slayer3d_properties_set_string(payload, "network_flow", SDL_strcmp(role, "host") == 0 ? "host" : "direct");
        const bool ok = slayer3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload);
        slayer3d_properties_destroy(payload);
        return ok;
    };
    ASSERT_TRUE(enter_play(host_data, "host"));
    ASSERT_TRUE(enter_play(client_data, "client"));

    const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_name =
        test_info != nullptr ? std::string(test_info->test_suite_name()) + "." + test_info->name() : "runtime_net";
    const int port = 30000 + (int)(std::hash<std::string>{}(test_name) % 20000U);
    if (!slayer3d_game_data_network_host_start(host_data, "host", port, "SLAYER3D Test", "host_status", "host_endpoint",
                                               "host_peer", "host_connected"))
    {
        slayer3d_data_game_runtime_destroy(client_runtime);
        slayer3d_data_game_runtime_destroy(host_runtime);
        slayer3d_game_session_destroy(client_session);
        slayer3d_game_session_destroy(host_session);
        GTEST_SKIP() << "network host unavailable: " << SDL_GetError();
    }
    ASSERT_TRUE(slayer3d_game_data_network_direct_connect_start(client_data, "direct_connect", "127.0.0.1", port,
                                                                "direct_status", "direct_state", "direct_connected"));

    const slayer3d_data_game_network_bindings bindings = {"state_snapshot", "client_input",   "start_game",
                                                          "pause_request",  "resume_request", "disconnect"};
    slayer3d_game_context host_ctx{};
    host_ctx.session = host_session;
    slayer3d_game_context client_ctx{};
    client_ctx.session = client_session;
    slayer3d_data_game_network_loop_result host_result{};
    slayer3d_data_game_network_loop_result client_result{};

    slayer3d_network_session *host_net = slayer3d_game_data_get_network_host_session(host_data, "host");
    slayer3d_network_session *client_net =
        slayer3d_game_data_get_network_direct_connect_session(client_data, "direct_connect");
    ASSERT_NE(host_net, nullptr);
    ASSERT_NE(client_net, nullptr);
    bool connected = false;
    for (int i = 0; i < 1200 && !connected; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_network_host_session(
            host_runtime, &host_ctx, "host", &bindings, false, 0.01f, &host_result, error, sizeof(error)))
            << error;
        ASSERT_TRUE(slayer3d_data_game_runtime_update_network_client_session(
            client_runtime, &client_ctx, "direct_connect", &bindings, false, false, 0.01f, &client_result, error,
            sizeof(error)))
            << error;
        connected =
            slayer3d_network_session_is_connected(host_net) && slayer3d_network_session_is_connected(client_net);
    }
    ASSERT_TRUE(connected);

    slayer3d_input_manager *client_input = slayer3d_game_session_get_input(client_session);
    slayer3d_input_manager *host_input = slayer3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);
    const int client_up = slayer3d_game_data_find_action(client_data, "action.paddle.local.up");
    const int host_up = slayer3d_game_data_find_action(host_data, "action.paddle.local.up");
    ASSERT_GE(client_up, 0);
    ASSERT_GE(host_up, 0);
    slayer3d_input_set_action_override(client_input, client_up, 1.0f);
    ASSERT_NE(slayer3d_input_update(client_input, 101), nullptr);
    ASSERT_TRUE(slayer3d_data_game_runtime_update_network_client_session(client_runtime, &client_ctx, "direct_connect",
                                                                         &bindings, true, false, 0.016f, &client_result,
                                                                         error, sizeof(error)))
        << error;
    EXPECT_TRUE(client_result.sent_input);
    for (int i = 0; i < 120 && !host_result.applied_input; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_network_host_session(
            host_runtime, &host_ctx, "host", &bindings, true, 0.01f, &host_result, error, sizeof(error)))
            << error;
    }
    ASSERT_TRUE(host_result.applied_input);
    ASSERT_NE(slayer3d_input_update(host_input, 102), nullptr);
    EXPECT_NEAR(slayer3d_input_get_value(host_input, host_up), 1.0f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_send_network_runtime_control(client_data, client_net, "pause_request", 202U, error,
                                                                sizeof(error)))
        << error;
    host_ctx.paused = false;
    host_result = {};
    for (int i = 0; i < 120 && !host_result.received_pause_request; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_network_host_session(
            host_runtime, &host_ctx, "host", &bindings, true, 0.01f, &host_result, error, sizeof(error)))
            << error;
    }
    EXPECT_TRUE(host_result.received_pause_request);
    EXPECT_TRUE(host_ctx.paused);

    slayer3d_registered_actor *host_ball = slayer3d_game_data_find_actor(host_data, "entity.ball");
    slayer3d_registered_actor *client_ball = slayer3d_game_data_find_actor(client_data, "entity.ball");
    ASSERT_NE(host_ball, nullptr);
    ASSERT_NE(client_ball, nullptr);
    host_ball->position = slayer3d_vec3_make(3.0f, 2.0f, 0.0f);
    ASSERT_TRUE(slayer3d_data_game_runtime_publish_network_host_snapshot(host_runtime, &host_ctx, "host", &bindings,
                                                                         &host_result, error, sizeof(error)))
        << error;
    EXPECT_TRUE(host_result.sent_snapshot);
    client_ctx.paused = false;
    client_result = {};
    for (int i = 0; i < 120 && !client_result.applied_snapshot; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_network_client_session(
            client_runtime, &client_ctx, "direct_connect", &bindings, true, false, 0.01f, &client_result, error,
            sizeof(error)))
            << error;
    }
    ASSERT_TRUE(client_result.applied_snapshot);
    EXPECT_NEAR(client_ball->position.x, host_ball->position.x, 0.0001f);
    EXPECT_NEAR(client_ball->position.y, host_ball->position.y, 0.0001f);
    EXPECT_TRUE(client_ctx.paused);

    ASSERT_TRUE(
        slayer3d_game_data_send_network_runtime_control(host_data, host_net, "start_game", 303U, error, sizeof(error)))
        << error;
    client_result = {};
    for (int i = 0; i < 120 && !client_result.received_start_game; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_network_client_session(
            client_runtime, &client_ctx, "direct_connect", &bindings, true, false, 0.01f, &client_result, error,
            sizeof(error)))
            << error;
    }
    EXPECT_TRUE(client_result.received_start_game);

    slayer3d_data_game_runtime_destroy(client_runtime);
    slayer3d_data_game_runtime_destroy(host_runtime);
    slayer3d_game_session_destroy(client_session);
    slayer3d_game_session_destroy(host_session);
}

TEST(GameDataRuntime, ManagedNetworkRuntimeStartsPongMatchAndReplicatesState)
{
    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_session *client_session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &host_session));
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &client_session));

    const std::filesystem::path data_path = pong_data_path();
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    slayer3d_data_game_runtime_desc host_desc{};
    slayer3d_data_game_runtime_desc_init(&host_desc);
    host_desc.session = host_session;
    host_desc.media_dir = SLAYER3D_MEDIA_DIR;
    host_desc.data_asset_path = asset_path.c_str();
    host_desc.mount_assets = mount_test_directory_assets;
    host_desc.mount_userdata = const_cast<char *>(root.c_str());
    host_desc.enable_managed_network = true;

    slayer3d_data_game_runtime_desc client_desc = host_desc;
    client_desc.session = client_session;

    char error[512]{};
    slayer3d_data_game_runtime *host_runtime = nullptr;
    slayer3d_data_game_runtime *client_runtime = nullptr;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&host_desc, &host_runtime, error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&client_desc, &client_runtime, error, sizeof(error))) << error;
    slayer3d_game_data_runtime *host_data = slayer3d_data_game_runtime_data(host_runtime);
    slayer3d_game_data_runtime *client_data = slayer3d_data_game_runtime_data(client_runtime);
    ASSERT_NE(host_data, nullptr);
    ASSERT_NE(client_data, nullptr);

    auto enter_scene = [](slayer3d_game_data_runtime *runtime, const char *scene, const char *network_flow) {
        slayer3d_properties *payload = slayer3d_properties_create();
        if (payload == nullptr)
            return false;
        slayer3d_properties_set_string(payload, "network_flow", network_flow);
        const bool ok = slayer3d_game_data_set_active_scene_with_payload(runtime, scene, payload);
        slayer3d_properties_destroy(payload);
        return ok;
    };
    ASSERT_TRUE(enter_scene(host_data, "scene.multiplayer.lobby", "host"));
    ASSERT_TRUE(enter_scene(client_data, "scene.multiplayer.direct_connect", "direct"));

    const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_name =
        test_info != nullptr ? std::string(test_info->test_suite_name()) + "." + test_info->name() : "managed_net";
    const int port = 30000 + (int)(std::hash<std::string>{}(test_name) % 20000U);
    if (!slayer3d_game_data_network_host_start(host_data, "host", port, "SLAYER3D Test", "host_status", "host_endpoint",
                                               "host_peer", "host_connected"))
    {
        slayer3d_data_game_runtime_destroy(client_runtime);
        slayer3d_data_game_runtime_destroy(host_runtime);
        slayer3d_game_session_destroy(client_session);
        slayer3d_game_session_destroy(host_session);
        GTEST_SKIP() << "network host unavailable: " << SDL_GetError();
    }
    ASSERT_TRUE(slayer3d_game_data_network_direct_connect_start(client_data, "direct_connect", "127.0.0.1", port,
                                                                "direct_connect_status", "direct_connect_state",
                                                                "direct_connect_connected"));

    slayer3d_game_context host_ctx{};
    host_ctx.session = host_session;
    slayer3d_game_context client_ctx{};
    client_ctx.session = client_session;

    slayer3d_network_session *host_net = slayer3d_game_data_get_network_host_session(host_data, "host");
    slayer3d_network_session *client_net =
        slayer3d_game_data_get_network_direct_connect_session(client_data, "direct_connect");
    ASSERT_NE(host_net, nullptr);
    ASSERT_NE(client_net, nullptr);
    bool connected = false;
    for (int i = 0; i < 1200 && !connected; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(host_runtime, &host_ctx, 0.01f));
        ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.01f));
        connected =
            slayer3d_network_session_is_connected(host_net) && slayer3d_network_session_is_connected(client_net);
    }
    ASSERT_TRUE(connected);

    int lobby_start_signal = -1;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_signal(host_data, "lobby_start", &lobby_start_signal));
    ASSERT_GE(lobby_start_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(host_session), lobby_start_signal, nullptr);

    bool started = false;
    for (int i = 0; i < 240 && !started; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(host_runtime, &host_ctx, 0.016f));
        ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.016f));
        started = SDL_strcmp(slayer3d_game_data_active_scene(host_data), "scene.play") == 0 &&
                  SDL_strcmp(slayer3d_game_data_active_scene(client_data), "scene.play") == 0;
    }
    ASSERT_TRUE(started);
    const slayer3d_properties *host_scene_state = slayer3d_game_data_scene_state(host_data);
    const slayer3d_properties *client_scene_state = slayer3d_game_data_scene_state(client_data);
    ASSERT_NE(host_scene_state, nullptr);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(host_scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(slayer3d_properties_get_string(host_scene_state, "network_role", ""), "host");
    EXPECT_STREQ(slayer3d_properties_get_string(client_scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(slayer3d_properties_get_string(client_scene_state, "network_role", ""), "client");

    slayer3d_registered_actor *host_ball = slayer3d_game_data_find_actor(host_data, "entity.ball");
    slayer3d_registered_actor *client_ball = slayer3d_game_data_find_actor(client_data, "entity.ball");
    ASSERT_NE(host_ball, nullptr);
    ASSERT_NE(client_ball, nullptr);
    host_ctx.paused = true;
    client_ctx.paused = false;
    host_ball->position = slayer3d_vec3_make(4.0f, 1.5f, 0.0f);
    bool snapshot_applied = false;
    for (int i = 0; i < 240 && !snapshot_applied; ++i)
    {
        ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(host_runtime, &host_ctx, 0.016f));
        ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.016f));
        snapshot_applied = SDL_fabsf(client_ball->position.x - host_ball->position.x) < 0.0001f &&
                           SDL_fabsf(client_ball->position.y - host_ball->position.y) < 0.0001f;
    }
    EXPECT_TRUE(snapshot_applied);
    EXPECT_TRUE(client_ctx.paused);

    slayer3d_properties *termination_payload = slayer3d_properties_create();
    ASSERT_NE(termination_payload, nullptr);
    slayer3d_properties_set_string(termination_payload, "reason", "Test disconnect");
    ASSERT_TRUE(slayer3d_game_data_run_network_session_flow_event(client_data, &client_ctx, "client_match_terminated",
                                                                  termination_payload, error, sizeof(error)))
        << error;
    slayer3d_properties_destroy(termination_payload);
    client_scene_state = slayer3d_game_data_scene_state(client_data);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(client_scene_state, "network_match_termination_active", false));

    int select_action = -1;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_action(client_data, "menu_select", &select_action));
    slayer3d_input_manager *client_input = slayer3d_game_session_get_input(client_session);
    ASSERT_NE(client_input, nullptr);
    slayer3d_input_set_action_override(client_input, select_action, 1.0f);
    ASSERT_NE(slayer3d_input_update(client_input, 5000), nullptr);
    ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.25f));
    client_scene_state = slayer3d_game_data_scene_state(client_data);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(client_scene_state, "network_match_termination_active", false));
    EXPECT_STREQ(slayer3d_game_data_active_scene(client_data), "scene.play");

    slayer3d_input_set_action_override(client_input, select_action, 0.0f);
    ASSERT_NE(slayer3d_input_update(client_input, 5001), nullptr);
    ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 2.74f));
    EXPECT_TRUE(slayer3d_properties_get_bool(client_scene_state, "network_match_termination_active", false));

    slayer3d_input_set_action_override(client_input, select_action, 1.0f);
    ASSERT_NE(slayer3d_input_update(client_input, 5002), nullptr);
    ASSERT_TRUE(slayer3d_data_game_runtime_update_frame(client_runtime, &client_ctx, 0.02f));
    client_scene_state = slayer3d_game_data_scene_state(client_data);
    ASSERT_NE(client_scene_state, nullptr);
    EXPECT_FALSE(slayer3d_properties_get_bool(client_scene_state, "network_match_termination_active", true));
    EXPECT_FALSE(client_ctx.paused);
    EXPECT_STREQ(slayer3d_game_data_active_scene(client_data), "scene.title");

    slayer3d_data_game_runtime_destroy(client_runtime);
    slayer3d_data_game_runtime_destroy(host_runtime);
    slayer3d_game_session_destroy(client_session);
    slayer3d_game_session_destroy(host_session);
}

TEST(GameDataRuntime, AuthoredNetworkSessionFlowEventsDriveSceneTransitions)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    const std::filesystem::path data_path = pong_data_path();
    const std::string root = data_path.parent_path().string();
    const std::string asset_path = std::string("asset://") + data_path.filename().string();

    slayer3d_data_game_runtime_desc desc{};
    slayer3d_data_game_runtime_desc_init(&desc);
    desc.session = session;
    desc.media_dir = SLAYER3D_MEDIA_DIR;
    desc.data_asset_path = asset_path.c_str();
    desc.mount_assets = mount_test_directory_assets;
    desc.mount_userdata = const_cast<char *>(root.c_str());

    char error[512]{};
    slayer3d_data_game_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_data_game_runtime_create(&desc, &runtime, error, sizeof(error))) << error;
    slayer3d_game_data_runtime *data = slayer3d_data_game_runtime_data(runtime);
    ASSERT_NE(data, nullptr);

    slayer3d_game_context ctx{};
    ctx.session = session;

    ASSERT_TRUE(slayer3d_game_data_run_network_session_flow_event(data, &ctx, "client_start_game", nullptr, error,
                                                                  sizeof(error)))
        << error;
    EXPECT_STREQ(slayer3d_game_data_active_scene(data), "scene.play");
    const slayer3d_properties *scene_state = slayer3d_game_data_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "network_role", ""), "client");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "network_flow", ""), "direct");

    slayer3d_properties *payload = slayer3d_properties_create();
    ASSERT_NE(payload, nullptr);
    slayer3d_properties_set_string(payload, "reason", "Cable unplugged");
    ctx.paused = false;
    ASSERT_TRUE(slayer3d_game_data_run_network_session_flow_event(data, &ctx, "client_match_terminated", payload, error,
                                                                  sizeof(error)))
        << error;
    slayer3d_properties_destroy(payload);
    scene_state = slayer3d_game_data_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_TRUE(ctx.paused);
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "network_match_termination_active", false));
    EXPECT_NE(SDL_strstr(slayer3d_properties_get_string(scene_state, "network_match_termination_message", ""),
                         "Cable unplugged"),
              nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""), "Cable unplugged");

    ASSERT_TRUE(slayer3d_game_data_run_network_session_flow_event(data, &ctx, "network_match_termination_ack", nullptr,
                                                                  error, sizeof(error)))
        << error;
    scene_state = slayer3d_game_data_scene_state(data);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_FALSE(ctx.paused);
    EXPECT_STREQ(slayer3d_game_data_active_scene(data), "scene.title");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "network_match_termination_active", true));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "network_match_termination_message", "x"), "");

    slayer3d_data_game_runtime_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, NetworkSessionFlowPlaceholderMalformedBraceIsLiteral)
{
    const std::filesystem::path dir = unique_test_dir("network_flow_malformed_placeholder");
    const std::filesystem::path source = std::filesystem::path(pong_data_path()).parent_path();
    const std::filesystem::path dest = dir / "pong_data";
    std::filesystem::copy(source, dest,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);

    const std::filesystem::path game_path = dest / "pong.game.json";
    const std::filesystem::path network_path = dest / "fragments" / "network" / "session_flow.json";
    std::string network_json = read_text(network_path);
    const std::string marker = R"json("events": {)json";
    const size_t marker_pos = network_json.find(marker);
    ASSERT_NE(marker_pos, std::string::npos);
    network_json.insert(marker_pos + marker.size(), R"json(
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
    write_text(network_path, network_json.c_str());

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);

    ASSERT_TRUE(slayer3d_game_data_run_network_session_flow_event(runtime, nullptr, "malformed_placeholder", nullptr,
                                                                  error, sizeof(error)))
        << error;
    const slayer3d_properties *scene_state = slayer3d_game_data_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "malformed_placeholder_result", ""), "literal {reason");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReadsSpriteAssetMetadata)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(fixture_path("sprite_asset_fixture.game.json").c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_game_data_sprite_asset sprite{};
    ASSERT_TRUE(slayer3d_game_data_get_sprite_asset(runtime, "sprite.robot.walk", &sprite));
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

    EXPECT_FALSE(slayer3d_game_data_get_sprite_asset(runtime, "sprite.missing", &sprite));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ExposesAuthoredPongPresentationData)
{
    const std::filesystem::path dir = unique_test_dir("pong_presentation");
    const std::filesystem::path user_root = dir / "user";
    const std::filesystem::path cache_root = dir / "cache";
    const std::filesystem::path game_path = copy_pong_data_with_storage_overrides(dir, user_root, cache_root);

    slayer3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(game_path.string().c_str(), &config, title, sizeof(title),
                                                        app_error, sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "Slayer 3D Pong");
    EXPECT_EQ(config.width, 0);
    EXPECT_EQ(config.height, 0);
    EXPECT_EQ(config.logical_width, 1280);
    EXPECT_EQ(config.logical_height, 720);
    EXPECT_EQ(config.backend, SLAYER3D_BACKEND_OPENGL);
    EXPECT_EQ(config.display_mode, SLAYER3D_WINDOW_MODE_WINDOWED);
    EXPECT_GT(config.vsync, 0);
    EXPECT_GT(config.maximized, 0);
    EXPECT_NEAR(config.tick_rate, 1.0f / 120.0f, 0.00001f);
    EXPECT_EQ(config.max_ticks_per_frame, 12);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_storage_config storage{};
    ASSERT_TRUE(slayer3d_game_data_get_storage_config(runtime, &storage));
    EXPECT_STREQ(storage.organization, "Blue Sentinel Security");
    EXPECT_STREQ(storage.application, "Slayer 3D Pong");
    EXPECT_STREQ(storage.profile, "default");
    const std::string user_root_text = user_root.generic_string();
    const std::string cache_root_text = cache_root.generic_string();
    EXPECT_STREQ(storage.user_root_override, user_root_text.c_str());
    EXPECT_STREQ(storage.cache_root_override, cache_root_text.c_str());
    char storage_root[256]{};
    ASSERT_TRUE(slayer3d_storage_build_root_path(&storage, SLAYER3D_STORAGE_PLATFORM_UNIX, SLAYER3D_STORAGE_ROOT_USER,
                                                 "/home/player/.local/share", storage_root, sizeof(storage_root)));
    EXPECT_STREQ(storage_root, "/home/player/.local/share/Blue Sentinel Security/Slayer 3D Pong/profiles/default");

    slayer3d_camera3d camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.overhead", &camera));
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_ORTHOGRAPHIC);
    EXPECT_NEAR(camera.position.z, 16.0f, 0.0001f);
    EXPECT_NEAR(camera.fovy, 11.4f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.ball_chase", &camera));
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_NEAR(camera.fovy, 68.0f, 0.0001f);
    EXPECT_NEAR(camera.position.x, -2.6f, 0.0001f);
    EXPECT_NEAR(camera.position.z, 1.91f, 0.0001f);
    float chase_fovy = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_get_camera_float(runtime, "camera.ball_chase", "fovy", &chase_fovy));
    EXPECT_NEAR(chase_fovy, 68.0f, 0.0001f);

    slayer3d_game_data_app_control app{};
    ASSERT_TRUE(slayer3d_game_data_get_app_control(runtime, &app));
    EXPECT_EQ(app.start_signal_id, -1);
    EXPECT_GE(app.quit_action_id, 0);
    EXPECT_GE(app.pause_action_id, 0);
    EXPECT_EQ(app.startup_transition, nullptr);
    EXPECT_STREQ(app.quit_transition, "quit");
    EXPECT_GE(app.quit_signal_id, 0);
    EXPECT_EQ(app.window_apply_signal_id, slayer3d_game_data_find_signal(runtime, "signal.settings.apply"));
    EXPECT_STREQ(app.window_settings_target, "entity.settings");
    EXPECT_STREQ(app.window_display_mode_key, "display_mode");
    EXPECT_STREQ(app.window_renderer_key, "renderer");
    EXPECT_STREQ(app.window_vsync_key, "vsync");

    slayer3d_registered_actor *match = slayer3d_game_data_find_actor(runtime, "entity.match");
    ASSERT_NE(match, nullptr);
    EXPECT_TRUE(slayer3d_game_data_app_pause_allowed(runtime, nullptr));
    slayer3d_properties_set_bool(match->props, "finished", true);
    EXPECT_FALSE(slayer3d_game_data_app_pause_allowed(runtime, nullptr));
    slayer3d_properties_set_bool(match->props, "finished", false);

    slayer3d_game_data_font_asset font{};
    ASSERT_TRUE(slayer3d_game_data_get_font_asset(runtime, "font.hud", &font));
    EXPECT_TRUE(font.builtin);
    EXPECT_EQ(font.builtin_id, SLAYER3D_BUILTIN_FONT_INTER);
    EXPECT_NEAR(font.size, 34.0f, 0.0001f);
    ASSERT_TRUE(slayer3d_game_data_get_font_asset(runtime, "font.title", &font));
    EXPECT_TRUE(font.builtin);
    EXPECT_NEAR(font.size, 96.0f, 0.0001f);

    float ambient[3]{};
    ASSERT_TRUE(slayer3d_game_data_get_world_ambient_light(runtime, ambient));
    EXPECT_NEAR(ambient[0], 0.015f, 0.0001f);
    EXPECT_NEAR(ambient[1], 0.018f, 0.0001f);
    EXPECT_NEAR(ambient[2], 0.026f, 0.0001f);

    EXPECT_EQ(slayer3d_game_data_world_light_count(runtime), 3);
    slayer3d_light red_light{};
    ASSERT_TRUE(slayer3d_game_data_get_world_light(runtime, 0, &red_light));
    EXPECT_EQ(red_light.type, SLAYER3D_LIGHT_SPOT);
    EXPECT_NEAR(red_light.position.x, -8.15f, 0.0001f);
    EXPECT_NEAR(red_light.position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(red_light.direction.x, 0.0f, 0.0001f);
    EXPECT_NEAR(red_light.direction.z, -1.0f, 0.0001f);
    EXPECT_NEAR(red_light.color[0], 1.0f, 0.0001f);
    EXPECT_NEAR(red_light.color[1], 0.06f, 0.0001f);
    slayer3d_light red_eval{};
    slayer3d_game_data_render_eval red_light_eval{};
    red_light_eval.time = 1.0f;
    ASSERT_TRUE(slayer3d_game_data_get_world_light_evaluated(runtime, 0, &red_light_eval, &red_eval));
    EXPECT_GT(red_eval.color[1], red_light.color[1]);

    slayer3d_light blue_light{};
    ASSERT_TRUE(slayer3d_game_data_get_world_light(runtime, 2, &blue_light));
    EXPECT_EQ(blue_light.type, SLAYER3D_LIGHT_SPOT);
    EXPECT_NEAR(blue_light.position.x, 8.15f, 0.0001f);
    EXPECT_NEAR(blue_light.position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(blue_light.position.z, 3.35f, 0.0001f);
    EXPECT_NEAR(blue_light.direction.x, 0.0f, 0.0001f);
    EXPECT_NEAR(blue_light.direction.z, -1.0f, 0.0001f);
    EXPECT_NEAR(blue_light.color[0], 0.08f, 0.0001f);
    EXPECT_NEAR(blue_light.color[1], 0.28f, 0.0001f);
    EXPECT_NEAR(blue_light.color[2], 1.0f, 0.0001f);
    slayer3d_light blue_eval{};
    slayer3d_game_data_render_eval blue_light_eval{};
    blue_light_eval.time = 1.0f;
    ASSERT_TRUE(slayer3d_game_data_get_world_light_evaluated(runtime, 2, &blue_light_eval, &blue_eval));
    EXPECT_GT(blue_eval.color[1], blue_light.color[1]);

    slayer3d_light lamp_light{};
    ASSERT_TRUE(slayer3d_game_data_get_world_light(runtime, 1, &lamp_light));
    EXPECT_EQ(lamp_light.type, SLAYER3D_LIGHT_SPOT);
    EXPECT_NEAR(lamp_light.position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(lamp_light.position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(lamp_light.position.z, 2.92f, 0.0001f);
    EXPECT_NEAR(lamp_light.direction.z, -1.0f, 0.0001f);
    EXPECT_NEAR(lamp_light.range, 5.8f, 0.0001f);

    slayer3d_particle_config particles{};
    ASSERT_TRUE(slayer3d_game_data_get_particle_emitter(runtime, "entity.effect.ambient_particles", &particles));
    EXPECT_EQ(particles.shape, SLAYER3D_PARTICLE_EMITTER_BOX);
    EXPECT_EQ(particles.max_particles, 360);
    EXPECT_NEAR(particles.emit_rate, 95.0f, 0.0001f);
    EXPECT_EQ(particles.color_start.a, 105);
    slayer3d_vec3 particle_emissive{};
    ASSERT_TRUE(slayer3d_game_data_get_particle_emitter_draw_emissive(runtime, "entity.effect.ambient_particles",
                                                                      &particle_emissive));
    EXPECT_NEAR(particle_emissive.x, 0.8f, 0.0001f);

    ParticleCapture title_particles{};
    ASSERT_TRUE(slayer3d_game_data_for_each_particle_emitter(runtime, capture_particle, &title_particles));
    EXPECT_EQ(title_particles.count, 0);

    slayer3d_game_data_render_settings render{};
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render));
    EXPECT_EQ(render.clear_color.r, 3);
    EXPECT_EQ(render.clear_color.g, 4);
    EXPECT_EQ(render.clear_color.b, 8);
    EXPECT_TRUE(render.lighting_enabled);
    EXPECT_TRUE(render.bloom_enabled);
    EXPECT_TRUE(render.ssao_enabled);
    EXPECT_EQ(render.tonemap, SLAYER3D_TONEMAP_ACES);

    slayer3d_game_data_transition_desc transition{};
    ASSERT_TRUE(slayer3d_game_data_get_transition(runtime, "quit", &transition));
    EXPECT_EQ(transition.type, SLAYER3D_TRANSITION_FADE);
    EXPECT_EQ(transition.direction, SLAYER3D_TRANSITION_OUT);
    EXPECT_NEAR(transition.duration, 0.45f, 0.0001f);
    EXPECT_GE(transition.done_signal_id, 0);

    RenderPrimitiveCapture title_capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &title_capture));
    EXPECT_EQ(title_capture.cubes, 0);
    EXPECT_EQ(title_capture.spheres, 0);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.play"));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.play");
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.overhead");
    EXPECT_TRUE(slayer3d_game_data_active_scene_updates_game(runtime));
    EXPECT_TRUE(slayer3d_game_data_active_scene_renders_world(runtime));
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.ball"));
    EXPECT_EQ(slayer3d_timer_pool_active_count(slayer3d_game_session_get_timer_pool(session)), 1);

    ParticleCapture play_particles{};
    ASSERT_TRUE(slayer3d_game_data_for_each_particle_emitter(runtime, capture_particle, &play_particles));
    EXPECT_EQ(play_particles.count, 1);
    EXPECT_TRUE(play_particles.saw_ambient);

    slayer3d_game_data_particle_cache particle_cache{};
    slayer3d_game_data_particle_cache_init(&particle_cache);
    ASSERT_TRUE(slayer3d_game_data_update_particles(runtime, &particle_cache, 0.1f));
    EXPECT_EQ(particle_cache.count, 1);
    EXPECT_TRUE(particle_cache.entries[0].visible);
    slayer3d_game_data_particle_cache_free(&particle_cache);

    RenderPrimitiveCapture capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &capture));
    EXPECT_EQ(capture.cubes, 16);
    EXPECT_EQ(capture.spheres, 1);
    EXPECT_TRUE(capture.saw_player_paddle);
    EXPECT_TRUE(capture.saw_ball);
    EXPECT_NEAR(capture.ball_rotation_angle, 0.0f, 0.0001f);

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(ball, nullptr);
    slayer3d_properties_set_bool(ball->props, "active_motion", true);
    slayer3d_properties_set_float(ball->props, "spin_angle", 0.0f);
    slayer3d_game_context spin_context{};
    spin_context.session = session;
    slayer3d_game_data_frame_state spin_frame_state{};
    slayer3d_game_data_frame_state_init(&spin_frame_state);
    slayer3d_game_data_update_frame_desc spin_update{};
    spin_update.ctx = &spin_context;
    spin_update.runtime = runtime;
    spin_update.dt = 0.5f;
    ASSERT_TRUE(slayer3d_game_data_update_frame(&spin_frame_state, &spin_update));
    EXPECT_NEAR(slayer3d_properties_get_float(ball->props, "spin_angle", -1.0f), 2.7f, 0.0001f);

    RenderPrimitiveCapture spun_capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &spun_capture));
    EXPECT_TRUE(spun_capture.saw_ball);
    EXPECT_NEAR(spun_capture.ball_rotation_angle, 2.7f, 0.0001f);

    slayer3d_registered_actor *presentation = slayer3d_game_data_find_actor(runtime, "entity.presentation");
    ASSERT_NE(presentation, nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(presentation->props, "border_flash_decay", 0.0f), 2.8f, 0.0001f);
    slayer3d_properties_set_float(presentation->props, "border_flash", 1.0f);
    ASSERT_TRUE(slayer3d_game_data_update_property_effects(runtime, 0.25f));
    EXPECT_NEAR(slayer3d_properties_get_float(presentation->props, "border_flash", -1.0f), 0.3f, 0.0001f);
    slayer3d_properties_set_float(presentation->props, "border_flash", 1.0f);

    slayer3d_light base_light{};
    slayer3d_light flashed_light{};
    ASSERT_TRUE(slayer3d_game_data_get_world_light(runtime, 1, &base_light));
    slayer3d_game_data_render_eval light_eval{};
    ASSERT_TRUE(slayer3d_game_data_get_world_light_evaluated(runtime, 1, &light_eval, &flashed_light));
    EXPECT_GT(flashed_light.intensity, base_light.intensity);
    EXPECT_GT(flashed_light.range, base_light.range);
    slayer3d_game_data_render_eval render_eval{};
    render_eval.time = 0.25f;
    EvaluatedPrimitiveCapture evaluated{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive_evaluated(runtime, &render_eval,
                                                                       capture_evaluated_primitive, &evaluated));
    EXPECT_TRUE(evaluated.saw_border);
    EXPECT_TRUE(evaluated.saw_ball);

    UiTextCapture ui{};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, capture_ui_text, &ui));
    EXPECT_EQ(ui.count, 8);
    EXPECT_TRUE(ui.saw_score);
    EXPECT_TRUE(ui.saw_pause);
    EXPECT_TRUE(ui.saw_network_match_terminated);

    slayer3d_game_data_ui_metrics metrics{};
    metrics.fps = 119.5f;
    metrics.frame = 42;
    char ui_buffer[128]{};
    auto format_score = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.score")
            return true;
        auto *args = static_cast<std::pair<slayer3d_game_data_runtime *, char *> *>(userdata);
        slayer3d_game_data_ui_metrics local_metrics{};
        EXPECT_TRUE(slayer3d_game_data_format_ui_text(args->first, text, &local_metrics, args->second, 128));
        return false;
    };
    std::pair<slayer3d_game_data_runtime *, char *> score_args{runtime, ui_buffer};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, format_score, &score_args));
    EXPECT_STREQ(ui_buffer, "00   00");
    auto find_pause_visible = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.pause")
            return true;
        auto *args =
            static_cast<std::tuple<slayer3d_game_data_runtime *, slayer3d_game_data_ui_metrics *, bool *> *>(userdata);
        *std::get<2>(*args) = slayer3d_game_data_ui_text_is_visible(std::get<0>(*args), text, std::get<1>(*args));
        return false;
    };
    bool pause_visible = false;
    metrics.paused = true;
    std::tuple<slayer3d_game_data_runtime *, slayer3d_game_data_ui_metrics *, bool *> pause_args{runtime, &metrics,
                                                                                                 &pause_visible};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_pause_visible, &pause_args));
    EXPECT_TRUE(pause_visible);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_bool(scene_state, "network_match_termination_active", true);
    slayer3d_properties_set_string(scene_state, "network_match_termination_message",
                                   "Match terminated: Client exited - Press Enter to return to title screen.");
    bool termination_visible = false;
    char termination_buffer[192]{};
    auto find_termination_visible = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.network.match_terminated")
            return true;
        auto *args =
            static_cast<std::tuple<slayer3d_game_data_runtime *, slayer3d_game_data_ui_metrics *, bool *, char *> *>(
                userdata);
        *std::get<2>(*args) = slayer3d_game_data_ui_text_is_visible(std::get<0>(*args), text, std::get<1>(*args));
        EXPECT_TRUE(
            slayer3d_game_data_format_ui_text(std::get<0>(*args), text, std::get<1>(*args), std::get<3>(*args), 192));
        return false;
    };
    std::tuple<slayer3d_game_data_runtime *, slayer3d_game_data_ui_metrics *, bool *, char *> termination_args{
        runtime, &metrics, &termination_visible, termination_buffer};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_termination_visible, &termination_args));
    EXPECT_TRUE(termination_visible);
    EXPECT_STREQ(termination_buffer, "Match terminated: Client exited - Press Enter to return to title screen.");

    bool pause_hidden = true;
    std::tuple<slayer3d_game_data_runtime *, slayer3d_game_data_ui_metrics *, bool *> pause_hidden_args{
        runtime, &metrics, &pause_hidden};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_pause_visible, &pause_hidden_args));
    EXPECT_FALSE(pause_hidden);

    slayer3d_properties_set_bool(scene_state, "network_match_termination_active", false);

    struct PauseMenuTextArgs
    {
        bool saw_resume = false;
        bool saw_options = false;
    } pause_menu_text;
    auto find_pause_menu_text = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *args = static_cast<PauseMenuTextArgs *>(userdata);
        if (std::string(text->name) != "ui.pause.menu")
            return true;
        const std::string value = text->text != nullptr ? text->text : "";
        args->saw_resume = args->saw_resume || value == "Resume";
        args->saw_options = args->saw_options || value == "Options";
        return !(args->saw_resume && args->saw_options);
    };
    ASSERT_TRUE(
        slayer3d_game_data_for_each_ui_text_for_metrics(runtime, &metrics, find_pause_menu_text, &pause_menu_text));
    EXPECT_TRUE(pause_menu_text.saw_resume);
    EXPECT_TRUE(pause_menu_text.saw_options);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ExposesDataDrivenScenesAndMenus)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_game_data_skip_policy skip{};
    ASSERT_TRUE(slayer3d_game_data_get_active_skip_policy(runtime, &skip));
    EXPECT_TRUE(skip.enabled);
    EXPECT_EQ(skip.input, SLAYER3D_GAME_DATA_SKIP_INPUT_ANY);
    EXPECT_STREQ(skip.scene, "scene.title");
    EXPECT_TRUE(skip.preserve_exit_transition);
    EXPECT_TRUE(skip.consume_input);

    slayer3d_game_data_image_asset image_asset{};
    ASSERT_TRUE(slayer3d_game_data_get_image_asset(runtime, "image.splash.logo", &image_asset));
    EXPECT_STREQ(image_asset.sprite, "sprite.splash.logo");
    EXPECT_EQ(image_asset.path, nullptr);

    slayer3d_game_data_image_asset ball_image_asset{};
    ASSERT_TRUE(slayer3d_game_data_get_image_asset(runtime, "image.ball.texture", &ball_image_asset));
    EXPECT_STREQ(ball_image_asset.path, "asset://images/ball-texture.png");
    EXPECT_EQ(ball_image_asset.sprite, nullptr);

    slayer3d_game_data_sprite_asset sprite_asset{};
    ASSERT_TRUE(slayer3d_game_data_get_sprite_asset(runtime, "sprite.splash.logo", &sprite_asset));
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

    slayer3d_game_data_sound_asset sound_asset{};
    ASSERT_TRUE(slayer3d_game_data_get_sound_asset(runtime, "sound.pong.hit", &sound_asset));
    EXPECT_STREQ(sound_asset.path, "asset://audio/ui/click3.wav");
    EXPECT_EQ(sound_asset.bus, SLAYER3D_AUDIO_BUS_SOUND_EFFECTS);
    EXPECT_GT(sound_asset.volume, 0.0f);

    slayer3d_game_data_music_asset music_asset{};
    ASSERT_TRUE(slayer3d_game_data_get_music_asset(runtime, "music.title", &music_asset));
    EXPECT_STREQ(music_asset.path, "asset://audio/music/moonlight-sonata-allegretto.ogg");
    EXPECT_TRUE(music_asset.loop);
    EXPECT_GT(music_asset.volume, 0.0f);

    UiImageCapture images{};
    auto capture_image = [](void *userdata, const slayer3d_game_data_ui_image *image) -> bool {
        auto *capture = static_cast<UiImageCapture *>(userdata);
        capture->count++;
        if (image->name != nullptr && std::string(image->name) == "ui.splash.logo")
        {
            capture->saw_splash_logo = true;
            EXPECT_STREQ(image->image, "image.splash.logo");
            EXPECT_EQ(image->align, SLAYER3D_GAME_DATA_UI_ALIGN_CENTER);
            EXPECT_EQ(image->valign, SLAYER3D_GAME_DATA_UI_VALIGN_CENTER);
            EXPECT_TRUE(image->preserve_aspect);
        }
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_image(runtime, capture_image, &images));
    EXPECT_EQ(images.count, 1);
    EXPECT_TRUE(images.saw_splash_logo);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));
    EXPECT_TRUE(slayer3d_game_data_active_scene_updates_game(runtime));
    EXPECT_TRUE(slayer3d_game_data_active_scene_renders_world(runtime));
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.ball.attract"));
    EXPECT_FALSE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.ball"));

    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.title");
    EXPECT_EQ(menu.item_count, 4);
    EXPECT_EQ(menu.selected_index, 0);
    EXPECT_GE(menu.up_action_id, 0);
    EXPECT_GE(menu.down_action_id, 0);
    EXPECT_GE(menu.select_action_id, 0);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(slayer3d_game_data_active_menu_input_is_idle(runtime, input));
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);
    EXPECT_FALSE(slayer3d_game_data_active_menu_input_is_idle(runtime, input));
    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    EXPECT_TRUE(slayer3d_game_data_active_menu_input_is_idle(runtime, input));

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Single Player");
    EXPECT_STREQ(item.scene, "scene.play");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "single");

    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Multiplayer");
    EXPECT_STREQ(item.scene, "scene.multiplayer");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "multiplayer");

    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Options");
    EXPECT_STREQ(item.scene, "scene.options");
    EXPECT_FALSE(item.quit);

    ASSERT_TRUE(slayer3d_game_data_menu_move(runtime, menu.name, -1));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.selected_index, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, menu.selected_index, &item));
    EXPECT_STREQ(item.label, "Exit");
    EXPECT_TRUE(item.quit);

    ASSERT_EQ(slayer3d_game_data_scene_shortcut_count(runtime), 3);
    slayer3d_game_data_scene_shortcut shortcut{};
    ASSERT_TRUE(slayer3d_game_data_scene_shortcut_at(runtime, 2, &shortcut));
    EXPECT_STREQ(shortcut.action, "action.scene.play");
    EXPECT_STREQ(shortcut.scene, "scene.play");
    EXPECT_GE(shortcut.action_id, 0);

    slayer3d_game_data_transition_desc transition{};
    ASSERT_TRUE(slayer3d_game_data_get_scene_transition(runtime, "scene.title", "exit", &transition));
    EXPECT_EQ(transition.type, SLAYER3D_TRANSITION_FADE);
    EXPECT_EQ(transition.direction, SLAYER3D_TRANSITION_OUT);

    bool saw_title_cursor = false;
    bool saw_title_exit = false;
    auto find_title_menu_ui = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
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
            EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
            EXPECT_TRUE(text->pulse_alpha);
        }
        if (*flags->first && *flags->second)
            return false;
        return true;
    };
    std::pair<bool *, bool *> title_menu_flags{&saw_title_cursor, &saw_title_exit};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_title_menu_ui, &title_menu_flags));
    EXPECT_TRUE(saw_title_cursor);
    EXPECT_TRUE(saw_title_exit);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.multiplayer"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer");
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Local");
    EXPECT_STREQ(item.scene, "scene.play");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "local");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "LAN");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");
    EXPECT_STREQ(item.scene_state_key, "match_mode");
    EXPECT_STREQ(item.scene_state_value, "lan");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.multiplayer.lan"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.lan");
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Create Match");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lobby");
    EXPECT_STREQ(item.scene_state_key, "network_flow");
    EXPECT_STREQ(item.scene_state_value, "host");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Join Match");
    EXPECT_STREQ(item.scene, "scene.multiplayer.join");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.multiplayer.lobby"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.lobby.waiting");
    EXPECT_EQ(menu.item_count, 1);
    const int lobby_start_signal = slayer3d_game_data_find_signal(runtime, "signal.multiplayer.lobby.start");
    ASSERT_GE(lobby_start_signal, 0);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");
    slayer3d_properties_set_bool(slayer3d_game_data_mutable_scene_state(runtime), "multiplayer_host_connected", true);
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.lobby.connected");
    EXPECT_EQ(menu.item_count, 2);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Client 1");
    EXPECT_EQ(item.signal_id, lobby_start_signal);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.multiplayer.join"));
    EXPECT_TRUE(slayer3d_game_data_active_scene_renders_world(runtime));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.join");
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Search for local matches");
    EXPECT_STREQ(item.scene, "scene.multiplayer.discovery");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Join match with IP address or hostname");
    EXPECT_STREQ(item.scene, "scene.multiplayer.direct_connect");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.multiplayer.direct_connect"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.direct");
    EXPECT_EQ(menu.item_count, 5);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Host");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Port");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Connect");
    EXPECT_EQ(item.signal_id, slayer3d_game_data_find_signal(runtime, "signal.multiplayer.direct.connect"));
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 3, &item));
    EXPECT_STREQ(item.label, "Disconnect");
    EXPECT_EQ(item.signal_id, slayer3d_game_data_find_signal(runtime, "signal.multiplayer.direct.disconnect"));
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 4, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.join");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.multiplayer.discovery"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.discovery");
    EXPECT_EQ(menu.item_count, 2);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_EQ(item.dynamic_list_index, -1);
    EXPECT_STREQ(item.label, "Searching local network...");
    EXPECT_EQ(item.signal_id, -1);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.join");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options"));
    EXPECT_FALSE(slayer3d_game_data_active_scene_updates_game(runtime));
    EXPECT_TRUE(slayer3d_game_data_active_scene_renders_world(runtime));
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.overhead");
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.options.background.base"));
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.options.flow.magenta"));
    EXPECT_FALSE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.ball"));

    RenderPrimitiveCapture options_render{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &options_render));
    EXPECT_EQ(options_render.cubes, 1);
    EXPECT_EQ(options_render.spheres, 3);
    EXPECT_TRUE(options_render.saw_options_background);
    EXPECT_TRUE(options_render.saw_options_glow);

    ParticleCapture options_particles{};
    ASSERT_TRUE(slayer3d_game_data_for_each_particle_emitter(runtime, capture_particle, &options_particles));
    EXPECT_EQ(options_particles.count, 3);
    EXPECT_TRUE(options_particles.saw_options_flow);

    slayer3d_game_data_render_eval options_eval{};
    options_eval.time = 1.0f;
    EvaluatedPrimitiveCapture options_evaluated{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive_evaluated(
        runtime, &options_eval, capture_evaluated_primitive, &options_evaluated));
    EXPECT_TRUE(options_evaluated.saw_options_drift);

    const char *option_submenus[] = {"scene.options.display", "scene.options.keyboard", "scene.options.mouse",
                                     "scene.options.gamepad", "scene.options.audio"};
    for (const char *scene : option_submenus)
    {
        ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, scene));
        EXPECT_TRUE(slayer3d_game_data_active_scene_renders_world(runtime)) << scene;
        EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.overhead") << scene;
        EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.options.background.base")) << scene;
        EXPECT_FALSE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.ball")) << scene;
    }
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options"));

    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options");
    EXPECT_EQ(menu.item_count, 6);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Display");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "display");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Mouse");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "mouse");
    bool saw_options_display = false;
    bool saw_options_divider = false;
    auto find_options_display = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *flags = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.menu" && value == "Display")
        {
            *flags->first = true;
            EXPECT_FLOAT_EQ(text->x, 0.43f);
            EXPECT_FLOAT_EQ(text->y, 0.36f);
            EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
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
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_options_display, &options_display_flags));
    EXPECT_TRUE(saw_options_display);
    EXPECT_TRUE(saw_options_divider);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.keyboard");
    EXPECT_EQ(menu.item_count, 9);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Up");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 2);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 5, &item));
    EXPECT_STREQ(item.label, "Cancel");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 1);

    bool saw_keyboard_up = false;
    bool saw_keyboard_cancel = false;
    auto find_keyboard_up = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
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
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_keyboard_up, &keyboard_binding_labels));
    EXPECT_TRUE(saw_keyboard_up);
    EXPECT_TRUE(saw_keyboard_cancel);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.display"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.display");
    EXPECT_EQ(menu.item_count, 5);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Display Mode");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE);
    EXPECT_STREQ(item.control_target, "entity.settings");
    EXPECT_STREQ(item.control_key, "display_mode");
    EXPECT_EQ(item.choice_count, 3);
    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "windowed");

    bool saw_options_value = false;
    auto find_options_value = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *saw = static_cast<bool *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.display.menu" && value == "Display Mode: Windowed")
        {
            *saw = true;
            EXPECT_FLOAT_EQ(text->x, 0.3f);
            EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
            return false;
        }
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_options_value, &saw_options_value));
    EXPECT_TRUE(saw_options_value);

    ASSERT_TRUE(slayer3d_game_data_apply_menu_item_control(runtime, &item));
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "fullscreen_exclusive");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 3, &item));
    EXPECT_STREQ(item.label, "Reset Settings");
    EXPECT_TRUE(slayer3d_game_data_app_signal_applies_window_settings(runtime, item.signal_id));
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 4, &item));
    EXPECT_STREQ(item.scene, "scene.options");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.audio"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.audio");
    EXPECT_EQ(menu.item_count, 4);
    EXPECT_GE(menu.left_action_id, 0);
    EXPECT_GE(menu.right_action_id, 0);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Sound Effects");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_RANGE);
    EXPECT_STREQ(item.control_target, "entity.settings");
    EXPECT_STREQ(item.control_key, "sfx_volume");
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "music_volume", 0), 7);

    bool saw_sfx_slider = false;
    bool saw_music_slider = false;
    auto find_audio_sliders = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *flags = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.audio.menu" && value == "Sound Effects  [########--] 8/10")
        {
            *flags->first = true;
            EXPECT_FLOAT_EQ(text->x, 0.34f);
            EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
        }
        if (name == "ui.options.audio.menu" && value == "Music  [#######---] 7/10")
            *flags->second = true;
        if (*flags->first && *flags->second)
            return false;
        return true;
    };
    std::pair<bool *, bool *> audio_slider_flags{&saw_sfx_slider, &saw_music_slider};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_audio_sliders, &audio_slider_flags));
    EXPECT_TRUE(saw_sfx_slider);
    EXPECT_TRUE(saw_music_slider);

    ScenePayloadCapture payload_capture{};
    const int start_signal = slayer3d_game_data_find_signal(runtime, "signal.game.start");
    ASSERT_GE(start_signal, 0);
    ASSERT_NE(slayer3d_signal_connect(slayer3d_game_session_get_signal_bus(session), start_signal,
                                      capture_scene_payload, &payload_capture),
              0);

    slayer3d_properties *payload = slayer3d_properties_create();
    ASSERT_NE(payload, nullptr);
    slayer3d_properties_set_string(payload, "from_scene", "scene.options");
    slayer3d_properties_set_string(payload, "selected_level", "level.test");
    slayer3d_input_manager *play_input = slayer3d_game_session_get_input(session);
    ASSERT_NE(play_input, nullptr);
    const int remote_up_action = slayer3d_game_data_find_action(runtime, "action.paddle.local.up");
    ASSERT_GE(remote_up_action, 0);
    slayer3d_input_set_action_override(play_input, remote_up_action, 1.0f);
    ASSERT_NE(slayer3d_input_update(play_input, 10), nullptr);
    EXPECT_NEAR(slayer3d_input_get_value(play_input, remote_up_action), 1.0f, 0.0001f);
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options"));
    ASSERT_TRUE(slayer3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload));
    ASSERT_NE(slayer3d_input_update(play_input, 11), nullptr);
    EXPECT_NEAR(slayer3d_input_get_value(play_input, remote_up_action), 0.0f, 0.0001f);
    EXPECT_TRUE(payload_capture.called);
    EXPECT_EQ(payload_capture.from_scene, "scene.options");
    EXPECT_EQ(payload_capture.to_scene, "scene.play");
    EXPECT_EQ(payload_capture.selected_level, "level.test");
    slayer3d_properties_destroy(payload);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "match_mode", ""), "single");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "network_role", ""), "none");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "network_flow", ""), "none");

    slayer3d_properties *lan_payload = slayer3d_properties_create();
    ASSERT_NE(lan_payload, nullptr);
    slayer3d_properties_set_string(lan_payload, "match_mode", "lan");
    slayer3d_properties_set_string(lan_payload, "network_role", "client");
    slayer3d_properties_set_string(lan_payload, "network_flow", "direct");
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(slayer3d_game_data_set_active_scene_with_payload(runtime, "scene.play", lan_payload));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "match_mode", ""), "lan");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "network_role", ""), "client");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "network_flow", ""), "direct");
    slayer3d_properties_destroy(lan_payload);

    slayer3d_properties_set_string(scene_state, "selected_level", "level.002");
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "selected_level", ""),
                 "level.002");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ResolvesRuntimeUiStateForTextAndImages)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_game_data_ui_image logo{};
    bool saw_logo = false;
    auto find_logo = [](void *userdata, const slayer3d_game_data_ui_image *image) -> bool {
        auto *args = static_cast<std::pair<slayer3d_game_data_ui_image *, bool *> *>(userdata);
        if (image->name != nullptr && std::string(image->name) == "ui.splash.logo")
        {
            *args->first = *image;
            *args->second = true;
            return false;
        }
        return true;
    };
    std::pair<slayer3d_game_data_ui_image *, bool *> logo_args{&logo, &saw_logo};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_image(runtime, find_logo, &logo_args));
    ASSERT_TRUE(saw_logo);
    EXPECT_EQ(logo.effect, nullptr);
    EXPECT_NEAR(logo.effect_speed, 1.0f, 0.0001f);

    slayer3d_game_data_ui_state image_state{};
    slayer3d_game_data_ui_state_init(&image_state);
    image_state.flags = SLAYER3D_GAME_DATA_UI_STATE_OFFSET | SLAYER3D_GAME_DATA_UI_STATE_SCALE |
                        SLAYER3D_GAME_DATA_UI_STATE_ALPHA | SLAYER3D_GAME_DATA_UI_STATE_TINT;
    image_state.offset_x = 0.10f;
    image_state.offset_y = -0.05f;
    image_state.scale = 0.5f;
    image_state.alpha = 0.25f;
    image_state.tint = {128, 64, 255, 200};
    ASSERT_TRUE(slayer3d_game_data_set_ui_state(runtime, "ui.splash.logo", &image_state));

    slayer3d_game_data_ui_state stored_image_state{};
    ASSERT_TRUE(slayer3d_game_data_get_ui_state(runtime, "ui.splash.logo", &stored_image_state));
    EXPECT_EQ(stored_image_state.flags, image_state.flags);
    EXPECT_NEAR(stored_image_state.scale, 0.5f, 0.0001f);

    slayer3d_game_data_ui_image resolved_logo{};
    bool logo_visible = false;
    ASSERT_TRUE(slayer3d_game_data_resolve_ui_image(runtime, &logo, nullptr, &resolved_logo, &logo_visible));
    EXPECT_TRUE(logo_visible);
    EXPECT_NEAR(resolved_logo.x, 0.60f, 0.0001f);
    EXPECT_NEAR(resolved_logo.y, 0.45f, 0.0001f);
    EXPECT_NEAR(resolved_logo.scale, 0.5f, 0.0001f);
    EXPECT_EQ(resolved_logo.color.r, 128);
    EXPECT_EQ(resolved_logo.color.g, 64);
    EXPECT_EQ(resolved_logo.color.b, 255);
    EXPECT_EQ(resolved_logo.color.a, 50);
    EXPECT_EQ(resolved_logo.effect, nullptr);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.play"));

    slayer3d_game_data_ui_text pause{};
    bool saw_pause = false;
    auto find_pause = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *args = static_cast<std::pair<slayer3d_game_data_ui_text *, bool *> *>(userdata);
        if (text->name != nullptr && std::string(text->name) == "ui.pause")
        {
            *args->first = *text;
            *args->second = true;
            return false;
        }
        return true;
    };
    std::pair<slayer3d_game_data_ui_text *, bool *> pause_args{&pause, &saw_pause};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_pause, &pause_args));
    ASSERT_TRUE(saw_pause);

    slayer3d_game_data_ui_metrics metrics{};
    metrics.paused = false;
    EXPECT_FALSE(slayer3d_game_data_ui_text_is_visible(runtime, &pause, &metrics));

    slayer3d_game_data_ui_state text_state{};
    slayer3d_game_data_ui_state_init(&text_state);
    text_state.flags = SLAYER3D_GAME_DATA_UI_STATE_VISIBLE | SLAYER3D_GAME_DATA_UI_STATE_OFFSET |
                       SLAYER3D_GAME_DATA_UI_STATE_SCALE | SLAYER3D_GAME_DATA_UI_STATE_ALPHA |
                       SLAYER3D_GAME_DATA_UI_STATE_TINT;
    text_state.visible = true;
    text_state.offset_x = 0.02f;
    text_state.offset_y = -0.03f;
    text_state.scale = 2.0f;
    text_state.alpha = 0.5f;
    text_state.tint = {128, 255, 64, 128};
    ASSERT_TRUE(slayer3d_game_data_set_ui_state(runtime, "ui.pause", &text_state));

    slayer3d_game_data_ui_text resolved_pause{};
    bool pause_visible = false;
    ASSERT_TRUE(slayer3d_game_data_resolve_ui_text(runtime, &pause, &metrics, &resolved_pause, &pause_visible));
    EXPECT_TRUE(pause_visible);
    EXPECT_NEAR(resolved_pause.x, 0.52f, 0.0001f);
    EXPECT_NEAR(resolved_pause.y, 0.32f, 0.0001f);
    EXPECT_NEAR(resolved_pause.scale, 2.0f, 0.0001f);
    EXPECT_EQ(resolved_pause.color.r, 123);
    EXPECT_EQ(resolved_pause.color.g, 248);
    EXPECT_EQ(resolved_pause.color.b, 64);
    EXPECT_EQ(resolved_pause.color.a, 64);

    text_state.visible = false;
    ASSERT_TRUE(slayer3d_game_data_set_ui_state(runtime, "ui.pause", &text_state));
    EXPECT_FALSE(slayer3d_game_data_ui_text_is_visible(runtime, &pause, &metrics));

    EXPECT_TRUE(slayer3d_game_data_clear_ui_state(runtime, "ui.pause"));
    EXPECT_FALSE(slayer3d_game_data_get_ui_state(runtime, "ui.pause", &text_state));
    slayer3d_game_data_clear_ui_states(runtime);
    EXPECT_FALSE(slayer3d_game_data_get_ui_state(runtime, "ui.splash.logo", &image_state));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DataAuthoredInputPolicyUpdatePhasesAndPresentationClocks)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    const int pause = slayer3d_game_data_find_action(runtime, "action.pause");
    const int scene_play = slayer3d_game_data_find_action(runtime, "action.scene.play");
    ASSERT_GE(pause, 0);
    ASSERT_GE(scene_play, 0);
    EXPECT_FALSE(slayer3d_game_data_active_scene_allows_action(runtime, pause));
    EXPECT_TRUE(slayer3d_game_data_active_scene_allows_action(runtime, scene_play));

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.play"));
    EXPECT_TRUE(slayer3d_game_data_active_scene_allows_action(runtime, pause));
    EXPECT_TRUE(slayer3d_game_data_active_scene_allows_action(
        runtime, slayer3d_game_data_find_action(runtime, "action.paddle.local.up")));
    EXPECT_TRUE(slayer3d_game_data_active_scene_allows_action(
        runtime, slayer3d_game_data_find_action(runtime, "action.paddle.local.down")));
    EXPECT_TRUE(slayer3d_game_data_active_scene_update_phase(runtime, "presentation", true));
    EXPECT_FALSE(slayer3d_game_data_active_scene_update_phase(runtime, "simulation", true));

    slayer3d_registered_actor *presentation = slayer3d_game_data_find_actor(runtime, "entity.presentation");
    ASSERT_NE(presentation, nullptr);
    slayer3d_game_context ctx{};
    ctx.session = session;

    slayer3d_game_data_frame_state frame_state{};
    slayer3d_game_data_frame_state_init(&frame_state);
    slayer3d_game_data_update_frame_desc update{};
    update.ctx = &ctx;
    update.runtime = runtime;
    update.dt = 0.25f;
    ASSERT_TRUE(slayer3d_game_data_update_frame(&frame_state, &update));
    EXPECT_NEAR(frame_state.time, 0.25f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(presentation->props, "pause_flash", -1.0f), 0.0f, 0.0001f);

    ctx.paused = true;
    update.dt = 0.1f;
    ASSERT_TRUE(slayer3d_game_data_update_frame(&frame_state, &update));
    EXPECT_NEAR(slayer3d_properties_get_float(presentation->props, "pause_flash", -1.0f), 0.3f, 0.0001f);
    EXPECT_NEAR(slayer3d_game_data_ui_pulse_phase(runtime, -1.0f), 0.3f, 0.0001f);

    slayer3d_game_data_scene_transition_policy policy{};
    ASSERT_TRUE(slayer3d_game_data_get_scene_transition_policy(runtime, &policy));
    EXPECT_FALSE(policy.allow_same_scene);
    EXPECT_FALSE(policy.allow_interrupt);
    EXPECT_TRUE(policy.reset_menu_input_on_request);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppliesAuthoredPongPlayInputProfiles)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int p1_up = slayer3d_game_data_find_action(runtime, "action.paddle.up");
    const int p2_up = slayer3d_game_data_find_action(runtime, "action.paddle.local.up");
    ASSERT_GE(p1_up, 0);
    ASSERT_GE(p2_up, 0);

    ASSERT_TRUE(
        slayer3d_game_data_apply_input_profile(runtime, input, "profile.pong.play.single", error, sizeof(error)))
        << error;
    error[0] = '\0';
    EXPECT_FALSE(
        slayer3d_game_data_apply_input_profile(runtime, input, "profile.pong.play.missing", error, sizeof(error)));
    EXPECT_NE(std::string(error).find("profile.pong.play.missing"), std::string::npos);
    error[0] = '\0';

    const char *profile_name = nullptr;
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    ASSERT_STREQ(profile_name, "profile.pong.play.single");

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 0.0f);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "match_mode", "lan");
    slayer3d_properties_set_string(scene_state, "network_role", "client");
    slayer3d_properties_set_string(scene_state, "network_flow", "direct");
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    ASSERT_STREQ(profile_name, "profile.pong.play.lan.client");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 1.0f);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 4);

    slayer3d_properties_set_string(scene_state, "match_mode", "local");
    slayer3d_properties_set_string(scene_state, "network_role", "none");
    slayer3d_properties_set_string(scene_state, "network_flow", "none");
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    ASSERT_STREQ(profile_name, "profile.pong.play.local.keyboard_only");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 5);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 0.0f);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 6);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppliesAuthoredPongGamepadAssignmentPolicies)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    if (slayer3d_input_gamepad_count(input) != 0)
    {
        slayer3d_game_data_destroy(runtime);
        slayer3d_game_session_destroy(session);
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }

    const int p1_up = slayer3d_game_data_find_action(runtime, "action.paddle.up");
    const int p2_up = slayer3d_game_data_find_action(runtime, "action.paddle.local.up");
    ASSERT_GE(p1_up, 0);
    ASSERT_GE(p2_up, 0);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "match_mode", "local");
    slayer3d_properties_set_string(scene_state, "network_role", "none");
    slayer3d_properties_set_string(scene_state, "network_flow", "none");

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7101;
    slayer3d_input_process_event(input, &event);

    const char *profile_name = nullptr;
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    EXPECT_STREQ(profile_name, "profile.pong.play.local.one_gamepad");

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7101;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 1.0f);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 2);

    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7102;
    slayer3d_input_process_event(input, &event);
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    EXPECT_STREQ(profile_name, "profile.pong.play.local.two_gamepads");

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7101;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 3);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 1.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 0.0f);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 4);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 7102;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 5);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p1_up), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, p2_up), 1.0f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RefreshesActiveInputProfileWhenGamepadCountChanges)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    if (slayer3d_input_gamepad_count(input) != 0)
    {
        slayer3d_game_data_destroy(runtime);
        slayer3d_game_session_destroy(session);
        GTEST_SKIP() << "requires no pre-connected gamepads";
    }

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "match_mode", "local");
    slayer3d_properties_set_string(scene_state, "network_role", "none");
    slayer3d_properties_set_string(scene_state, "network_flow", "none");

    slayer3d_game_data_input_profile_refresh_state refresh{};
    slayer3d_game_data_input_profile_refresh_state_init(&refresh);

    const char *profile_name = nullptr;
    bool applied = false;
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                               &applied, error, sizeof(error)))
        << error;
    EXPECT_TRUE(applied);
    EXPECT_STREQ(profile_name, "profile.pong.play.local.keyboard_only");
    EXPECT_EQ(refresh.gamepad_count, 0);

    profile_name = "unchanged";
    applied = true;
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                               &applied, error, sizeof(error)))
        << error;
    EXPECT_FALSE(applied);
    EXPECT_EQ(profile_name, nullptr);

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7201;
    slayer3d_input_process_event(input, &event);

    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                               &applied, error, sizeof(error)))
        << error;
    EXPECT_TRUE(applied);
    EXPECT_STREQ(profile_name, "profile.pong.play.local.one_gamepad");
    EXPECT_EQ(refresh.gamepad_count, 1);

    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 7202;
    slayer3d_input_process_event(input, &event);

    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile_on_device_change(runtime, input, &refresh, &profile_name,
                                                                               &applied, error, sizeof(error)))
        << error;
    EXPECT_TRUE(applied);
    EXPECT_STREQ(profile_name, "profile.pong.play.local.two_gamepads");
    EXPECT_EQ(refresh.gamepad_count, 2);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppliesFallbackAndMouseInputProfiles)
{
    const std::filesystem::path dir = unique_test_dir("input_profile_mouse");
    write_text(dir / "mouse_profile.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "mouse_profile.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int action = slayer3d_game_data_find_action(runtime, "action.pointer.primary");
    ASSERT_GE(action, 0);

    const char *profile_name = nullptr;
    ASSERT_TRUE(slayer3d_game_data_apply_active_input_profile(runtime, input, &profile_name, error, sizeof(error)))
        << error;
    EXPECT_STREQ(profile_name, "profile.fallback.mouse");

    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_LEFT;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 1);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, action), 1.0f);

    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 2);
    EXPECT_FLOAT_EQ(slayer3d_input_get_value(input, action), 0.0f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, MenuControllerConsumesAuthoredMenuInput)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    bool armed = false;
    slayer3d_game_data_menu_update_result result{};
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(armed);
    EXPECT_FALSE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_STREQ(result.menu, "menu.title");
    EXPECT_EQ(result.selected_index, 0);
    EXPECT_EQ(result.move_signal_id, -1);
    EXPECT_EQ(result.select_signal_id, -1);

    const int menu_move_signal = slayer3d_game_data_find_signal(runtime, "signal.ui.menu.move");
    const int menu_select_signal = slayer3d_game_data_find_signal(runtime, "signal.ui.menu.select");
    ASSERT_GE(menu_move_signal, 0);
    ASSERT_GE(menu_select_signal, 0);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_DOWN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_STREQ(result.menu, "menu.title");
    EXPECT_EQ(result.selected_index, 1);
    EXPECT_EQ(result.move_signal_id, menu_move_signal);
    EXPECT_EQ(result.select_signal_id, -1);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);

    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_FALSE(result.quit);
    EXPECT_STREQ(result.menu, "menu.title");
    EXPECT_EQ(result.selected_index, 1);
    EXPECT_STREQ(result.scene, "scene.multiplayer");
    EXPECT_EQ(result.signal_id, -1);
    EXPECT_EQ(result.move_signal_id, -1);
    EXPECT_EQ(result.select_signal_id, menu_select_signal);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, MenuTextEntryCapturesEditingInput)
{
    const std::filesystem::path dir = unique_test_dir("menu_text_entry");
    write_text(dir / "text_entry.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "text_entry.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    ASSERT_STREQ(menu.name, "menu.form");
    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT);

    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 1);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_capture_started);
    EXPECT_TRUE(slayer3d_game_data_menu_text_entry_capture_active(runtime));

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "host local@1"
                      "\xC3"
                      "\xA9"
                      "\xF0"
                      "\x9F"
                      "\x98"
                      "\x80";
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_changed);
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "host", ""), "hostlocal1");

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DOWN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 4);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.selected_index, 0);

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 5);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_BACKSPACE;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 6);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "host", ""), "hostlocal");

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 7);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DELETE;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 8);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "host", ""), "hostloca");

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 9);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 10);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_canceled);
    EXPECT_FALSE(slayer3d_game_data_menu_text_entry_capture_active(runtime));
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "host", ""), "");

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 11);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 12);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_capture_started);

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 13);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "192.168.1.20:27183";
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 14);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 15);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.text_entry_submitted);
    EXPECT_FALSE(slayer3d_game_data_menu_text_entry_capture_active(runtime));
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "host", ""),
                 "192.168.1.20:27183");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidMenuTextControlSchema)
{
    const std::filesystem::path dir = unique_test_dir("menu_text_entry_validation");
    const auto write_case = [&](const char *name, const char *control_json) {
        write_text(dir / (std::string(name) + ".game.json"),
                   R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
        slayer3d_game_data_validate_file((dir / "alias.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("menu item control requires type"), std::string::npos) << error;

    error[0] = '\0';
    write_case("too_long", R"json({ "type": "text", "target": "scene_state", "key": "host", "max_length": 256 })json");
    EXPECT_FALSE(
        slayer3d_game_data_validate_file((dir / "too_long.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("max_length must be 255 bytes or fewer"), std::string::npos) << error;

    remove_test_dir(dir);
}

TEST(GameDataRuntime, DynamicListMenuUsesIndexedSceneStateEntries)
{
    const std::filesystem::path dir = unique_test_dir("menu_dynamic_list");
    write_text(dir / "dynamic_list.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "dynamic_list.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.sessions");
    EXPECT_EQ(menu.item_count, 2);

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_EQ(item.dynamic_list_index, -1);
    EXPECT_STREQ(item.label, "No sessions discovered");
    EXPECT_EQ(item.signal_id, -1);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_int(scene_state, "session_count", 4);
    slayer3d_properties_set_string(scene_state, "session_0_label", "Alpha");
    slayer3d_properties_set_string(scene_state, "session_0_value", "10.0.0.1:27183");
    slayer3d_properties_set_string(scene_state, "session_1_label", "Beta");
    slayer3d_properties_set_string(scene_state, "session_1_value", "10.0.0.2:27183");
    slayer3d_properties_set_string(scene_state, "session_2_label", "Gamma");
    slayer3d_properties_set_string(scene_state, "session_2_value", "10.0.0.3:27183");
    slayer3d_properties_set_string(scene_state, "session_3_label", "Delta");
    slayer3d_properties_set_string(scene_state, "session_3_value", "10.0.0.4:27183");

    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 5);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_STREQ(item.dynamic_list_name, "list.sessions");
    EXPECT_EQ(item.dynamic_list_index, 1);
    EXPECT_STREQ(item.dynamic_list_value, "10.0.0.2:27183");
    EXPECT_STREQ(item.label, "Join Beta");
    slayer3d_game_data_menu_item first_dynamic_item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &first_dynamic_item));
    slayer3d_game_data_menu_item second_dynamic_item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &second_dynamic_item));
    EXPECT_STREQ(first_dynamic_item.label, "Join Alpha");
    EXPECT_STREQ(second_dynamic_item.label, "Join Beta");
    EXPECT_STREQ(first_dynamic_item.dynamic_list_value, "10.0.0.1:27183");
    EXPECT_STREQ(second_dynamic_item.dynamic_list_value, "10.0.0.2:27183");

    struct MenuLabels
    {
        std::vector<std::string> labels;
    } labels;
    auto collect_menu_labels = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *capture = static_cast<MenuLabels *>(userdata);
        if (std::string(text->name) == "ui.sessions")
            capture->labels.emplace_back(text->text);
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, collect_menu_labels, &labels));
    ASSERT_EQ(labels.labels.size(), 2U);
    EXPECT_EQ(labels.labels[0], "Join Alpha");
    EXPECT_EQ(labels.labels[1], "Join Beta");

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DOWN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 1);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_EQ(result.selected_index, 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "selected_session_index", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "selected_session_live", ""), "10.0.0.2:27183");

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_EQ(result.signal_id, slayer3d_game_data_find_signal(runtime, "signal.session.join"));
    EXPECT_STREQ(result.scene_state_key, "selected_session");
    EXPECT_STREQ(result.scene_state_value, "10.0.0.2:27183");

    slayer3d_properties_set_int(scene_state, "session_count", 1);
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 2);
    EXPECT_EQ(menu.selected_index, 1);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, DynamicListMenuReadsRuntimeCollections)
{
    const std::filesystem::path dir = unique_test_dir("menu_dynamic_runtime_collection");
    write_text(dir / "dynamic_collection.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "dynamic_collection.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    EXPECT_FALSE(slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 2, "name", "Sparse"));
    EXPECT_EQ(slayer3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);

    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 2);

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_TRUE(item.dynamic_list_item);
    EXPECT_EQ(item.dynamic_list_index, -1);
    EXPECT_STREQ(item.label, "No runtime rows");

    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "name", "Alpha"));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_int(runtime, "local_matches", 0, "latency_ms", 42));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 1, "name", "Beta"));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_float(runtime, "local_matches", 1, "latency_ms", 19.5f));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_bool(runtime, "local_matches", 1, "secure", true));
    EXPECT_EQ(slayer3d_game_data_runtime_collection_count(runtime, "local_matches"), 2);

    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 3);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Alpha");
    EXPECT_STREQ(item.dynamic_list_value, "42");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Beta");
    EXPECT_STREQ(item.dynamic_list_value, "19.500");

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_DOWN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 1);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_EQ(result.selected_index, 1);
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "selected_runtime_index", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "selected_runtime_latency", ""), "19.500");

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.selected);
    EXPECT_STREQ(result.scene_state_key, "selected_runtime_latency_on_accept");
    EXPECT_STREQ(result.scene_state_value, "19.500");

    EXPECT_TRUE(slayer3d_game_data_runtime_collection_clear(runtime, "local_matches"));
    EXPECT_EQ(slayer3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.item_count, 2);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "No runtime rows");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, UiToolPanelsAndInspectorsEmitReusableOverlayPrimitives)
{
    const std::filesystem::path dir = unique_test_dir("ui_tooling");
    write_text(dir / "ui_tooling.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "UI Tooling", "id": "test.ui_tooling", "version": "0.1.0" },
  "world": { "name": "world.ui_tooling", "kind": "fixed_screen" },
  "assets": { "fonts": [{ "id": "font.tool", "builtin": "Inter", "size": 16 }] },
  "entities": [
    {
      "name": "entity.selection",
      "properties": {
        "health": { "type": "int", "value": 100 }
      }
    }
  ],
  "scenes": { "initial": "scene.editor", "files": ["scenes/editor.scene.json"] }
})json");
    write_text(dir / "scenes" / "editor.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.editor",
  "entities": ["entity.selection"],
  "ui": {
    "panels": [
      {
        "name": "ui.editor.sidebar",
        "x": 12,
        "y": 12,
        "w": 260,
        "h": 160,
        "color": [20, 28, 40, 220],
        "border_color": [90, 130, 210, 255],
        "border_thickness": 2
      }
    ],
    "inspectors": [
      {
        "name": "ui.editor.inspector",
        "font": "font.tool",
        "x": 24,
        "y": 24,
        "w": 232,
        "row_height": 22,
        "padding": 8,
        "title": "Selection",
        "background_color": [8, 10, 16, 180],
        "row_color": [255, 255, 255, 18],
        "rows": [
          {
            "label": "World",
            "binding": { "type": "scene_state", "key": "editor.world", "default": "none" }
          },
          {
            "label": "Health",
            "binding": { "type": "property", "entity": "entity.selection", "key": "health" }
          },
          {
            "label": "FPS",
            "binding": { "type": "metric", "metric": "fps", "default": 0 }
          }
        ]
      }
    ]
  }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "ui_tooling.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "editor.world", "brush.dojo");

    struct Rects
    {
        int sidebar_rects = 0;
        int inspector_rects = 0;
    } rects;
    auto capture_tool_rects = [](void *userdata, const slayer3d_game_data_ui_rect *rect) -> bool {
        auto *capture = static_cast<Rects *>(userdata);
        if (std::string(rect->name) == "ui.editor.sidebar")
            capture->sidebar_rects++;
        if (std::string(rect->name) == "ui.editor.inspector")
            capture->inspector_rects++;
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_rect(runtime, capture_tool_rects, &rects));
    EXPECT_EQ(rects.sidebar_rects, 5);
    EXPECT_EQ(rects.inspector_rects, 4);

    slayer3d_game_data_ui_metrics metrics{};
    metrics.fps = 59.75f;
    struct Texts
    {
        std::vector<std::string> values;
    } texts;
    auto capture_tool_texts = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *capture = static_cast<Texts *>(userdata);
        if (std::string(text->name) == "ui.editor.inspector")
            capture->values.emplace_back(text->text != nullptr ? text->text : "");
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text_for_metrics(runtime, &metrics, capture_tool_texts, &texts));
    ASSERT_EQ(texts.values.size(), 7U);
    EXPECT_EQ(texts.values[0], "Selection");
    EXPECT_EQ(texts.values[1], "World");
    EXPECT_EQ(texts.values[2], "brush.dojo");
    EXPECT_EQ(texts.values[3], "Health");
    EXPECT_EQ(texts.values[4], "100");
    EXPECT_EQ(texts.values[5], "FPS");
    EXPECT_EQ(texts.values[6], "59.8");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataValidation, RejectsInvalidUiTooling)
{
    const std::filesystem::path dir = unique_test_dir("ui_tooling_validation");
    write_text(dir / "bad_ui_tooling.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad UI Tooling", "id": "test.bad_ui_tooling", "version": "0.1.0" },
  "world": { "name": "world.bad_ui_tooling", "kind": "fixed_screen" },
  "assets": { "fonts": [{ "id": "font.tool", "builtin": "Inter", "size": 16 }] },
  "entities": [],
  "scenes": { "initial": "scene.editor", "files": ["scenes/editor.scene.json"] }
})json");
    write_text(dir / "scenes" / "editor.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.editor",
  "ui": {
    "inspectors": [
      {
        "name": "ui.editor.inspector",
        "font": "font.tool",
        "rows": [
          {
            "label": "Broken",
            "binding": { "type": "property", "entity": "entity.missing", "key": "health" }
          }
        ]
      }
    ]
  }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "bad_ui_tooling.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.scenes.resolved[0].ui.inspectors[0].rows[0].binding"), std::string::npos)
        << error;
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidDynamicListMenuSchema)
{
    const std::filesystem::path dir = unique_test_dir("menu_dynamic_list_validation");
    const auto write_case = [&](const char *name, const char *item_json) {
        write_text(dir / (std::string(name) + ".game.json"),
                   R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "missing_source.game.json").string().c_str(), nullptr, error,
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_value_from.game.json").string().c_str(), nullptr, error,
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_label_format.game.json").string().c_str(), nullptr, error,
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_value_format.game.json").string().c_str(), nullptr, error,
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "missing_runtime_collection.game.json").string().c_str(),
                                                  nullptr, error, sizeof(error)));
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "missing_runtime_label_field.game.json").string().c_str(),
                                                  nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("runtime_collection source requires a non-empty label_field"), std::string::npos)
        << error;

    remove_test_dir(dir);
}

TEST(GameDataRuntime, PlaySceneMenusAreSelectedByAuthoredConditions)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.play"));

    slayer3d_game_data_ui_metrics metrics{};
    slayer3d_game_data_menu menu{};
    EXPECT_FALSE(slayer3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));

    metrics.paused = true;
    ASSERT_TRUE(slayer3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    EXPECT_STREQ(menu.name, "menu.pause");
    EXPECT_EQ(menu.item_count, 3);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "match_mode", "lan");
    slayer3d_properties_set_string(scene_state, "network_role", "host");
    ASSERT_TRUE(slayer3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    EXPECT_STREQ(menu.name, "menu.pause.network");
    EXPECT_EQ(menu.item_count, 2);

    slayer3d_properties_set_bool(scene_state, "network_match_termination_active", true);
    EXPECT_FALSE(slayer3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    slayer3d_properties_set_bool(scene_state, "network_match_termination_active", false);

    slayer3d_registered_actor *match = slayer3d_game_data_find_actor(runtime, "entity.match");
    ASSERT_NE(match, nullptr);
    slayer3d_properties_set_bool(match->props, "finished", true);
    metrics.paused = false;
    ASSERT_TRUE(slayer3d_game_data_get_active_menu_for_metrics(runtime, &metrics, &menu));
    EXPECT_STREQ(menu.name, "menu.match_over");
    EXPECT_EQ(menu.item_count, 2);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PauseMenuResumeConsumesSharedEnterWithoutRepausing)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_context ctx{};
    ctx.session = session;
    ctx.paused = true;

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.play"));

    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));
    flow.scene_input_armed = true;

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, OptionsMenuCanReturnToAuthoredScene)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options"));

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "return_scene", "scene.play");
    slayer3d_properties_set_bool(scene_state, "return_paused", true);

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, "menu.options", 5, &item));
    EXPECT_TRUE(item.return_scene);
    EXPECT_STREQ(item.scene, "scene.title");

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    bool armed = true;
    slayer3d_game_data_ui_metrics metrics{};
    slayer3d_game_data_menu_update_result result{};

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);
    ASSERT_TRUE(slayer3d_game_data_update_menus_for_metrics(runtime, input, &armed, &metrics, &result));
    EXPECT_EQ(result.selected_index, 5);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus_for_metrics(runtime, input, &armed, &metrics, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_menus_for_metrics(runtime, input, &armed, &metrics, &result));
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.return_scene);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "return_scene", ""), "scene.play");
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "return_paused", false));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, OptionsSubmenusDoNotOverwriteCallerReturnScene)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "return_scene", "scene.title");
    slayer3d_properties_set_bool(scene_state, "return_paused", false);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options"));
    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, "menu.options", 0, &item));
    EXPECT_STREQ(item.label, "Display");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "display");
    EXPECT_EQ(item.return_to, nullptr);

    slayer3d_properties_set_string(scene_state, "options_menu", "display");
    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.display");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, "menu.options.display", 4, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_EQ(item.scene, nullptr);
    EXPECT_STREQ(item.scene_state_key, "options_menu");
    EXPECT_STREQ(item.scene_state_value, "root");
    EXPECT_FALSE(item.return_scene);

    slayer3d_properties_set_string(scene_state, "options_menu", "root");
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, "menu.options", 5, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_TRUE(item.return_scene);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "return_scene", ""), "scene.title");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "return_paused", true));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DisplayOptionControlsApplyImmediately)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.display"));

    const int menu_select_signal = slayer3d_game_data_find_signal(runtime, "signal.ui.menu.select");
    ASSERT_GE(menu_select_signal, 0);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.control_changed);
    EXPECT_EQ(result.select_signal_id, menu_select_signal);
    EXPECT_EQ(result.signal_id, slayer3d_game_data_find_signal(runtime, "signal.settings.apply"));
    EXPECT_TRUE(slayer3d_game_data_app_signal_applies_window_settings(runtime, result.signal_id));
    EXPECT_EQ(result.scene, nullptr);

    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "fullscreen_exclusive");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AudioOptionSlidersApplyImmediatelyWithLeftRightInput)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.audio"));

    const int menu_select_signal = slayer3d_game_data_find_signal(runtime, "signal.ui.menu.select");
    const int apply_audio_signal = slayer3d_game_data_find_signal(runtime, "signal.settings.apply_audio");
    ASSERT_GE(menu_select_signal, 0);
    ASSERT_GE(apply_audio_signal, 0);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 8);

    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_LEFT;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.control_changed);
    EXPECT_EQ(result.select_signal_id, menu_select_signal);
    EXPECT_EQ(result.signal_id, apply_audio_signal);
    EXPECT_EQ(result.scene, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 7);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RIGHT;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);

    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.control_changed);
    EXPECT_EQ(result.signal_id, apply_audio_signal);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 8);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 4);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    ASSERT_TRUE(slayer3d_game_data_menu_move(runtime, "menu.options.audio", 3));
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_LEFT;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 5);

    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_FALSE(result.selected);
    EXPECT_FALSE(result.control_changed);
    EXPECT_EQ(result.select_signal_id, -1);
    EXPECT_EQ(result.signal_id, -1);
    EXPECT_EQ(result.scene, nullptr);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, KeyboardOptionsCaptureAndApplyAuthoredInputBindings)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int paddle_up = slayer3d_game_data_find_action(runtime, "action.paddle.up");
    ASSERT_GE(paddle_up, 0);
    const int menu_up = slayer3d_game_data_find_action(runtime, "action.menu.up");
    ASSERT_GE(menu_up, 0);
    const int exit_action = slayer3d_game_data_find_action(runtime, "action.exit");
    ASSERT_GE(exit_action, 0);

    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.selected);
    EXPECT_TRUE(result.input_binding_capture_started);
    EXPECT_TRUE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_I;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.input_binding_changed);
    EXPECT_FALSE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 4);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_I;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 5);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(slayer3d_input_is_pressed(input, menu_up));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 6);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    ASSERT_TRUE(slayer3d_game_data_menu_move(runtime, "menu.options.keyboard", 1));
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 7);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 8);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_I;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 9);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.handled_input);
    EXPECT_TRUE(result.input_binding_conflict);
    EXPECT_TRUE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 10);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_ESCAPE;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 11);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_FALSE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    const int reset_keyboard = slayer3d_game_data_find_signal(runtime, "signal.settings.reset_keyboard");
    ASSERT_GE(reset_keyboard, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), reset_keyboard, nullptr);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 12);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 13);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(slayer3d_input_is_pressed(input, menu_up));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 14);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_BACKSPACE;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 15);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, exit_action));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, GamepadOptionsCaptureAndApplyAuthoredInputBindings)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.gamepad"));

    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.gamepad");
    EXPECT_EQ(menu.item_count, 12);

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Button Icons");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Up");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 2);
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 10, &item));
    EXPECT_STREQ(item.label, "Reset Settings");

    bool saw_button_icons = false;
    bool saw_ball_camera = false;
    auto find_gamepad_labels = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *flags = static_cast<std::pair<bool *, bool *> *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.gamepad.menu" && value == "Button Icons: Xbox")
        {
            *flags->first = true;
            EXPECT_FLOAT_EQ(text->x, 0.34f);
            EXPECT_FLOAT_EQ(text->y, 0.24f);
            EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
        }
        if (name == "ui.options.gamepad.menu" && value.rfind("Ball Camera:", 0) == 0)
        {
            *flags->second = true;
            EXPECT_FLOAT_EQ(text->x, 0.34f);
            EXPECT_NEAR(text->y, 0.735f, 0.0001f);
            EXPECT_EQ(text->align, SLAYER3D_GAME_DATA_UI_ALIGN_LEFT);
        }
        return !*flags->first || !*flags->second;
    };
    std::pair<bool *, bool *> gamepad_label_flags{&saw_button_icons, &saw_ball_camera};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_gamepad_labels, &gamepad_label_flags));
    EXPECT_TRUE(saw_button_icons);
    EXPECT_TRUE(saw_ball_camera);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int paddle_up = slayer3d_game_data_find_action(runtime, "action.paddle.up");
    ASSERT_GE(paddle_up, 0);
    const int menu_up = slayer3d_game_data_find_action(runtime, "action.menu.up");
    ASSERT_GE(menu_up, 0);
    const int exit_action = slayer3d_game_data_find_action(runtime, "action.exit");
    ASSERT_GE(exit_action, 0);

    ASSERT_TRUE(slayer3d_game_data_menu_move(runtime, "menu.options.gamepad", 2));
    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 1);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);
    EXPECT_TRUE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_changed);
    EXPECT_FALSE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 4);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 5);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(slayer3d_input_is_pressed(input, menu_up));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 6);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    ASSERT_TRUE(slayer3d_game_data_menu_move(runtime, "menu.options.gamepad", 1));
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 7);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 8);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 9);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_conflict);
    EXPECT_TRUE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 10);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 11);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_FALSE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    const int reset_gamepad = slayer3d_game_data_find_signal(runtime, "signal.settings.reset_gamepad");
    ASSERT_GE(reset_gamepad, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), reset_gamepad, nullptr);

    event.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 12);
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 13);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, paddle_up));
    EXPECT_TRUE(slayer3d_input_is_pressed(input, menu_up));

    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 14);
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_BACK;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 15);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, exit_action));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, OptionsMenusUseGamepadAxesAndBack)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.display"));

    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_GE(menu.back_action_id, 0);

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 2041;
    slayer3d_input_process_event(input, &event);

    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = 2041;
    event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    event.gaxis.value = -30000;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 1);

    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.control_changed);
    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "fullscreen_borderless");

    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = 2041;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    slayer3d_input_process_event(input, &event);
    slayer3d_input_update(input, 2);

    SDL_zero(result);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_FALSE(result.return_scene);
    EXPECT_STREQ(result.scene, "scene.options");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, MouseOptionsCaptureAndApplyAuthoredInputBindings)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.mouse"));

    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.mouse");
    EXPECT_EQ(menu.item_count, 3);

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Accept");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 1);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int menu_select = slayer3d_game_data_find_action(runtime, "action.menu.select");
    ASSERT_GE(menu_select, 0);

    bool armed = true;
    slayer3d_game_data_menu_update_result result{};
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_capture_started);
    EXPECT_TRUE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_MIDDLE;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));
    EXPECT_TRUE(result.input_binding_changed);
    EXPECT_FALSE(slayer3d_game_data_menu_input_binding_capture_active(runtime));

    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 4);
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &armed, &result));

    bool saw_mouse_label = false;
    auto find_mouse_accept = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
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
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_mouse_accept, &saw_mouse_label));
    EXPECT_TRUE(saw_mouse_label);

    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_LEFT;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 5);
    EXPECT_FALSE(slayer3d_input_is_pressed(input, menu_select));
    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 6);

    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_MIDDLE;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 7);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, menu_select));

    const int reset_mouse = slayer3d_game_data_find_signal(runtime, "signal.settings.reset_mouse");
    ASSERT_GE(reset_mouse, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), reset_mouse, nullptr);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 8);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_LEFT;
    slayer3d_input_process_event(input, &mouse);
    slayer3d_input_update(input, 9);
    EXPECT_TRUE(slayer3d_input_is_pressed(input, menu_select));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AuthoredSettingsResetRestoresSelectedDefaults)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    slayer3d_properties_set_string(settings->props, "display_mode", "fullscreen_exclusive");
    slayer3d_properties_set_bool(settings->props, "vsync", false);
    slayer3d_properties_set_string(settings->props, "renderer", "software");
    slayer3d_properties_set_int(settings->props, "sfx_volume", 2);
    slayer3d_properties_set_int(settings->props, "music_volume", 3);

    const int reset_display = slayer3d_game_data_find_signal(runtime, "signal.settings.reset_display");
    ASSERT_GE(reset_display, 0);
    EXPECT_TRUE(slayer3d_game_data_app_signal_applies_window_settings(runtime, reset_display));
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), reset_display, nullptr);

    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
    EXPECT_TRUE(slayer3d_properties_get_bool(settings->props, "vsync", false));
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "renderer", ""), "opengl");

    const int reset_audio = slayer3d_game_data_find_signal(runtime, "signal.settings.reset_audio");
    ASSERT_GE(reset_audio, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), reset_audio, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "music_volume", 0), 7);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongStandardOptionsUseImmediateApplyContract)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
    EXPECT_TRUE(slayer3d_properties_get_bool(settings->props, "vsync", false));
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "renderer", ""), "opengl");
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "music_volume", 0), 7);
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "gamepad_icons", ""), "xbox");
    EXPECT_TRUE(slayer3d_properties_get_bool(settings->props, "vibration", false));

    EXPECT_LT(slayer3d_game_data_find_signal(runtime, "signal.settings.snapshot_display"), 0);
    EXPECT_LT(slayer3d_game_data_find_signal(runtime, "signal.settings.snapshot_audio"), 0);
    EXPECT_LT(slayer3d_game_data_find_signal(runtime, "signal.settings.snapshot_gamepad"), 0);
    EXPECT_LT(slayer3d_game_data_find_signal(runtime, "signal.settings.cancel_display"), 0);
    EXPECT_LT(slayer3d_game_data_find_signal(runtime, "signal.settings.cancel_audio"), 0);
    EXPECT_LT(slayer3d_game_data_find_signal(runtime, "signal.settings.cancel_gamepad"), 0);
    EXPECT_GE(slayer3d_game_data_find_signal(runtime, "signal.settings.vibration"), 0);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.display"));
    slayer3d_game_data_menu_item display_mode{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, "menu.options.display", 0, &display_mode));
    EXPECT_EQ(display_mode.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE);
    EXPECT_STREQ(display_mode.control_target, "entity.settings");
    EXPECT_STREQ(display_mode.control_key, "display_mode");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));
    slayer3d_game_data_menu_item up_binding{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, "menu.options.keyboard", 0, &up_binding));
    EXPECT_EQ(up_binding.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(up_binding.input_binding_count, 2);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppFlowConsumesAuthoredLifecycleAndSceneShortcutControls)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_game_context ctx{};
    ctx.session = session;

    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.8f));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_FALSE(ctx.quit_requested);
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.splash");

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_F9;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.splash");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_3;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.play");
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 4);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 5);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 6);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 7);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    slayer3d_registered_actor *match = slayer3d_game_data_find_actor(runtime, "entity.match");
    ASSERT_NE(match, nullptr);
    slayer3d_properties_set_bool(match->props, "finished", true);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 8);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 9);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 10);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_BACKSPACE;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 11);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_quit_pending(&flow));
    EXPECT_FALSE(ctx.quit_requested);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.5f));
    EXPECT_TRUE(ctx.quit_requested);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, SceneFlowRunsAuthoredExitAndEnterTransitions)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));

    slayer3d_game_data_scene_flow flow{};
    slayer3d_game_data_scene_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_scene_flow_request(&flow, runtime, "scene.play"));
    EXPECT_TRUE(slayer3d_game_data_scene_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");
    EXPECT_FALSE(slayer3d_game_data_scene_flow_request(&flow, runtime, "scene.options"));

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    slayer3d_game_data_scene_flow_update(&flow, runtime, bus, 0.29f);
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.play");
    EXPECT_TRUE(slayer3d_game_data_scene_flow_is_transitioning(&flow));

    slayer3d_game_data_scene_flow_update(&flow, runtime, bus, 0.29f);
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.play");
    EXPECT_FALSE(slayer3d_game_data_scene_flow_is_transitioning(&flow));
    EXPECT_FALSE(slayer3d_game_data_scene_flow_request(&flow, runtime, "scene.play"));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AppFlowRunsAuthoredSceneTimelineActions)
{
    const std::filesystem::path dir = unique_test_dir("timeline");
    write_timeline_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "timeline.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    const int timeline_signal = slayer3d_game_data_find_signal(runtime, "signal.timeline");
    ASSERT_GE(timeline_signal, 0);
    SignalCapture signal_capture{};
    ASSERT_NE(slayer3d_signal_connect(slayer3d_game_session_get_signal_bus(session), timeline_signal, count_signal,
                                      &signal_capture),
              0);

    slayer3d_registered_actor *flag = slayer3d_game_data_find_actor(runtime, "entity.flag");
    ASSERT_NE(flag, nullptr);
    EXPECT_FALSE(slayer3d_properties_get_bool(flag->props, "ready", true));

    slayer3d_game_context ctx{};
    ctx.session = session;
    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.05f));
    EXPECT_FALSE(slayer3d_properties_get_bool(flag->props, "ready", true));
    EXPECT_EQ(signal_capture.calls, 0);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.06f));
    EXPECT_TRUE(slayer3d_properties_get_bool(flag->props, "ready", false));
    EXPECT_EQ(signal_capture.calls, 0);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.09f));
    EXPECT_EQ(signal_capture.calls, 1);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.10f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_EQ(signal_capture.calls, 1);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, AppFlowAppliesAuthoredSkipPolicyWithoutMenuBleedThrough)
{
    const std::filesystem::path dir = unique_test_dir("skip_policy");
    write_skip_policy_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "skip.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_game_context ctx{};
    ctx.session = session;
    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.intro");

    slayer3d_game_data_skip_policy policy{};
    ASSERT_TRUE(slayer3d_game_data_get_active_skip_policy(runtime, &policy));
    EXPECT_EQ(policy.input, SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION);
    EXPECT_STREQ(policy.action, "action.skip");
    EXPECT_STREQ(policy.scene, "scene.title");
    EXPECT_TRUE(policy.preserve_exit_transition);
    EXPECT_TRUE(policy.block_menus);
    EXPECT_TRUE(policy.block_scene_shortcuts);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    slayer3d_input_update(input, 0);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.intro");

    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 4);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 5);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.11f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.play");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, TimelinePolicyCanBlockMenusAndSceneShortcuts)
{
    const std::filesystem::path dir = unique_test_dir("timeline_blocks_input");
    write_scene_flow_policy_json(dir, true, true);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "flow_policy.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_game_data_timeline_policy policy{};
    ASSERT_TRUE(slayer3d_game_data_get_active_timeline_policy(runtime, &policy));
    EXPECT_TRUE(policy.block_menus);
    EXPECT_TRUE(policy.block_scene_shortcuts);

    slayer3d_game_context ctx{};
    ctx.session = session;
    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    slayer3d_input_update(input, 0);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    key.key.scancode = SDL_SCANCODE_3;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 1.0f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.intro");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, TimelinePolicyCanAllowInteractiveIntroMenus)
{
    const std::filesystem::path dir = unique_test_dir("timeline_allows_input");
    write_scene_flow_policy_json(dir, false, false);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "flow_policy.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_game_data_timeline_policy policy{};
    ASSERT_TRUE(slayer3d_game_data_get_active_timeline_policy(runtime, &policy));
    EXPECT_FALSE(policy.block_menus);
    EXPECT_FALSE(policy.block_scene_shortcuts);

    slayer3d_game_context ctx{};
    ctx.session = session;
    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    slayer3d_input_update(input, 0);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(flow.scene_input_armed);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    bool menu_armed = true;
    slayer3d_game_data_menu_update_result menu_result{};
    ASSERT_TRUE(slayer3d_game_data_update_menus(runtime, input, &menu_armed, &menu_result));
    EXPECT_TRUE(menu_result.selected);
    EXPECT_STREQ(menu_result.scene, "scene.play");

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.play");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, SceneActivityDrivesIdleWakeAndPeriodicActions)
{
    const std::filesystem::path dir = unique_test_dir("scene_activity");
    write_scene_activity_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "activity.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_registered_actor *state = slayer3d_game_data_find_actor(runtime, "entity.state");
    ASSERT_NE(state, nullptr);
    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.overhead");
    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 0.0f));
    EXPECT_TRUE(slayer3d_properties_get_bool(state->props, "entered", false));
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.close");

    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 0.75f));
    EXPECT_FALSE(slayer3d_properties_get_bool(state->props, "idle", false));
    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 0.30f));
    EXPECT_TRUE(slayer3d_properties_get_bool(state->props, "idle", false));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_SPACE;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);
    bool block_menus = false;
    bool block_shortcuts = false;
    EXPECT_TRUE(slayer3d_game_data_scene_activity_consumes_wake_input(runtime, input, &block_menus, &block_shortcuts));
    EXPECT_TRUE(block_menus);
    EXPECT_TRUE(block_shortcuts);
    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 0.0f));
    EXPECT_FALSE(slayer3d_properties_get_bool(state->props, "idle", true));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 2.1f));
    EXPECT_EQ(slayer3d_properties_get_int(state->props, "periodic", 0), 1);
    EXPECT_FALSE(slayer3d_properties_get_bool(state->props, "idle", true));

    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 1.1f));
    EXPECT_TRUE(slayer3d_properties_get_bool(state->props, "idle", false));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, AppFlowConsumesActivityWakeInputBeforeMenus)
{
    const std::filesystem::path dir = unique_test_dir("scene_activity_app_flow");
    write_scene_activity_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "activity.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_game_context ctx{};
    ctx.session = session;
    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    slayer3d_registered_actor *state = slayer3d_game_data_find_actor(runtime, "entity.state");
    ASSERT_NE(state, nullptr);

    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 0.0f));
    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 1.1f));
    ASSERT_TRUE(slayer3d_properties_get_bool(state->props, "idle", false));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");
    EXPECT_FALSE(slayer3d_game_data_app_flow_is_transitioning(&flow));

    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, input, 0.0f));
    EXPECT_FALSE(slayer3d_properties_get_bool(state->props, "idle", true));

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");

    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.play");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, MotionOscillateMovesActiveSceneActors)
{
    const std::filesystem::path dir = unique_test_dir("motion_oscillate");
    write_scene_activity_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "activity.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_registered_actor *lamp = slayer3d_game_data_find_actor(runtime, "entity.lamp");
    ASSERT_NE(lamp, nullptr);
    EXPECT_NEAR(lamp->position.x, 2.0f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f));
    EXPECT_NEAR(lamp->position.x, 2.0f + 3.0f * SDL_sinf(1.0f), 0.0001f);
    EXPECT_NEAR(lamp->position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(lamp->position.z, 0.0f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f));
    EXPECT_NEAR(lamp->position.x, 2.0f + 3.0f * SDL_sinf(2.0f), 0.0001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, AppFlowRunsAuthoredTweenActions)
{
    const std::filesystem::path dir = unique_test_dir("animation");
    write_animation_json(dir);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "animation.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    const int property_done = slayer3d_game_data_find_signal(runtime, "signal.property.done");
    const int ui_done = slayer3d_game_data_find_signal(runtime, "signal.ui.done");
    ASSERT_GE(property_done, 0);
    ASSERT_GE(ui_done, 0);
    SignalCapture property_capture{};
    SignalCapture ui_capture{};
    ASSERT_NE(slayer3d_signal_connect(slayer3d_game_session_get_signal_bus(session), property_done, count_signal,
                                      &property_capture),
              0);
    ASSERT_NE(
        slayer3d_signal_connect(slayer3d_game_session_get_signal_bus(session), ui_done, count_signal, &ui_capture), 0);

    slayer3d_game_context ctx{};
    ctx.session = session;
    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));

    slayer3d_registered_actor *box = slayer3d_game_data_find_actor(runtime, "entity.box");
    ASSERT_NE(box, nullptr);

    ASSERT_TRUE(slayer3d_game_data_update_animations(runtime, 0.5f));
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "x", -1.0f), 5.0f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "ease", -1.0f), 0.75f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "loop", -1.0f), 0.5f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "ping", -1.0f), 0.5f, 0.0001f);
    EXPECT_EQ(property_capture.calls, 0);
    EXPECT_EQ(ui_capture.calls, 0);

    slayer3d_game_data_ui_text logo{};
    bool saw_logo = false;
    auto find_logo = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *args = static_cast<std::pair<slayer3d_game_data_ui_text *, bool *> *>(userdata);
        if (text->name != nullptr && std::string(text->name) == "ui.logo")
        {
            *args->first = *text;
            *args->second = true;
            return false;
        }
        return true;
    };
    std::pair<slayer3d_game_data_ui_text *, bool *> logo_args{&logo, &saw_logo};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_logo, &logo_args));
    ASSERT_TRUE(saw_logo);

    slayer3d_game_data_ui_text resolved_logo{};
    bool logo_visible = false;
    ASSERT_TRUE(slayer3d_game_data_resolve_ui_text(runtime, &logo, nullptr, &resolved_logo, &logo_visible));
    EXPECT_TRUE(logo_visible);
    EXPECT_EQ(resolved_logo.color.a, 128);
    EXPECT_NEAR(resolved_logo.scale, 1.5f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_update_animations(runtime, 0.5f));
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "x", -1.0f), 10.0f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "ease", -1.0f), 1.0f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "loop", -1.0f), 0.0f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "ping", -1.0f), 1.0f, 0.0001f);
    EXPECT_EQ(property_capture.calls, 1);
    EXPECT_EQ(ui_capture.calls, 1);

    ASSERT_TRUE(slayer3d_game_data_resolve_ui_text(runtime, &logo, nullptr, &resolved_logo, &logo_visible));
    EXPECT_TRUE(logo_visible);
    EXPECT_EQ(resolved_logo.color.a, 255);
    EXPECT_NEAR(resolved_logo.scale, 2.0f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_update_animations(runtime, 0.25f));
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "loop", -1.0f), 0.25f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "ping", -1.0f), 0.75f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(slayer3d_game_data_update_animations(runtime, 1.0f));
    EXPECT_NEAR(slayer3d_properties_get_float(box->props, "loop", -1.0f), 0.25f, 0.0001f);
    slayer3d_game_data_ui_state logo_state{};
    EXPECT_FALSE(slayer3d_game_data_get_ui_state(runtime, "ui.logo", &logo_state));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, SignalBindingsResolveLuaAdaptersDeclaredInJson)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    const int serve_signal = slayer3d_game_data_find_signal(runtime, "signal.ball.serve");
    ASSERT_GE(serve_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), serve_signal, nullptr);

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(ball, nullptr);
    const slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(ball->position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(ball->position.y, 0.0f, 0.0001f);
    EXPECT_GT(SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y), 5.0f);
    EXPECT_GT(SDL_fabsf(velocity.x), 4.8f);
    EXPECT_GT(SDL_fabsf(velocity.y), 0.70f);
    EXPECT_TRUE(slayer3d_properties_get_bool(ball->props, "active_motion", false));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongTitleAttractServeHasJitterAndMovesCpuPaddles)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(slayer3d_game_data_update_scene_activity(runtime, slayer3d_game_session_get_input(session), 0.0f));

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball.attract");
    slayer3d_registered_actor *left = slayer3d_game_data_find_actor(runtime, "entity.paddle.attract_left");
    slayer3d_registered_actor *right = slayer3d_game_data_find_actor(runtime, "entity.paddle.attract_right");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    const slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(slayer3d_properties_get_bool(ball->props, "active_motion", false));
    EXPECT_GT(SDL_fabsf(velocity.x), 4.8f);
    EXPECT_GT(SDL_fabsf(velocity.y), 1.0f);

    const float initial_left_y = left->position.y;
    const float initial_right_y = right->position.y;
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));

    EXPECT_NE(left->position.y, initial_left_y);
    EXPECT_NE(right->position.y, initial_right_y);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LuaAdapterReflectsBallFromPaddle)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    slayer3d_registered_actor *paddle = slayer3d_game_data_find_actor(runtime, "entity.paddle.player");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(paddle, nullptr);
    ball->position = slayer3d_vec3_make(paddle->position.x + 0.10f, paddle->position.y + 0.40f, 0.12f);
    slayer3d_properties_set_vec3(ball->props, "origin", ball->position);
    slayer3d_properties_set_vec3(ball->props, "velocity", slayer3d_vec3_make(-5.6f, 0.0f, 0.0f));

    slayer3d_properties *payload = slayer3d_properties_create();
    ASSERT_NE(payload, nullptr);
    slayer3d_properties_set_string(payload, "actor_name", "entity.ball");
    slayer3d_properties_set_string(payload, "other_actor_name", "entity.paddle.player");
    const int hit_signal = slayer3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    ASSERT_GE(hit_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), hit_signal, payload);
    slayer3d_properties_destroy(payload);

    const slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(velocity.x, 0.0f);
    EXPECT_GT(SDL_sqrtf(velocity.x * velocity.x + velocity.y * velocity.y), 5.6f);
    EXPECT_GT(ball->position.x, paddle->position.x);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LuaAdapterAddsJitterAfterRepeatedFlatPaddleReflects)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    slayer3d_registered_actor *left = slayer3d_game_data_find_actor(runtime, "entity.paddle.player");
    slayer3d_registered_actor *right = slayer3d_game_data_find_actor(runtime, "entity.paddle.cpu");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    const int hit_signal = slayer3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    ASSERT_GE(hit_signal, 0);

    auto emit_center_hit = [&](const char *paddle_name, float ball_x) {
        ball->position = slayer3d_vec3_make(ball_x, 0.0f, 0.12f);
        slayer3d_properties_set_vec3(ball->props, "origin", ball->position);

        slayer3d_properties *payload = slayer3d_properties_create();
        ASSERT_NE(payload, nullptr);
        slayer3d_properties_set_string(payload, "actor_name", "entity.ball");
        slayer3d_properties_set_string(payload, "other_actor_name", paddle_name);
        slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), hit_signal, payload);
        slayer3d_properties_destroy(payload);
    };

    emit_center_hit("entity.paddle.player", left->position.x + 0.10f);
    slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(velocity.y, 0.0f, 0.0001f);
    EXPECT_EQ(slayer3d_properties_get_int(ball->props, "stagnant_reflect_count", -1), 1);

    emit_center_hit("entity.paddle.cpu", right->position.x - 0.10f);
    velocity = slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(SDL_fabsf(velocity.y), 1.0f);
    EXPECT_EQ(slayer3d_properties_get_int(ball->props, "stagnant_reflect_count", -1), 0);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AttractBallReflectsApplyAuthoredRandomJitter)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball.attract");
    slayer3d_registered_actor *left = slayer3d_game_data_find_actor(runtime, "entity.paddle.attract_left");
    slayer3d_registered_actor *right = slayer3d_game_data_find_actor(runtime, "entity.paddle.attract_right");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    const int hit_signal = slayer3d_game_data_find_signal(runtime, "signal.ball.hit_paddle");
    ASSERT_GE(hit_signal, 0);

    auto emit_center_attract_hit = [&](const char *paddle_name, float ball_x) {
        ball->position = slayer3d_vec3_make(ball_x, 0.0f, 0.12f);
        slayer3d_properties_set_vec3(ball->props, "origin", ball->position);

        slayer3d_properties *payload = slayer3d_properties_create();
        ASSERT_NE(payload, nullptr);
        slayer3d_properties_set_string(payload, "actor_name", "entity.ball.attract");
        slayer3d_properties_set_string(payload, "other_actor_name", paddle_name);
        slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), hit_signal, payload);
        slayer3d_properties_destroy(payload);
    };

    emit_center_attract_hit("entity.paddle.attract_left", left->position.x + 0.10f);
    slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(velocity.x, 0.0f);
    EXPECT_GT(SDL_fabsf(velocity.y), 0.55f);

    emit_center_attract_hit("entity.paddle.attract_right", right->position.x - 0.10f);
    velocity = slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_LT(velocity.x, 0.0f);
    EXPECT_GT(SDL_fabsf(velocity.y), 0.55f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LuaControllerMovesCpuPaddleTowardBall)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    slayer3d_registered_actor *cpu = slayer3d_game_data_find_actor(runtime, "entity.paddle.cpu");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(cpu, nullptr);
    ball->position.y = 2.0f;
    cpu->position.y = 0.0f;
    slayer3d_properties_set_vec3(ball->props, "origin", ball->position);
    slayer3d_properties_set_vec3(cpu->props, "origin", cpu->position);
    slayer3d_properties *payload = slayer3d_properties_create();
    ASSERT_NE(payload, nullptr);
    slayer3d_properties_set_string(payload, "match_mode", "single");
    ASSERT_TRUE(slayer3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload));
    slayer3d_properties_destroy(payload);
    slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(runtime), "match_mode", "single");
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));

    EXPECT_GT(cpu->position.y, 0.0f);
    EXPECT_LE(cpu->position.y, 0.55f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongClientDoesNotStartServeTimer)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_register_adapter(runtime, "adapter.pong.configure_play_input",
                                                    configure_play_input_adapter, nullptr));

    slayer3d_properties *payload = slayer3d_properties_create();
    ASSERT_NE(payload, nullptr);
    slayer3d_properties_set_string(payload, "match_mode", "lan");
    slayer3d_properties_set_string(payload, "network_role", "client");
    ASSERT_TRUE(slayer3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload));
    slayer3d_properties_destroy(payload);

    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "network_role", ""), "client");
    EXPECT_FALSE(slayer3d_game_data_active_scene_update_phase(runtime, "simulation", false));
    EXPECT_EQ(slayer3d_timer_pool_active_count(slayer3d_game_session_get_timer_pool(session)), 0);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, PongMatchStateAndRestartAreAuthoredLogic)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *player_score = slayer3d_game_data_find_actor(runtime, "entity.score.player");
    slayer3d_registered_actor *cpu_score = slayer3d_game_data_find_actor(runtime, "entity.score.cpu");
    slayer3d_registered_actor *match = slayer3d_game_data_find_actor(runtime, "entity.match");
    slayer3d_registered_actor *presentation = slayer3d_game_data_find_actor(runtime, "entity.presentation");
    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(player_score, nullptr);
    ASSERT_NE(cpu_score, nullptr);
    ASSERT_NE(match, nullptr);
    ASSERT_NE(presentation, nullptr);
    ASSERT_NE(ball, nullptr);

    slayer3d_properties_set_int(player_score->props, "value", 9);
    const int player_score_signal = slayer3d_game_data_find_signal(runtime, "signal.score.player");
    ASSERT_GE(player_score_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), player_score_signal, nullptr);

    EXPECT_EQ(slayer3d_properties_get_int(player_score->props, "value", 0), 10);
    EXPECT_TRUE(slayer3d_properties_get_bool(match->props, "finished", false));
    EXPECT_STREQ(slayer3d_properties_get_string(match->props, "winner", ""), "player");
    EXPECT_FALSE(ball->active);
    EXPECT_FALSE(slayer3d_properties_get_bool(ball->props, "active_motion", true));

    slayer3d_properties_set_float(presentation->props, "border_flash", 1.0f);
    slayer3d_properties_set_float(presentation->props, "paddle_flash", 1.0f);
    const int restart_signal = slayer3d_game_data_find_signal(runtime, "signal.match.restart");
    ASSERT_GE(restart_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), restart_signal, nullptr);

    EXPECT_EQ(slayer3d_properties_get_int(player_score->props, "value", -1), 0);
    EXPECT_EQ(slayer3d_properties_get_int(cpu_score->props, "value", -1), 0);
    EXPECT_FALSE(slayer3d_properties_get_bool(match->props, "finished", true));
    EXPECT_STREQ(slayer3d_properties_get_string(match->props, "winner", ""), "none");
    EXPECT_TRUE(ball->active);
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(presentation->props, "border_flash", -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(presentation->props, "paddle_flash", -1.0f), 0.0f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RegisteredCAdaptersOverrideLuaAdapters)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    AdapterCapture capture{};
    ASSERT_TRUE(slayer3d_game_data_register_adapter(runtime, "adapter.pong.serve_random", serve_adapter, &capture));

    const int serve_signal = slayer3d_game_data_find_signal(runtime, "signal.ball.serve");
    ASSERT_GE(serve_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), serve_signal, nullptr);

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    ASSERT_NE(ball, nullptr);
    const slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(capture.calls, 1);
    EXPECT_FLOAT_EQ(velocity.x, 3.0f);
    EXPECT_TRUE(slayer3d_properties_get_bool(ball->props, "active_motion", false));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LoadsLuaScriptDependenciesBeforeDependentAdapters)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    const std::string path = fixture_path("module_success.game.json");
    ASSERT_TRUE(slayer3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << error;

    const int run_signal = slayer3d_game_data_find_signal(runtime, "signal.run");
    ASSERT_GE(run_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), run_signal, nullptr);

    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    const slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(target->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const float speed_length = slayer3d_properties_get_float(target->props, "speed_length", 0.0f);
    const float random_value = slayer3d_properties_get_float(target->props, "random_value", -1.0f);
    EXPECT_FLOAT_EQ(target->position.x, 1.0f);
    EXPECT_FLOAT_EQ(target->position.y, 2.0f);
    EXPECT_FLOAT_EQ(target->position.z, 3.0f);
    EXPECT_FLOAT_EQ(velocity.x, 7.0f);
    EXPECT_FLOAT_EQ(velocity.y, 2.0f);
    EXPECT_NEAR(speed_length, SDL_sqrtf(53.0f), 0.0001f);
    EXPECT_TRUE(slayer3d_properties_get_bool(target->props, "ctx_ok", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(target->props, "state_ok", false));
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "last_adapter", ""),
                 "adapter.test.run");
    EXPECT_GE(random_value, 0.0f);
    EXPECT_LT(random_value, 1.0f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << error;
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session),
                         slayer3d_game_data_find_signal(runtime, "signal.run"), nullptr);
    target = slayer3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(target->props, "random_value", -1.0f), random_value);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ActorPoolsSpawnDespawnAndResetActors)
{
    const std::filesystem::path dir = unique_test_dir("actor_pools");
    write_text(dir / "shot.png", "test sprite placeholder");
    write_text(dir / "actor_pools.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Actor Pools", "id": "test.actor_pools", "version": "0.1.0" },
  "assets": {
    "sprites": [
      { "id": "sprite.player_shot", "path": "asset://shot.png", "frame_width": 1, "frame_height": 1 }
    ]
  },
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
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "actor_pools.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_registered_actor *shot0 = slayer3d_game_data_find_actor(runtime, "pool.player_shots.0");
    slayer3d_registered_actor *shot1 = slayer3d_game_data_find_actor(runtime, "pool.player_shots.1");
    slayer3d_registered_actor *reusable_shot0 = slayer3d_game_data_find_actor(runtime, "pool.reusable_shots.0");
    slayer3d_registered_actor *reusable_shot1 = slayer3d_game_data_find_actor(runtime, "pool.reusable_shots.1");
    ASSERT_NE(shot0, nullptr);
    ASSERT_NE(shot1, nullptr);
    ASSERT_NE(reusable_shot0, nullptr);
    ASSERT_NE(reusable_shot1, nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(shot1->active);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(shot0->props, "pool", ""), "pool.player_shots");
    EXPECT_STREQ(slayer3d_properties_get_string(shot0->props, "pool_scene", ""), "scene.play");
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "pool_index", -1), 0);

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn"), nullptr);
    EXPECT_TRUE(shot0->active);
    EXPECT_FALSE(shot1->active);
    expect_vec3_near(shot0->position, slayer3d_vec3_make(1.5f, 2.0f, 3.0f));
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 7);
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "last_actor", ""),
                 "pool.player_shots.0");
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "last_actor_id", -1), shot0->id);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "last_actor_pool_index", -1), 0);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.second"), nullptr);
    EXPECT_TRUE(shot1->active);
    expect_vec3_near(shot1->position, slayer3d_vec3_make(4.0f, 5.0f, 6.0f));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.reuse"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);
    expect_vec3_near(reusable_shot0->position, slayer3d_vec3_make(7.0f, 8.0f, 9.0f));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.reuse.second"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);
    expect_vec3_near(reusable_shot1->position, slayer3d_vec3_make(8.0f, 9.0f, 10.0f));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.despawn.reuse.first"), nullptr);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.reuse.again"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);
    expect_vec3_near(reusable_shot0->position, slayer3d_vec3_make(10.0f, 11.0f, 12.0f));
    EXPECT_EQ(slayer3d_properties_get_int(reusable_shot0->props, "damage", 0), 99);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.reuse.exhausted"), nullptr);
    EXPECT_TRUE(reusable_shot0->active);
    EXPECT_TRUE(reusable_shot1->active);
    expect_vec3_near(reusable_shot0->position, slayer3d_vec3_make(10.0f, 11.0f, 12.0f));
    expect_vec3_near(reusable_shot1->position, slayer3d_vec3_make(13.0f, 14.0f, 15.0f));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.despawn.first"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_TRUE(shot1->active);
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 1);
    expect_vec3_near(shot0->position, slayer3d_vec3_make(0.0f, 0.0f, 0.25f));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.second"), nullptr);
    EXPECT_TRUE(shot0->active);
    expect_vec3_near(shot0->position, slayer3d_vec3_make(4.0f, 5.0f, 6.0f));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.despawn.projectiles"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(shot1->active);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.despawn.projectiles"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(shot1->active);
    EXPECT_FALSE(reusable_shot0->active);
    EXPECT_FALSE(reusable_shot1->active);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ActorInstancesExpandArchetypesBeforeValidation)
{
    const std::filesystem::path dir = unique_test_dir("actor_instances");
    write_text(dir / "fragments" / "props.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "actor_archetypes": [
    {
      "name": "archetype.crate",
      "active": true,
      "tags": ["prop", "crate"],
      "properties": {
        "health": { "type": "int", "value": 10 }
      },
      "components": [
        { "type": "render.cube", "size": [1.0, 1.0, 1.0], "color": [40, 160, 70, 255], "lighting": true }
      ]
    }
  ],
  "actor_instances": [
    {
      "name": "entity.crate.a",
      "archetype": "archetype.crate",
      "transform": { "position": [1.0, 2.0, 3.0] },
      "properties": { "health": { "type": "int", "value": 7 } }
    },
    {
      "name": "entity.crate.b",
      "archetype": "archetype.crate",
      "transform": { "position": [4.0, 5.0, 6.0] }
    }
  ]
})json");
    write_text(
        dir / "scenes" / "play.scene.json",
        R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.crate.a", "entity.crate.b"] })json");
    write_text(dir / "actor_instances.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Actor Instances", "id": "test.actor_instances", "version": "0.1.0" },
  "imports": [
    { "path": "fragments/props.json", "sections": ["actor_archetypes", "actor_instances"] }
  ],
  "world": { "name": "world.actor_instances", "kind": "fixed_screen" },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "actor_instances.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_registered_actor *crate_a = slayer3d_game_data_find_actor(runtime, "entity.crate.a");
    slayer3d_registered_actor *crate_b = slayer3d_game_data_find_actor(runtime, "entity.crate.b");
    ASSERT_NE(crate_a, nullptr);
    ASSERT_NE(crate_b, nullptr);
    EXPECT_TRUE(crate_a->active);
    EXPECT_TRUE(crate_b->active);
    expect_vec3_near(crate_a->position, slayer3d_vec3_make(1.0f, 2.0f, 3.0f));
    expect_vec3_near(crate_b->position, slayer3d_vec3_make(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(slayer3d_properties_get_int(crate_a->props, "health", 0), 7);
    EXPECT_EQ(slayer3d_properties_get_int(crate_b->props, "health", 0), 10);

    RenderPrimitiveCapture render{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &render));
    EXPECT_EQ(render.cubes, 2);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "bad_instances.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Actor Instances" },
  "world": { "name": "world.bad_actor_instances", "kind": "fixed_screen" },
  "actor_instances": [
    { "name": "entity.bad", "archetype": "archetype.missing" }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "bad_instances.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("unknown actor archetype"), std::string::npos) << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EditorMetadataValidatesAndFpsMechanicsDojoLoads)
{
    const std::filesystem::path dojo_path = fps_mechanics_dojo_data_path();
    ASSERT_TRUE(std::filesystem::exists(dojo_path)) << dojo_path;

    slayer3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(dojo_path.string().c_str(), &config, title, sizeof(title),
                                                        app_error, sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "Slayer 3D FPS Mechanics Dojo");
    EXPECT_EQ(config.logical_width, SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH);
    EXPECT_EQ(config.logical_height, SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT);
    EXPECT_EQ(config.backend, SLAYER3D_BACKEND_OPENGL);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(dojo_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.dojo.fps_world"));
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.player"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.dojo.launch_pad"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.dojo.teleporter_pad"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.dojo.teleporter_destination"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.dojo.combat_dummy"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.dojo.health_pickup"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.dojo.health_station"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.dojo.powerup"), nullptr);

    const char *units = nullptr;
    float meters_per_unit = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_get_world_units(runtime, &units, &meters_per_unit));
    EXPECT_STREQ(units, SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS);
    EXPECT_FLOAT_EQ(meters_per_unit, SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT);
    slayer3d_camera3d camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.dojo.player", &camera));
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_FLOAT_EQ(camera.fovy, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);

    RenderPrimitiveCapture render{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &render));
    EXPECT_GE(render.cubes, 3);

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    struct DojoSceneCase
    {
        const char *scene;
        const char *scene_key;
        slayer3d_vec3 expected_position;
        const char *ui_title;
    };
    const DojoSceneCase scenes[] = {
        {"scene.dojo.movement", "movement", slayer3d_vec3_make(3.0f, 1.6f, 4.0f), "ui.dojo.movement.title"},
        {"scene.dojo.combat_resources", "combat_resources", slayer3d_vec3_make(21.0f, 1.6f, 5.0f),
         "ui.dojo.combat_resources.title"},
        {"scene.dojo.hazards", "hazards", slayer3d_vec3_make(16.0f, 1.6f, 5.0f), "ui.dojo.hazards.title"},
        {"scene.dojo.navigation", "navigation", slayer3d_vec3_make(4.0f, 1.6f, 14.0f), "ui.dojo.navigation.title"},
    };
    for (const DojoSceneCase &scene : scenes)
    {
        ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, scene.scene)) << scene.scene;
        EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), scene.scene);
        EXPECT_STREQ(slayer3d_properties_get_string(player->props, "dojo_scene", ""), scene.scene_key);
        expect_vec3_near(player->position, scene.expected_position);

        bool saw_title = false;
        auto find_dojo_scene_title = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
            auto *args = static_cast<std::pair<const char *, bool *> *>(userdata);
            if (std::string(text->name) == args->first)
            {
                *args->second = true;
                return false;
            }
            return true;
        };
        std::pair<const char *, bool *> title_args{scene.ui_title, &saw_title};
        ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_dojo_scene_title, &title_args));
        EXPECT_TRUE(saw_title) << scene.ui_title;
    }

    EXPECT_TRUE(slayer3d_game_data_sector_nav_path_available(
        runtime, "nav.dojo.arena", slayer3d_vec3_make(4.0f, 1.0f, 4.0f), slayer3d_vec3_make(24.0f, 1.0f, 6.0f)));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, MeshPrimitivesDojoLoadsGrayboxShowcase)
{
    const std::filesystem::path dojo_path = mesh_primitives_dojo_data_path();
    ASSERT_TRUE(std::filesystem::exists(dojo_path)) << dojo_path;

    slayer3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(dojo_path.string().c_str(), &config, title, sizeof(title),
                                                        app_error, sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "Slayer 3D Mesh Primitives Dojo");
    EXPECT_EQ(config.logical_width, SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH);
    EXPECT_EQ(config.logical_height, SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT);
    EXPECT_EQ(config.backend, SLAYER3D_BACKEND_OPENGL);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(dojo_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.mesh_primitives.showcase"));
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.player"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.sun"), nullptr);

    const char *units = nullptr;
    float meters_per_unit = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_get_world_units(runtime, &units, &meters_per_unit));
    EXPECT_STREQ(units, SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS);
    EXPECT_FLOAT_EQ(meters_per_unit, SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT);

    slayer3d_camera3d camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.mesh_primitives.player", &camera));
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_FLOAT_EQ(camera.fovy, 90.0f);
    EXPECT_EQ(camera.fov_axis, SLAYER3D_CAMERA_FOV_HORIZONTAL);

    ASSERT_EQ(slayer3d_game_data_world_light_count(runtime), 1);
    slayer3d_light sun{};
    ASSERT_TRUE(slayer3d_game_data_get_world_light(runtime, 0, &sun));
    EXPECT_EQ(sun.type, SLAYER3D_LIGHT_DIRECTIONAL);
    EXPECT_NEAR(sun.direction.y, -1.0f, 0.0001f);
    EXPECT_NEAR(sun.color[0], 1.0f, 0.0001f);

    struct MeshDojoCapture
    {
        int count = 0;
        bool seen[SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE + 1] = {};
        bool saw_solid_wire = false;
    } capture;
    auto capture_mesh = [](void *userdata, const slayer3d_game_data_render_primitive *primitive) -> bool {
        auto *mesh_capture = static_cast<MeshDojoCapture *>(userdata);
        if (primitive->type != SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
            return true;
        mesh_capture->count++;
        EXPECT_TRUE(primitive->lighting_enabled) << primitive->entity_name;
        EXPECT_GT(primitive->mesh_primitive, SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID);
        EXPECT_LE(primitive->mesh_primitive, SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE);
        if (primitive->mesh_primitive > SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID &&
            primitive->mesh_primitive <= SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE)
        {
            mesh_capture->seen[primitive->mesh_primitive] = true;
        }
        if (primitive->draw_mode == SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID_WIRE)
            mesh_capture->saw_solid_wire = true;
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_mesh, &capture));
    EXPECT_GE(capture.count, 15);
    EXPECT_TRUE(capture.saw_solid_wire);
    for (int kind = SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE; kind <= SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE;
         ++kind)
        EXPECT_TRUE(capture.seen[kind]) << kind;

    struct UiCapture
    {
        bool saw_fps = false;
        int label_count = 0;
    } ui_capture;
    auto capture_ui = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *capture = static_cast<UiCapture *>(userdata);
        if (std::string(text->name) == "ui.mesh_primitives.fps")
        {
            capture->saw_fps = true;
            EXPECT_NE(text->format, nullptr);
            EXPECT_TRUE(text->normalized);
            EXPECT_FLOAT_EQ(text->x, 0.985f);
            EXPECT_FLOAT_EQ(text->y, 0.03f);
        }
        if (std::string(text->name).find("ui.mesh_primitives.labels.") == 0)
        {
            ++capture->label_count;
            EXPECT_TRUE(text->normalized);
            EXPECT_GT(text->y, 0.1f);
        }
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, capture_ui, &ui_capture));
    EXPECT_TRUE(ui_capture.saw_fps);
    EXPECT_EQ(ui_capture.label_count, 3);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, LightingDojoLoadsSectorLocalLightingShowcase)
{
    const std::filesystem::path dojo_path = lighting_dojo_data_path();
    ASSERT_TRUE(std::filesystem::exists(dojo_path)) << dojo_path;

    slayer3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(dojo_path.string().c_str(), &config, title, sizeof(title),
                                                        app_error, sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "Slayer 3D Lighting Dojo");
    EXPECT_EQ(config.logical_width, SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH);
    EXPECT_EQ(config.logical_height, SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(dojo_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.lighting_dojo.showcase"));
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.player"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.sample.warm_probe"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.sample.blue_probe"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.sample.lab_probe"), nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor(runtime, "entity.mock.lab_robot"), nullptr);

    slayer3d_game_data_sector_level level{};
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.lighting_dojo.showcase", &level));
    ASSERT_GE(level.sector_count, 12);
    bool saw_dark = false;
    bool saw_green = false;
    for (int i = 0; i < level.sector_count; ++i)
    {
        const std::string name = level.sector_names[i] != nullptr ? level.sector_names[i] : "";
        if (name == "dark_room")
        {
            saw_dark = true;
            EXPECT_TRUE(level.sectors[i].has_lighting);
            EXPECT_GT(level.sectors[i].lighting_level, 90.0f);
            EXPECT_LT(level.sectors[i].lighting_level, 140.0f);
        }
        if (name == "green_toxic_room")
        {
            saw_green = true;
            EXPECT_TRUE(level.sectors[i].has_lighting);
            EXPECT_GT(level.sectors[i].lighting_color[1], level.sectors[i].lighting_color[0]);
        }
    }
    EXPECT_TRUE(saw_dark);
    EXPECT_TRUE(saw_green);

    ASSERT_GE(slayer3d_game_data_world_light_count(runtime), 5);

    SectorLevelInstanceCapture sector_capture{};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_sector_level_instance(runtime, capture_sector_level_instance, &sector_capture));
    ASSERT_EQ(sector_capture.count, 1);
    EXPECT_TRUE(sector_capture.sector_lighting_enabled);
    const slayer3d_level *sector_lit_level = sector_capture.level;

    slayer3d_game_data_render_settings render_settings{};
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render_settings));
    EXPECT_TRUE(render_settings.bloom_enabled);

    struct LightingDojoCapture
    {
        bool saw_green_sample = false;
        bool saw_lab_robot = false;
        bool green_sample_lighting_enabled = false;
        slayer3d_color green_sample{};
    } capture;
    auto capture_lighting_dojo = [](void *userdata, const slayer3d_game_data_render_primitive *primitive) -> bool {
        auto *data = static_cast<LightingDojoCapture *>(userdata);
        const std::string name = primitive->entity_name != nullptr ? primitive->entity_name : "";
        if (name == "entity.sample.green")
        {
            data->saw_green_sample = true;
            data->green_sample = primitive->color;
            data->green_sample_lighting_enabled = primitive->lighting_enabled;
        }
        if (name == "entity.mock.lab_robot" && primitive->type == SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
            data->saw_lab_robot = true;
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_lighting_dojo, &capture));
    EXPECT_TRUE(capture.saw_green_sample);
    EXPECT_TRUE(capture.saw_lab_robot);
    EXPECT_TRUE(capture.green_sample_lighting_enabled);
    EXPECT_GT(capture.green_sample.g, capture.green_sample.r);
    EXPECT_GT(capture.green_sample.g, capture.green_sample.b);

    auto emit_signal = [session, runtime](const char *name) {
        const int signal = slayer3d_game_data_find_signal(runtime, name);
        ASSERT_GE(signal, 0) << name;
        slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), signal, nullptr);
    };

    emit_signal("signal.lighting.dynamic.toggle");
    EXPECT_EQ(slayer3d_game_data_world_light_count(runtime), 0);

    emit_signal("signal.lighting.bloom.toggle");
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render_settings));
    EXPECT_FALSE(render_settings.bloom_enabled);

    emit_signal("signal.lighting.actor.toggle");
    LightingDojoCapture actor_toggle_capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_lighting_dojo, &actor_toggle_capture));
    EXPECT_TRUE(actor_toggle_capture.saw_green_sample);
    EXPECT_FALSE(actor_toggle_capture.green_sample_lighting_enabled);
    EXPECT_GT(actor_toggle_capture.green_sample.b, actor_toggle_capture.green_sample.g);

    emit_signal("signal.lighting.sector.toggle");
    sector_capture = {};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_sector_level_instance(runtime, capture_sector_level_instance, &sector_capture));
    ASSERT_EQ(sector_capture.count, 1);
    EXPECT_FALSE(sector_capture.sector_lighting_enabled);
    EXPECT_NE(sector_capture.level, sector_lit_level);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, BrushGeometryDojoLoadsCompiledBrushShowcase)
{
    const std::filesystem::path dojo_path = brush_geometry_dojo_data_path();
    ASSERT_TRUE(std::filesystem::exists(dojo_path)) << dojo_path;

    slayer3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(dojo_path.string().c_str(), &config, title, sizeof(title),
                                                        app_error, sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "Slayer 3D Brush Geometry Dojo");
    EXPECT_EQ(config.backend, SLAYER3D_BACKEND_OPENGL);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(dojo_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.brush_geometry.showcase"));
    slayer3d_game_data_scene_skybox skybox{};
    ASSERT_TRUE(slayer3d_game_data_get_active_scene_skybox(runtime, &skybox));
    EXPECT_STREQ(skybox.pos_x, "image.brush_geometry.skybox.px");
    EXPECT_STREQ(skybox.neg_x, "image.brush_geometry.skybox.nx");
    EXPECT_STREQ(skybox.pos_y, "image.brush_geometry.skybox.py");
    EXPECT_STREQ(skybox.neg_y, "image.brush_geometry.skybox.ny");
    EXPECT_STREQ(skybox.pos_z, "image.brush_geometry.skybox.pz");
    EXPECT_STREQ(skybox.neg_z, "image.brush_geometry.skybox.nz");
    EXPECT_FLOAT_EQ(skybox.size, 400.0f);
    slayer3d_game_data_brush_world world{};
    ASSERT_TRUE(slayer3d_game_data_get_brush_world(runtime, "brush.brush_geometry.showcase", &world));
    ASSERT_NE(world.render_model, nullptr);
    EXPECT_EQ(world.material_count, 7);
    EXPECT_GE(world.brush_count, 45);
    EXPECT_GE(world.render_model->mesh_count, 6);
    auto find_material = [&world](const char *name) -> const slayer3d_game_data_brush_material * {
        for (int i = 0; i < world.material_count; ++i)
        {
            if (world.materials[i].name != nullptr && SDL_strcmp(world.materials[i].name, name) == 0)
                return &world.materials[i];
        }
        return nullptr;
    };
    auto find_brush = [&world](const char *name) -> const slayer3d_game_data_brush * {
        for (int i = 0; i < world.brush_count; ++i)
        {
            if (world.brushes[i].name != nullptr && SDL_strcmp(world.brushes[i].name, name) == 0)
                return &world.brushes[i];
        }
        return nullptr;
    };
    auto x_range = [](const slayer3d_game_data_brush *brush) {
        std::pair<float, float> range{-FLT_MAX, FLT_MAX};
        if (brush == nullptr)
            return range;
        for (int i = 0; i < brush->face_count; ++i)
        {
            const slayer3d_game_data_brush_face &face = brush->faces[i];
            if (SDL_fabsf(face.normal.x - 1.0f) <= 0.001f)
                range.second = SDL_min(range.second, face.distance);
            else if (SDL_fabsf(face.normal.x + 1.0f) <= 0.001f)
                range.first = SDL_max(range.first, -face.distance);
        }
        return range;
    };
    const slayer3d_game_data_brush_material *blue_material = find_material("mat.accent_blue");
    ASSERT_NE(blue_material, nullptr);
    EXPECT_GT(blue_material->emissive.z, 0.0f);
    const slayer3d_game_data_brush_material *floor_material = find_material("mat.floor");
    const slayer3d_game_data_brush_material *wall_material = find_material("mat.wall");
    const slayer3d_game_data_brush_material *ceiling_material = find_material("mat.ceiling");
    ASSERT_NE(floor_material, nullptr);
    ASSERT_NE(wall_material, nullptr);
    ASSERT_NE(ceiling_material, nullptr);
    EXPECT_STREQ(floor_material->texture, "asset://textures/rock_floor.jpg");
    EXPECT_STREQ(wall_material->texture, "asset://textures/wall_metal.jpg");
    EXPECT_STREQ(ceiling_material->texture, "asset://textures/ceiling_metal.jpg");
    bool saw_model_emissive = false;
    bool saw_model_floor_texture = false;
    bool saw_model_wall_texture = false;
    bool saw_model_ceiling_texture = false;
    for (int i = 0; i < world.render_model->material_count; ++i)
    {
        const slayer3d_material &material = world.render_model->materials[i];
        if (material.name != nullptr && SDL_strcmp(material.name, "mat.accent_blue") == 0)
        {
            saw_model_emissive = true;
            EXPECT_GT(material.emissive[2], 0.0f);
        }
        if (material.name != nullptr && SDL_strcmp(material.name, "mat.floor") == 0)
        {
            saw_model_floor_texture = true;
            ASSERT_NE(material.albedo_map, nullptr);
            EXPECT_STREQ(material.albedo_map, "asset://textures/rock_floor.jpg");
        }
        if (material.name != nullptr && SDL_strcmp(material.name, "mat.wall") == 0)
        {
            saw_model_wall_texture = true;
            ASSERT_NE(material.albedo_map, nullptr);
            EXPECT_STREQ(material.albedo_map, "asset://textures/wall_metal.jpg");
        }
        if (material.name != nullptr && SDL_strcmp(material.name, "mat.ceiling") == 0)
        {
            saw_model_ceiling_texture = true;
            ASSERT_NE(material.albedo_map, nullptr);
            EXPECT_STREQ(material.albedo_map, "asset://textures/ceiling_metal.jpg");
        }
    }
    EXPECT_TRUE(saw_model_emissive);
    EXPECT_TRUE(saw_model_floor_texture);
    EXPECT_TRUE(saw_model_wall_texture);
    EXPECT_TRUE(saw_model_ceiling_texture);
    ASSERT_NE(find_brush("brush.room.ceiling.north"), nullptr);
    ASSERT_NE(find_brush("brush.room.ceiling.south"), nullptr);
    ASSERT_NE(find_brush("brush.room.ceiling.west"), nullptr);
    ASSERT_NE(find_brush("brush.room.ceiling.east"), nullptr);
    ASSERT_NE(find_brush("brush.room.wall_south.window_top"), nullptr);
    ASSERT_NE(find_brush("brush.room.wall_north.window_top"), nullptr);
    ASSERT_NE(find_brush("brush.side_room.west.ceiling"), nullptr);
    ASSERT_NE(find_brush("brush.side_room.east.ceiling"), nullptr);
    ASSERT_NE(find_brush("brush.side_room.north.ceiling"), nullptr);
    ASSERT_NE(find_brush("brush.bridge.deck"), nullptr);
    ASSERT_NE(find_brush("brush.bridge.rail_west"), nullptr);
    ASSERT_NE(find_brush("brush.stairs.south.0"), nullptr);
    ASSERT_NE(find_brush("brush.stairs.south.landing"), nullptr);
    ASSERT_NE(find_brush("brush.conveyor.floor_escalator"), nullptr);
    ASSERT_NE(find_brush("brush.tower.second_floor"), nullptr);
    ASSERT_NE(find_brush("brush.tower.wall_south.header"), nullptr);
    ASSERT_NE(find_brush("brush.tower.stair.5"), nullptr);
    const slayer3d_game_data_brush *pillar = find_brush("brush.pillar.center");
    const slayer3d_game_data_brush *overhang_west = find_brush("brush.bridge.overhang_west");
    const slayer3d_game_data_brush *overhang_east = find_brush("brush.bridge.overhang_east");
    ASSERT_NE(pillar, nullptr);
    ASSERT_NE(overhang_west, nullptr);
    ASSERT_NE(overhang_east, nullptr);
    const auto pillar_x = x_range(pillar);
    const auto west_x = x_range(overhang_west);
    const auto east_x = x_range(overhang_east);
    EXPECT_LT(west_x.second, pillar_x.first);
    EXPECT_GT(east_x.first, pillar_x.second);
    const slayer3d_game_data_brush *floor = find_brush("brush.room.floor");
    ASSERT_NE(floor, nullptr);
    bool saw_floor_uv = false;
    for (int i = 0; i < floor->face_count; ++i)
    {
        const slayer3d_game_data_brush_face &face = floor->faces[i];
        if (SDL_fabsf(face.normal.y - 1.0f) <= 0.001f)
        {
            saw_floor_uv = true;
            EXPECT_FLOAT_EQ(face.uv_scale[0], 0.5f);
            EXPECT_FLOAT_EQ(face.uv_scale[1], 0.5f);
        }
    }
    EXPECT_TRUE(saw_floor_uv);
    int rendered_vertices = 0;
    for (int i = 0; i < world.render_model->mesh_count; ++i)
    {
        const slayer3d_mesh *mesh = &world.render_model->meshes[i];
        EXPECT_GE(mesh->material_index, 0);
        EXPECT_LT(mesh->material_index, world.render_model->material_count);
        EXPECT_NE(mesh->positions, nullptr);
        EXPECT_NE(mesh->normals, nullptr);
        EXPECT_NE(mesh->uvs, nullptr);
        EXPECT_EQ(mesh->vertex_count, mesh->index_count);
        EXPECT_TRUE(mesh->has_local_bounds);
        rendered_vertices += mesh->vertex_count;
    }
    EXPECT_GT(rendered_vertices, 150);

    BrushWorldInstanceCapture capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_brush_world_instance(runtime, capture_brush_world_instance, &capture));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.world_name, "brush.brush_geometry.showcase");
    EXPECT_TRUE(capture.acceleration_enabled);
    EXPECT_TRUE(capture.lighting_enabled);

    ASSERT_EQ(slayer3d_game_data_world_light_count(runtime), 14);
    slayer3d_camera3d camera{};
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.brush_geometry.player", &camera));
    EXPECT_EQ(camera.fov_axis, SLAYER3D_CAMERA_FOV_HORIZONTAL);
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_NEAR(camera.position.y, 1.6f, 0.05f);
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.brush_geometry.player");
    RenderPrimitiveCapture player_camera_capture{};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &player_camera_capture));
    EXPECT_EQ(player_camera_capture.brush_player_capsules, 0);
    const int security_camera_signal = slayer3d_game_data_find_signal(runtime, "signal.brush_geometry.camera.toggle");
    ASSERT_GE(security_camera_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), security_camera_signal, nullptr);
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.brush_geometry.security");
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.brush_geometry.security", &camera));
    EXPECT_EQ(camera.fov_axis, SLAYER3D_CAMERA_FOV_HORIZONTAL);
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_NEAR(camera.position.x, 0.0f, 0.05f);
    EXPECT_NEAR(camera.position.y, 5.8f, 0.05f);
    EXPECT_NEAR(camera.position.z, 28.0f, 0.05f);
    RenderPrimitiveCapture security_camera_capture{};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &security_camera_capture));
    EXPECT_EQ(security_camera_capture.brush_player_capsules, 1);
    const slayer3d_vec3 first_security_target = camera.target;
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f));
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.brush_geometry.security", &camera));
    EXPECT_NEAR(camera.target.y, 1.45f, 0.05f);
    EXPECT_LT(SDL_fabsf(camera.target.x - first_security_target.x), 18.1f);
    EXPECT_GT(SDL_fabsf(camera.target.x - first_security_target.x), 0.25f);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), security_camera_signal, nullptr);
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.brush_geometry.player");

    RenderPrimitiveCapture primitive_capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &primitive_capture));
    EXPECT_GE(primitive_capture.brush_mesh_primitives, 15);
    EXPECT_TRUE(primitive_capture.saw_brush_spinning_cube);
    EXPECT_EQ(primitive_capture.brush_sprites, 2);
    EXPECT_TRUE(primitive_capture.saw_brush_robot_sprite);
    EXPECT_TRUE(primitive_capture.saw_brush_health_sprite);

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.brush_geometry.player");
    slayer3d_registered_actor *zombie = slayer3d_game_data_find_actor(runtime, "entity.brush_geometry.zombie");
    slayer3d_registered_actor *visible = slayer3d_game_data_find_actor(runtime, "entity.brush_geometry.visible_target");
    slayer3d_registered_actor *occluded =
        slayer3d_game_data_find_actor(runtime, "entity.brush_geometry.occluded_target");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(zombie, nullptr);
    ASSERT_NE(visible, nullptr);
    ASSERT_NE(occluded, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(zombie->props, "on_ground", false));
    EXPECT_GT(slayer3d_properties_get_float(zombie->props, "walk_anim_time", 0.0f), 0.0f);
    bool saw_zombie_model = false;
    auto find_zombie_model = [](void *userdata, const slayer3d_game_data_render_primitive *primitive) -> bool {
        bool *saw = static_cast<bool *>(userdata);
        if (primitive != nullptr && primitive->entity_name != nullptr &&
            std::string(primitive->entity_name) == "entity.brush_geometry.zombie")
        {
            *saw = true;
            EXPECT_EQ(primitive->type, SLAYER3D_GAME_DATA_RENDER_MODEL);
            EXPECT_STREQ(primitive->model_asset, "model.brush_geometry.zombie.walking");
            EXPECT_TRUE(primitive->animation_loop);
            EXPECT_EQ(primitive->animation_clip, 0);
        }
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, find_zombie_model, &saw_zombie_model));
    EXPECT_TRUE(saw_zombie_model);
    EXPECT_GT(slayer3d_properties_get_int(visible->props, "spotted", 0), 0);
    EXPECT_EQ(slayer3d_properties_get_int(occluded->props, "spotted", 0), 0);

    slayer3d_render_stats before_stats{};
    slayer3d_render_stats after_stats{};
    after_stats.model_mesh_submissions = 7;
    after_stats.model_mesh_culled = 2;
    after_stats.model_mesh_draws = 5;
    after_stats.model_triangles_submitted = 128;
    slayer3d_game_data_accumulate_brush_render_diagnostics(runtime, &before_stats, &after_stats);
    bool saw_render_diagnostics = false;
    char render_diagnostics[128]{};
    auto find_render_diagnostics = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.brush_geometry.render_diagnostics")
            return true;
        auto *args = static_cast<std::tuple<slayer3d_game_data_runtime *, bool *, char *> *>(userdata);
        *std::get<1>(*args) = true;
        slayer3d_game_data_ui_metrics metrics{};
        EXPECT_TRUE(slayer3d_game_data_format_ui_text(std::get<0>(*args), text, &metrics, std::get<2>(*args), 128));
        return false;
    };
    std::tuple<slayer3d_game_data_runtime *, bool *, char *> render_diagnostics_args{runtime, &saw_render_diagnostics,
                                                                                     render_diagnostics};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_render_diagnostics, &render_diagnostics_args));
    EXPECT_TRUE(saw_render_diagnostics);
    EXPECT_STREQ(render_diagnostics, "RENDER 5/7 CULL 2 TRI 128");

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int fire_action = slayer3d_game_data_find_action(runtime, "action.fire");
    ASSERT_GE(fire_action, 0);
    slayer3d_input_set_action_override(input, fire_action, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 2500), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    slayer3d_registered_actor *projectile = slayer3d_game_data_find_actor(runtime, "pool.brush_geometry.projectiles.0");
    ASSERT_NE(projectile, nullptr);
    EXPECT_TRUE(projectile->active);
    slayer3d_vec3 camera_forward =
        slayer3d_properties_get_vec3(player->props, "camera_forward", slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
    camera_forward = slayer3d_vec3_normalize(camera_forward);
    const slayer3d_vec3 projectile_velocity =
        slayer3d_properties_get_vec3(projectile->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    expect_vec3_near(
        projectile->position,
        slayer3d_vec3_make(player->position.x + camera_forward.x * 0.75f + projectile_velocity.x * 0.016f,
                           player->position.y + camera_forward.y * 0.75f + projectile_velocity.y * 0.016f,
                           player->position.z + camera_forward.z * 0.75f + projectile_velocity.z * 0.016f));
    RenderPrimitiveCapture projectile_capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &projectile_capture));
    EXPECT_EQ(projectile_capture.brush_projectile_spheres, 1);
    slayer3d_input_set_action_override(input, fire_action, 0.0f);

    const int forward = slayer3d_game_data_find_action(runtime, "action.move.forward");
    ASSERT_GE(forward, 0);
    slayer3d_input_set_action_override(input, forward, 1.0f);
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(3000 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    }
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "brush_trigger_active", false));
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "last_trigger_brush", ""), "brush.trigger.los_demo");

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, BrushGeometryZombieGlbCarriesEmbeddedTextureMaterial)
{
    const std::filesystem::path model_path = std::filesystem::path(SLAYER3D_DEMOS_ROOT) / "brush_geometry_dojo" /
                                             "data" / "models" / "zombie" / "male-zombie-walking.glb";
    ASSERT_TRUE(std::filesystem::exists(model_path)) << model_path;

    slayer3d_model model{};
    ASSERT_TRUE(slayer3d_load_model_from_file(model_path.string().c_str(), &model)) << SDL_GetError();
    ASSERT_GE(model.material_count, 1);
    ASSERT_NE(model.materials, nullptr);
    ASSERT_NE(model.materials[0].albedo_map, nullptr);
    EXPECT_STREQ(model.materials[0].albedo_map, "#0");
    ASSERT_GT(model.embedded_texture_count, 0);
    ASSERT_NE(model.embedded_textures, nullptr);
    EXPECT_GT(model.embedded_textures[0].width, 0);
    EXPECT_GT(model.embedded_textures[0].height, 0);
    EXPECT_NE(model.embedded_textures[0].pixels, nullptr);
    EXPECT_NE(model.materials[0].emissive_map, nullptr);

    slayer3d_free_model(&model);
}

TEST(GameDataRuntime, UsesDefaultWorldUnitsAndPerspectiveFovy)
{
    const std::filesystem::path dir = unique_test_dir("world_camera_defaults");
    write_text(dir / "game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "World Camera Defaults" },
  "world": {
    "name": "world.defaults",
    "kind": "3d",
    "cameras": [
      {
        "name": "camera.default",
        "type": "perspective",
        "position": [0.0, 0.0, 2.0],
        "target": [0.0, 0.0, 0.0]
      },
      {
        "name": "camera.runtime_fov",
        "type": "perspective",
        "position": [0.0, 0.0, 2.0],
        "target": [0.0, 0.0, 0.0],
        "fov": 75.0,
        "fov_axis": "vertical",
        "fov_key": "camera.fov",
        "fov_axis_key": "camera.fov_axis"
      }
    ]
  }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file((dir / "game.json").string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    const char *units = nullptr;
    float meters_per_unit = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_get_world_units(runtime, &units, &meters_per_unit));
    EXPECT_STREQ(units, SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS);
    EXPECT_FLOAT_EQ(meters_per_unit, SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT);
    slayer3d_camera3d camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.default", &camera));
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_FLOAT_EQ(camera.fovy, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
    EXPECT_EQ(camera.fov_axis, SLAYER3D_CAMERA_FOV_VERTICAL);

    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.runtime_fov", &camera));
    EXPECT_FLOAT_EQ(camera.fovy, 75.0f);
    EXPECT_EQ(camera.fov_axis, SLAYER3D_CAMERA_FOV_VERTICAL);

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_float(scene_state, "camera.fov", 90.0f);
    slayer3d_properties_set_string(scene_state, "camera.fov_axis", "horizontal");
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.runtime_fov", &camera));
    EXPECT_FLOAT_EQ(camera.fovy, 90.0f);
    EXPECT_EQ(camera.fov_axis, SLAYER3D_CAMERA_FOV_HORIZONTAL);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidWorldDisplayAndCameraConventions)
{
    const std::filesystem::path dir = unique_test_dir("world_display_camera_validation");
    struct Case
    {
        const char *name;
        const char *json;
        const char *expected_error;
    };
    const Case cases[] = {
        {
            "bad_logical_width",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Logical Width" },
  "app": { "logical_width": 0 },
  "world": { "name": "world.bad", "kind": "3d" }
})json",
            "app dimensions must be positive integers",
        },
        {
            "bad_world_scale",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad World Scale" },
  "world": { "name": "world.bad", "kind": "3d", "units": "meters", "meters_per_unit": 0.0 }
})json",
            "world meters_per_unit must be positive",
        },
        {
            "bad_fovy",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad FOV" },
  "world": {
    "name": "world.bad",
    "kind": "3d",
    "cameras": [
      { "name": "camera.bad", "type": "perspective", "fovy": 180.0 }
    ]
  }
})json",
            "camera fovy must be in the range",
        },
        {
            "bad_fov_axis",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad FOV Axis" },
  "world": {
    "name": "world.bad",
    "kind": "3d",
    "cameras": [
      { "name": "camera.bad", "type": "perspective", "fov": 90.0, "fov_axis": "diagonal" }
    ]
  }
})json",
            "camera fov_axis must be",
        },
        {
            "ambiguous_fov",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Ambiguous FOV" },
  "world": {
    "name": "world.bad",
    "kind": "3d",
    "cameras": [
      { "name": "camera.bad", "type": "perspective", "fov": 90.0, "fovy": 60.0 }
    ]
  }
})json",
            "must not define both fov and fovy",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path path = dir / (std::string(test_case.name) + ".game.json");
        write_text(path, test_case.json);
        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidEditorMetadata)
{
    const std::filesystem::path dir = unique_test_dir("editor_metadata");
    write_text(dir / "bad_editor.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Editor Metadata", "id": "test.bad_editor", "version": "0.1.0" },
  "world": { "name": "world.bad_editor", "kind": "fixed_screen" },
  "editor": {
    "display_name": "Bad Metadata",
    "tags": ["valid", ""],
    "snap": { "grid": 0.0 }
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play" })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "bad_editor.game.json").string().c_str(), session, &runtime, error,
                                              sizeof(error)));
    EXPECT_NE(std::string(error).find("editor tag entries must be non-empty strings"), std::string::npos) << error;

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "bad_editor_default.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Editor Default", "id": "test.bad_editor_default", "version": "0.1.0" },
  "world": { "name": "world.bad_editor_default", "kind": "fixed_screen" },
  "editor": {
    "display_name": "Bad Editor Default",
    "exposed_properties": [
      { "name": "position", "type": "vec3", "default": [1.0, 2.0, 3.0, 4.0] }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "bad_editor_default.game.json").string().c_str(), session,
                                              &runtime, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("editor exposed property default does not match its type"), std::string::npos)
        << error;

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidSceneEditorTooling)
{
    struct Case
    {
        const char *name;
        const char *trace_json;
        const char *expected_error;
    };

    const Case cases[] = {
        {
            "bad_source",
            R"json({ "trace": { "source": "pointer", "model_filter": "brush_worlds" }, "outputs": { "hit_key": "editor.hit" } })json",
            "scene editor selection trace source must be 'world' or 'camera_screen'",
        },
        {
            "bad_camera",
            R"json({ "trace": { "source": "camera_screen", "camera": "camera.missing", "viewport": [1280, 720] }, "outputs": { "hit_key": "editor.hit" } })json",
            "unknown camera",
        },
        {
            "bad_far",
            R"json({ "trace": { "source": "camera_screen", "camera": "camera.valid", "near": 10.0, "far": 1.0 }, "outputs": { "hit_key": "editor.hit" } })json",
            "far must be greater than near",
        },
        {
            "bad_mode",
            R"json({ "mode": "drag", "trace": { "source": "camera_screen", "camera": "camera.valid" }, "outputs": { "hit_key": "editor.hit" } })json",
            "scene editor selection mode must be 'hover' or 'click'",
        },
        {
            "bad_select_button",
            R"json({ "select_button": "PRIMARY", "trace": { "source": "camera_screen", "camera": "camera.valid" }, "outputs": { "hit_key": "editor.hit" } })json",
            "scene editor selection select_button must be a mouse button",
        },
    };

    const std::filesystem::path dir = unique_test_dir("scene_editor_tooling");
    for (const Case &test_case : cases)
    {
        const std::filesystem::path root_path = dir / (std::string(test_case.name) + ".game.json");
        write_text(root_path, R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Scene Editor" },
  "world": {
    "name": "world.bad_scene_editor",
    "kind": "3d",
    "cameras": [
      { "name": "camera.valid", "type": "perspective", "position": [0, 0, 5], "target": [0, 0, 0], "up": [0, 1, 0] }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
        const std::string scene_json = std::string(R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "camera": "camera.valid",
  "editor": {
    "selection": )json") + test_case.trace_json +
                                       R"json(
  }
})json";
        write_text(dir / "scenes" / "play.scene.json", scene_json.c_str());

        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(root_path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidEditorSelectionActions)
{
    const std::filesystem::path dir = unique_test_dir("editor_selection_actions");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play" })json");

    write_text(dir / "bad_selection_action.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Editor Selection Action" },
  "world": { "name": "world.bad_editor_selection_action", "kind": "fixed_screen" },
  "signals": ["signal.editor.inspect"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.editor.inspect",
        "actions": [
          { "type": "editor.selection.run", "else": [] }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    char error[512]{};
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_selection_action.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("logic action list must be an array"), std::string::npos) << error;

    write_text(dir / "bad_preview_action.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Editor Preview Action" },
  "world": { "name": "world.bad_editor_preview_action", "kind": "fixed_screen" },
  "signals": ["signal.editor.preview"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.editor.preview",
        "actions": [
          { "type": "editor.command.preview", "command": "resize", "target": "element" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_preview_action.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("editor.command.preview command must be"), std::string::npos) << error;

    write_text(dir / "bad_paint_preview_action.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Editor Paint Preview Action" },
  "world": { "name": "world.bad_editor_paint_preview_action", "kind": "fixed_screen" },
  "signals": ["signal.editor.preview"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.editor.preview",
        "actions": [
          { "type": "editor.command.preview", "command": "paint", "target": "face" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_paint_preview_action.game.json").string().c_str(),
                                                  nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("editor.command.preview paint requires a non-empty material"), std::string::npos)
        << error;

    write_text(dir / "bad_transaction_action.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Editor Transaction Action" },
  "world": { "name": "world.bad_editor_transaction_action", "kind": "fixed_screen" },
  "signals": ["signal.editor.commit"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.editor.commit",
        "actions": [
          {
            "type": "editor.command.commit",
            "outputs": { "transaction_id_key": "" }
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_transaction_action.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("editor.command.commit output keys must be non-empty strings"), std::string::npos)
        << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, CombatActionsApplyHealthArmorDeathAndRevive)
{
    const std::filesystem::path dir = unique_test_dir("combat_actions");
    write_text(dir / "combat.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Combat Actions", "id": "test.combat_actions", "version": "0.1.0" },
  "world": { "name": "world.combat_actions", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.hero",
      "tags": ["player"]
    },
    {
      "name": "entity.monster",
      "tags": ["enemy"],
      "properties": {
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 },
        "armor": { "type": "float", "value": 20.0 },
        "armor_absorb": { "type": "float", "value": 0.5 },
        "alive": { "type": "bool", "value": true }
      },
      "components": [
        { "type": "combat.health" }
      ]
    }
  ],
  "signals": [
    "signal.damage",
    "signal.damage.lethal",
    "signal.damage.after_death",
    "signal.heal",
    "signal.revive",
    "signal.kill",
    "signal.damaged",
    "signal.dead",
    "signal.healed",
    "signal.revived"
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.damage",
        "actions": [
          {
            "type": "combat.damage",
            "target": "entity.monster",
            "source": "entity.hero",
            "amount": 30.0,
            "damage_type": "slash",
            "on_damage": "signal.damaged",
            "on_death": "signal.dead"
          }
        ]
      },
      {
        "signal": "signal.damage.lethal",
        "actions": [
          {
            "type": "combat.damage",
            "target": "entity.monster",
            "source": "entity.hero",
            "amount": 200.0,
            "on_damage": "signal.damaged",
            "on_death": "signal.dead"
          }
        ]
      },
      {
        "signal": "signal.damage.after_death",
        "actions": [
          {
            "type": "combat.damage",
            "target": "entity.monster",
            "source": "entity.hero",
            "amount": 1.0,
            "on_damage": "signal.damaged",
            "on_death": "signal.dead"
          }
        ]
      },
      {
        "signal": "signal.heal",
        "actions": [
          { "type": "combat.heal", "target": "entity.monster", "amount": 25.0, "on_heal": "signal.healed" }
        ]
      },
      {
        "signal": "signal.revive",
        "actions": [
          { "type": "combat.revive", "target": "entity.monster", "health": 50.0, "on_revive": "signal.revived" }
        ]
      },
      {
        "signal": "signal.kill",
        "actions": [
          { "type": "combat.kill", "target": "entity.monster", "source": "entity.hero", "on_death": "signal.dead" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(
        dir / "scenes" / "play.scene.json",
        R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.hero", "entity.monster"] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "combat.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_registered_actor *monster = slayer3d_game_data_find_actor(runtime, "entity.monster");
    ASSERT_NE(monster, nullptr);
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);

    CombatSignalCapture damaged{};
    CombatSignalCapture dead{};
    CombatSignalCapture healed{};
    CombatSignalCapture revived{};
    ASSERT_NE(slayer3d_signal_connect(bus, slayer3d_game_data_find_signal(runtime, "signal.damaged"),
                                      capture_combat_signal, &damaged),
              0);
    ASSERT_NE(slayer3d_signal_connect(bus, slayer3d_game_data_find_signal(runtime, "signal.dead"),
                                      capture_combat_signal, &dead),
              0);
    ASSERT_NE(slayer3d_signal_connect(bus, slayer3d_game_data_find_signal(runtime, "signal.healed"),
                                      capture_combat_signal, &healed),
              0);
    ASSERT_NE(slayer3d_signal_connect(bus, slayer3d_game_data_find_signal(runtime, "signal.revived"),
                                      capture_combat_signal, &revived),
              0);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.damage"), nullptr);
    EXPECT_EQ(damaged.calls, 1);
    EXPECT_EQ(dead.calls, 0);
    EXPECT_EQ(damaged.actor_name, "entity.monster");
    EXPECT_EQ(damaged.source_actor_name, "entity.hero");
    EXPECT_NEAR(damaged.amount, 30.0f, 0.001f);
    EXPECT_NEAR(damaged.armor_delta, 15.0f, 0.001f);
    EXPECT_NEAR(damaged.health_delta, 15.0f, 0.001f);
    EXPECT_NEAR(damaged.health, 85.0f, 0.001f);
    EXPECT_NEAR(damaged.armor, 5.0f, 0.001f);
    EXPECT_TRUE(damaged.alive);
    EXPECT_NEAR(slayer3d_properties_get_float(monster->props, "health", 0.0f), 85.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(monster->props, "armor", 0.0f), 5.0f, 0.001f);
    EXPECT_TRUE(slayer3d_properties_get_bool(monster->props, "alive", false));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.damage.lethal"), nullptr);
    EXPECT_EQ(damaged.calls, 2);
    EXPECT_EQ(dead.calls, 1);
    EXPECT_NEAR(slayer3d_properties_get_float(monster->props, "health", 1.0f), 0.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(monster->props, "armor", 1.0f), 0.0f, 0.001f);
    EXPECT_FALSE(slayer3d_properties_get_bool(monster->props, "alive", true));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.damage.after_death"), nullptr);
    EXPECT_EQ(damaged.calls, 3);
    EXPECT_EQ(dead.calls, 1);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.heal"), nullptr);
    EXPECT_EQ(healed.calls, 1);
    EXPECT_NEAR(slayer3d_properties_get_float(monster->props, "health", 0.0f), 25.0f, 0.001f);
    EXPECT_FALSE(slayer3d_properties_get_bool(monster->props, "alive", true));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.revive"), nullptr);
    EXPECT_EQ(revived.calls, 1);
    EXPECT_NEAR(slayer3d_properties_get_float(monster->props, "health", 0.0f), 50.0f, 0.001f);
    EXPECT_TRUE(slayer3d_properties_get_bool(monster->props, "alive", false));
    EXPECT_TRUE(monster->active);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.kill"), nullptr);
    EXPECT_EQ(dead.calls, 2);
    EXPECT_NEAR(slayer3d_properties_get_float(monster->props, "health", 1.0f), 0.0f, 0.001f);
    EXPECT_FALSE(slayer3d_properties_get_bool(monster->props, "alive", true));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidCombatActionsAndComponents)
{
    const std::filesystem::path dir = unique_test_dir("invalid_combat");
    write_text(dir / "invalid_combat.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Combat", "id": "test.invalid_combat", "version": "0.1.0" },
  "world": { "name": "world.invalid_combat", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.actor",
      "components": [
        { "type": "combat.health", "armor_absorb": 1.5 }
      ]
    }
  ],
  "signals": ["signal.damage"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.damage",
        "actions": [
          { "type": "combat.damage", "target": "entity.actor", "amount": -1.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.actor"] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_combat.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("combat.health armor_absorb must be in 0..1"), std::string::npos) << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "invalid_combat_action.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Combat Action", "id": "test.invalid_combat_action", "version": "0.1.0" },
  "world": { "name": "world.invalid_combat_action", "kind": "fixed_screen" },
  "entities": [{ "name": "entity.actor" }],
  "signals": ["signal.damage"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.damage",
        "actions": [
          { "type": "combat.damage", "target": "entity.actor", "amount": -1.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_combat_action.game.json").string().c_str(), session,
                                              &runtime, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("combat.damage amount must be a non-negative number"), std::string::npos)
        << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EffectExplosionAppliesRadialDamageImpulseAndActions)
{
    const std::filesystem::path dir = unique_test_dir("effect_explosion");
    write_text(dir / "explosion.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Explosion", "id": "test.effect_explosion", "version": "0.1.0" },
  "world": { "name": "world.explosion", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "tags": ["player"],
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": { "health": { "type": "float", "value": 100.0 } }
    },
    {
      "name": "entity.enemy.near",
      "active": true,
      "tags": ["enemy"],
      "transform": { "position": [1.0, 0.0, 0.0] },
      "properties": {
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 },
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
      }
    },
    {
      "name": "entity.enemy.mid",
      "active": true,
      "tags": ["enemy"],
      "transform": { "position": [3.0, 0.0, 0.0] },
      "properties": {
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 },
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
      }
    },
    {
      "name": "entity.enemy.outside",
      "active": true,
      "tags": ["enemy"],
      "transform": { "position": [6.0, 0.0, 0.0] },
      "properties": {
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    },
    {
      "name": "entity.ally",
      "active": true,
      "tags": ["ally"],
      "transform": { "position": [1.0, 0.0, 1.0] },
      "properties": {
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    }
  ],
  "signals": ["signal.blast"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.blast",
        "actions": [
          {
            "type": "effect.explosion",
            "source": "entity.player",
            "radius": 5.0,
            "inner_radius": 1.0,
            "damage": 40.0,
            "damage_type": "blast",
            "target_tag": "enemy",
            "impulse": 2.0,
            "actions": [
              {
                "type": "property.set",
                "target_from_payload": "actor_name",
                "key": "last_explosion_falloff",
                "value_from_payload": "falloff"
              }
            ]
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": [
    "entity.player",
    "entity.enemy.near",
    "entity.enemy.mid",
    "entity.enemy.outside",
    "entity.ally"
  ]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "explosion.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    ASSERT_GE(slayer3d_game_data_find_signal(runtime, "signal.blast"), 0);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.blast"), nullptr);

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *near_enemy = slayer3d_game_data_find_actor(runtime, "entity.enemy.near");
    slayer3d_registered_actor *mid_enemy = slayer3d_game_data_find_actor(runtime, "entity.enemy.mid");
    slayer3d_registered_actor *outside_enemy = slayer3d_game_data_find_actor(runtime, "entity.enemy.outside");
    slayer3d_registered_actor *ally = slayer3d_game_data_find_actor(runtime, "entity.ally");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(near_enemy, nullptr);
    ASSERT_NE(mid_enemy, nullptr);
    ASSERT_NE(outside_enemy, nullptr);
    ASSERT_NE(ally, nullptr);

    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 100.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(near_enemy->props, "health", 0.0f), 60.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(mid_enemy->props, "health", 0.0f), 80.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(outside_enemy->props, "health", 0.0f), 100.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(ally->props, "health", 0.0f), 100.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(near_enemy->props, "last_explosion_falloff", 0.0f), 1.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(mid_enemy->props, "last_explosion_falloff", 0.0f), 0.5f, 0.001f);
    const slayer3d_vec3 near_velocity =
        slayer3d_properties_get_vec3(near_enemy->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const slayer3d_vec3 mid_velocity =
        slayer3d_properties_get_vec3(mid_enemy->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(near_velocity.x, 2.0f, 0.001f);
    EXPECT_NEAR(mid_velocity.x, 1.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, TargetFiltersApplyFactionAndOwnerRules)
{
    const std::filesystem::path dir = unique_test_dir("target_filters");
    write_text(dir / "target_filters.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Target Filters", "id": "test.target_filters", "version": "0.1.0" },
  "world": { "name": "world.target_filters", "kind": "fixed_screen" },
  "factions": {
    "player": { "player": "friendly", "monster": "hostile", "neutral": "neutral" },
    "monster": { "player": "hostile", "monster": "friendly", "neutral": "neutral" },
    "neutral": { "player": "neutral", "monster": "neutral", "neutral": "friendly" }
  },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "tags": ["combatant"],
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "faction": { "type": "string", "value": "player" },
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    },
    {
      "name": "entity.projectile",
      "active": true,
      "tags": ["projectile"],
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "faction": { "type": "string", "value": "player" },
        "owner": { "type": "string", "value": "entity.player" }
      }
    },
    {
      "name": "entity.enemy",
      "active": true,
      "tags": ["combatant"],
      "transform": { "position": [1.0, 0.0, 0.0] },
      "properties": {
        "faction": { "type": "string", "value": "monster" },
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    },
    {
      "name": "entity.ally",
      "active": true,
      "tags": ["combatant"],
      "transform": { "position": [1.0, 0.0, 1.0] },
      "properties": {
        "faction": { "type": "string", "value": "player" },
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    },
    {
      "name": "entity.neutral",
      "active": true,
      "tags": ["combatant"],
      "transform": { "position": [1.0, 0.0, -1.0] },
      "properties": {
        "faction": { "type": "string", "value": "neutral" },
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    }
  ],
  "signals": ["signal.blast", "signal.filtered_damage"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.blast",
        "actions": [
          {
            "type": "effect.explosion",
            "source": "entity.projectile",
            "radius": 3.0,
            "falloff": "constant",
            "damage": 20.0,
            "target_filter": {
              "relationship": "hostile",
              "include_tags": ["combatant"],
              "exclude_owner": true,
              "exclude_source": true
            }
          }
        ]
      },
      {
        "signal": "signal.filtered_damage",
        "actions": [
          {
            "type": "combat.damage",
            "source": "entity.player",
            "target": "entity.ally",
            "amount": 20.0,
            "target_filter": { "relationship": "hostile" }
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player", "entity.projectile", "entity.enemy", "entity.ally", "entity.neutral"]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "target_filters.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.blast"), nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.filtered_damage"), nullptr);

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *enemy = slayer3d_game_data_find_actor(runtime, "entity.enemy");
    slayer3d_registered_actor *ally = slayer3d_game_data_find_actor(runtime, "entity.ally");
    slayer3d_registered_actor *neutral = slayer3d_game_data_find_actor(runtime, "entity.neutral");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(enemy, nullptr);
    ASSERT_NE(ally, nullptr);
    ASSERT_NE(neutral, nullptr);

    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 100.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(enemy->props, "health", 0.0f), 80.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(ally->props, "health", 0.0f), 100.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(neutral->props, "health", 0.0f), 100.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidFactionAndTargetFilterData)
{
    const std::filesystem::path dir = unique_test_dir("invalid_target_filters");
    write_text(dir / "invalid_target_filters.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Target Filters", "id": "test.invalid_target_filters", "version": "0.1.0" },
  "world": { "name": "world.invalid_target_filters", "kind": "fixed_screen" },
  "factions": {
    "player": { "monster": "very_hostile" }
  },
  "entities": [{ "name": "entity.actor", "active": true }],
  "signals": ["signal.test"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.test",
        "actions": [
          {
            "type": "effect.explosion",
            "source": "entity.actor",
            "radius": 1.0,
            "target_filter": { "relationship": "hostile" }
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.actor"] })json");

    char error[512]{};
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "invalid_target_filters.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("faction relationships"), std::string::npos) << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EffectExplosionSupportsBoundedChainReactions)
{
    const std::filesystem::path dir = unique_test_dir("effect_explosion_chain");
    write_text(dir / "explosion_chain.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Explosion Chain", "id": "test.effect_explosion_chain", "version": "0.1.0" },
  "world": { "name": "world.explosion_chain", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.player", "active": true, "transform": { "position": [0.0, 0.0, 0.0] } },
    {
      "name": "entity.barrel",
      "active": true,
      "tags": ["explosive"],
      "transform": { "position": [1.0, 0.0, 0.0] },
      "properties": { "health": { "type": "float", "value": 100.0 } }
    },
    {
      "name": "entity.enemy",
      "active": true,
      "tags": ["enemy"],
      "transform": { "position": [3.0, 0.0, 0.0] },
      "properties": {
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    }
  ],
  "signals": ["signal.blast"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.blast",
        "actions": [
          {
            "type": "effect.explosion",
            "source": "entity.player",
            "radius": 2.0,
            "target_tag": "explosive",
            "max_targets": 1,
            "actions": [
              {
                "type": "effect.explosion",
                "from_payload": "actor_name",
                "radius": 3.0,
                "inner_radius": 3.0,
                "damage": 25.0,
                "target_tag": "enemy",
                "damage_type": "chain_blast"
              }
            ]
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player", "entity.barrel", "entity.enemy"]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "explosion_chain.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.blast"), nullptr);

    slayer3d_registered_actor *barrel = slayer3d_game_data_find_actor(runtime, "entity.barrel");
    slayer3d_registered_actor *enemy = slayer3d_game_data_find_actor(runtime, "entity.enemy");
    ASSERT_NE(barrel, nullptr);
    ASSERT_NE(enemy, nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(barrel->props, "health", 0.0f), 100.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(enemy->props, "health", 0.0f), 75.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidExplosionActions)
{
    const std::filesystem::path dir = unique_test_dir("invalid_explosion_action");
    write_text(dir / "invalid_explosion.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Explosion", "id": "test.invalid_explosion", "version": "0.1.0" },
  "world": { "name": "world.invalid_explosion", "kind": "fixed_screen" },
  "entities": [{ "name": "entity.actor" }],
  "signals": ["signal.blast"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.blast",
        "actions": [
          { "type": "effect.explosion", "source": "entity.actor", "radius": -1.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.actor"] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_explosion.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("effect.explosion radius must be a non-negative number"), std::string::npos)
        << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ResourcesPickupsStationsAndStatusEffectsAreDataDriven)
{
    const std::filesystem::path dir = unique_test_dir("resources_pickups_status");
    write_text(dir / "resources.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Resources", "id": "test.resources", "version": "0.1.0" },
  "world": { "name": "world.resources", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "properties": {
        "health": { "type": "float", "value": 40.0 },
        "max_health": { "type": "float", "value": 100.0 },
        "ammo": { "type": "int", "value": 2 },
        "max_ammo": { "type": "int", "value": 8 },
        "quad_damage": { "type": "bool", "value": false },
        "quad_damage_remaining": { "type": "float", "value": 0.0 },
        "quad_damage_active": { "type": "bool", "value": false }
      },
      "components": [
        {
          "type": "status_effect.timer",
          "property": "quad_damage",
          "duration_property": "quad_damage_remaining",
          "active_property": "quad_damage_active",
          "expired_value": false
        }
      ]
    },
    {
      "name": "entity.health_pickup",
      "active": true,
      "properties": {
        "pickup_available": { "type": "bool", "value": true },
        "pickup_respawn_remaining": { "type": "float", "value": 0.0 }
      },
      "components": [
        { "type": "pickup.respawn" }
      ]
    },
    {
      "name": "entity.heal_station",
      "active": true,
      "properties": {
        "charges": { "type": "int", "value": 2 },
        "cooldown": { "type": "float", "value": 0.0 }
      },
      "components": [
        { "type": "property.decay", "property": "cooldown", "rate": 10.0, "target": 0.0, "min": 0.0 }
      ]
    }
  ],
  "signals": [
    "signal.resource.add",
    "signal.resource.consume",
    "signal.resource.consume_fail",
    "signal.pickup.collect",
    "signal.station.use",
    "signal.status.apply"
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.resource.add",
        "actions": [
          { "type": "resource.add", "target": "entity.player", "resource": "ammo", "amount": 12.0 }
        ]
      },
      {
        "signal": "signal.resource.consume",
        "actions": [
          { "type": "resource.consume", "target": "entity.player", "resource": "ammo", "amount": 3.0 }
        ]
      },
      {
        "signal": "signal.resource.consume_fail",
        "actions": [
          { "type": "resource.consume", "target": "entity.player", "resource": "ammo", "amount": 99.0 }
        ]
      },
      {
        "signal": "signal.pickup.collect",
        "actions": [
          {
            "type": "pickup.collect",
            "target": "entity.player",
            "pickup": "entity.health_pickup",
            "respawn_seconds": 0.5,
            "resources": [
              { "resource": "health", "amount": 25.0 },
              { "resource": "ammo", "amount": 2.0 }
            ]
          }
        ]
      },
      {
        "signal": "signal.station.use",
        "actions": [
          {
            "type": "resource.station.use",
            "target": "entity.player",
            "station": "entity.heal_station",
            "cooldown": 0.5,
            "resources": [
              { "resource": "health", "amount": 30.0 }
            ]
          }
        ]
      },
      {
        "signal": "signal.status.apply",
        "actions": [
          {
            "type": "status_effect.apply",
            "target": "entity.player",
            "property": "quad_damage",
            "value": true,
            "duration": 0.25,
            "duration_property": "quad_damage_remaining",
            "active_property": "quad_damage_active"
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player", "entity.health_pickup", "entity.heal_station"]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "resources.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *pickup = slayer3d_game_data_find_actor(runtime, "entity.health_pickup");
    slayer3d_registered_actor *station = slayer3d_game_data_find_actor(runtime, "entity.heal_station");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(pickup, nullptr);
    ASSERT_NE(station, nullptr);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.resource.add"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "ammo", 0), 8);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.resource.consume"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "ammo", 0), 5);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.resource.consume_fail"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "ammo", 0), 5);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.pickup.collect"), nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 65.0f, 0.001f);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "ammo", 0), 7);
    EXPECT_FALSE(pickup->active);
    EXPECT_FALSE(slayer3d_properties_get_bool(pickup->props, "pickup_available", true));
    EXPECT_NEAR(slayer3d_properties_get_float(pickup->props, "pickup_respawn_remaining", 0.0f), 0.5f, 0.001f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_FALSE(pickup->active);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_TRUE(pickup->active);
    EXPECT_TRUE(slayer3d_properties_get_bool(pickup->props, "pickup_available", false));

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.station.use"), nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 95.0f, 0.001f);
    EXPECT_EQ(slayer3d_properties_get_int(station->props, "charges", 0), 1);
    EXPECT_NEAR(slayer3d_properties_get_float(station->props, "cooldown", 0.0f), 0.5f, 0.001f);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.station.use"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(station->props, "charges", 0), 1);
    ASSERT_TRUE(slayer3d_game_data_update_property_effects(runtime, 0.05f));
    EXPECT_NEAR(slayer3d_properties_get_float(station->props, "cooldown", 1.0f), 0.0f, 0.001f);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.station.use"), nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 100.0f, 0.001f);
    EXPECT_EQ(slayer3d_properties_get_int(station->props, "charges", -1), 0);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.status.apply"), nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "quad_damage", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "quad_damage_active", false));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "quad_damage_remaining", 0.0f), 0.25f, 0.001f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_FALSE(slayer3d_properties_get_bool(player->props, "quad_damage", true));
    EXPECT_FALSE(slayer3d_properties_get_bool(player->props, "quad_damage_active", true));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "quad_damage_remaining", 1.0f), 0.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidResourcePickupAndStatusActions)
{
    const std::filesystem::path dir = unique_test_dir("invalid_resources");
    write_text(dir / "invalid_resources.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Resources", "id": "test.invalid_resources", "version": "0.1.0" },
  "world": { "name": "world.invalid_resources", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.player" },
    { "name": "entity.pickup" }
  ],
  "signals": ["signal.invalid"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.invalid",
        "actions": [
          { "type": "resource.add", "target": "entity.player", "resource": "ammo", "amount": -1.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(
        dir / "scenes" / "play.scene.json",
        R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.player", "entity.pickup"] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_resources.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("resource.add amount must be a non-negative number"), std::string::npos) << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "invalid_pickup.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Pickup", "id": "test.invalid_pickup", "version": "0.1.0" },
  "world": { "name": "world.invalid_pickup", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.player" },
    { "name": "entity.pickup" }
  ],
  "signals": ["signal.invalid"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.invalid",
        "actions": [
          { "type": "pickup.collect", "target": "entity.player", "pickup": "entity.pickup", "resources": [] }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_pickup.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("pickup.collect requires a non-empty resources array"), std::string::npos)
        << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "invalid_status.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Status", "id": "test.invalid_status", "version": "0.1.0" },
  "world": { "name": "world.invalid_status", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.player",
      "components": [
        { "type": "status_effect.timer", "property": "haste", "expired_value": [1, 2, 3] }
      ]
    }
  ],
  "signals": ["signal.invalid"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.invalid",
        "actions": [
          { "type": "status_effect.apply", "target": "entity.player", "property": "haste", "value": true, "duration": -1.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_status.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("status_effect.timer expired_value must be scalar"), std::string::npos) << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, WeaponProjectileComponentFiresFromOwningActor)
{
    const std::filesystem::path dir = unique_test_dir("weapon_projectile_component");
    write_text(dir / "weapon_projectile.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Weapon Projectile", "id": "test.weapon_projectile", "version": "0.1.0" },
  "world": { "name": "world.weapon_projectile", "kind": "fixed_screen" },
  "input": {
    "contexts": [
      { "name": "gameplay", "actions": [{ "name": "action.fire" }] }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [1.0, 2.0, 3.0] },
      "properties": {
        "camera_forward": { "type": "vec3", "value": [0.0, 0.0, -1.0] },
        "fire_timer": { "type": "float", "value": 0.0 },
        "fire_cooldown": { "type": "float", "value": 0.25 }
      },
      "components": [
        {
          "type": "weapon.projectile",
          "action": "action.fire",
          "pool": "pool.shots",
          "directional_offset": { "property": "camera_forward", "distance": 0.75 },
          "velocity_from_property": "camera_forward",
          "speed": 12.0,
          "cooldown": 0.25,
          "properties": { "damage": 5 }
        }
      ]
    }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "tags": ["projectile"],
      "properties": {
        "damage": { "type": "int", "value": 1 },
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
      },
      "components": [
        { "type": "render.sphere", "radius": 0.1, "color": [255, 220, 80, 255], "lighting": true }
      ]
    }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 2, "scene": "scene.play" }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(
        dir / "scenes" / "play.scene.json",
        R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "input": { "actions": ["action.fire"] } })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "weapon_projectile.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    const int fire_action = slayer3d_game_data_find_action(runtime, "action.fire");
    ASSERT_GE(fire_action, 0);
    slayer3d_input_set_action_override(input, fire_action, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 10), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));

    slayer3d_registered_actor *shot0 = slayer3d_game_data_find_actor(runtime, "pool.shots.0");
    ASSERT_NE(shot0, nullptr);
    EXPECT_TRUE(shot0->active);
    expect_vec3_near(shot0->position, slayer3d_vec3_make(1.0f, 2.0f, 2.25f));
    expect_vec3_near(slayer3d_properties_get_vec3(shot0->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
                     slayer3d_vec3_make(0.0f, 0.0f, -12.0f));
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 5);

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "fire_timer", 0.0f), 0.25f, 0.0001f);
    ASSERT_NE(slayer3d_input_update(input, 11), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    slayer3d_registered_actor *shot1 = slayer3d_game_data_find_actor(runtime, "pool.shots.1");
    ASSERT_NE(shot1, nullptr);
    EXPECT_FALSE(shot1->active);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, WeaponStateReloadsAndProjectileFireConsumesClips)
{
    const std::filesystem::path dir = unique_test_dir("weapon_state_projectile");
    write_text(dir / "weapon_state.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Weapon State", "id": "test.weapon_state", "version": "0.1.0" },
  "world": { "name": "world.weapon_state", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [1.0, 1.0, 1.0] },
      "properties": {
        "camera_forward": { "type": "vec3", "value": [0.0, 0.0, -1.0] },
        "clip": { "type": "int", "value": 1 },
        "clip_size": { "type": "int", "value": 2 },
        "ammo_reserve": { "type": "int", "value": 3 },
        "reload_timer": { "type": "float", "value": 0.0 },
        "reload_pending": { "type": "bool", "value": false },
        "fire_timer": { "type": "float", "value": 0.0 }
      },
      "components": [
        {
          "type": "weapon.state",
          "clip_property": "clip",
          "clip_size_property": "clip_size",
          "reserve_property": "ammo_reserve",
          "reload_timer_property": "reload_timer",
          "reload_pending_property": "reload_pending",
          "cooldown_property": "fire_timer",
          "cooldown_rate": 10.0
        }
      ]
    }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "properties": {
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
      }
    }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 2, "scene": "scene.play" }
  ],
  "signals": ["signal.fire", "signal.reload"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "projectile.fire",
            "target": "entity.player",
            "pool": "pool.shots",
            "velocity_from_property": "camera_forward",
            "speed": 10.0,
            "clip_property": "clip",
            "reload_timer_property": "reload_timer",
            "cooldown": 0.1
          }
        ]
      },
      {
        "signal": "signal.reload",
        "actions": [
          {
            "type": "weapon.reload",
            "target": "entity.player",
            "clip_property": "clip",
            "clip_size_property": "clip_size",
            "reserve_property": "ammo_reserve",
            "reload_timer_property": "reload_timer",
            "reload_pending_property": "reload_pending",
            "reload_seconds": 0.2
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(
        dir / "scenes" / "play.scene.json",
        R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "updates_game": true, "entities": ["entity.player"] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "weapon_state.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    const int fire = slayer3d_game_data_find_signal(runtime, "signal.fire");
    const int reload = slayer3d_game_data_find_signal(runtime, "signal.reload");
    ASSERT_GE(fire, 0);
    ASSERT_GE(reload, 0);
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *shot0 = slayer3d_game_data_find_actor(runtime, "pool.shots.0");
    slayer3d_registered_actor *shot1 = slayer3d_game_data_find_actor(runtime, "pool.shots.1");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(shot0, nullptr);
    ASSERT_NE(shot1, nullptr);

    slayer3d_signal_emit(bus, fire, nullptr);
    EXPECT_TRUE(shot0->active);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "clip", -1), 0);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "fire_timer", 0.0f), 0.1f, 0.0001f);

    slayer3d_signal_emit(bus, fire, nullptr);
    EXPECT_FALSE(shot1->active);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "fire_timer", 1.0f), 0.0f, 0.0001f);
    slayer3d_signal_emit(bus, fire, nullptr);
    EXPECT_FALSE(shot1->active);

    slayer3d_signal_emit(bus, reload, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "reload_pending", false));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "reload_timer", 0.0f), 0.2f, 0.0001f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "clip", -1), 0);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    EXPECT_FALSE(slayer3d_properties_get_bool(player->props, "reload_pending", true));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "clip", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "ammo_reserve", -1), 1);

    slayer3d_signal_emit(bus, fire, nullptr);
    EXPECT_TRUE(shot1->active);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "clip", -1), 1);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, WeaponHitscanTracesSectorAndRunsImpactActions)
{
    const std::filesystem::path dir = unique_test_dir("weapon_hitscan");
    write_text(dir / "weapon_hitscan.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Weapon Hitscan", "id": "test.weapon_hitscan", "version": "0.1.0" },
  "world": { "name": "world.weapon_hitscan", "kind": "sector" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [2.0, 1.5, 8.0] },
      "properties": {
        "camera_forward": { "type": "vec3", "value": [0.0, 0.0, -1.0] },
        "energy": { "type": "int", "value": 1 },
        "fire_timer": { "type": "float", "value": 0.0 }
      }
    },
    {
      "name": "entity.enemy",
      "active": true,
      "tags": ["enemy"],
      "transform": { "position": [2.0, 1.5, 4.0] },
      "properties": {
        "hit_radius": { "type": "float", "value": 0.5 },
        "health": { "type": "float", "value": 50.0 },
        "max_health": { "type": "float", "value": 50.0 },
        "alive": { "type": "bool", "value": true }
      },
      "components": [{ "type": "combat.health" }]
    }
  ],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "weapon.hitscan",
            "target": "entity.player",
            "sector_level": "sector.test",
            "target_tag": "enemy",
            "range": 20.0,
            "ammo_resource": "energy",
            "cooldown": 0.0,
            "actions": [
              {
                "type": "combat.damage",
                "target_from_payload": "actor_name",
                "source_from_payload": "source_actor_name",
                "amount": 15.0,
                "damage_type": "hitscan"
              },
              {
                "type": "property.set",
                "target": "entity.player",
                "key": "last_hit_distance",
                "value_from_payload": "hit_distance"
              }
            ]
          }
        ]
      }
    ]
  },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "wall" }],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 10], [0, 10]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "wall_material": "wall"
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player", "entity.enemy"],
  "world": { "sector_levels": [{ "level": "sector.test", "variant": "unlit" }] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "weapon_hitscan.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    const int fire = slayer3d_game_data_find_signal(runtime, "signal.fire");
    ASSERT_GE(fire, 0);
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *enemy = slayer3d_game_data_find_actor(runtime, "entity.enemy");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(enemy, nullptr);

    slayer3d_signal_emit(bus, fire, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "energy", -1), 0);
    EXPECT_NEAR(slayer3d_properties_get_float(enemy->props, "health", 0.0f), 35.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "last_hit_distance", 0.0f), 3.5f, 0.251f);

    slayer3d_signal_emit(bus, fire, nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(enemy->props, "health", 0.0f), 35.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, WeaponHitscanTracesActiveBrushWorlds)
{
    const std::filesystem::path dir = unique_test_dir("weapon_hitscan_brush");
    write_text(dir / "weapon_hitscan_brush.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Weapon Brush Hitscan", "id": "test.weapon_hitscan_brush", "version": "0.1.0" },
  "world": { "name": "world.weapon_hitscan_brush", "kind": "brush" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [0.0, 1.5, 2.0] },
      "properties": {
        "camera_forward": { "type": "vec3", "value": [0.0, 0.0, -1.0] },
        "energy": { "type": "int", "value": 1 },
        "fire_timer": { "type": "float", "value": 0.0 }
      }
    }
  ],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "weapon.hitscan",
            "target": "entity.player",
            "trace_brush_worlds": true,
            "brush_contents_mask": ["solid", "projectile_clip"],
            "range": 20.0,
            "ammo_resource": "energy",
            "cooldown": 0.0,
            "run_actions_on_miss": false,
            "actions": [
              {
                "type": "property.set",
                "target": "entity.player",
                "key": "last_hit_brush",
                "value_from_payload": "hit_brush_name"
              },
              {
                "type": "property.set",
                "target": "entity.player",
                "key": "last_hit_material",
                "value_from_payload": "hit_material"
              },
              {
                "type": "property.set",
                "target": "entity.player",
                "key": "last_hit_distance",
                "value_from_payload": "hit_distance"
              },
              {
                "type": "property.set",
                "target": "entity.player",
                "key": "last_hit_brush_flag",
                "value_from_payload": "hit_brush"
              }
            ]
          }
        ]
      }
    ]
  },
  "brush_worlds": [
    {
      "name": "brush.hitscan",
      "materials": [{ "name": "mat.wall", "albedo": [0.3, 0.3, 0.3, 1.0] }],
      "brushes": [
        {
          "name": "brush.target_wall",
          "contents": ["solid", "projectile_clip"],
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 2.0 }, "material": "mat.wall" },
            { "plane": { "normal": [-1, 0, 0], "distance": 2.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 1, 0], "distance": 3.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0, 1], "distance": 0.2 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0, -1], "distance": 0.2 }, "material": "mat.wall" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player"],
  "world": { "brush_worlds": [{ "world": "brush.hitscan" }] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "weapon_hitscan_brush.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    const int fire = slayer3d_game_data_find_signal(runtime, "signal.fire");
    ASSERT_GE(fire, 0);
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);

    slayer3d_signal_emit(bus, fire, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "energy", -1), 0);
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "last_hit_brush", ""), "brush.target_wall");
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "last_hit_material", ""), "mat.wall");
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "last_hit_brush_flag", false));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "last_hit_distance", 0.0f), 1.8f, 0.01f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidWeaponPrimitives)
{
    const std::filesystem::path dir = unique_test_dir("invalid_weapon_primitives");
    write_text(dir / "invalid_weapon.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Weapon", "id": "test.invalid_weapon", "version": "0.1.0" },
  "world": { "name": "world.invalid_weapon", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.player",
      "components": [
        { "type": "weapon.state", "clip_size": -1.0 }
      ]
    }
  ],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          { "type": "weapon.hitscan", "target": "entity.player", "range": -1.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.player"] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_weapon.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("weapon.state numeric values must be non-negative"), std::string::npos) << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "invalid_hitscan.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Hitscan", "id": "test.invalid_hitscan", "version": "0.1.0" },
  "world": { "name": "world.invalid_hitscan", "kind": "fixed_screen" },
  "entities": [{ "name": "entity.player" }],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          { "type": "weapon.hitscan", "target": "entity.player", "range": -1.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_hitscan.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("weapon.hitscan range and hit_radius must be non-negative"), std::string::npos)
        << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "invalid_hitscan_brush_mask.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Hitscan Brush Mask", "id": "test.invalid_hitscan_brush_mask", "version": "0.1.0" },
  "world": { "name": "world.invalid_hitscan_brush_mask", "kind": "brush" },
  "entities": [{ "name": "entity.player" }],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "weapon.hitscan",
            "target": "entity.player",
            "trace_brush_worlds": true,
            "brush_contents_mask": ["solid", "missing_content"]
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_hitscan_brush_mask.game.json").string().c_str(), session,
                                              &runtime, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("brush content value is unknown"), std::string::npos) << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, GenericInteractionUseRunsLockedCooldownAndSuccessActions)
{
    const std::filesystem::path dir = unique_test_dir("generic_interaction_use");
    write_text(dir / "interaction.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Interaction", "id": "test.interaction", "version": "0.1.0" },
  "world": { "name": "world.interaction", "kind": "sector" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "keycard": { "type": "int", "value": 0 },
        "locked_feedback": { "type": "bool", "value": false },
        "cooldown_feedback": { "type": "bool", "value": false }
      }
    },
    {
      "name": "entity.switch",
      "active": true,
      "tags": ["usable"],
      "transform": { "position": [0.0, 0.0, -1.0] },
      "properties": {
        "use_count": { "type": "int", "value": 0 },
        "interaction_cooldown": { "type": "float", "value": 0.0 }
      },
      "components": [
        {
          "type": "interactable",
          "prompt_key": "prompt.open",
          "range": 2.0,
          "min_dot": 0.5,
          "cooldown_property": "interaction_cooldown",
          "cooldown": 0.25,
          "requires": { "property": "keycard", "amount": 1.0 },
          "actions": [
            { "type": "property.add", "target": "entity.switch", "key": "use_count", "value": 1 }
          ],
          "locked_actions": [
            { "type": "property.set", "target": "entity.player", "key": "locked_feedback", "value": true }
          ],
          "cooldown_actions": [
            { "type": "property.set", "target": "entity.player", "key": "cooldown_feedback", "value": true }
          ]
        }
      ]
    }
  ],
  "signals": ["signal.use"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.use",
        "actions": [
          { "type": "interaction.use", "actor": "entity.player", "target_tag": "usable" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player", "entity.switch"]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "interaction.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    const int use = slayer3d_game_data_find_signal(runtime, "signal.use");
    ASSERT_GE(use, 0);
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *sw = slayer3d_game_data_find_actor(runtime, "entity.switch");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(sw, nullptr);

    slayer3d_signal_emit(bus, use, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "locked_feedback", false));
    EXPECT_EQ(slayer3d_properties_get_int(sw->props, "use_count", -1), 0);

    slayer3d_properties_set_bool(player->props, "locked_feedback", false);
    slayer3d_properties_set_int(player->props, "keycard", 1);
    slayer3d_signal_emit(bus, use, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(sw->props, "use_count", -1), 1);
    EXPECT_NEAR(slayer3d_properties_get_float(sw->props, "interaction_cooldown", 0.0f), 0.25f, 0.0001f);

    slayer3d_signal_emit(bus, use, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "cooldown_feedback", false));
    EXPECT_EQ(slayer3d_properties_get_int(sw->props, "use_count", -1), 1);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    slayer3d_signal_emit(bus, use, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(sw->props, "use_count", -1), 2);

    slayer3d_properties_set_float(player->props, "yaw", 3.14159f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    slayer3d_signal_emit(bus, use, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(sw->props, "use_count", -1), 2);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidInteractionPrimitives)
{
    const std::filesystem::path dir = unique_test_dir("invalid_interaction");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.player"] })json");
    write_text(dir / "invalid_interactable.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Interaction", "id": "test.invalid_interaction", "version": "0.1.0" },
  "world": { "name": "world.invalid_interaction", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.player",
      "components": [
        { "type": "interactable", "range": -1.0, "actions": [] }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_interactable.game.json").string().c_str(), session,
                                              &runtime, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("interactable range, min_dot, and cooldown values are invalid"),
              std::string::npos)
        << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);

    write_text(dir / "invalid_use.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Use", "id": "test.invalid_use", "version": "0.1.0" },
  "world": { "name": "world.invalid_use", "kind": "fixed_screen" },
  "entities": [{ "name": "entity.player" }],
  "signals": ["signal.use"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.use",
        "actions": [
          { "type": "interaction.use", "actor": "entity.player", "min_dot": 2.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    runtime = nullptr;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_load_file((dir / "invalid_use.game.json").string().c_str(), session, &runtime,
                                              error, sizeof(error)));
    EXPECT_NE(std::string(error).find("interaction.use range/min_dot are invalid"), std::string::npos) << error;
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ActivePooledActorsRenderAndEmitParticles)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_render");
    write_text(dir / "actor_pool_render.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Actor Pool Render", "id": "test.actor_pool_render", "version": "0.1.0" },
  "world": { "name": "world.actor_pool_render", "kind": "fixed_screen" },
  "actor_archetypes": [
    {
      "name": "archetype.renderable",
      "tags": ["renderable"],
      "components": [
        { "type": "render.cube", "size": [0.5, 0.4, 0.3], "color": [10, 20, 30, 255] },
        { "type": "render.sphere", "radius": 0.2, "rings": 8, "slices": 12, "offset": [0.0, 1.0, 0.0] },
        {
          "type": "particles.emitter",
          "shape": "box",
          "extents": [0.1, 0.2, 0.0],
          "direction": [0.0, 1.0, 0.0],
          "max_particles": 12,
          "emit_rate": 4.0,
          "draw_emissive": [0.2, 0.8, 0.4]
        }
      ]
    }
  ],
  "actor_pools": [
    {
      "name": "pool.renderables",
      "archetype": "archetype.renderable",
      "capacity": 2,
      "scene": "scene.play",
      "initial_active": false,
      "on_exhausted": "fail"
    }
  ],
  "signals": ["signal.spawn.one", "signal.spawn.two"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.spawn.one",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.renderables", "position": [2.0, 2.0, 0.5] }
        ]
      },
      {
        "signal": "signal.spawn.two",
        "actions": [
          { "type": "actor.spawn", "pool": "pool.renderables", "position": [3.0, 2.0, 0.5] }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "actor_pool_render.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    RenderPrimitiveCapture inactive_render{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &inactive_render));
    EXPECT_EQ(inactive_render.cubes, 0);
    EXPECT_EQ(inactive_render.spheres, 0);

    ParticleCapture inactive_particles{};
    ASSERT_TRUE(slayer3d_game_data_for_each_particle_emitter(runtime, capture_particle, &inactive_particles));
    EXPECT_EQ(inactive_particles.count, 0);

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.one"), nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.two"), nullptr);

    RenderPrimitiveCapture active_render{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &active_render));
    EXPECT_EQ(active_render.cubes, 2);
    EXPECT_EQ(active_render.spheres, 2);
    EXPECT_TRUE(active_render.saw_pooled_cube);
    EXPECT_TRUE(active_render.saw_pooled_sphere);

    ParticleCapture active_particles{};
    ASSERT_TRUE(slayer3d_game_data_for_each_particle_emitter(runtime, capture_particle, &active_particles));
    EXPECT_EQ(active_particles.count, 2);
    EXPECT_TRUE(active_particles.saw_pooled_emitter);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EmitsAuthoredMeshPrimitiveDescriptors)
{
    const std::filesystem::path dir = unique_test_dir("mesh_primitives");
    write_text(dir / "mesh_primitives.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Mesh Primitives", "id": "test.mesh_primitives", "version": "0.1.0" },
  "world": { "name": "world.mesh_primitives", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.mesh.cube",
      "active": true,
      "transform": { "position": [1.0, 0.0, 0.0] },
      "components": [
        { "type": "render.mesh_primitive", "primitive": "cube", "size": [1.0, 2.0, 3.0], "draw_mode": "solid_wire", "wire_color": [255, 0, 0, 255] }
      ]
    },
    {
      "name": "entity.mesh.sphere",
      "active": true,
      "components": [
        { "type": "render.mesh_primitive", "primitive": "sphere", "radius": 0.25, "segments": 12, "rings": 6 }
      ]
    },
    {
      "name": "entity.mesh.capsule",
      "active": true,
      "components": [
        { "type": "render.mesh_primitive", "primitive": "capsule", "radius": 0.3, "height": 1.8 }
      ]
    },
    {
      "name": "entity.mesh.cylinder",
      "active": true,
      "components": [
        { "type": "render.mesh_primitive", "primitive": "cylinder", "radius_top": 0.4, "radius_bottom": 0.6, "height": 1.5, "draw_mode": "wire" }
      ]
    },
    {
      "name": "entity.mesh.cone",
      "active": true,
      "components": [
        { "type": "render.mesh_primitive", "primitive": "cone", "radius": 0.7, "height": 1.2, "radius_top": 0.0 }
      ]
    },
    {
      "name": "entity.mesh.torus",
      "active": true,
      "components": [
        { "type": "render.mesh_primitive", "primitive": "torus", "major_radius": 0.8, "minor_radius": 0.15, "segments": 32, "tube_segments": 10 }
      ]
    },
    {
      "name": "entity.mesh.pyramid",
      "active": true,
      "components": [
        { "type": "render.mesh_primitive", "primitive": "pyramid", "size": [1.0, 1.4, 1.0] }
      ]
    },
    {
      "name": "entity.mesh.wedge",
      "active": true,
      "components": [
        { "type": "render.mesh_primitive", "primitive": "wedge", "size": [1.0, 0.5, 1.5], "lighting": false }
      ]
    },
    {
      "name": "entity.mesh.composite",
      "active": true,
      "components": [
        {
          "type": "render.composite",
          "parts": [
            { "primitive": "plane", "size": [1.2, 0.8, 0.05] },
            { "primitive": "disc", "radius": 0.4, "segments": 16 },
            { "primitive": "hemisphere", "radius": 0.45, "segments": 16, "rings": 8 },
            { "primitive": "rounded_box", "size": [0.9, 0.7, 0.5], "bevel_radius": 0.1, "rings": 4 },
            { "primitive": "pipe", "major_radius": 0.55, "minor_radius": 0.08, "arc_angle": 1.5707964, "segments": 12, "tube_segments": 8 },
            { "primitive": "arrow", "radius": 0.2, "height": 1.0, "segments": 12 },
            { "primitive": "billboard_plane", "size": [0.8, 0.8, 0.05] }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "mesh_primitives.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    struct MeshCapture
    {
        int count = 0;
        bool seen[SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE + 1] = {};
    } capture;
    auto capture_mesh = [](void *userdata, const slayer3d_game_data_render_primitive *primitive) -> bool {
        auto *mesh_capture = static_cast<MeshCapture *>(userdata);
        if (primitive->type != SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
            return true;
        mesh_capture->count++;
        EXPECT_GT(primitive->mesh_primitive, SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID);
        EXPECT_LE(primitive->mesh_primitive, SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE);
        if (primitive->mesh_primitive <= SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID ||
            primitive->mesh_primitive > SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE)
        {
            return false;
        }
        mesh_capture->seen[primitive->mesh_primitive] = true;
        EXPECT_TRUE(primitive->lighting_enabled ||
                    primitive->mesh_primitive == SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE);
        if (std::string(primitive->entity_name) == "entity.mesh.cube")
        {
            EXPECT_EQ(primitive->draw_mode, SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID_WIRE);
            EXPECT_NEAR(primitive->size.y, 2.0f, 0.0001f);
            EXPECT_EQ(primitive->wire_color.r, 255);
        }
        if (std::string(primitive->entity_name) == "entity.mesh.cylinder")
        {
            EXPECT_EQ(primitive->draw_mode, SLAYER3D_GAME_DATA_RENDER_DRAW_WIRE);
            EXPECT_NEAR(primitive->radius_top, 0.4f, 0.0001f);
            EXPECT_NEAR(primitive->radius_bottom, 0.6f, 0.0001f);
            EXPECT_NEAR(primitive->height, 1.5f, 0.0001f);
        }
        if (std::string(primitive->entity_name) == "entity.mesh.cone")
        {
            EXPECT_NEAR(primitive->radius_top, 0.0f, 0.0001f);
            EXPECT_NEAR(primitive->radius_bottom, 0.7f, 0.0001f);
        }
        if (std::string(primitive->entity_name) == "entity.mesh.torus")
        {
            EXPECT_NEAR(primitive->major_radius, 0.8f, 0.0001f);
            EXPECT_NEAR(primitive->minor_radius, 0.15f, 0.0001f);
            EXPECT_EQ(primitive->tube_segments, 10);
        }
        return true;
    };

    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_mesh, &capture));
    EXPECT_EQ(capture.count, 15);
    for (int kind = SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE; kind <= SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE;
         ++kind)
        EXPECT_TRUE(capture.seen[kind]) << kind;

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataValidation, RejectsInvalidMeshPrimitiveComponents)
{
    const std::filesystem::path dir = unique_test_dir("invalid_mesh_primitive");
    write_text(dir / "bad_kind.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Mesh Primitive", "id": "test.bad_mesh_primitive", "version": "0.1.0" },
  "world": { "name": "world.bad_mesh_primitive", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.bad",
      "components": [
        { "type": "render.mesh_primitive", "primitive": "octahedron" }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "bad_segments.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Mesh Primitive", "id": "test.bad_mesh_primitive", "version": "0.1.0" },
  "world": { "name": "world.bad_mesh_primitive", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.bad",
      "components": [
        { "type": "render.mesh_primitive", "primitive": "torus", "segments": 2 }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "bad_draw_mode.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Mesh Primitive", "id": "test.bad_mesh_primitive", "version": "0.1.0" },
  "world": { "name": "world.bad_mesh_primitive", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.bad",
      "components": [
        { "type": "render.mesh_primitive", "primitive": "cube", "draw_mode": "xray" }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "bad_camera_visibility.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Mesh Primitive", "id": "test.bad_mesh_primitive", "version": "0.1.0" },
  "world": {
    "name": "world.bad_mesh_primitive",
    "kind": "fixed_screen",
    "cameras": [
      { "name": "camera.main", "type": "perspective", "position": [0.0, 2.0, 5.0], "target": [0.0, 1.0, 0.0] }
    ]
  },
  "entities": [
    {
      "name": "entity.bad",
      "components": [
        { "type": "render.mesh_primitive", "primitive": "capsule", "visible_to_cameras": ["camera.missing"] }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    char error[512]{};
    EXPECT_FALSE(
        slayer3d_game_data_validate_file((dir / "bad_kind.game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("render.mesh_primitive primitive is unknown"), std::string::npos) << error;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_segments.game.json").string().c_str(), nullptr, error,
                                                  sizeof(error)));
    EXPECT_NE(std::string(error).find("render.mesh_primitive tessellation values must be integers >= 3"),
              std::string::npos)
        << error;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_draw_mode.game.json").string().c_str(), nullptr, error,
                                                  sizeof(error)));
    EXPECT_NE(std::string(error).find("render.mesh_primitive draw_mode is unknown"), std::string::npos) << error;
    SDL_zeroa(error);
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_camera_visibility.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("unknown camera reference 'camera.missing'"), std::string::npos) << error;

    remove_test_dir(dir);
}

TEST(GameDataRuntime, ActorPoolsApplySceneExitPolicies)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_scene_policy");
    write_text(dir / "actor_pool_scene_policy.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": [] })json");
    write_text(dir / "scenes" / "shop.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.shop", "entities": [] })json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.title", "entities": [] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "actor_pool_scene_policy.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session),
                         slayer3d_game_data_find_signal(runtime, "signal.spawn"), nullptr);
    slayer3d_registered_actor *reset = slayer3d_game_data_find_actor(runtime, "pool.reset_shots.0");
    slayer3d_registered_actor *preserved = slayer3d_game_data_find_actor(runtime, "pool.preserved_shots.0");
    slayer3d_registered_actor *shared = slayer3d_game_data_find_actor(runtime, "pool.shared_shots.0");
    slayer3d_registered_actor *despawned = slayer3d_game_data_find_actor(runtime, "pool.despawned_shots.0");
    ASSERT_NE(reset, nullptr);
    ASSERT_NE(preserved, nullptr);
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(despawned, nullptr);
    EXPECT_TRUE(reset->active);
    EXPECT_TRUE(preserved->active);
    EXPECT_TRUE(shared->active);
    EXPECT_TRUE(despawned->active);
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "pool.shared_shots.0"));

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.shop"));
    EXPECT_FALSE(reset->active);
    EXPECT_EQ(slayer3d_properties_get_int(reset->props, "damage", 0), 1);
    expect_vec3_near(reset->position, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(preserved->active);
    EXPECT_EQ(slayer3d_properties_get_int(preserved->props, "damage", 0), 9);
    expect_vec3_near(preserved->position, slayer3d_vec3_make(2.0f, 0.0f, 0.0f));
    EXPECT_TRUE(shared->active);
    EXPECT_EQ(slayer3d_properties_get_int(shared->props, "damage", 0), 11);
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "pool.shared_shots.0"));
    EXPECT_FALSE(despawned->active);
    EXPECT_EQ(slayer3d_properties_get_int(despawned->props, "damage", 0), 1);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));
    EXPECT_TRUE(preserved->active);
    EXPECT_EQ(slayer3d_properties_get_int(preserved->props, "damage", 0), 9);
    EXPECT_FALSE(shared->active);
    EXPECT_EQ(slayer3d_properties_get_int(shared->props, "damage", 0), 1);
    EXPECT_FALSE(slayer3d_game_data_active_scene_has_entity(runtime, "pool.shared_shots.0"));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ContactSensorsMatchActivePooledActorTags)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_tag_sensors");
    write_text(dir / "actor_pool_tag_sensors.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": [] })json");
    write_text(dir / "scenes.title.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.title", "entities": [] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "actor_pool_tag_sensors.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    SensorSignalCapture capture{};
    ASSERT_NE(slayer3d_signal_connect(slayer3d_game_session_get_signal_bus(session),
                                      slayer3d_game_data_find_signal(runtime, "signal.hit"), capture_sensor_signal,
                                      &capture),
              0);

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session),
                         slayer3d_game_data_find_signal(runtime, "signal.spawn.first"), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(capture.actor_name, "pool.player_shots.0");
    EXPECT_EQ(capture.other_actor_name, "pool.invaders.0");

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 1);

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session),
                         slayer3d_game_data_find_signal(runtime, "signal.spawn.second"), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 2);
    EXPECT_EQ(capture.actor_name, "pool.player_shots.1");
    EXPECT_EQ(capture.other_actor_name, "pool.invaders.0");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.title"));
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(capture.calls, 2);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ArcadeShooterPrimitivesAreDataDriven)
{
    const std::filesystem::path dir = unique_test_dir("arcade_shooter_primitives");
    write_text(dir / "arcade_shooter_primitives.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Arcade Shooter Primitives", "id": "test.arcade_shooter_primitives", "version": "0.1.0" },
  "world": { "name": "world.arcade_shooter_primitives", "kind": "fixed_screen" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "tags": ["player"],
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "fire_timer": { "type": "float", "value": 0.0 },
        "fire_cooldown": { "type": "float", "value": 0.5 },
        "camera_forward": { "type": "vec3", "value": [1.0, 0.0, 0.0] },
        "half_width": { "type": "float", "value": 0.25 },
        "half_height": { "type": "float", "value": 0.25 }
      },
      "components": [
        { "type": "property.decay", "property": "fire_timer", "rate": 1.0, "target": 0.0, "min": 0.0 },
        { "type": "collision.aabb", "half_extents": [0.25, 0.25, 0.1] }
      ]
    },
    {
      "name": "entity.game",
      "active": true,
      "properties": {
        "score": { "type": "int", "value": 0 },
        "game_over": { "type": "bool", "value": false }
      }
    },
    {
      "name": "entity.parallax",
      "active": true,
      "transform": { "position": [-0.9, 0.0, -0.2] },
      "components": [
        { "type": "motion.scroll_wrap", "axis": "x", "speed": -0.5, "min": -1.0, "max": 1.0 }
      ]
    }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.player_shot",
      "tags": ["projectile", "player_projectile"],
      "properties": {
        "radius": { "type": "float", "value": 0.2 },
        "damage": { "type": "int", "value": 1 },
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
      },
      "components": [
        { "type": "motion.velocity_2d", "property": "velocity" },
        { "type": "light.point", "color": [0.25, 0.85, 1.0], "intensity": 2.5, "range": 2.0 }
      ]
    },
    {
      "name": "archetype.invader",
      "tags": ["threat", "enemy"],
      "properties": {
        "half_width": { "type": "float", "value": 0.25 },
        "half_height": { "type": "float", "value": 0.25 },
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
      },
      "components": [
        { "type": "motion.velocity_2d", "property": "velocity" },
        { "type": "collision.aabb", "half_extents": [0.25, 0.25, 0.1] }
      ]
    },
    {
      "name": "archetype.explosion",
      "tags": ["effect"],
      "properties": {
        "radius": { "type": "float", "value": 0.3 },
        "age": { "type": "float", "value": 0.0 },
        "ttl": { "type": "float", "value": 0.1 },
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 1.0] }
      },
      "components": [
        { "type": "motion.velocity_3d", "property": "velocity" },
        { "type": "lifecycle.ttl", "age_property": "age", "ttl_property": "ttl" },
        { "type": "light.point", "color": [1.0, 0.45, 0.08], "intensity": 4.0, "range": 2.6 },
        { "type": "particles.emitter", "shape": "sphere", "radius": 0.2, "emit_rate": 10.0, "max_particles": 8 }
      ]
    }
  ],
  "actor_pools": [
    { "name": "pool.player_shots", "archetype": "archetype.player_shot", "capacity": 2, "scene": "scene.play" },
    { "name": "pool.invaders", "archetype": "archetype.invader", "capacity": 1, "scene": "scene.play" },
    { "name": "pool.explosions", "archetype": "archetype.explosion", "capacity": 1, "scene": "scene.play" }
  ],
  "signals": ["signal.fire"],
  "logic": {
    "sensors": [
      {
        "name": "sensor.projectile_hits_threat",
        "type": "collision.on_overlap",
        "a_tag": "player_projectile",
        "b_tag": "threat",
        "edge": "enter",
        "actions": [
          { "type": "actor.despawn", "target_from_payload": "actor_name" },
          { "type": "actor.spawn", "pool": "pool.explosions", "from_payload": "other_actor_name" },
          { "type": "actor.despawn", "target_from_payload": "other_actor_name" },
          { "type": "property.add", "target": "entity.game", "key": "score", "value": 10 }
        ]
      }
    ],
    "wave_schedules": [
      {
        "name": "wave.invaders",
        "pool": "pool.invaders",
        "interval": 0.1,
        "initial_delay": 0.0,
        "max_active_tag": "threat",
        "max_active": 1,
        "active_if": { "type": "property.compare", "target": "entity.game", "key": "game_over", "op": "==", "value": false },
        "position": [0.0, 0.0, 0.0],
        "velocity": [0.0, 0.0, 0.0]
      }
    ],
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "projectile.fire",
            "target": "entity.player",
            "pool": "pool.player_shots",
            "velocity_from_property": "camera_forward",
            "speed": 2.0,
            "cooldown_property": "fire_timer"
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.player", "entity.game", "entity.parallax"]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "arcade_shooter_primitives.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *parallax = slayer3d_game_data_find_actor(runtime, "entity.parallax");
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *shot0 = slayer3d_game_data_find_actor(runtime, "pool.player_shots.0");
    slayer3d_registered_actor *shot1 = slayer3d_game_data_find_actor(runtime, "pool.player_shots.1");
    slayer3d_registered_actor *invader = slayer3d_game_data_find_actor(runtime, "pool.invaders.0");
    slayer3d_registered_actor *explosion = slayer3d_game_data_find_actor(runtime, "pool.explosions.0");
    ASSERT_NE(parallax, nullptr);
    ASSERT_NE(player, nullptr);
    ASSERT_NE(shot0, nullptr);
    ASSERT_NE(shot1, nullptr);
    ASSERT_NE(invader, nullptr);
    ASSERT_NE(explosion, nullptr);
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "entity.parallax"));
    EXPECT_TRUE(parallax->active);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.3f));
    EXPECT_TRUE(invader->active);
    expect_vec3_near(parallax->position, slayer3d_vec3_make(1.0f, 0.0f, -0.2f));

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session),
                         slayer3d_game_data_find_signal(runtime, "signal.fire"), nullptr);
    EXPECT_TRUE(shot0->active);
    EXPECT_FALSE(shot1->active);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "fire_timer", 0.0f), 0.5f, 0.0001f);
    expect_vec3_near(slayer3d_properties_get_vec3(shot0->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
                     slayer3d_vec3_make(2.0f, 0.0f, 0.0f));

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session),
                         slayer3d_game_data_find_signal(runtime, "signal.fire"), nullptr);
    EXPECT_FALSE(shot1->active);

    EXPECT_EQ(slayer3d_game_data_world_light_count(runtime), 1);
    slayer3d_light projectile_light{};
    ASSERT_TRUE(slayer3d_game_data_get_world_light(runtime, 0, &projectile_light));
    EXPECT_EQ(projectile_light.type, SLAYER3D_LIGHT_POINT);
    EXPECT_NEAR(projectile_light.position.x, shot0->position.x, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(invader->active);
    EXPECT_TRUE(explosion->active);
    slayer3d_registered_actor *game = slayer3d_game_data_find_actor(runtime, "entity.game");
    ASSERT_NE(game, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(game->props, "score", 0), 10);
    EXPECT_EQ(slayer3d_game_data_world_light_count(runtime), 1);

    ParticleCapture particles{};
    ASSERT_TRUE(slayer3d_game_data_for_each_particle_emitter(runtime, capture_particle, &particles));
    EXPECT_EQ(particles.count, 1);
    const float explosion_z = explosion->position.z;
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.05f));
    EXPECT_TRUE(explosion->active);
    EXPECT_GT(explosion->position.z, explosion_z);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.06f));
    EXPECT_FALSE(explosion->active);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, MazePrimitivesSupportGridMovementLuaQueriesAndGlyphSpawns)
{
    const std::filesystem::path dir = unique_test_dir("maze_primitives");
    write_text(dir / "scripts" / "rules.lua",
               R"lua(
local rules = {}

function rules.inspect(_, _, ctx)
  local world = ctx:grid_cell_to_world("map.maze", 1, 1)
  ctx:state_set("cell_world_x", world and world.x or -99)
  ctx:state_set("cell_world_y", world and world.y or -99)

  local cell = ctx:grid_world_to_cell("map.maze", world)
  ctx:state_set("cell_col", cell and cell.col or -1)
  ctx:state_set("cell_row", cell and cell.row or -1)

  ctx:state_set("wall_tile", ctx:grid_tile("map.maze", 0, 0) or "")
  ctx:state_set("player_walkable", ctx:grid_walkable("map.maze", 1, 1))
  ctx:state_set("wall_walkable", ctx:grid_walkable("map.maze", 0, 0))

  local neighbors = ctx:grid_neighbors("map.maze", 1, 1)
  ctx:state_set("neighbor_count", #neighbors)

  local step = ctx:grid_next_step("map.maze", 1, 3, 3, 1)
  ctx:state_set("path_found", step ~= nil)
  ctx:state_set("path_step_col", step and step.col or -1)
  ctx:state_set("path_step_row", step and step.row or -1)

  local pellet = ctx:grid_actor_at("map.maze", "pool.pellets", 2, 1)
  ctx:state_set("lookup_pellet", pellet and pellet.name or "")
  local pickup = ctx:grid_pickup_at("pickup.collectibles", 3, 1)
  ctx:state_set("pickup_kind", pickup and pickup.kind or "")
  local collected = ctx:grid_collect_at("pickup.collectibles", 3, 1)
  ctx:state_set("collected_kind", collected and collected.kind or "")
  ctx:state_set("pickup_remaining", ctx:grid_pickup_count("pickup.collectibles"))
  return true
end

return rules
)lua");
    write_text(dir / "maze_primitives.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Maze Primitives", "id": "test.maze_primitives", "version": "0.1.0" },
  "world": { "name": "world.maze_primitives", "kind": "fixed_screen" },
  "scripts": [
    { "id": "script.rules", "path": "scripts/rules.lua", "module": "test.maze" }
  ],
  "grid_maps": [
    {
      "name": "map.maze",
      "origin": [0.0, 0.0, 0.0],
      "cell_size": [1.0, 1.0],
      "row_direction": -1.0,
      "walkable": [" ", ".", "o", "P", "G"],
      "rows": [
        "#####",
        "#P.o#",
        "# # #",
        "#G..#",
        "#####"
      ]
    }
  ],
  "grid_pickup_layers": [
    {
      "name": "pickup.collectibles",
      "map": "map.maze",
      "kinds": [
        { "glyph": ".", "kind": "pellet", "points": 10, "z": 0.2, "radius": 0.1, "rings": 4, "slices": 5 },
        { "glyph": "o", "kind": "power", "points": 50, "z": 0.2, "radius": 0.2, "rings": 5, "slices": 6 }
      ]
    }
  ],
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [1.0, -1.0, 0.25] },
      "properties": {
        "grid_col": { "type": "int", "value": 1 },
        "grid_row": { "type": "int", "value": 1 },
        "grid_dir_x": { "type": "int", "value": 1 },
        "grid_dir_y": { "type": "int", "value": 0 },
        "grid_next_dir_x": { "type": "int", "value": 0 },
        "grid_next_dir_y": { "type": "int", "value": 0 },
        "grid_speed": { "type": "float", "value": 4.0 }
      },
      "components": [
        { "type": "motion.grid_agent", "map": "map.maze", "speed": 4.0 }
      ]
    }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.pellet",
      "tags": ["collectible", "pellet"],
      "properties": {
        "points": { "type": "int", "value": 10 },
        "kind": { "type": "string", "value": "pellet" }
      }
    },
    {
      "name": "archetype.power_pellet",
      "tags": ["collectible", "power_pellet"],
      "properties": {
        "points": { "type": "int", "value": 50 },
        "kind": { "type": "string", "value": "power" }
      }
    },
    {
      "name": "archetype.wall",
      "tags": ["wall"],
      "properties": {
        "grid_run_size": { "type": "vec3", "value": [1.0, 1.0, 0.25] }
      },
      "components": [
        { "type": "render.cube", "size_property": "grid_run_size", "color": [10, 20, 30, 255] }
      ]
    }
  ],
  "actor_pools": [
    { "name": "pool.pellets", "archetype": "archetype.pellet", "capacity": 3, "scene": "scene.play" },
    { "name": "pool.power_pellets", "archetype": "archetype.power_pellet", "capacity": 1, "scene": "scene.play" },
    { "name": "pool.walls", "archetype": "archetype.wall", "capacity": 10, "scene": "scene.play" }
  ],
  "signals": ["signal.inspect", "signal.spawn.collectibles", "signal.reset.pickups", "signal.spawn.walls"],
  "adapters": [
    { "name": "adapter.inspect", "kind": "action", "script": "script.rules", "function": "inspect" }
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.inspect",
        "actions": [
          { "type": "adapter.invoke", "adapter": "adapter.inspect" }
        ]
      },
      {
        "signal": "signal.reset.pickups",
        "actions": [
          {
            "type": "grid.pickup_layer.reset",
            "layer": "pickup.collectibles",
            "output_count_key": "pickup_count"
          }
        ]
      },
      {
        "signal": "signal.spawn.collectibles",
        "actions": [
          {
            "type": "grid.spawn_from_glyphs",
            "map": "map.maze",
            "output_count_key": "spawned_collectibles",
            "spawns": [
              { "glyph": ".", "pool": "pool.pellets", "properties": { "kind": "pellet" } },
              { "glyph": "o", "pool": "pool.power_pellets", "properties": { "kind": "power" } }
            ]
          }
        ]
      },
      {
        "signal": "signal.spawn.walls",
        "actions": [
          {
            "type": "grid.spawn_runs_from_glyphs",
            "map": "map.maze",
            "output_count_key": "spawned_wall_runs",
            "axis": "x",
            "depth": 0.3,
            "spawns": [
              { "glyph": "#", "pool": "pool.walls" }
            ]
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.player"] })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "maze_primitives.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.inspect"), nullptr);
    const slayer3d_properties *state = slayer3d_game_data_scene_state(runtime);
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(state, "cell_world_x", -99.0f), 1.0f);
    EXPECT_FLOAT_EQ(slayer3d_properties_get_float(state, "cell_world_y", -99.0f), -1.0f);
    EXPECT_EQ(slayer3d_properties_get_int(state, "cell_col", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(state, "cell_row", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(state, "wall_tile", ""), "#");
    EXPECT_TRUE(slayer3d_properties_get_bool(state, "player_walkable", false));
    EXPECT_FALSE(slayer3d_properties_get_bool(state, "wall_walkable", true));
    EXPECT_EQ(slayer3d_properties_get_int(state, "neighbor_count", 0), 2);
    EXPECT_TRUE(slayer3d_properties_get_bool(state, "path_found", false));
    EXPECT_GE(slayer3d_properties_get_int(state, "path_step_col", -1), 1);
    EXPECT_GE(slayer3d_properties_get_int(state, "path_step_row", -1), 1);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.collectibles"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(state, "spawned_collectibles", 0), 4);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.reset.pickups"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(state, "pickup_count", 0), 4);
    RenderPrimitiveCapture pickup_render{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &pickup_render));
    EXPECT_TRUE(pickup_render.saw_pickup_batch);
    EXPECT_EQ(pickup_render.pickup_batch_instances, 4);
    slayer3d_registered_actor *pellet0 = slayer3d_game_data_find_actor(runtime, "pool.pellets.0");
    slayer3d_registered_actor *pellet2 = slayer3d_game_data_find_actor(runtime, "pool.pellets.2");
    slayer3d_registered_actor *power = slayer3d_game_data_find_actor(runtime, "pool.power_pellets.0");
    ASSERT_NE(pellet0, nullptr);
    ASSERT_NE(pellet2, nullptr);
    ASSERT_NE(power, nullptr);
    EXPECT_TRUE(pellet0->active);
    EXPECT_TRUE(pellet2->active);
    EXPECT_TRUE(power->active);
    EXPECT_EQ(slayer3d_properties_get_int(pellet0->props, "grid_col", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(pellet0->props, "grid_row", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(power->props, "kind", ""), "power");
    expect_vec3_near(power->position, slayer3d_vec3_make(3.0f, -1.0f, 0.0f));
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.inspect"), nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(state, "lookup_pellet", ""), "pool.pellets.0");
    EXPECT_STREQ(slayer3d_properties_get_string(state, "pickup_kind", ""), "power");
    EXPECT_STREQ(slayer3d_properties_get_string(state, "collected_kind", ""), "power");
    EXPECT_EQ(slayer3d_properties_get_int(state, "pickup_remaining", 0), 3);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn.walls"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(state, "spawned_wall_runs", 0), 9);
    slayer3d_registered_actor *wall0 = slayer3d_game_data_find_actor(runtime, "pool.walls.0");
    slayer3d_registered_actor *wall1 = slayer3d_game_data_find_actor(runtime, "pool.walls.1");
    ASSERT_NE(wall0, nullptr);
    ASSERT_NE(wall1, nullptr);
    EXPECT_TRUE(wall0->active);
    EXPECT_EQ(slayer3d_properties_get_int(wall0->props, "grid_run_length", 0), 5);
    EXPECT_STREQ(slayer3d_properties_get_string(wall0->props, "grid_run_axis", ""), "x");
    expect_vec3_near(slayer3d_properties_get_vec3(wall0->props, "grid_run_size", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
                     slayer3d_vec3_make(5.0f, 1.0f, 0.3f));
    expect_vec3_near(wall0->position, slayer3d_vec3_make(2.0f, 0.0f, 0.0f));
    EXPECT_EQ(slayer3d_properties_get_int(wall1->props, "grid_run_length", 0), 1);

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "grid_col", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "grid_row", -1), 1);
    expect_vec3_near(player->position, slayer3d_vec3_make(2.0f, -1.0f, 0.25f));

    slayer3d_properties_set_int(player->props, "grid_next_dir_x", 0);
    slayer3d_properties_set_int(player->props, "grid_next_dir_y", 1);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "grid_col", -1), 3);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "grid_row", -1), 1);

    slayer3d_properties_set_int(player->props, "grid_dir_x", 0);
    slayer3d_properties_set_int(player->props, "grid_dir_y", -1);
    slayer3d_properties_set_int(player->props, "grid_next_dir_x", 0);
    slayer3d_properties_set_int(player->props, "grid_next_dir_y", -1);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "grid_col", -1), 3);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "grid_row", -1), 1);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, LoadsAuthoredSectorLevels)
{
    const std::filesystem::path dir = unique_test_dir("sector_levels");
    write_text(dir / "sector_level.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Level Test" },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [
        { "name": "floor", "albedo": [0.7, 0.7, 0.7, 1.0], "roughness": 0.9, "tex_scale": 2.0 },
        { "name": "wall", "albedo": [0.2, 0.25, 0.35, 1.0], "metallic": 0.1, "roughness": 0.6 }
      ],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "floor_material": "floor",
          "ceil_material": "wall",
          "wall_material": "wall",
          "ambient_sound_id": 2,
          "damage_per_second": 1.5,
          "lighting": { "level": 176, "color": [1.0, 0.2, 0.1, 0.75] }
        },
        {
          "name": "hall",
          "points": [[4, 1], [7, 1], [7, 3], [4, 3]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "floor_material": "floor",
          "ceil_material": -1,
          "wall_material": "wall",
          "push_velocity": [1.0, 0.0, 0.0]
        }
      ],
      "lights": [
        { "position": [2.0, 2.5, 2.0], "color": [1.0, 0.8, 0.6], "intensity": 2.0, "range": 6.0 }
      ]
    }
  ]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_level.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_game_data_sector_level level{};
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.test", &level));
    EXPECT_STREQ(level.name, "sector.test");
    ASSERT_EQ(level.material_count, 2);
    EXPECT_FLOAT_EQ(level.materials[0].tex_scale, 2.0f);
    EXPECT_FLOAT_EQ(level.materials[1].metallic, 0.1f);
    ASSERT_EQ(level.sector_count, 2);
    ASSERT_NE(level.sector_names, nullptr);
    EXPECT_STREQ(level.sector_names[0], "room");
    EXPECT_STREQ(level.sector_names[1], "hall");
    EXPECT_EQ(level.sectors[0].ambient_sound_id, 2);
    EXPECT_FLOAT_EQ(slayer3d_sector_damage_per_second(&level.sectors[0]), 1.5f);
    EXPECT_TRUE(level.sectors[0].has_lighting);
    EXPECT_FLOAT_EQ(level.sectors[0].lighting_level, 176.0f);
    EXPECT_FLOAT_EQ(level.sectors[0].lighting_color[1], 0.2f);
    EXPECT_FLOAT_EQ(level.sectors[0].lighting_color[3], 0.75f);
    EXPECT_FALSE(level.sectors[1].has_lighting);
    expect_vec3_near(slayer3d_sector_push_velocity(&level.sectors[1]), slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
    ASSERT_EQ(level.light_count, 1);
    EXPECT_FLOAT_EQ(level.lights[0].intensity, 2.0f);

    ASSERT_NE(level.lightmapped, nullptr);
    ASSERT_NE(level.vertex_baked, nullptr);
    ASSERT_NE(level.unlit, nullptr);
    EXPECT_EQ(level.lightmapped->sector_count, 2);
    EXPECT_GT(level.lightmapped->model.mesh_count, 0);
    EXPECT_GT(level.lightmapped->portal_count, 0);
    EXPECT_GT(level.lightmapped->lightmap_width, 0);
    EXPECT_GT(level.lightmapped->lightmap_height, 0);
    EXPECT_EQ(level.vertex_baked->lightmap_width, 0);
    EXPECT_EQ(level.vertex_baked->lightmap_height, 0);
    EXPECT_EQ(slayer3d_level_find_sector(level.lightmapped, level.sectors, 1.0f, 1.0f), 0);
    EXPECT_EQ(slayer3d_level_find_sector(level.lightmapped, level.sectors, 5.0f, 2.0f), 1);

    slayer3d_game_data_sector_level missing{};
    EXPECT_FALSE(slayer3d_game_data_get_sector_level(runtime, "sector.missing", &missing));
    EXPECT_EQ(missing.name, nullptr);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, LoadsAuthoredBrushWorlds)
{
    const std::filesystem::path dir = unique_test_dir("brush_worlds");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": {
    "brush_worlds": [
      {
        "world": "brush.test",
        "position": [1.0, 2.0, 3.0],
        "acceleration": false,
        "lighting": false,
        "debug_wireframe": true
      }
    ]
  }
})json");
    write_text(dir / "brush_world.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush World Test" },
  "world": { "name": "world.brush_test", "kind": "brush", "units": "meters", "meters_per_unit": 1.0 },
  "brush_worlds": [
    {
      "name": "brush.test",
      "units": "meters",
      "meters_per_unit": 1.0,
      "editor": {
        "stable_id": "brush_world.test.v1",
        "display_name": "Brush Test World",
        "category": "tests/brush",
        "group": "runtime",
        "tags": ["brush", "runtime"],
        "snap": { "grid": [0.5, 0.25, 0.5], "rotation_degrees": 15.0, "align_to_floor": true }
      },
      "materials": [
        {
          "name": "mat.wall",
          "albedo": [0.25, 0.35, 0.45, 1.0],
          "metallic": 0.0,
          "roughness": 0.8,
          "tex_scale": 2.0,
          "editor": {
            "stable_id": "brush_material.test.wall.v1",
            "display_name": "Test Wall",
            "category": "materials/test",
            "tags": ["wall"]
          }
        },
        {
          "name": "mat.trim",
          "albedo": [0.85, 0.55, 0.20, 1.0],
          "emissive": [0.4, 0.2, 0.05],
          "roughness": 0.7
        }
      ],
      "brushes": [
        {
          "name": "brush.room",
          "tags": ["solid", "room"],
          "contents": ["solid", "player_clip"],
          "editor": {
            "stable_id": "brush.test.room.v1",
            "display_name": "Runtime Test Room",
            "group": "room",
            "prefab": "prefab.test.room"
          },
          "faces": [
            { "plane": { "normal": [ 1.0,  0.0,  0.0], "distance":  4.0 }, "material": "mat.wall" },
            { "plane": { "normal": [-1.0,  0.0,  0.0], "distance":  0.0 }, "material": 0 },
            {
              "plane": { "normal": [ 0.0,  1.0,  0.0], "distance":  3.0 },
              "material": "mat.wall",
              "surface_flags": ["slick"],
              "uv": { "scale": [2.0, 0.5], "offset": [0.25, -0.5], "rotation_degrees": 90.0 },
              "editor": {
                "stable_id": "brush_face.test.room.ceiling.v1",
                "display_name": "Room Ceiling Face",
                "group": "room_faces"
              }
            },
            { "plane": { "normal": [ 0.0, -1.0,  0.0], "distance":  0.0 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0.0,  0.0,  1.0], "distance":  4.0 }, "material": "mat.trim", "surface_flags": "emissive" },
            { "plane": { "normal": [ 0.0,  0.0, -1.0], "distance":  0.0 }, "material": "mat.wall" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "brush_world.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_game_data_brush_world world{};
    ASSERT_TRUE(slayer3d_game_data_get_brush_world(runtime, "brush.test", &world));
    EXPECT_STREQ(world.name, "brush.test");
    EXPECT_STREQ(world.units, "meters");
    EXPECT_FLOAT_EQ(world.meters_per_unit, 1.0f);
    EXPECT_STREQ(world.editor.stable_id, "brush_world.test.v1");
    EXPECT_STREQ(world.editor.display_name, "Brush Test World");
    EXPECT_STREQ(world.editor.category, "tests/brush");
    EXPECT_STREQ(world.editor.group, "runtime");
    ASSERT_EQ(world.editor.tag_count, 2);
    EXPECT_STREQ(world.editor.tags[1], "runtime");
    EXPECT_TRUE(world.editor.has_snap_grid);
    expect_vec3_near(world.editor.snap_grid, slayer3d_vec3_make(0.5f, 0.25f, 0.5f));
    EXPECT_FLOAT_EQ(world.editor.snap_rotation_degrees, 15.0f);
    EXPECT_TRUE(world.editor.snap_align_to_floor);
    ASSERT_EQ(world.material_count, 2);
    EXPECT_STREQ(world.materials[0].name, "mat.wall");
    EXPECT_FLOAT_EQ(world.materials[0].albedo.z, 0.45f);
    EXPECT_FLOAT_EQ(world.materials[0].roughness, 0.8f);
    EXPECT_FLOAT_EQ(world.materials[0].tex_scale, 2.0f);
    EXPECT_STREQ(world.materials[0].editor.stable_id, "brush_material.test.wall.v1");
    EXPECT_STREQ(world.materials[0].editor.display_name, "Test Wall");
    ASSERT_EQ(world.materials[0].editor.tag_count, 1);
    EXPECT_STREQ(world.materials[0].editor.tags[0], "wall");
    EXPECT_STREQ(world.materials[1].name, "mat.trim");
    EXPECT_FLOAT_EQ(world.materials[1].emissive.x, 0.4f);
    ASSERT_EQ(world.brush_count, 1);
    EXPECT_STREQ(world.brushes[0].name, "brush.room");
    EXPECT_STREQ(world.brushes[0].editor.stable_id, "brush.test.room.v1");
    EXPECT_STREQ(world.brushes[0].editor.display_name, "Runtime Test Room");
    EXPECT_STREQ(world.brushes[0].editor.prefab, "prefab.test.room");
    EXPECT_EQ(world.brushes[0].contents,
              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);
    ASSERT_EQ(world.brushes[0].tag_count, 2);
    EXPECT_STREQ(world.brushes[0].tags[1], "room");
    ASSERT_EQ(world.brushes[0].face_count, 6);
    EXPECT_FLOAT_EQ(world.brushes[0].faces[0].normal.x, 1.0f);
    EXPECT_FLOAT_EQ(world.brushes[0].faces[0].distance, 4.0f);
    EXPECT_EQ(world.brushes[0].faces[1].material_index, 0);
    EXPECT_STREQ(world.brushes[0].faces[1].material_name, "mat.wall");
    EXPECT_EQ(world.brushes[0].faces[2].surface_flags, SLAYER3D_GAME_DATA_BRUSH_SURFACE_SLICK);
    EXPECT_FLOAT_EQ(world.brushes[0].faces[2].uv_scale[0], 2.0f);
    EXPECT_FLOAT_EQ(world.brushes[0].faces[2].uv_scale[1], 0.5f);
    EXPECT_FLOAT_EQ(world.brushes[0].faces[2].uv_offset[0], 0.25f);
    EXPECT_FLOAT_EQ(world.brushes[0].faces[2].uv_offset[1], -0.5f);
    EXPECT_FLOAT_EQ(world.brushes[0].faces[2].uv_rotation_degrees, 90.0f);
    EXPECT_STREQ(world.brushes[0].faces[2].editor.stable_id, "brush_face.test.room.ceiling.v1");
    EXPECT_STREQ(world.brushes[0].faces[2].editor.display_name, "Room Ceiling Face");
    EXPECT_EQ(world.brushes[0].faces[4].material_index, 1);
    EXPECT_EQ(world.brushes[0].faces[4].surface_flags, SLAYER3D_GAME_DATA_BRUSH_SURFACE_EMISSIVE);
    ASSERT_NE(world.render_model, nullptr);
    ASSERT_EQ(world.render_model->material_count, 2);
    EXPECT_FLOAT_EQ(world.render_model->materials[1].emissive[0], 0.4f);
    ASSERT_EQ(world.render_model->mesh_count, 2);
    EXPECT_EQ(world.render_model->meshes[0].material_index, 0);
    EXPECT_EQ(world.render_model->meshes[0].vertex_count, 30);
    EXPECT_EQ(world.render_model->meshes[0].index_count, 30);
    EXPECT_EQ(world.render_model->meshes[1].material_index, 1);
    EXPECT_EQ(world.render_model->meshes[1].vertex_count, 6);
    EXPECT_EQ(world.render_model->meshes[1].index_count, 6);
    EXPECT_NE(world.render_model->meshes[0].normals, nullptr);
    EXPECT_NE(world.render_model->meshes[0].uvs, nullptr);
    EXPECT_TRUE(world.render_model->meshes[0].has_local_bounds);

    slayer3d_game_data_brush_world missing{};
    EXPECT_FALSE(slayer3d_game_data_get_brush_world(runtime, "brush.missing", &missing));
    EXPECT_EQ(missing.name, nullptr);

    BrushWorldInstanceCapture capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_brush_world_instance(runtime, capture_brush_world_instance, &capture));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.world_name, "brush.test");
    ASSERT_NE(capture.world, nullptr);
    EXPECT_STREQ(capture.world->name, "brush.test");
    expect_vec3_near(capture.position, slayer3d_vec3_make(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(capture.acceleration_enabled);
    EXPECT_FALSE(capture.lighting_enabled);
    EXPECT_TRUE(capture.debug_wireframe);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidBrushWorlds)
{
    struct Case
    {
        const char *name;
        const char *brush_world_json;
        const char *scene_json;
        const char *error;
    };
    const Case cases[] = {
        {
            "bad_material_ref",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.bad",
      "materials": [{ "name": "mat.good" }],
      "brushes": [
        {
          "name": "brush.box",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1 }, "material": "mat.missing" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.good" }
          ]
        }
      ]
    }
  ]
})json",
            nullptr,
            "brush face material must reference a declared material",
        },
        {
            "zero_normal",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.bad",
      "materials": [{ "name": "mat.good" }],
      "brushes": [
        {
          "name": "brush.box",
          "faces": [
            { "plane": { "normal": [0, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.good" }
          ]
        }
      ]
    }
  ]
})json",
            nullptr,
            "brush face plane requires non-zero normal vec3",
        },
        {
            "bad_content",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.bad",
      "materials": [{ "name": "mat.good" }],
      "brushes": [
        {
          "name": "brush.box",
          "contents": ["opaque"],
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.good" }
          ]
        }
      ]
    }
  ]
})json",
            nullptr,
            "brush content value is unknown",
        },
        {
            "bad_emissive",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.bad",
      "materials": [{ "name": "mat.bad", "emissive": [0.1, -0.2, 0.3] }],
      "brushes": [
        {
          "name": "brush.box",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1 }, "material": "mat.bad" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.bad" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.bad" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.bad" }
          ]
        }
      ]
    }
  ]
})json",
            nullptr,
            "brush material emissive must be a non-negative vec3",
        },
        {
            "bad_uv_scale",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.bad",
      "materials": [{ "name": "mat.good" }],
      "brushes": [
        {
          "name": "brush.box",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1 }, "material": "mat.good", "uv": { "scale": [1, 0] } },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.good" }
          ]
        }
      ]
    }
  ]
})json",
            nullptr,
            "brush face uv scale must be a positive vec2",
        },
        {
            "bad_scene_ref",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.good",
      "materials": [{ "name": "mat.good" }],
      "brushes": [
        {
          "name": "brush.box",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.good" }
          ]
        }
      ]
    }
  ]
})json",
            R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": { "brush_worlds": [{ "world": "brush.missing" }] }
})json",
            "unknown brush world reference",
        },
        {
            "bad_lighting_flag",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.good",
      "materials": [{ "name": "mat.good" }],
      "brushes": [
        {
          "name": "brush.box",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.good" }
          ]
        }
      ]
    }
  ]
})json",
            R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": { "brush_worlds": [{ "world": "brush.good", "lighting": "yes" }] }
})json",
            "scene brush world lighting must be a boolean",
        },
        {
            "duplicate_editor_stable_id",
            R"json({
  "brush_worlds": [
    {
      "name": "brush.bad",
      "editor": { "stable_id": "stable.duplicate" },
      "materials": [
        { "name": "mat.good", "editor": { "stable_id": "stable.duplicate" } }
      ],
      "brushes": [
        {
          "name": "brush.box",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, 1, 0], "distance": 1 }, "material": "mat.good" },
            { "plane": { "normal": [0, -1, 0], "distance": 1 }, "material": "mat.good" }
          ]
        }
      ]
    }
  ]
})json",
            nullptr,
            "duplicate editor stable id",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path dir = unique_test_dir(test_case.name);
        write_text(dir / "scenes" / "play.scene.json", test_case.scene_json != nullptr ? test_case.scene_json : R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play"
})json");
        std::string brush_world_section = test_case.brush_world_json;
        const size_t section_start = brush_world_section.find("\"brush_worlds\"");
        const size_t section_end = brush_world_section.rfind('}');
        ASSERT_NE(section_start, std::string::npos) << test_case.name;
        ASSERT_NE(section_end, std::string::npos) << test_case.name;
        brush_world_section = brush_world_section.substr(section_start, section_end - section_start);
        const std::string game_json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Brush World" },
  "world": { "name": "world.invalid", "kind": "brush" },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] },
)json") + brush_world_section + "\n}";
        write_text(dir / "bad_brush.game.json", game_json.c_str());

        slayer3d_game_session *session = nullptr;
        ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
        char error[512]{};
        slayer3d_game_data_runtime *runtime = nullptr;
        EXPECT_FALSE(slayer3d_game_data_load_file((dir / "bad_brush.game.json").string().c_str(), session, &runtime,
                                                  error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.error), std::string::npos) << test_case.name << ": " << error;
        slayer3d_game_data_destroy(runtime);
        slayer3d_game_session_destroy(session);
        remove_test_dir(dir);
    }
}

TEST(GameDataRuntime, TracesAuthoredBrushWorldsWithContentsAndSweptHulls)
{
    const std::filesystem::path dir = unique_test_dir("brush_world_traces");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": {
    "brush_worlds": [
      { "world": "brush.trace", "position": [10.0, 0.0, 0.0] }
    ]
  }
})json");
    write_text(dir / "brush_trace.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush Trace Test" },
  "world": { "name": "world.brush_trace", "kind": "brush" },
  "brush_worlds": [
    {
      "name": "brush.trace",
      "materials": [
        { "name": "mat.wall", "albedo": [0.5, 0.5, 0.55, 1.0] },
        { "name": "mat.ramp", "albedo": [0.8, 0.5, 0.25, 1.0] },
        { "name": "mat.trigger", "albedo": [0.25, 0.8, 0.9, 0.35] }
      ],
      "brushes": [
        {
          "name": "brush.box",
          "contents": ["solid", "player_clip"],
          "faces": [
            { "plane": { "normal": [ 1,  0,  0], "distance":  4 }, "material": "mat.wall" },
            { "plane": { "normal": [-1,  0,  0], "distance":  0 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0,  1,  0], "distance":  3 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0, -1,  0], "distance":  0 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0,  0,  1], "distance":  4 }, "material": "mat.wall", "surface_flags": "slick" },
            { "plane": { "normal": [ 0,  0, -1], "distance":  0 }, "material": "mat.wall" }
          ]
        },
        {
          "name": "brush.ramp",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [ 1, 0, 0], "distance":  9 }, "material": "mat.ramp" },
            { "plane": { "normal": [-1, 0, 0], "distance": -6 }, "material": "mat.ramp" },
            { "plane": { "normal": [ 0, 0, 1], "distance":  3 }, "material": "mat.ramp" },
            { "plane": { "normal": [ 0, 0,-1], "distance":  0 }, "material": "mat.ramp" },
            { "plane": { "normal": [ 0,-1, 0], "distance":  0 }, "material": "mat.ramp" },
            { "plane": { "normal": [-0.5, 1, 0], "distance": -2.5 }, "material": "mat.ramp" }
          ]
        },
        {
          "name": "brush.trigger",
          "contents": "trigger",
          "faces": [
            { "plane": { "normal": [ 1,  0,  0], "distance":  8 }, "material": "mat.trigger" },
            { "plane": { "normal": [-1,  0,  0], "distance": -6 }, "material": "mat.trigger" },
            { "plane": { "normal": [ 0,  1,  0], "distance":  2 }, "material": "mat.trigger" },
            { "plane": { "normal": [ 0, -1,  0], "distance":  0 }, "material": "mat.trigger" },
            { "plane": { "normal": [ 0,  0,  1], "distance": -5 }, "material": "mat.trigger" },
            { "plane": { "normal": [ 0,  0, -1], "distance":  7 }, "material": "mat.trigger" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "brush_trace.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_game_data_brush_world world{};
    ASSERT_TRUE(slayer3d_game_data_get_brush_world(runtime, "brush.trace", &world));
    EXPECT_TRUE(world.has_bounds);
    EXPECT_NEAR(world.bounds.min.x, 0.0f, 0.001f);
    EXPECT_NEAR(world.bounds.max.x, 9.0f, 0.001f);
    ASSERT_EQ(world.brush_count, 3);
    EXPECT_TRUE(world.brushes[0].has_bounds);
    EXPECT_NEAR(world.brushes[0].bounds.max.y, 3.0f, 0.001f);

    slayer3d_game_data_brush_trace_desc trace{};
    slayer3d_game_data_brush_trace_result result{};
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    trace.start = slayer3d_vec3_make(-2.0f, 1.0f, 2.0f);
    trace.end = slayer3d_vec3_make(2.0f, 1.0f, 2.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_FALSE(result.start_solid);
    EXPECT_NEAR(result.fraction, 0.5f, 0.001f);
    EXPECT_STREQ(result.brush_name, "brush.box");
    EXPECT_NEAR(result.normal.x, -1.0f, 0.0001f);
    slayer3d_game_data_brush_diagnostics diagnostics{};
    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.trace_count, 1u);
    EXPECT_EQ(diagnostics.brush_count, 3u);
    EXPECT_EQ(diagnostics.contents_reject_count, 1u);
    EXPECT_EQ(diagnostics.bounds_reject_count, 1u);
    EXPECT_EQ(diagnostics.collision_candidate_count, 1u);
    EXPECT_EQ(diagnostics.hit_count, 1u);
    slayer3d_game_data_reset_brush_diagnostics(runtime);

    trace.start = slayer3d_vec3_make(2.0f, 1.0f, 6.0f);
    trace.end = slayer3d_vec3_make(2.0f, 1.0f, 2.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_STREQ(result.material_name, "mat.wall");
    EXPECT_EQ(result.surface_flags, SLAYER3D_GAME_DATA_BRUSH_SURFACE_SLICK);
    EXPECT_NEAR(result.normal.z, 1.0f, 0.0001f);

    trace.start = slayer3d_vec3_make(-2.0f, 4.0f, 2.0f);
    trace.end = slayer3d_vec3_make(2.0f, 4.0f, 2.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_FALSE(result.hit);
    EXPECT_FLOAT_EQ(result.fraction, 1.0f);

    trace.start = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    trace.end = slayer3d_vec3_make(2.0f, 1.0f, 1.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_TRUE(result.start_solid);
    EXPECT_FLOAT_EQ(result.fraction, 0.0f);

    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE;
    trace.extents = slayer3d_vec3_make(0.5f, 0.0f, 0.0f);
    trace.start = slayer3d_vec3_make(-2.0f, 1.0f, 2.0f);
    trace.end = slayer3d_vec3_make(2.0f, 1.0f, 2.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_NEAR(result.fraction, 0.375f, 0.001f);

    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB;
    trace.extents = slayer3d_vec3_make(0.25f, 0.5f, 0.25f);
    trace.start = slayer3d_vec3_make(-2.0f, 1.0f, 2.0f);
    trace.end = slayer3d_vec3_make(2.0f, 1.0f, 2.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_NEAR(result.fraction, 0.4375f, 0.001f);

    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.extents = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER;
    trace.start = slayer3d_vec3_make(7.0f, 1.0f, -8.0f);
    trace.end = slayer3d_vec3_make(7.0f, 1.0f, -6.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_STREQ(result.brush_name, "brush.trigger");
    EXPECT_EQ(result.contents, SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER);

    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_FALSE(result.hit);

    trace.start = slayer3d_vec3_make(7.0f, 5.0f, 1.0f);
    trace.end = slayer3d_vec3_make(7.0f, -1.0f, 1.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_brush_world(runtime, "brush.trace", &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_STREQ(result.brush_name, "brush.ramp");
    EXPECT_GT(result.normal.y, 0.8f);
    EXPECT_LT(result.normal.x, -0.4f);

    trace.start = slayer3d_vec3_make(8.0f, 1.0f, 2.0f);
    trace.end = slayer3d_vec3_make(12.0f, 1.0f, 2.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_STREQ(result.world_name, "brush.trace");
    EXPECT_STREQ(result.brush_name, "brush.box");
    EXPECT_NEAR(result.end_position.x, 10.0f, 0.001f);

    trace.start = slayer3d_vec3_make(-2.0f, 1.0f, 1.0f);
    trace.end = slayer3d_vec3_make(2.0f, 1.0f, 3.0f);
    ASSERT_TRUE(slayer3d_game_data_slide_brush_world(runtime, "brush.trace", &trace, 4, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_STREQ(result.brush_name, "brush.box");
    EXPECT_NEAR(result.end_position.x, -0.0035f, 0.001f);
    EXPECT_GT(result.end_position.z, 2.9f);

    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB;
    trace.extents = slayer3d_vec3_make(0.25f, 0.5f, 0.25f);
    trace.start = slayer3d_vec3_make(-2.0f, 1.0f, 1.0f);
    trace.end = slayer3d_vec3_make(2.0f, 1.0f, 6.0f);
    ASSERT_TRUE(slayer3d_game_data_slide_brush_world(runtime, "brush.trace", &trace, 6, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_STREQ(result.brush_name, "brush.box");
    EXPECT_NEAR(result.end_position.x, -0.25375f, 0.002f);
    EXPECT_GT(result.end_position.z, 5.9f);

    slayer3d_game_data_reset_brush_diagnostics(runtime);
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.extents = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    trace.start = slayer3d_vec3_make(-100.0f, 1.0f, 1.0f);
    trace.end = slayer3d_vec3_make(-90.0f, 1.0f, 1.0f);
    ASSERT_TRUE(slayer3d_game_data_trace_active_brush_worlds(runtime, &trace, &result));
    EXPECT_FALSE(result.hit);
    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.world_instance_count, 1u);
    EXPECT_EQ(diagnostics.world_bounds_reject_count, 1u);
    EXPECT_EQ(diagnostics.brush_count, 0u);
    EXPECT_EQ(diagnostics.collision_candidate_count, 0u);

    slayer3d_render_stats before_stats{};
    slayer3d_render_stats after_stats{};
    before_stats.model_mesh_submissions = 4;
    before_stats.model_mesh_culled = 1;
    before_stats.model_mesh_draws = 3;
    before_stats.model_triangles_submitted = 48;
    after_stats.model_mesh_submissions = 7;
    after_stats.model_mesh_culled = 2;
    after_stats.model_mesh_draws = 5;
    after_stats.model_triangles_submitted = 96;
    slayer3d_game_data_accumulate_brush_render_diagnostics(runtime, &before_stats, &after_stats);
    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.render_mesh_submissions, 3u);
    EXPECT_EQ(diagnostics.render_mesh_culled, 1u);
    EXPECT_EQ(diagnostics.render_mesh_draws, 2u);
    EXPECT_EQ(diagnostics.render_triangles_submitted, 48u);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, WorldModelInterfaceEnumeratesQueriesAndTracesSectorAndBrushWorlds)
{
    const std::filesystem::path dir = unique_test_dir("world_model_interface");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": {
    "sector_levels": [
      { "level": "sector.world_model", "variant": "lightmapped" }
    ],
    "brush_worlds": [
      { "world": "brush.world_model", "position": [10.0, 0.0, 0.0] }
    ]
  }
})json");
    write_text(dir / "world_model.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "World Model Interface Test" },
  "world": { "name": "world.model_interface", "kind": "mixed" },
  "sector_levels": [
    {
      "name": "sector.world_model",
      "materials": [
        { "name": "floor", "albedo": [0.25, 0.25, 0.25, 1.0] },
        { "name": "wall", "albedo": [0.55, 0.55, 0.55, 1.0] }
      ],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "floor_material": "floor",
          "ceil_material": "floor",
          "wall_material": "wall"
        }
      ]
    }
  ],
  "brush_worlds": [
    {
      "name": "brush.world_model",
      "editor": { "stable_id": "editor.world.brush" },
      "materials": [
        {
          "name": "mat.wall",
          "albedo": [0.8, 0.2, 0.2, 1.0],
          "editor": { "stable_id": "editor.material.wall", "display_name": "Wall Material" }
        }
      ],
      "brushes": [
        {
          "name": "brush.cube",
          "editor": { "stable_id": "editor.brush.cube", "display_name": "Cube Brush" },
          "contents": ["solid", "player_clip"],
          "faces": [
            { "plane": { "normal": [ 1,  0,  0], "distance":  2 }, "material": "mat.wall" },
            {
              "plane": { "normal": [-1,  0,  0], "distance":  0 },
              "material": "mat.wall",
              "editor": { "stable_id": "editor.face.cube.left", "display_name": "Cube Left Face" }
            },
            { "plane": { "normal": [ 0,  1,  0], "distance":  2 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0, -1,  0], "distance":  0 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0,  0,  1], "distance":  2 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0,  0, -1], "distance":  0 }, "material": "mat.wall" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "world_model.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    WorldModelInstanceCapture capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_world_model_instance(runtime, capture_world_model_instance, &capture));
    EXPECT_EQ(capture.count, 2);
    EXPECT_EQ(capture.sectors, 1);
    EXPECT_EQ(capture.brushes, 1);
    EXPECT_TRUE(capture.saw_sector_bounds);
    EXPECT_TRUE(capture.saw_brush_bounds);
    EXPECT_NEAR(capture.sector_bounds.min.x, 0.0f, 0.001f);
    EXPECT_NEAR(capture.sector_bounds.max.z, 4.0f, 0.001f);
    EXPECT_NEAR(capture.brush_bounds.min.x, 10.0f, 0.001f);
    EXPECT_NEAR(capture.brush_bounds.max.x, 12.0f, 0.001f);

    slayer3d_game_data_world_point_result point{};
    ASSERT_TRUE(
        slayer3d_game_data_query_world_model_point(runtime, slayer3d_vec3_make(1.0f, 1.0f, 1.0f), 0, 0, &point));
    EXPECT_TRUE(point.inside);
    EXPECT_EQ(point.type, SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL);
    EXPECT_STREQ(point.world_name, "sector.world_model");
    EXPECT_STREQ(point.element_name, "room");
    EXPECT_EQ(point.element_index, 0);

    ASSERT_TRUE(slayer3d_game_data_query_world_model_point(runtime, slayer3d_vec3_make(11.0f, 1.0f, 1.0f),
                                                           SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS,
                                                           SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID, &point));
    EXPECT_TRUE(point.inside);
    EXPECT_EQ(point.type, SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD);
    EXPECT_STREQ(point.world_name, "brush.world_model");
    EXPECT_STREQ(point.element_name, "brush.cube");
    EXPECT_EQ(point.contents, SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);

    slayer3d_game_data_world_trace_desc trace{};
    slayer3d_game_data_world_trace_result result{};
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.start = slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    trace.end = slayer3d_vec3_make(6.0f, 1.0f, 1.0f);
    trace.model_filter = SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS;
    ASSERT_TRUE(slayer3d_game_data_trace_world_models(runtime, &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.type, SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL);
    EXPECT_STREQ(result.world_name, "sector.world_model");
    EXPECT_STREQ(result.element_name, "room");
    EXPECT_LT(result.fraction, 1.0f);

    trace.start = slayer3d_vec3_make(8.0f, 1.0f, 1.0f);
    trace.end = slayer3d_vec3_make(11.0f, 1.0f, 1.0f);
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    trace.model_filter = SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS;
    ASSERT_TRUE(slayer3d_game_data_trace_world_models(runtime, &trace, &result));
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.type, SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD);
    EXPECT_STREQ(result.world_name, "brush.world_model");
    EXPECT_STREQ(result.element_name, "brush.cube");
    EXPECT_STREQ(result.material_name, "mat.wall");
    EXPECT_NEAR(result.end_position.x, 10.0f, 0.001f);
    EXPECT_NEAR(result.normal.x, -1.0f, 0.001f);

    slayer3d_game_data_editor_selection selection{};
    ASSERT_TRUE(slayer3d_game_data_pick_editor_world_model(runtime, &trace, &selection));
    EXPECT_TRUE(selection.hit);
    EXPECT_EQ(selection.type, SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD);
    EXPECT_STREQ(selection.world_name, "brush.world_model");
    EXPECT_NEAR(selection.world_position.x, 10.0f, 0.001f);
    EXPECT_STREQ(selection.element_name, "brush.cube");
    EXPECT_STREQ(selection.material_name, "mat.wall");
    EXPECT_EQ(selection.element_index, 0);
    EXPECT_EQ(selection.face_index, 1);
    EXPECT_TRUE(selection.has_bounds);
    EXPECT_NEAR(selection.bounds.min.x, 10.0f, 0.001f);
    EXPECT_NEAR(selection.bounds.max.x, 12.0f, 0.001f);
    ASSERT_NE(selection.world_editor, nullptr);
    EXPECT_STREQ(selection.world_editor->stable_id, "editor.world.brush");
    ASSERT_NE(selection.element_editor, nullptr);
    EXPECT_STREQ(selection.element_editor->stable_id, "editor.brush.cube");
    ASSERT_NE(selection.material_editor, nullptr);
    EXPECT_STREQ(selection.material_editor->stable_id, "editor.material.wall");
    ASSERT_NE(selection.face_editor, nullptr);
    EXPECT_STREQ(selection.face_editor->stable_id, "editor.face.cube.left");

    struct EditorDebugCapture
    {
        int world_edges = 0;
        int selection_edges = 0;
        int rays = 0;
        int normals = 0;
        int markers = 0;
        bool saw_selection_world = false;
        bool saw_normal = false;
    } debug_capture;
    auto capture_editor_debug = [](void *userdata, const slayer3d_game_data_editor_debug_primitive *primitive) -> bool {
        auto *capture = static_cast<EditorDebugCapture *>(userdata);
        if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORLD_BOUNDS_EDGE)
            capture->world_edges++;
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE)
        {
            capture->selection_edges++;
            if (primitive->world_name != nullptr && std::string(primitive->world_name) == "brush.world_model" &&
                primitive->element_name != nullptr && std::string(primitive->element_name) == "brush.cube")
            {
                capture->saw_selection_world = true;
            }
        }
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_TRACE_RAY)
        {
            capture->rays++;
            EXPECT_NEAR(primitive->start.x, 8.0f, 0.001f);
            EXPECT_NEAR(primitive->end.x, 11.0f, 0.001f);
        }
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_FACE_NORMAL)
        {
            capture->normals++;
            capture->saw_normal = primitive->end.x < primitive->start.x;
        }
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_HIT_MARKER)
        {
            capture->markers++;
        }
        return true;
    };
    slayer3d_game_data_editor_debug_desc debug_desc{};
    debug_desc.flags = SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL;
    debug_desc.selection = &selection;
    debug_desc.trace = &trace;
    debug_desc.normal_length = 0.5f;
    debug_desc.hit_marker_size = 0.125f;
    ASSERT_TRUE(
        slayer3d_game_data_for_each_editor_debug_primitive(runtime, &debug_desc, capture_editor_debug, &debug_capture));
    EXPECT_EQ(debug_capture.world_edges, 24);
    EXPECT_EQ(debug_capture.selection_edges, 12);
    EXPECT_EQ(debug_capture.rays, 1);
    EXPECT_EQ(debug_capture.normals, 1);
    EXPECT_EQ(debug_capture.markers, 3);
    EXPECT_TRUE(debug_capture.saw_selection_world);
    EXPECT_TRUE(debug_capture.saw_normal);

    slayer3d_game_data_world_model_diagnostics diagnostics{};
    ASSERT_TRUE(slayer3d_game_data_get_world_model_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.active_sector_level_instances, 1u);
    EXPECT_EQ(diagnostics.active_brush_world_instances, 1u);
    EXPECT_EQ(diagnostics.world_trace_count, 3u);
    EXPECT_EQ(diagnostics.point_query_count, 2u);
    EXPECT_GE(diagnostics.brush.trace_count, 2u);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EditorPickingPreservesRepeatedBrushWorldInstancePlacement)
{
    const std::filesystem::path dir = unique_test_dir("editor_repeated_brush_pick");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": {
    "brush_worlds": [
      { "world": "brush.repeated", "position": [0.0, 0.0, 0.0] },
      { "world": "brush.repeated", "position": [10.0, 0.0, 0.0] }
    ]
  }
})json");
    write_text(dir / "editor_repeated_pick.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Repeated Brush Pick Test" },
  "world": { "name": "world.repeated_pick", "kind": "brush" },
  "brush_worlds": [
    {
      "name": "brush.repeated",
      "materials": [{ "name": "mat.wall", "albedo": [0.8, 0.2, 0.2, 1.0] }],
      "brushes": [
        {
          "name": "brush.cube",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [ 1,  0,  0], "distance":  2 }, "material": "mat.wall" },
            { "plane": { "normal": [-1,  0,  0], "distance":  0 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0,  1,  0], "distance":  2 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0, -1,  0], "distance":  0 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0,  0,  1], "distance":  2 }, "material": "mat.wall" },
            { "plane": { "normal": [ 0,  0, -1], "distance":  0 }, "material": "mat.wall" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "editor_repeated_pick.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    slayer3d_game_data_world_trace_desc trace{};
    trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    trace.contents_mask = SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID;
    trace.model_filter = SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS;
    trace.start = slayer3d_vec3_make(8.0f, 1.0f, 1.0f);
    trace.end = slayer3d_vec3_make(11.0f, 1.0f, 1.0f);

    slayer3d_game_data_editor_selection selection{};
    ASSERT_TRUE(slayer3d_game_data_pick_editor_world_model(runtime, &trace, &selection));
    EXPECT_TRUE(selection.hit);
    EXPECT_STREQ(selection.world_name, "brush.repeated");
    EXPECT_STREQ(selection.element_name, "brush.cube");
    EXPECT_NEAR(selection.world_position.x, 10.0f, 0.001f);
    ASSERT_TRUE(selection.has_bounds);
    EXPECT_NEAR(selection.bounds.min.x, 10.0f, 0.001f);
    EXPECT_NEAR(selection.bounds.max.x, 12.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EditorShellDojoPublishesSelectionAndDebugOverlay)
{
    const std::filesystem::path dojo_path = editor_shell_dojo_data_path();
    ASSERT_TRUE(std::filesystem::exists(dojo_path)) << dojo_path;

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(dojo_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 564.8f;
    motion.motion.y = 392.9f;
    motion.motion.xrel = 0.0f;
    motion.motion.yrel = 0.0f;
    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = motion.motion.x;
    click.button.y = motion.motion.y;
    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    slayer3d_input_process_event(input, &motion);
    slayer3d_input_process_event(input, &click);
    slayer3d_input_update(input, 1);

    ASSERT_TRUE(slayer3d_game_data_update_active_editor_tooling(runtime));
    const slayer3d_properties *scene_state = slayer3d_game_data_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.selection.hit", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.selection.type", ""), "brush_world");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.selection.world", ""),
                 "brush.editor_shell.target");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.selection.element", ""), "brush.target.cube");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.selection.material", ""), "mat.editor.wall");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.selection.face_stable_id", ""),
                 "editor.face.target_cube.left");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.selection.face_index", -1), 1);
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.hover.hit", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.hover.element", ""), "brush.target.cube");

    slayer3d_game_data_editor_selection active_selection{};
    ASSERT_TRUE(slayer3d_game_data_get_active_editor_selection(runtime, &active_selection));
    EXPECT_STREQ(active_selection.element_name, "brush.target.cube");

    auto press_key = [&](SDL_Scancode scancode, Uint64 frame) {
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.scancode = scancode;
        slayer3d_input_process_event(input, &key);
        slayer3d_input_update(input, frame);
    };
    auto target_cube_min_y = [&]() -> float {
        struct BoundsCapture
        {
            float min_y = 0.0f;
            bool found = false;
        } capture;
        auto capture_bounds = [](void *userdata, const slayer3d_game_data_brush_world_instance *instance) -> bool {
            auto *capture = static_cast<BoundsCapture *>(userdata);
            if (instance == nullptr || instance->world == nullptr ||
                std::string(instance->world_name != nullptr ? instance->world_name : "") != "brush.editor_shell.target")
            {
                return true;
            }
            for (int i = 0; i < instance->world->brush_count; ++i)
            {
                const slayer3d_game_data_brush &brush = instance->world->brushes[i];
                if (brush.name != nullptr && std::string(brush.name) == "brush.target.cube")
                {
                    capture->min_y = brush.bounds.min.y + instance->position.y;
                    capture->found = true;
                    return false;
                }
            }
            return true;
        };
        EXPECT_TRUE(slayer3d_game_data_for_each_brush_world_instance(runtime, capture_bounds, &capture));
        EXPECT_TRUE(capture.found);
        return capture.min_y;
    };
    auto target_cube_face_material = [&]() -> std::string {
        struct MaterialCapture
        {
            std::string material;
            bool found = false;
        } capture;
        auto capture_material = [](void *userdata, const slayer3d_game_data_brush_world_instance *instance) -> bool {
            auto *capture = static_cast<MaterialCapture *>(userdata);
            if (instance == nullptr || instance->world == nullptr ||
                std::string(instance->world_name != nullptr ? instance->world_name : "") != "brush.editor_shell.target")
            {
                return true;
            }
            for (int i = 0; i < instance->world->brush_count; ++i)
            {
                const slayer3d_game_data_brush &brush = instance->world->brushes[i];
                if (brush.name != nullptr && std::string(brush.name) == "brush.target.cube" && brush.face_count > 1)
                {
                    capture->material = brush.faces[1].material_name != nullptr ? brush.faces[1].material_name : "";
                    capture->found = true;
                    return false;
                }
            }
            return true;
        };
        EXPECT_TRUE(slayer3d_game_data_for_each_brush_world_instance(runtime, capture_material, &capture));
        EXPECT_TRUE(capture.found);
        return capture.material;
    };

    press_key(SDL_SCANCODE_I, 4);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.tool.last_action", ""),
                 "inspect brush.target.cube face editor.face.target_cube.left");

    press_key(SDL_SCANCODE_TAB, 5);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.tool.mode", ""), "move");

    press_key(SDL_SCANCODE_P, 6);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.command_preview.active", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.command_preview.valid", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.command_preview.command", ""), "translate");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.command_preview.target", ""), "element");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.command_preview.message", ""),
                 "preview translate brush.target.cube");
    const slayer3d_value *preview_min = slayer3d_properties_get_value(scene_state, "editor.command_preview.bounds_min");
    ASSERT_NE(preview_min, nullptr);
    ASSERT_EQ(preview_min->type, SLAYER3D_VALUE_VEC3);
    EXPECT_NEAR(preview_min->as_vec3.y, 0.35f, 0.001f);

    struct DebugCapture
    {
        int world_edges = 0;
        int selection_edges = 0;
        int command_preview_edges = 0;
        int rays = 0;
        int normals = 0;
        int markers = 0;
    } debug;
    auto capture_debug = [](void *userdata, const slayer3d_game_data_editor_debug_primitive *primitive) -> bool {
        auto *capture = static_cast<DebugCapture *>(userdata);
        if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORLD_BOUNDS_EDGE)
            capture->world_edges++;
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE)
            capture->selection_edges++;
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_COMMAND_PREVIEW_BOUNDS_EDGE)
            capture->command_preview_edges++;
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_TRACE_RAY)
            capture->rays++;
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_FACE_NORMAL)
            capture->normals++;
        else if (primitive->type == SLAYER3D_GAME_DATA_EDITOR_DEBUG_HIT_MARKER)
            capture->markers++;
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_active_editor_debug_primitive(runtime, capture_debug, &debug));
    EXPECT_EQ(debug.world_edges, 12);
    EXPECT_EQ(debug.selection_edges, 12);
    EXPECT_EQ(debug.command_preview_edges, 12);
    EXPECT_EQ(debug.rays, 1);
    EXPECT_EQ(debug.normals, 1);
    EXPECT_EQ(debug.markers, 3);

    struct InspectorText
    {
        std::vector<std::string> values;
    } inspector;
    auto capture_inspector = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *capture = static_cast<InspectorText *>(userdata);
        if (std::string(text->name) == "ui.editor_shell.inspector")
            capture->values.emplace_back(text->text != nullptr ? text->text : "");
        return true;
    };
    slayer3d_game_data_ui_metrics metrics{};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text_for_metrics(runtime, &metrics, capture_inspector, &inspector));
    ASSERT_GE(inspector.values.size(), 13U);
    EXPECT_EQ(inspector.values[0], "Editor Selection");
    EXPECT_EQ(inspector.values[1], "Hit");
    EXPECT_EQ(inspector.values[2], "true");
    EXPECT_EQ(inspector.values[3], "World");
    EXPECT_EQ(inspector.values[4], "brush.editor_shell.target");

    press_key(SDL_SCANCODE_RETURN, 7);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "editor.command_preview.active", true));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.transaction.valid", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.event", ""), "commit");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.message", ""),
                 "committed translate #1");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.id", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.undo_count", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.redo_count", -1), 0);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.element", ""), "brush.target.cube");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.tool.last_action", ""), "commit translate #1");
    EXPECT_NEAR(target_cube_min_y(), 0.35f, 0.001f);
    ASSERT_TRUE(slayer3d_game_data_get_active_editor_selection(runtime, &active_selection));
    ASSERT_TRUE(active_selection.has_bounds);
    EXPECT_NEAR(active_selection.bounds.min.y, 0.35f, 0.001f);

    press_key(SDL_SCANCODE_Z, 8);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.transaction.valid", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.event", ""), "undo");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.message", ""), "undo translate #1");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.undo_count", -1), 0);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.redo_count", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.tool.last_action", ""), "undo translate #1");
    EXPECT_NEAR(target_cube_min_y(), 0.0f, 0.001f);
    ASSERT_TRUE(slayer3d_game_data_get_active_editor_selection(runtime, &active_selection));
    ASSERT_TRUE(active_selection.has_bounds);
    EXPECT_NEAR(active_selection.bounds.min.y, 0.0f, 0.001f);

    press_key(SDL_SCANCODE_Y, 9);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.transaction.valid", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.event", ""), "redo");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.message", ""), "redo translate #1");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.undo_count", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.redo_count", -1), 0);
    EXPECT_NEAR(target_cube_min_y(), 0.35f, 0.001f);

    press_key(SDL_SCANCODE_TAB, 10);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.tool.mode", ""), "paint");

    EXPECT_EQ(target_cube_face_material(), "mat.editor.wall");
    press_key(SDL_SCANCODE_P, 11);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.command_preview.active", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.command_preview.command", ""), "paint");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.command_preview.target", ""), "face");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.command_preview.message", ""),
                 "preview paint brush.target.cube");

    press_key(SDL_SCANCODE_RETURN, 12);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "editor.transaction.valid", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.event", ""), "commit");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.message", ""), "committed paint #2");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.undo_count", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.redo_count", -1), 0);
    EXPECT_EQ(target_cube_face_material(), "mat.editor.floor");
    ASSERT_TRUE(slayer3d_game_data_get_active_editor_selection(runtime, &active_selection));
    EXPECT_STREQ(active_selection.material_name, "mat.editor.floor");

    press_key(SDL_SCANCODE_Z, 13);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.message", ""), "undo paint #2");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.undo_count", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.redo_count", -1), 1);
    EXPECT_EQ(target_cube_face_material(), "mat.editor.wall");
    ASSERT_TRUE(slayer3d_game_data_get_active_editor_selection(runtime, &active_selection));
    EXPECT_STREQ(active_selection.material_name, "mat.editor.wall");

    press_key(SDL_SCANCODE_Y, 14);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.transaction.message", ""), "redo paint #2");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.undo_count", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "editor.transaction.redo_count", -1), 0);
    EXPECT_EQ(target_cube_face_material(), "mat.editor.floor");

    press_key(SDL_SCANCODE_BACKSPACE, 15);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "editor.selection.hit", true));
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "editor.command_preview.active", true));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.command_preview.message", ""), "preview cleared");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "editor.tool.last_action", ""), "selection cleared");
    EXPECT_FALSE(slayer3d_game_data_get_active_editor_selection(runtime, &active_selection));

    SDL_Event release{};
    release.type = SDL_EVENT_MOUSE_BUTTON_UP;
    release.button.button = SDL_BUTTON_LEFT;
    release.button.x = click.button.x;
    release.button.y = click.button.y;
    slayer3d_input_process_event(input, &release);
    slayer3d_input_update(input, 2);

    SDL_Event miss_motion{};
    miss_motion.type = SDL_EVENT_MOUSE_MOTION;
    miss_motion.motion.x = 20.0f;
    miss_motion.motion.y = 20.0f;
    SDL_Event miss_click{};
    miss_click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    miss_click.button.button = SDL_BUTTON_LEFT;
    miss_click.button.x = miss_motion.motion.x;
    miss_click.button.y = miss_motion.motion.y;
    slayer3d_input_process_event(input, &miss_motion);
    slayer3d_input_process_event(input, &miss_click);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_update_active_editor_tooling(runtime));
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "editor.selection.hit", true));
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "editor.hover.hit", true));
    EXPECT_FALSE(slayer3d_game_data_get_active_editor_selection(runtime, &active_selection));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RunsAuthoredFpsBrushController)
{
    const std::filesystem::path dir = unique_test_dir("fps_brush_controller");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "camera": "camera.player",
  "updates_game": true,
  "entities": ["entity.player"],
  "input": {
    "actions": [
      "action.move.forward",
      "action.move.back",
      "action.move.left",
      "action.move.right",
      "action.jump"
    ]
  },
  "world": {
    "brush_worlds": [
      { "world": "brush.test_room", "position": [0.0, 0.0, 0.0] }
    ]
  }
})json");
    write_text(dir / "fps_brush.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "FPS Brush Controller Test" },
  "world": {
    "name": "world.test",
    "kind": "brush",
    "cameras": [
      {
        "name": "camera.player",
        "type": "fps",
        "target_entity": "entity.player",
        "fov": 90.0,
        "fov_axis": "horizontal",
        "active": true
      }
    ]
  },
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" },
          { "name": "action.jump" }
        ]
      }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [0.0, 1.6, 1.5] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "pitch": { "type": "float", "value": 0.0 },
        "current_sector": { "type": "int", "value": 7 }
      },
      "components": [
        {
          "type": "controller.fps_brush",
          "brush_world": "brush.test_room",
          "contents_mask": ["solid", "player_clip"],
          "actions": {
            "forward": "action.move.forward",
            "back": "action.move.back",
            "left": "action.move.left",
            "right": "action.move.right",
            "jump": "action.jump"
          },
          "move_speed": 4.0,
          "jump_velocity": 4.0,
          "gravity": 9.0,
          "player_height": 1.6,
          "player_radius": 0.25,
          "step_height": 0.4,
          "ceiling_clearance": 0.1,
          "walkable_normal_y": 0.65,
          "mouse_sensitivity": 0.0
        }
      ]
    }
  ],
  "signals": ["signal.push"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.push",
        "actions": [
          {
            "type": "controller.fps.push",
            "target": "entity.player",
            "velocity": [0.5, 0.0, 0.0],
            "scale_by_dt": false
          }
        ]
      }
    ]
  },
  "brush_worlds": [
    {
      "name": "brush.test_room",
      "materials": [
        { "name": "mat.floor", "albedo": [0.5, 0.5, 0.5, 1.0] },
        { "name": "mat.wall", "albedo": [0.25, 0.25, 0.3, 1.0] }
      ],
      "brushes": [
        {
          "name": "brush.floor",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 4.0 }, "material": "mat.floor" },
            { "plane": { "normal": [-1, 0, 0], "distance": 4.0 }, "material": "mat.floor" },
            { "plane": { "normal": [0, 1, 0], "distance": 0.0 }, "material": "mat.floor" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.5 }, "material": "mat.floor" },
            { "plane": { "normal": [0, 0, 1], "distance": 4.0 }, "material": "mat.floor" },
            { "plane": { "normal": [0, 0, -1], "distance": 4.0 }, "material": "mat.floor" }
          ]
        },
        {
          "name": "brush.north_wall",
          "contents": ["solid", "player_clip"],
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 4.0 }, "material": "mat.wall" },
            { "plane": { "normal": [-1, 0, 0], "distance": 4.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 1, 0], "distance": 3.5 }, "material": "mat.wall" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0, 1], "distance": -3.5 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0, -1], "distance": 4.0 }, "material": "mat.wall" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "fps_brush.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    const float initial_z = player->position.z;
    const float initial_y = player->position.y;
    const int push_signal = slayer3d_game_data_find_signal(runtime, "signal.push");
    ASSERT_GE(push_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), push_signal, nullptr);
    EXPECT_NEAR(player->position.x, 0.5f, 0.0001f);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    slayer3d_camera3d initial_camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &initial_camera));
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(900 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
        slayer3d_camera3d idle_camera{};
        ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &idle_camera));
        EXPECT_NEAR(player->position.y, initial_y, 0.03f);
        EXPECT_NEAR(idle_camera.position.y, initial_camera.position.y, 0.03f);
        EXPECT_NEAR(idle_camera.target.y - idle_camera.position.y, initial_camera.target.y - initial_camera.position.y,
                    0.03f);
    }

    const int forward = slayer3d_game_data_find_action(runtime, "action.move.forward");
    ASSERT_GE(forward, 0);
    slayer3d_input_set_action_override(input, forward, 1.0f);
    for (int i = 0; i < 20; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(1000 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    }

    EXPECT_LT(player->position.z, initial_z);
    EXPECT_GT(player->position.z, -3.35f);
    EXPECT_NEAR(player->position.y, 1.6f, 0.03f);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "current_sector", 99), -1);
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "on_ground", false));
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "brush_collision_kind", ""), "wall");
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "brush_collision_brush", ""), "brush.north_wall");
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "brush_floor_brush", ""), "brush.floor");
    expect_vec3_near(
        slayer3d_properties_get_vec3(player->props, "brush_floor_normal", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
        slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    expect_vec3_near(
        slayer3d_properties_get_vec3(player->props, "camera_forward", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
        slayer3d_vec3_make(0.0f, 0.0f, -1.0f));

    slayer3d_input_set_action_override(input, forward, 0.0f);
    const int jump = slayer3d_game_data_find_action(runtime, "action.jump");
    ASSERT_GE(jump, 0);
    const float grounded_y = player->position.y;
    slayer3d_input_set_action_override(input, jump, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 2000), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_GT(player->position.y, grounded_y);
    EXPECT_GT(slayer3d_properties_get_float(player->props, "vertical_velocity", 0.0f), 0.0f);
    EXPECT_FALSE(slayer3d_properties_get_bool(player->props, "on_ground", true));
    slayer3d_input_set_action_override(input, jump, 0.0f);
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(2016 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    }
    EXPECT_GT(player->position.y, grounded_y + 0.15f);
    EXPECT_FALSE(slayer3d_properties_get_bool(player->props, "on_ground", true));
    float previous_jump_y = player->position.y;
    bool landed = false;
    for (int i = 0; i < 120; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(2100 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
        const float jump_y = player->position.y;
        EXPECT_GT(jump_y - previous_jump_y, -0.12f);
        previous_jump_y = jump_y;
        if (slayer3d_properties_get_bool(player->props, "on_ground", false))
        {
            landed = true;
            break;
        }
    }
    EXPECT_TRUE(landed);
    EXPECT_NEAR(player->position.y, grounded_y, 0.04f);

    slayer3d_camera3d camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &camera));
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_NEAR(camera.position.x, player->position.x, 0.001f);
    EXPECT_NEAR(camera.position.y, player->position.y, 0.001f);
    EXPECT_NEAR(camera.position.z, player->position.z, 0.001f);
    EXPECT_LT(camera.target.z, camera.position.z);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, FpsBrushControllerSmoothsStairStepCamera)
{
    const std::filesystem::path dir = unique_test_dir("fps_brush_stair_smoothing");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "camera": "camera.player",
  "updates_game": true,
  "entities": ["entity.player"],
  "input": {
    "actions": [
      "action.move.forward",
      "action.move.back",
      "action.move.left",
      "action.move.right"
    ]
  },
  "world": { "brush_worlds": [{ "world": "brush.stair_room", "position": [0.0, 0.0, 0.0] }] }
})json");
    write_text(dir / "fps_brush_stair.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "FPS Brush Stair Smoothing Test" },
  "world": {
    "name": "world.test",
    "kind": "brush",
    "cameras": [
      { "name": "camera.player", "type": "fps", "target_entity": "entity.player", "fov": 90.0, "active": true }
    ]
  },
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" }
        ]
      }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [0.0, 1.6, 1.5] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "pitch": { "type": "float", "value": 0.0 },
        "view_smooth": { "type": "float", "value": 0.0 }
      },
      "components": [
        {
          "type": "controller.fps_brush",
          "brush_world": "brush.stair_room",
          "contents_mask": ["solid"],
          "actions": {
            "forward": "action.move.forward",
            "back": "action.move.back",
            "left": "action.move.left",
            "right": "action.move.right"
          },
          "move_speed": 4.0,
          "jump_velocity": 4.0,
          "gravity": 9.0,
          "player_height": 1.6,
          "player_radius": 0.25,
          "step_height": 0.45,
          "ceiling_clearance": 0.1,
          "walkable_normal_y": 0.65,
          "mouse_sensitivity": 0.0
        }
      ]
    }
  ],
  "brush_worlds": [
    {
      "name": "brush.stair_room",
      "materials": [{ "name": "mat.default", "albedo": [0.5, 0.5, 0.5, 1.0] }],
      "brushes": [
        {
          "name": "brush.floor",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 3.0 }, "material": "mat.default" },
            { "plane": { "normal": [-1, 0, 0], "distance": 3.0 }, "material": "mat.default" },
            { "plane": { "normal": [0, 1, 0], "distance": 0.0 }, "material": "mat.default" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.5 }, "material": "mat.default" },
            { "plane": { "normal": [0, 0, 1], "distance": 3.0 }, "material": "mat.default" },
            { "plane": { "normal": [0, 0, -1], "distance": 3.0 }, "material": "mat.default" }
          ]
        },
        {
          "name": "brush.step",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1.5 }, "material": "mat.default" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1.5 }, "material": "mat.default" },
            { "plane": { "normal": [0, 1, 0], "distance": 0.30 }, "material": "mat.default" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.5 }, "material": "mat.default" },
            { "plane": { "normal": [0, 0, 1], "distance": 0.55 }, "material": "mat.default" },
            { "plane": { "normal": [0, 0, -1], "distance": 1.5 }, "material": "mat.default" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "fps_brush_stair.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);

    slayer3d_camera3d initial_camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &initial_camera));
    const float initial_eye_y = player->position.y;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int forward = slayer3d_game_data_find_action(runtime, "action.move.forward");
    ASSERT_GE(forward, 0);
    slayer3d_input_set_action_override(input, forward, 1.0f);

    bool stepped = false;
    slayer3d_camera3d stepped_camera{};
    for (int i = 0; i < 80; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(4000 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
        if (slayer3d_properties_get_bool(player->props, "brush_step_up", false))
        {
            stepped = true;
            ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &stepped_camera));
            break;
        }
    }

    ASSERT_TRUE(stepped);
    EXPECT_GT(player->position.y, initial_eye_y + 0.20f);
    EXPECT_LT(stepped_camera.position.y, player->position.y - 0.15f);
    EXPECT_NEAR(stepped_camera.position.y, initial_camera.position.y, 0.10f);
    EXPECT_LT(slayer3d_properties_get_float(player->props, "view_smooth", 0.0f), -0.15f);

    slayer3d_input_set_action_override(input, forward, 0.0f);
    float previous_camera_y = stepped_camera.position.y;
    for (int i = 0; i < 30; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(5000 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
        slayer3d_camera3d camera{};
        ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &camera));
        EXPECT_GE(camera.position.y + 0.001f, previous_camera_y);
        EXPECT_LE(camera.position.y, player->position.y + 0.001f);
        previous_camera_y = camera.position.y;
    }
    EXPECT_NEAR(previous_camera_y, player->position.y, 0.03f);

    const float top_eye_y = player->position.y;
    slayer3d_camera3d top_camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &top_camera));
    EXPECT_NEAR(top_camera.position.y, top_eye_y, 0.03f);

    const int back = slayer3d_game_data_find_action(runtime, "action.move.back");
    ASSERT_GE(back, 0);
    slayer3d_input_set_action_override(input, back, 1.0f);

    bool descended = false;
    slayer3d_camera3d descended_camera{};
    for (int i = 0; i < 80; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(6000 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
        if (player->position.y < top_eye_y - 0.20f)
        {
            descended = true;
            ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &descended_camera));
            break;
        }
    }

    ASSERT_TRUE(descended);
    EXPECT_NEAR(player->position.y, initial_eye_y, 0.04f);
    EXPECT_GT(descended_camera.position.y, player->position.y + 0.15f);
    EXPECT_NEAR(descended_camera.position.y, top_camera.position.y, 0.10f);
    EXPECT_GT(slayer3d_properties_get_float(player->props, "view_smooth", 0.0f), 0.15f);

    slayer3d_input_set_action_override(input, back, 0.0f);
    previous_camera_y = descended_camera.position.y;
    for (int i = 0; i < 30; ++i)
    {
        ASSERT_NE(slayer3d_input_update(input, (Uint64)(7000 + i)), nullptr);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
        slayer3d_camera3d camera{};
        ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &camera));
        EXPECT_LE(camera.position.y - 0.001f, previous_camera_y);
        EXPECT_GE(camera.position.y, player->position.y - 0.001f);
        previous_camera_y = camera.position.y;
    }
    EXPECT_NEAR(previous_camera_y, player->position.y, 0.03f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, PatrolCanUseBrushCollisionAndGrounding)
{
    const std::filesystem::path dir = unique_test_dir("patrol_brush_collision");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.patrol"],
  "world": {
    "brush_worlds": [
      { "world": "brush.patrol_room", "position": [0.0, 0.0, 0.0] }
    ]
  }
})json");
    write_text(dir / "patrol_brush.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Patrol Brush Collision Test" },
  "world": { "name": "world.patrol_brush", "kind": "brush" },
  "entities": [
    {
      "name": "entity.patrol",
      "active": true,
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "walk_anim_time": { "type": "float", "value": 0.0 },
        "on_ground": { "type": "bool", "value": false }
      },
      "components": [
        {
          "type": "motion.patrol",
          "waypoints": [[0.0, 0.0, 0.0], [0.0, 0.0, 5.0]],
          "speed": 6.0,
          "wait_time": 0.0,
          "arrival_radius": 0.05,
          "mode": "loop",
          "start_idle": false,
          "yaw_property": "yaw",
          "yaw_forward": "+z",
          "face_target": true,
          "animation_time_property": "walk_anim_time",
          "collision": {
            "type": "brush",
            "shape": "aabb",
            "extents": [0.25, 0.5, 0.25],
            "center_offset": [0.0, 0.5, 0.0],
            "contents_mask": ["solid", "player_clip"],
            "slide_iterations": 4,
            "ground_probe_distance": 0.2,
            "walkable_normal_y": 0.65,
            "on_ground_property": "on_ground"
          }
        }
      ]
    }
  ],
  "brush_worlds": [
    {
      "name": "brush.patrol_room",
      "materials": [{ "name": "mat", "albedo": [0.5, 0.5, 0.5, 1.0] }],
      "brushes": [
        {
          "name": "brush.floor",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 4.0 }, "material": "mat" },
            { "plane": { "normal": [-1, 0, 0], "distance": 4.0 }, "material": "mat" },
            { "plane": { "normal": [0, 1, 0], "distance": 0.0 }, "material": "mat" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.5 }, "material": "mat" },
            { "plane": { "normal": [0, 0, 1], "distance": 4.0 }, "material": "mat" },
            { "plane": { "normal": [0, 0, -1], "distance": 4.0 }, "material": "mat" }
          ]
        },
        {
          "name": "brush.wall",
          "contents": ["solid", "player_clip"],
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1.0 }, "material": "mat" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1.0 }, "material": "mat" },
            { "plane": { "normal": [0, 1, 0], "distance": 2.0 }, "material": "mat" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.0 }, "material": "mat" },
            { "plane": { "normal": [0, 0, 1], "distance": 1.85 }, "material": "mat" },
            { "plane": { "normal": [0, 0, -1], "distance": -1.5 }, "material": "mat" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "patrol_brush.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, "entity.patrol");
    ASSERT_NE(actor, nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.5f));

    EXPECT_LT(actor->position.z, 1.35f);
    EXPECT_GT(actor->position.z, 1.0f);
    EXPECT_NEAR(actor->position.x, 0.0f, 0.03f);
    EXPECT_NEAR(actor->position.y, 0.0f, 0.03f);
    EXPECT_TRUE(slayer3d_properties_get_bool(actor->props, "on_ground", false));
    EXPECT_GT(slayer3d_properties_get_float(actor->props, "walk_anim_time", 0.0f), 0.0f);
    EXPECT_NEAR(slayer3d_properties_get_float(actor->props, "yaw", 1.0f), 0.0f, 0.05f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, SectorLightingModulatesLitActorsAndCanUpdateAtRuntime)
{
    const std::filesystem::path dir = unique_test_dir("sector_lighting_runtime");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.lit", "entity.unlit"],
  "world": {
    "sector_levels": [
      { "level": "sector.lighting", "variant": "lightmapped" }
    ]
  }
})json");
    write_text(dir / "sector_lighting_runtime.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Lighting Runtime Test" },
  "world": { "name": "world.sector_lighting", "kind": "sector" },
  "sector_levels": [
    {
      "name": "sector.lighting",
      "materials": [{ "name": "mat", "albedo": [1.0, 1.0, 1.0, 1.0] }],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [5, 0], [5, 5], [0, 5]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "floor_material": "mat",
          "ceil_material": "mat",
          "wall_material": "mat",
          "lighting": { "level": 128, "color": [1.0, 0.0, 0.0, 1.0] }
        }
      ]
    }
  ],
  "entities": [
    {
      "name": "entity.lit",
      "active": true,
      "transform": { "position": [2.0, 1.0, 2.0] },
      "components": [
        { "type": "render.cube", "size": [1.0, 1.0, 1.0], "color": [200, 100, 50, 255], "lighting": true }
      ]
    },
    {
      "name": "entity.unlit",
      "active": true,
      "transform": { "position": [3.0, 1.0, 2.0] },
      "components": [
        { "type": "render.sphere", "radius": 0.5, "color": [200, 100, 50, 255], "lighting": false }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_lighting_runtime.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    struct SectorActorCapture
    {
        slayer3d_color lit{};
        slayer3d_color unlit{};
        bool saw_lit = false;
        bool saw_unlit = false;
    } capture;
    auto capture_actor = [](void *userdata, const slayer3d_game_data_render_primitive *primitive) -> bool {
        auto *data = static_cast<SectorActorCapture *>(userdata);
        const std::string name = primitive->entity_name != nullptr ? primitive->entity_name : "";
        if (name == "entity.lit")
        {
            data->lit = primitive->color;
            data->saw_lit = true;
        }
        if (name == "entity.unlit")
        {
            data->unlit = primitive->color;
            data->saw_unlit = true;
        }
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_actor, &capture));
    ASSERT_TRUE(capture.saw_lit);
    ASSERT_TRUE(capture.saw_unlit);
    EXPECT_EQ(capture.lit.r, 100);
    EXPECT_EQ(capture.lit.g, 0);
    EXPECT_EQ(capture.lit.b, 0);
    EXPECT_EQ(capture.unlit.r, 200);
    EXPECT_EQ(capture.unlit.g, 100);
    EXPECT_EQ(capture.unlit.b, 50);

    float level = 0.0f;
    float color[4]{};
    ASSERT_TRUE(
        slayer3d_game_data_get_sector_lighting(runtime, "sector.lighting", "room", &level, color, error, sizeof(error)))
        << error;
    EXPECT_FLOAT_EQ(level, 128.0f);
    EXPECT_FLOAT_EQ(color[0], 1.0f);
    EXPECT_FLOAT_EQ(color[3], 1.0f);

    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    ASSERT_TRUE(
        slayer3d_game_data_set_sector_lighting(runtime, "sector.lighting", "room", 255.0f, green, error, sizeof(error)))
        << error;
    capture = {};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_actor, &capture));
    ASSERT_TRUE(capture.saw_lit);
    EXPECT_EQ(capture.lit.r, 0);
    EXPECT_EQ(capture.lit.g, 100);
    EXPECT_EQ(capture.lit.b, 0);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RunsAuthoredSectorNavigationQueries)
{
    const std::filesystem::path dir = unique_test_dir("sector_navigation");
    write_text(dir / "scripts" / "nav.lua",
               R"lua(
local nav = {}
function nav.inspect(_, _, ctx)
    local start = Vec3(1.0, 1.0, 1.0)
    local goal = Vec3(9.0, 1.0, 1.0)
    local next_node = ctx:sector_nav_next_node("nav.test", start, goal)
    ctx:state_set("lua_path_available", ctx:sector_nav_path_available("nav.test", start, goal))
    ctx:state_set("lua_next_node", next_node ~= nil and next_node.name or "none")
    local path = ctx:sector_nav_path("nav.test", start, goal)
    ctx:state_set("lua_path_count", path ~= nil and #path or 0)
end
return nav
)lua");
    write_text(dir / "sector_navigation.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Navigation Test" },
  "scripts": [
    { "id": "script.nav", "path": "scripts/nav.lua", "module": "test.nav" }
  ],
  "adapters": [
    { "name": "adapter.nav.inspect", "kind": "action", "script": "script.nav", "function": "inspect" }
  ],
  "signals": ["signal.nav.inspect"],
  "logic": {
    "bindings": [
      { "signal": "signal.nav.inspect", "actions": [{ "type": "adapter.invoke", "adapter": "adapter.nav.inspect" }] }
    ]
  },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [
        { "name": "floor", "albedo": [0.7, 0.7, 0.7, 1.0] },
        { "name": "wall", "albedo": [0.2, 0.25, 0.35, 1.0] }
      ],
      "sectors": [
        { "name": "room", "points": [[0, 0], [4, 0], [4, 4], [0, 4]], "floor_y": 0.0, "ceil_y": 3.0, "floor_material": "floor", "ceil_material": "wall", "wall_material": "wall" },
        { "name": "hall", "points": [[4, 0], [8, 0], [8, 4], [4, 4]], "floor_y": 0.0, "ceil_y": 3.0, "floor_material": "floor", "ceil_material": "wall", "wall_material": "wall" },
        { "name": "goal", "points": [[8, 0], [12, 0], [12, 4], [8, 4]], "floor_y": 0.0, "ceil_y": 3.0, "floor_material": "floor", "ceil_material": "wall", "wall_material": "wall" },
        { "name": "isolated", "points": [[20, 0], [24, 0], [24, 4], [20, 4]], "floor_y": 0.0, "ceil_y": 3.0, "floor_material": "floor", "ceil_material": "wall", "wall_material": "wall" }
      ]
    }
  ],
  "sector_navigation": [
    {
      "name": "nav.test",
      "sector_level": "sector.test",
      "nodes": [
        { "name": "room.center", "sector": "room", "position": [2.0, 1.0, 2.0] },
        { "name": "hall.center", "sector": "hall", "position": [6.0, 1.0, 2.0] },
        { "name": "goal.center", "sector": "goal", "position": [10.0, 1.0, 2.0] },
        { "name": "isolated.center", "sector": "isolated", "position": [22.0, 1.0, 2.0] }
      ],
      "links": [
        { "from": "room.center", "to": "hall.center" },
        { "from": "hall.center", "to": "goal.center" }
      ]
    }
  ]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_navigation.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_game_data_sector_nav_node nearest{};
    ASSERT_TRUE(slayer3d_game_data_sector_nav_nearest_node(runtime, "nav.test", slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                                                           &nearest));
    EXPECT_STREQ(nearest.name, "room.center");
    EXPECT_EQ(nearest.sector_index, 0);

    slayer3d_game_data_sector_nav_node path[4]{};
    int node_count = 0;
    float cost = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_sector_nav_path(runtime, "nav.test", slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                                                   slayer3d_vec3_make(9.0f, 1.0f, 1.0f), path, 4, &node_count, &cost));
    ASSERT_EQ(node_count, 3);
    EXPECT_STREQ(path[0].name, "room.center");
    EXPECT_STREQ(path[1].name, "hall.center");
    EXPECT_STREQ(path[2].name, "goal.center");
    EXPECT_GT(cost, 0.0f);

    slayer3d_game_data_sector_nav_node next{};
    ASSERT_TRUE(slayer3d_game_data_sector_nav_next_node(runtime, "nav.test", slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                                                        slayer3d_vec3_make(9.0f, 1.0f, 1.0f), &next));
    EXPECT_STREQ(next.name, "hall.center");
    EXPECT_FALSE(slayer3d_game_data_sector_nav_path_available(runtime, "nav.test", slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                                                              slayer3d_vec3_make(22.0f, 1.0f, 2.0f)));

    const int signal = slayer3d_game_data_find_signal(runtime, "signal.nav.inspect");
    ASSERT_GE(signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), signal, nullptr);
    const slayer3d_properties *scene_state = slayer3d_game_data_scene_state(runtime);
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "lua_path_available", false));
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "lua_next_node", ""), "hall.center");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "lua_path_count", 0), 3);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, FpsTemplateLoadsDataOnlyStarter)
{
    const std::filesystem::path template_path = fps_template_data_path();
    ASSERT_TRUE(std::filesystem::exists(template_path)) << template_path;

    char validate_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_validate_file(template_path.string().c_str(), nullptr, validate_error,
                                                 sizeof(validate_error)))
        << validate_error;

    slayer3d_game_config config{};
    char title[128]{};
    char config_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(template_path.string().c_str(), &config, title, sizeof(title),
                                                        config_error, sizeof(config_error)))
        << config_error;
    EXPECT_STREQ(config.title, "Slayer 3D FPS Template");
    EXPECT_EQ(config.logical_width, SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH);
    EXPECT_EQ(config.logical_height, SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(template_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);

    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.fps.play");
    const char *units = nullptr;
    float meters_per_unit = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_get_world_units(runtime, &units, &meters_per_unit));
    EXPECT_STREQ(units, SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS);
    EXPECT_FLOAT_EQ(meters_per_unit, SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT);

    slayer3d_game_data_sector_level level{};
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.fps.template_room", &level));
    ASSERT_EQ(level.sector_count, 2);
    ASSERT_NE(level.sector_names, nullptr);
    EXPECT_STREQ(level.sector_names[0], "spawn_room");
    EXPECT_STREQ(level.sector_names[1], "test_lane");
    EXPECT_EQ(level.light_count, 2);
    EXPECT_TRUE(slayer3d_game_data_sector_nav_path_available(
        runtime, "nav.fps.template_room", slayer3d_vec3_make(4.0f, 1.0f, 5.0f), slayer3d_vec3_make(22.0f, 1.0f, 5.0f)));

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *pickup = slayer3d_game_data_find_actor(runtime, "entity.fps.health_pickup");
    slayer3d_registered_actor *station = slayer3d_game_data_find_actor(runtime, "entity.fps.resource_station");
    slayer3d_registered_actor *projectile = slayer3d_game_data_find_actor(runtime, "pool.fps.player_projectiles.0");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(pickup, nullptr);
    ASSERT_NE(station, nullptr);
    ASSERT_NE(projectile, nullptr);
    EXPECT_TRUE(player->active);
    EXPECT_TRUE(pickup->active);
    EXPECT_TRUE(station->active);
    EXPECT_FALSE(projectile->active);

    struct FpsTemplateUiCapture
    {
        bool saw_reticle = false;
        bool saw_fps = false;
        bool saw_resources = false;
        bool saw_pause = false;
    } ui_capture;
    auto capture_fps_template_ui = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        auto *capture = static_cast<FpsTemplateUiCapture *>(userdata);
        if (std::string(text->name) == "ui.fps.reticle")
            capture->saw_reticle = true;
        else if (std::string(text->name) == "ui.fps.fps_counter")
            capture->saw_fps = true;
        else if (std::string(text->name) == "ui.fps.resources")
            capture->saw_resources = true;
        else if (std::string(text->name) == "ui.fps.pause.title")
            capture->saw_pause = true;
        return true;
    };
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, capture_fps_template_ui, &ui_capture));
    EXPECT_TRUE(ui_capture.saw_reticle);
    EXPECT_TRUE(ui_capture.saw_fps);
    EXPECT_TRUE(ui_capture.saw_resources);
    EXPECT_TRUE(ui_capture.saw_pause);

    const int fire_action = slayer3d_game_data_find_action(runtime, "action.fire");
    ASSERT_GE(fire_action, 0);
    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    slayer3d_input_set_action_override(input, fire_action, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 1), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(projectile->active);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "ammo", -1), 23);
    const slayer3d_vec3 projectile_velocity =
        slayer3d_properties_get_vec3(projectile->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(slayer3d_vec3_length(projectile_velocity), 22.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ResolvesActiveSceneSectorLevelInstances)
{
    const std::filesystem::path dir = unique_test_dir("sector_level_scene");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "camera": "camera.fixed",
  "world": {
    "sector_levels": [
      {
        "level": "sector.test",
        "variant": "unlit",
        "position": [1.0, 2.0, 3.0],
        "portal_culling": false
      }
    ]
  }
})json");
    write_text(dir / "sector_scene.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Scene Test" },
  "world": {
    "name": "world.sector_scene",
    "kind": "sector",
    "cameras": [
      {
        "name": "camera.fixed",
        "type": "perspective",
        "position": [2.0, 1.5, -4.0],
        "target": [2.0, 1.5, 2.0],
        "up": [0.0, 1.0, 0.0],
        "fovy": 60.0,
        "active": true
      }
    ]
  },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [
        { "name": "floor", "albedo": [0.7, 0.7, 0.7, 1.0] },
        { "name": "wall", "albedo": [0.2, 0.25, 0.35, 1.0] }
      ],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "floor_material": "floor",
          "ceil_material": "wall",
          "wall_material": "wall"
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_scene.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_game_data_sector_level level{};
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.test", &level));

    SectorLevelInstanceCapture capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_sector_level_instance(runtime, capture_sector_level_instance, &capture));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.level_name, "sector.test");
    EXPECT_EQ(capture.variant_name, "unlit");
    EXPECT_EQ(capture.variant, SLAYER3D_GAME_DATA_SECTOR_LEVEL_UNLIT);
    EXPECT_EQ(capture.level, level.unlit);
    expect_vec3_near(capture.position, slayer3d_vec3_make(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(capture.portal_culling);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidSectorNavigationGraphs)
{
    const std::filesystem::path dir = unique_test_dir("sector_navigation_invalid");
    struct InvalidSectorNavigationCase
    {
        const char *name;
        const char *navigation_json;
        const char *error_substring;
    };
    const InvalidSectorNavigationCase cases[] = {
        {"unknown_sector",
         R"json([{
           "name": "nav.bad",
           "sector_level": "sector.test",
           "nodes": [{ "name": "a", "sector": "missing", "position": [1.0, 1.0, 1.0] }]
         }])json",
         "unknown sector navigation node sector"},
        {"unknown_link_node",
         R"json([{
           "name": "nav.bad",
           "sector_level": "sector.test",
           "nodes": [{ "name": "a", "sector": "room", "position": [1.0, 1.0, 1.0] }],
           "links": [{ "from": "a", "to": "missing" }]
         }])json",
         "unknown sector navigation node reference"},
        {"bad_cost",
         R"json([{
           "name": "nav.bad",
           "sector_level": "sector.test",
           "nodes": [
             { "name": "a", "sector": "room", "position": [1.0, 1.0, 1.0] },
             { "name": "b", "sector": "room", "position": [2.0, 1.0, 1.0] }
           ],
           "links": [{ "from": "a", "to": "b", "cost": 0.0 }]
         }])json",
         "sector navigation link cost must be positive"},
    };

    for (const auto &test_case : cases)
    {
        const std::string game_json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Sector Navigation" },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [
        { "name": "floor", "albedo": [0.7, 0.7, 0.7, 1.0] },
        { "name": "wall", "albedo": [0.2, 0.25, 0.35, 1.0] }
      ],
      "sectors": [{
        "name": "room",
        "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
        "floor_y": 0.0,
        "ceil_y": 3.0,
        "floor_material": "floor",
        "ceil_material": "wall",
        "wall_material": "wall"
      }]
    }
  ],
  "sector_navigation": )json") + test_case.navigation_json +
                                      "\n}\n";
        write_text(dir / (std::string(test_case.name) + ".game.json"), game_json.c_str());
        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(
            (dir / (std::string(test_case.name) + ".game.json")).string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.error_substring), std::string::npos) << error;
    }
    remove_test_dir(dir);
}

TEST(GameDataRuntime, SectorLevelFragmentsComposeIntoNamedLevels)
{
    const std::filesystem::path dir = unique_test_dir("sector_level_fragments");
    write_text(dir / "fragments" / "materials.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "sector_level_fragments": [
    {
      "level": "sector.test",
      "materials": [
        { "name": "floor", "albedo": [0.7, 0.7, 0.7, 1.0] },
        { "name": "wall", "albedo": [0.2, 0.25, 0.35, 1.0] }
      ]
    }
  ]
})json");
    write_text(dir / "fragments" / "sectors.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "sector_level_fragments": [
    {
      "level": "sector.test",
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "floor_material": "floor",
          "ceil_material": "wall",
          "wall_material": "wall"
        }
      ]
    }
  ]
})json");
    write_text(dir / "fragments" / "lights.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "sector_level_fragments": [
    {
      "level": "sector.test",
      "lights": [
        { "position": [2.0, 2.5, 2.0], "color": [1.0, 0.8, 0.6], "intensity": 1.5, "range": 5.0 }
      ]
    }
  ]
})json");
    write_text(dir / "sector_fragments.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Fragments", "id": "test.sector_fragments", "version": "0.1.0" },
  "imports": [
    { "path": "fragments/materials.json", "sections": ["sector_level_fragments"] },
    { "path": "fragments/sectors.json", "sections": ["sector_level_fragments"] },
    { "path": "fragments/lights.json", "sections": ["sector_level_fragments"] }
  ],
  "world": { "name": "world.sector_fragments", "kind": "sector" },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play" })json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_fragments.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_game_data_sector_level level{};
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.test", &level));
    ASSERT_EQ(level.material_count, 2);
    ASSERT_EQ(level.sector_count, 1);
    ASSERT_EQ(level.light_count, 1);
    EXPECT_NEAR(level.materials[0].albedo[0], 0.7f, 0.0001f);
    EXPECT_STREQ(level.sector_names[0], "room");
    EXPECT_FLOAT_EQ(level.lights[0].intensity, 1.5f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RunsAuthoredFpsSectorController)
{
    const std::filesystem::path dir = unique_test_dir("fps_sector_controller");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "camera": "camera.player",
  "updates_game": true,
  "entities": ["entity.player"],
  "input": {
    "actions": [
      "action.move.forward",
      "action.move.back",
      "action.move.left",
      "action.move.right",
      "action.jump"
    ]
  },
  "world": {
    "sector_levels": [
      { "level": "sector.test", "variant": "unlit" }
    ]
  }
})json");
    write_text(dir / "fps_sector.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "FPS Sector Controller Test" },
  "world": {
    "name": "world.test",
    "kind": "sector",
    "cameras": [
      {
        "name": "camera.player",
        "type": "fps",
        "target_entity": "entity.player",
        "fovy": 70.0,
        "active": true
      }
    ]
  },
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" },
          { "name": "action.jump" }
        ]
      }
    ]
  },
  "signals": ["signal.launch", "signal.teleport"],
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [2.0, 1.6, 2.0] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "pitch": { "type": "float", "value": 0.0 },
        "current_sector": { "type": "int", "value": -1 }
      },
      "components": [
        {
          "type": "controller.fps_sector",
          "sector_level": "sector.test",
          "actions": {
            "forward": "action.move.forward",
            "back": "action.move.back",
            "left": "action.move.left",
            "right": "action.move.right",
            "jump": "action.jump"
          },
          "move_speed": 4.0,
          "jump_velocity": 4.0,
          "gravity": 9.0,
          "player_height": 1.6,
          "player_radius": 0.25,
          "step_height": 0.6,
          "ceiling_clearance": 0.1,
          "mouse_sensitivity": 0.0
        }
      ]
    }
  ],
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [
        { "name": "floor", "albedo": [0.6, 0.6, 0.6, 1.0] },
        { "name": "wall", "albedo": [0.2, 0.2, 0.25, 1.0] }
      ],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "floor_material": "floor",
          "ceil_material": "wall",
          "wall_material": "wall"
        }
      ]
    }
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.launch",
        "actions": [
          {
            "type": "controller.fps_sector.launch",
            "target": "entity.player",
            "vertical_velocity": 5.0
          }
        ]
      },
      {
        "signal": "signal.teleport",
        "actions": [
          {
            "type": "controller.fps_sector.teleport",
            "target": "entity.player",
            "position": [3.0, 1.8, 3.0],
            "yaw": 1.25,
            "pitch": -0.25
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "fps_sector.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    const float initial_z = player->position.z;

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int forward = slayer3d_game_data_find_action(runtime, "action.move.forward");
    ASSERT_GE(forward, 0);
    slayer3d_input_set_action_override(input, forward, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 1000), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));

    EXPECT_LT(player->position.z, initial_z);
    EXPECT_NEAR(player->position.y, 1.6f, 0.001f);
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "current_sector", -1), 0);
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "on_ground", false));
    expect_vec3_near(
        slayer3d_properties_get_vec3(player->props, "camera_forward", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
        slayer3d_vec3_make(0.0f, 0.0f, -1.0f));

    slayer3d_camera3d camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player", &camera));
    EXPECT_EQ(camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_NEAR(camera.position.x, player->position.x, 0.001f);
    EXPECT_NEAR(camera.position.y, player->position.y, 0.001f);
    EXPECT_NEAR(camera.position.z, player->position.z, 0.001f);
    EXPECT_LT(camera.target.z, camera.position.z);

    const int launch_signal = slayer3d_game_data_find_signal(runtime, "signal.launch");
    ASSERT_GE(launch_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), launch_signal, nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "vertical_velocity", 0.0f), 5.0f, 0.001f);

    const int teleport_signal = slayer3d_game_data_find_signal(runtime, "signal.teleport");
    ASSERT_GE(teleport_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), teleport_signal, nullptr);
    expect_vec3_near(player->position, slayer3d_vec3_make(3.0f, 1.8f, 3.0f));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "yaw", 0.0f), 1.25f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "pitch", 0.0f), -0.25f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, SectorVelocityMotionDespawnsPooledActorsOnImpact)
{
    const std::filesystem::path dir = unique_test_dir("sector_velocity_motion");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player"],
  "world": {
    "sector_levels": [
      { "level": "sector.test", "variant": "unlit" }
    ]
  }
})json");
    write_text(dir / "sector_velocity.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Velocity Motion Test" },
  "world": { "name": "world.test", "kind": "sector" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [2.0, 1.5, 2.0] },
      "properties": {
        "camera_forward": { "type": "vec3", "value": [1.0, 0.0, 0.0] },
        "fire_timer": { "type": "float", "value": 0.0 }
      }
    }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "properties": {
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] }
      },
      "components": [
        {
          "type": "motion.sector_velocity_3d",
          "property": "velocity",
          "sector_level": "sector.test",
          "reason": "hit wall"
        }
      ]
    }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1, "scene": "scene.play" }
  ],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "projectile.fire",
            "target": "entity.player",
            "pool": "pool.shots",
            "velocity_from_property": "camera_forward",
            "speed": 20.0
          }
        ]
      }
    ]
  },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "wall" }],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "wall_material": "wall"
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_velocity.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_registered_actor *shot = slayer3d_game_data_find_actor(runtime, "pool.shots.0");
    ASSERT_NE(shot, nullptr);
    ASSERT_FALSE(shot->active);
    const int fire_signal = slayer3d_game_data_find_signal(runtime, "signal.fire");
    ASSERT_GE(fire_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), fire_signal, nullptr);
    ASSERT_TRUE(shot->active);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.05f));
    EXPECT_TRUE(shot->active);
    EXPECT_NEAR(shot->position.x, 3.0f, 0.3f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    EXPECT_FALSE(shot->active);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, BrushVelocityMotionDespawnsPooledActorsOnImpact)
{
    const std::filesystem::path dir = unique_test_dir("brush_velocity_motion");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player"],
  "world": { "brush_worlds": [{ "world": "brush.test" }] }
})json");
    write_text(dir / "brush_velocity.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush Velocity Motion Test" },
  "world": { "name": "world.test", "kind": "brush" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [0.0, 1.5, 2.0] },
      "properties": {
        "camera_forward": { "type": "vec3", "value": [1.0, 0.0, 0.0] },
        "fire_timer": { "type": "float", "value": 0.0 }
      }
    }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "properties": {
        "velocity": { "type": "vec3", "value": [0.0, 0.0, 0.0] },
        "radius": { "type": "float", "value": 0.25 }
      },
      "components": [
        {
          "type": "motion.brush_velocity_3d",
          "property": "velocity",
          "shape": "sphere",
          "contents_mask": ["solid", "projectile_clip"],
          "impact_actions": [
            { "type": "property.set", "target": "entity.player", "key": "last_hit_brush", "value_from_payload": "hit_brush_name" },
            { "type": "property.set", "target": "entity.player", "key": "last_hit_material", "value_from_payload": "hit_material" },
            { "type": "property.set", "target": "entity.player", "key": "last_hit_distance", "value_from_payload": "hit_distance" }
          ],
          "reason": "hit brush"
        }
      ]
    }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1, "scene": "scene.play" }
  ],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "projectile.fire",
            "target": "entity.player",
            "pool": "pool.shots",
            "velocity_from_property": "camera_forward",
            "speed": 20.0
          }
        ]
      }
    ]
  },
  "brush_worlds": [
    {
      "name": "brush.test",
      "materials": [{ "name": "mat.wall", "albedo": [0.35, 0.35, 0.38, 1.0] }],
      "brushes": [
        {
          "name": "brush.projectile_wall",
          "contents": ["solid", "projectile_clip"],
          "faces": [
            { "plane": { "normal": [ 1, 0, 0], "distance": 4.0 }, "material": "mat.wall" },
            { "plane": { "normal": [-1, 0, 0], "distance": -3.5 }, "material": "mat.wall" },
            { "plane": { "normal": [0,  1, 0], "distance": 3.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0,  1], "distance": 4.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0, -1], "distance": -0.5 }, "material": "mat.wall" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "brush_velocity.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_registered_actor *shot = slayer3d_game_data_find_actor(runtime, "pool.shots.0");
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(shot, nullptr);
    ASSERT_NE(player, nullptr);
    ASSERT_FALSE(shot->active);
    const int fire_signal = slayer3d_game_data_find_signal(runtime, "signal.fire");
    ASSERT_GE(fire_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), fire_signal, nullptr);
    ASSERT_TRUE(shot->active);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    EXPECT_TRUE(shot->active);
    EXPECT_NEAR(shot->position.x, 2.0f, 0.05f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.2f));
    EXPECT_FALSE(shot->active);
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "last_hit_brush", ""), "brush.projectile_wall");
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "last_hit_material", ""), "mat.wall");
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "last_hit_distance", 0.0f), 1.25f, 0.1f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, VolumeSensorsRunEnterAndExitActions)
{
    const std::filesystem::path dir = unique_test_dir("volume_sensor_actions");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "camera": "camera.player",
  "updates_game": true,
  "entities": ["entity.player"]
})json");
    write_text(dir / "volume_sensor.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Volume Sensor Test" },
  "world": {
    "name": "world.volume",
    "kind": "3d",
    "cameras": [
      { "name": "camera.player", "type": "perspective", "position": [0.0, 2.0, 5.0], "target": [0.0, 0.0, 0.0], "active": true },
      { "name": "camera.zone", "type": "perspective", "position": [4.0, 3.0, 2.0], "target": [0.0, 0.0, 0.0] }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "zone_entries": { "type": "int", "value": 0 },
        "zone_exits": { "type": "int", "value": 0 }
      }
    }
  ],
  "logic": {
    "sensors": [
      {
        "name": "sensor.zone.enter",
        "type": "sensor.volume",
        "actor": "entity.player",
        "min": [1.0, -1.0, -1.0],
        "max": [3.0, 1.0, 1.0],
        "edge": "enter",
        "actions": [
          { "type": "camera.set", "camera": "camera.zone" },
          { "type": "property.add", "target_from_payload": "actor_name", "key": "zone_entries", "value": 1 }
        ]
      },
      {
        "name": "sensor.zone.exit",
        "type": "sensor.volume",
        "actor": "entity.player",
        "min": [1.0, -1.0, -1.0],
        "max": [3.0, 1.0, 1.0],
        "edge": "exit",
        "actions": [
          { "type": "camera.set", "camera": "camera.player" },
          { "type": "property.add", "target_from_payload": "actor_name", "key": "zone_exits", "value": 1 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "volume_sensor.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.player");

    player->position = slayer3d_vec3_make(2.0f, 0.0f, 0.0f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.zone");
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "zone_entries", 0), 1);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "zone_entries", 0), 1);

    player->position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.player");
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "zone_exits", 0), 1);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RunsAuthoredSectorDoorInteractionRenderAndCollision)
{
    const std::filesystem::path dir = unique_test_dir("sector_doors");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "camera": "camera.player",
  "updates_game": true,
  "entities": ["entity.player"],
  "input": { "actions": ["action.move.forward"] },
  "world": { "sector_levels": [{ "level": "sector.test", "variant": "unlit" }] }
})json");
    write_text(dir / "sector_doors.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Door Test" },
  "world": {
    "name": "world.test",
    "kind": "sector",
    "cameras": [
      { "name": "camera.player", "type": "fps", "target_entity": "entity.player", "fovy": 70.0, "active": true }
    ]
  },
  "signals": ["signal.interact"],
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" }
        ]
      }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [2.0, 1.6, 2.0] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "pitch": { "type": "float", "value": 0.0 },
        "current_sector": { "type": "int", "value": -1 }
      },
      "components": [
        {
          "type": "controller.fps_sector",
          "sector_level": "sector.test",
          "actions": {
            "forward": "action.move.forward",
            "back": "action.move.back",
            "left": "action.move.left",
            "right": "action.move.right"
          },
          "move_speed": 4.0,
          "jump_velocity": 0.0,
          "gravity": 0.0,
          "player_height": 1.6,
          "player_radius": 0.25,
          "step_height": 0.2,
          "ceiling_clearance": 0.1,
          "mouse_sensitivity": 0.0
        }
      ]
    }
  ],
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }, { "name": "wall" }],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "floor_material": "floor",
          "ceil_material": "wall",
          "wall_material": "wall"
        }
      ]
    }
  ],
  "sector_doors": [
    {
      "name": "door.test",
      "id": 7,
      "scene": "scene.play",
      "open_seconds": 1.0,
      "close_seconds": 1.0,
      "panels": [
        {
          "bounds": { "min": [1.8, 0.0, 0.8], "max": [2.2, 2.0, 1.2] },
          "open_offset": [0.0, 2.1, 0.0]
        }
      ],
      "render": { "color": [120, 180, 240, 255], "lighting": true }
    }
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.interact",
        "actions": [
          {
            "type": "sector_door.interact",
            "actor": "entity.player",
            "range": 2.0,
            "min_dot": 0.1,
            "actions": [
              { "type": "sector_door.open", "target_from_payload": "door_name", "stay_open_seconds": 0.5 }
            ]
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_doors.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);

    SectorDoorRenderCapture closed_render{};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_render_primitive(runtime, capture_sector_door_render_primitive, &closed_render));
    ASSERT_EQ(closed_render.door_primitives, 1);
    EXPECT_NEAR(closed_render.first_position.y, 1.0f, 0.001f);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int forward = slayer3d_game_data_find_action(runtime, "action.move.forward");
    ASSERT_GE(forward, 0);
    slayer3d_input_set_action_override(input, forward, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 1000), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.2f));
    EXPECT_GT(player->position.z, 1.2f);

    slayer3d_input_set_action_override(input, forward, 0.0f);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session),
                         slayer3d_game_data_find_signal(runtime, "signal.interact"), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f));
    SectorDoorRenderCapture open_render{};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_render_primitive(runtime, capture_sector_door_render_primitive, &open_render));
    ASSERT_EQ(open_render.door_primitives, 1);
    EXPECT_NEAR(open_render.first_position.y, 3.1f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RunsAuthoredSectorPlatform)
{
    const std::filesystem::path dir = unique_test_dir("sector_platforms");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player"],
  "world": { "sector_levels": [{ "level": "sector.test", "variant": "unlit" }] }
})json");
    write_text(dir / "sector_platforms.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Platform Test" },
  "world": { "name": "world.test", "kind": "sector" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "tags": ["player"],
      "transform": { "position": [2.0, 1.2, 2.0] },
      "properties": {
        "current_sector": { "type": "int", "value": 0 },
        "health": { "type": "float", "value": 100.0 },
        "max_health": { "type": "float", "value": 100.0 }
      }
    }
  ],
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }, { "name": "wall" }],
      "sectors": [
        {
          "name": "lift",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 4.0,
          "floor_material": "floor",
          "ceil_material": "wall",
          "wall_material": "wall"
        }
      ]
    }
  ],
  "sector_platforms": [
    {
      "name": "platform.test",
      "scene": "scene.play",
      "sector_level": "sector.test",
      "sector": "lift",
      "min_floor_y": 0.0,
      "max_floor_y": 2.0,
      "ceil_y": 4.0,
      "cycle_seconds": 4.0,
      "rebuild_min_delta": 0.0,
      "crush_damage_per_second": 10.0,
      "crush_clearance": 0.5,
      "crush_actor_tag": "player",
      "damage_type": "crusher",
      "crush_actions": [
        {
          "type": "property.set",
          "target_from_payload": "actor_name",
          "key": "last_platform",
          "value_from_payload": "sector_platform"
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_platforms.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_game_data_sector_level level{};
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.test", &level));
    ASSERT_EQ(level.sector_count, 1);
    EXPECT_NEAR(level.sectors[0].floor_y, 0.0f, 0.001f);
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 100.0f, 0.001f);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f));
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.test", &level));
    EXPECT_NEAR(level.sectors[0].floor_y, 1.0f, 0.001f);
    ASSERT_NE(level.unlit, nullptr);
    ASSERT_EQ(level.unlit->sector_count, 1);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 90.0f, 0.001f);
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "last_platform", ""), "platform.test");

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f));
    ASSERT_TRUE(slayer3d_game_data_get_sector_level(runtime, "sector.test", &level));
    EXPECT_NEAR(level.sectors[0].floor_y, 2.0f, 0.001f);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "health", 0.0f), 80.0f, 0.001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidSectorPlatforms)
{
    const std::filesystem::path dir = unique_test_dir("sector_platforms_invalid");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play" })json");

    struct Case
    {
        const char *name;
        const char *platform_json;
        const char *expected_error;
    };
    const Case cases[] = {
        {
            "unknown_sector",
            R"json([
              {
                "name": "platform.bad",
                "sector_level": "sector.test",
                "sector": "missing",
                "min_floor_y": 0.0,
                "max_floor_y": 2.0,
                "ceil_y": 4.0,
                "cycle_seconds": 4.0
              }
            ])json",
            "unknown sector platform sector",
        },
        {
            "bad_timing",
            R"json([
              {
                "name": "platform.bad",
                "sector_level": "sector.test",
                "sector": "lift",
                "min_floor_y": 0.0,
                "max_floor_y": 2.0,
                "ceil_y": 4.0,
                "cycle_seconds": 0.0
              }
            ])json",
            "cycle_seconds must be positive",
        },
        {
            "bad_crush_damage",
            R"json([
              {
                "name": "platform.bad",
                "sector_level": "sector.test",
                "sector": "lift",
                "min_floor_y": 0.0,
                "max_floor_y": 2.0,
                "ceil_y": 4.0,
                "cycle_seconds": 4.0,
                "crush_damage_per_second": -1.0
              }
            ])json",
            "crush_damage_per_second must be non-negative",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path game_path = dir / (std::string(test_case.name) + ".game.json");
        const std::string game_json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Sector Platform" },
  "world": { "name": "world.test", "kind": "sector" },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }],
      "sectors": [{
        "name": "lift",
        "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
        "floor_y": 0,
        "ceil_y": 4,
        "wall_material": "floor"
      }]
    }
  ],
  "sector_platforms": )json") + test_case.platform_json +
                                      R"json(,
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
        write_text(game_path, game_json.c_str());

        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(game_path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidFpsSectorController)
{
    const std::filesystem::path dir = unique_test_dir("fps_sector_controller_invalid");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.player"]
})json");

    struct Case
    {
        const char *name;
        const char *component_json;
        const char *expected_error;
    };
    const Case cases[] = {
        {
            "unknown_level",
            R"json({
              "type": "controller.fps_sector",
              "sector_level": "sector.missing",
              "actions": {
                "forward": "action.move.forward",
                "back": "action.move.back",
                "left": "action.move.left",
                "right": "action.move.right"
              }
            })json",
            "unknown sector level",
        },
        {
            "missing_actions",
            R"json({
              "type": "controller.fps_sector",
              "sector_level": "sector.test"
            })json",
            "requires an actions object",
        },
        {
            "unknown_action",
            R"json({
              "type": "controller.fps_sector",
              "sector_level": "sector.test",
              "actions": {
                "forward": "action.missing",
                "back": "action.move.back",
                "left": "action.move.left",
                "right": "action.move.right"
              }
            })json",
            "unknown input action",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path game_path = dir / (std::string(test_case.name) + ".game.json");
        const std::string game_json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid FPS Sector Controller" },
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" }
        ]
      }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "components": [
)json") + test_case.component_json +
                                      R"json(
      ]
    }
  ],
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }],
      "sectors": [{
        "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
        "floor_y": 0,
        "ceil_y": 3,
        "wall_material": "floor"
      }]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
        write_text(game_path, game_json.c_str());

        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(game_path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidFpsBrushController)
{
    const std::filesystem::path dir = unique_test_dir("fps_brush_controller_invalid");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.player"],
  "world": { "brush_worlds": [{ "world": "brush.test" }] }
})json");

    struct Case
    {
        const char *name;
        const char *component_json;
        const char *expected_error;
    };
    const Case cases[] = {
        {
            "unknown_world",
            R"json({
              "type": "controller.fps_brush",
              "brush_world": "brush.missing",
              "actions": {
                "forward": "action.move.forward",
                "back": "action.move.back",
                "left": "action.move.left",
                "right": "action.move.right"
              }
            })json",
            "unknown brush world",
        },
        {
            "bad_contents_mask",
            R"json({
              "type": "controller.fps_brush",
              "brush_world": "brush.test",
              "contents_mask": ["solid", "bad_content"],
              "actions": {
                "forward": "action.move.forward",
                "back": "action.move.back",
                "left": "action.move.left",
                "right": "action.move.right"
              }
            })json",
            "brush content value is unknown",
        },
        {
            "bad_walkable_normal",
            R"json({
              "type": "controller.fps_brush",
              "brush_world": "brush.test",
              "walkable_normal_y": 1.5,
              "actions": {
                "forward": "action.move.forward",
                "back": "action.move.back",
                "left": "action.move.left",
                "right": "action.move.right"
              }
            })json",
            "walkable_normal_y must be in [0, 1]",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path game_path = dir / (std::string(test_case.name) + ".game.json");
        const std::string game_json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid FPS Brush Controller" },
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" }
        ]
      }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "components": [
)json") + test_case.component_json +
                                      R"json(
      ]
    }
  ],
  "brush_worlds": [
    {
      "name": "brush.test",
      "materials": [{ "name": "mat" }],
      "brushes": [
        {
          "name": "brush.floor",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [1, 0, 0], "distance": 1.0 }, "material": "mat" },
            { "plane": { "normal": [-1, 0, 0], "distance": 1.0 }, "material": "mat" },
            { "plane": { "normal": [0, 1, 0], "distance": 0.0 }, "material": "mat" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.25 }, "material": "mat" }
          ]
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
        write_text(game_path, game_json.c_str());

        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(game_path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, DoomLevelDataLoadsAuthoredSectorDoors)
{
    const std::filesystem::path doom_path = doom_level_data_path();
    ASSERT_TRUE(std::filesystem::exists(doom_path)) << doom_path;
    for (const char *texture_name : {"rock_floor.jpg", "ceiling_metal.jpg", "wall_metal.jpg", "lava.jpg",
                                     "door-hatch.png", "radioactive-crate.png"})
    {
        slayer3d_image image{};
        const std::filesystem::path texture_path = doom_path.parent_path() / "textures" / texture_name;
        ASSERT_TRUE(slayer3d_load_image_from_file(texture_path.string().c_str(), &image))
            << texture_path << ": " << SDL_GetError();
        EXPECT_GE(image.width, 256) << texture_name;
        EXPECT_GE(image.height, 256) << texture_name;
        slayer3d_free_image(&image);
    }

    slayer3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(doom_path.string().c_str(), &config, title, sizeof(title),
                                                        app_error, sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "Slayer 3D Doom Level");
    EXPECT_EQ(config.logical_width, SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH);
    EXPECT_EQ(config.logical_height, SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT);
    EXPECT_EQ(config.backend, SLAYER3D_BACKEND_OPENGL);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(doom_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.doom_level.play"));
    const char *units = nullptr;
    float meters_per_unit = 0.0f;
    ASSERT_TRUE(slayer3d_game_data_get_world_units(runtime, &units, &meters_per_unit));
    EXPECT_STREQ(units, SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS);
    EXPECT_FLOAT_EQ(meters_per_unit, SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT);
    slayer3d_game_data_scene_skybox skybox{};
    ASSERT_TRUE(slayer3d_game_data_get_active_scene_skybox(runtime, &skybox));
    EXPECT_STREQ(skybox.pos_x, "image.doom.skybox.px");
    EXPECT_STREQ(skybox.neg_z, "image.doom.skybox.nz");
    EXPECT_FLOAT_EQ(skybox.size, 400.0f);

    slayer3d_game_data_app_control app{};
    ASSERT_TRUE(slayer3d_game_data_get_app_control(runtime, &app));
    EXPECT_EQ(app.start_signal_id, -1);
    EXPECT_GE(app.pause_action_id, 0);
    EXPECT_GE(app.quit_action_id, 0);
    EXPECT_STREQ(app.startup_transition, "startup");
    EXPECT_STREQ(app.quit_transition, "quit");

    slayer3d_game_context ctx{};
    ctx.session = session;
    slayer3d_game_data_app_flow flow{};
    slayer3d_game_data_app_flow_init(&flow);
    ASSERT_TRUE(slayer3d_game_data_app_flow_start(&flow, runtime));
    EXPECT_TRUE(slayer3d_game_data_app_flow_is_transitioning(&flow));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_P;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 1);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 2);
    key.type = SDL_EVENT_KEY_DOWN;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 3);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 4);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_ESCAPE;
    slayer3d_input_process_event(input, &key);
    slayer3d_input_update(input, 5);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(slayer3d_game_data_app_flow_quit_pending(&flow));
    EXPECT_FALSE(ctx.quit_requested);
    ASSERT_TRUE(slayer3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.36f));
    EXPECT_TRUE(ctx.quit_requested);
    EXPECT_TRUE(slayer3d_game_data_active_scene_mouse_capture(runtime, false));
    EXPECT_FALSE(slayer3d_game_data_active_scene_mouse_capture(runtime, true));

    slayer3d_game_data_render_settings render_settings{};
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render_settings));
    EXPECT_TRUE(render_settings.has_profile);
    EXPECT_STREQ(render_settings.profile_name, "modern");
    EXPECT_TRUE(render_settings.lighting_enabled);
    EXPECT_FALSE(render_settings.ssao_enabled);

    const int lighting_toggle_signal = slayer3d_game_data_find_signal(runtime, "signal.render.lighting.toggle");
    ASSERT_GE(lighting_toggle_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), lighting_toggle_signal, nullptr);
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render_settings));
    EXPECT_FALSE(render_settings.lighting_enabled);

    const int profile_ps1_signal = slayer3d_game_data_find_signal(runtime, "signal.render.profile.ps1");
    ASSERT_GE(profile_ps1_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), profile_ps1_signal, nullptr);
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render_settings));
    EXPECT_STREQ(render_settings.profile_name, "ps1");
    EXPECT_TRUE(render_settings.profile.vertex_snap);
    EXPECT_EQ(render_settings.profile.display_width, 320);
    EXPECT_EQ(render_settings.profile.display_height, 240);
    EXPECT_EQ(render_settings.profile.display_filter, SLAYER3D_DISPLAY_FILTER_NEAREST);
    EXPECT_EQ(render_settings.tonemap, SLAYER3D_TONEMAP_NONE);

    const int profile_grayscale_signal = slayer3d_game_data_find_signal(runtime, "signal.render.profile.grayscale");
    ASSERT_GE(profile_grayscale_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), profile_grayscale_signal, nullptr);
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render_settings));
    EXPECT_STREQ(render_settings.profile_name, "grayscale");
    EXPECT_EQ(render_settings.profile.display_profile, SLAYER3D_DISPLAY_PROFILE_GRAYSCALE);
    EXPECT_EQ(render_settings.profile.display_width, 512);
    EXPECT_EQ(render_settings.profile.display_height, 342);

    const int profile_gameboy_signal = slayer3d_game_data_find_signal(runtime, "signal.render.profile.gameboy");
    ASSERT_GE(profile_gameboy_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), profile_gameboy_signal, nullptr);
    ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &render_settings));
    EXPECT_STREQ(render_settings.profile_name, "gameboy");
    EXPECT_EQ(render_settings.profile.display_profile, SLAYER3D_DISPLAY_PROFILE_GAMEBOY);
    EXPECT_EQ(render_settings.profile.display_width, 160);
    EXPECT_EQ(render_settings.profile.display_height, 144);

    SectorLevelInstanceCapture sector_capture{};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_sector_level_instance(runtime, capture_sector_level_instance, &sector_capture));
    EXPECT_EQ(sector_capture.variant, SLAYER3D_GAME_DATA_SECTOR_LEVEL_LIGHTMAPPED);
    EXPECT_TRUE(sector_capture.portal_culling);
    const int variant_cycle_signal = slayer3d_game_data_find_signal(runtime, "signal.render.variant.cycle");
    ASSERT_GE(variant_cycle_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), variant_cycle_signal, nullptr);
    sector_capture = {};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_sector_level_instance(runtime, capture_sector_level_instance, &sector_capture));
    EXPECT_EQ(sector_capture.variant, SLAYER3D_GAME_DATA_SECTOR_LEVEL_VERTEX_BAKED);
    const int portal_toggle_signal = slayer3d_game_data_find_signal(runtime, "signal.render.portal_culling.toggle");
    ASSERT_GE(portal_toggle_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), portal_toggle_signal, nullptr);
    sector_capture = {};
    ASSERT_TRUE(
        slayer3d_game_data_for_each_sector_level_instance(runtime, capture_sector_level_instance, &sector_capture));
    EXPECT_FALSE(sector_capture.portal_culling);

    DoorPrefixRenderCapture capture{};
    capture.prefix = "door.";
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_door_prefix_render_primitive, &capture));
    EXPECT_EQ(capture.door_primitives, 5);
    EXPECT_EQ(capture.textured_door_primitives, 5);
    ParticleCapture particles{};
    ASSERT_TRUE(slayer3d_game_data_for_each_particle_emitter(runtime, capture_particle, &particles));
    EXPECT_TRUE(particles.saw_nukage_vapor);
    RenderPrimitiveCapture authored_props{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &authored_props));
    EXPECT_TRUE(authored_props.saw_doom_robot_sprite);
    EXPECT_TRUE(authored_props.saw_doom_health_sprite);
    EXPECT_TRUE(authored_props.saw_doom_crate);
    EXPECT_TRUE(authored_props.saw_doom_dragon_model);
    EXPECT_EQ(authored_props.doom_robot_sprites, 5);
    EXPECT_EQ(authored_props.doom_health_sprites, 5);
    EXPECT_EQ(authored_props.doom_crates, 8);
    EXPECT_EQ(authored_props.doom_textured_crates, 8);
    EXPECT_EQ(authored_props.doom_model_primitives, 1);
    EXPECT_EQ(authored_props.doom_presentation_cubes, 14);
    EXPECT_GE(authored_props.sprites, 10);
    slayer3d_game_data_ambient_asset ambient{};
    ASSERT_TRUE(slayer3d_game_data_get_ambient_asset(runtime, "ambient.doom.upper_deck", &ambient));
    EXPECT_EQ(ambient.ambient_id, 1);
    EXPECT_STREQ(ambient.path, "asset://audio/ambient_zone.wav");
    EXPECT_TRUE(ambient.loop);
    EXPECT_NEAR(ambient.volume, 0.45f, 0.0001f);
    UiTextCapture ui_text{};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, capture_ui_text, &ui_text));
    EXPECT_TRUE(ui_text.saw_doom_reticle);
    EXPECT_TRUE(ui_text.saw_doom_profile);
    EXPECT_TRUE(ui_text.saw_doom_fps);
    slayer3d_game_data_font_asset doom_hud_font{};
    ASSERT_TRUE(slayer3d_game_data_get_font_asset(runtime, "font.doom.hud", &doom_hud_font));
    EXPECT_TRUE(doom_hud_font.builtin);
    EXPECT_NEAR(doom_hud_font.size, 22.0f, 0.0001f);
    slayer3d_game_data_ui_text profile_text{};
    bool saw_profile_text = false;
    auto find_doom_profile_text = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.doom_level.profile")
            return true;
        auto *args = static_cast<std::pair<slayer3d_game_data_ui_text *, bool *> *>(userdata);
        *args->first = *text;
        *args->second = true;
        return false;
    };
    std::pair<slayer3d_game_data_ui_text *, bool *> profile_text_args{&profile_text, &saw_profile_text};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_doom_profile_text, &profile_text_args));
    ASSERT_TRUE(saw_profile_text);
    char profile_label[64]{};
    ASSERT_TRUE(
        slayer3d_game_data_format_ui_text(runtime, &profile_text, nullptr, profile_label, sizeof(profile_label)));
    EXPECT_STREQ(profile_label, "PROFILE gameboy");
    slayer3d_game_data_ui_text pause_text{};
    bool saw_pause_text = false;
    auto find_doom_pause_text = [](void *userdata, const slayer3d_game_data_ui_text *text) -> bool {
        if (std::string(text->name) != "ui.doom_level.pause.title")
            return true;
        auto *args = static_cast<std::pair<slayer3d_game_data_ui_text *, bool *> *>(userdata);
        *args->first = *text;
        *args->second = true;
        return false;
    };
    std::pair<slayer3d_game_data_ui_text *, bool *> pause_text_args{&pause_text, &saw_pause_text};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_text(runtime, find_doom_pause_text, &pause_text_args));
    ASSERT_TRUE(saw_pause_text);
    slayer3d_game_data_ui_metrics ui_metrics{};
    ui_metrics.paused = false;
    EXPECT_FALSE(slayer3d_game_data_ui_text_is_visible(runtime, &pause_text, &ui_metrics));
    ui_metrics.paused = true;
    EXPECT_TRUE(slayer3d_game_data_ui_text_is_visible(runtime, &pause_text, &ui_metrics));

    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    UiRectCapture ui_rects{};
    ASSERT_TRUE(slayer3d_game_data_for_each_ui_rect(runtime, capture_ui_rect, &ui_rects));
    EXPECT_EQ(ui_rects.count, 5);
    EXPECT_TRUE(ui_rects.saw_doom_damage_feedback);
    slayer3d_game_data_ui_rect resolved_damage_rect{};
    bool damage_rect_visible = true;
    ASSERT_TRUE(slayer3d_game_data_resolve_ui_rect(runtime, &ui_rects.damage_rect, nullptr, &resolved_damage_rect,
                                                   &damage_rect_visible));
    EXPECT_FALSE(damage_rect_visible);
    slayer3d_properties_set_float(player->props, "last_damage_per_second", 18.0f);
    ASSERT_TRUE(slayer3d_game_data_resolve_ui_rect(runtime, &ui_rects.damage_rect, nullptr, &resolved_damage_rect,
                                                   &damage_rect_visible));
    EXPECT_TRUE(damage_rect_visible);
    EXPECT_GT(resolved_damage_rect.color.a, 90);
    EXPECT_LT(resolved_damage_rect.color.a, 120);
    slayer3d_camera3d player_camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.doom.player", &player_camera));
    EXPECT_EQ(player_camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_FLOAT_EQ(player_camera.fovy, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
    slayer3d_camera3d surveillance_camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.doom.surveillance", &surveillance_camera));
    EXPECT_FLOAT_EQ(surveillance_camera.fovy, SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES);
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.doom.player");
    player->position = slayer3d_vec3_make(43.0f, 0.5f, 89.0f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.doom.surveillance");

    slayer3d_registered_actor *projectile = slayer3d_game_data_find_actor(runtime, "pool.doom.projectiles.0");
    ASSERT_NE(projectile, nullptr);
    EXPECT_FALSE(projectile->active);
    const int fire_action = slayer3d_game_data_find_action(runtime, "action.fire");
    ASSERT_GE(fire_action, 0);
    ASSERT_NE(input, nullptr);
    slayer3d_input_set_action_override(input, fire_action, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 100), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(projectile->active);
    expect_vec3_near(slayer3d_properties_get_vec3(projectile->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
                     slayer3d_vec3_make(0.0f, 0.0f, 20.0f));
    EXPECT_EQ(slayer3d_game_data_world_light_count(runtime), 1);
    RenderPrimitiveCapture projectile_capture{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &projectile_capture));
    EXPECT_EQ(projectile_capture.doom_projectile_spheres, 1);
    slayer3d_game_data_sprite_asset robot_sprite{};
    ASSERT_TRUE(slayer3d_game_data_get_sprite_asset(runtime, "sprite.doom.robot.walk", &robot_sprite));
    EXPECT_EQ(robot_sprite.source_kind, SLAYER3D_SPRITE_ASSET_SOURCE_FILES);
    EXPECT_EQ(robot_sprite.frame_count, 6);
    EXPECT_EQ(robot_sprite.direction_count, 8);
    slayer3d_sprite_asset_runtime robot_sprite_runtime{};
    ASSERT_TRUE(slayer3d_game_data_load_sprite_asset(runtime, "sprite.doom.robot.walk", &robot_sprite_runtime, error,
                                                     sizeof(error)))
        << error;
    EXPECT_EQ(robot_sprite_runtime.animation_frame_count, 6);
    EXPECT_EQ(robot_sprite_runtime.direction_count, 8);
    slayer3d_sprite_asset_free(&robot_sprite_runtime);

    slayer3d_registered_actor *robot = slayer3d_game_data_find_actor(runtime, "entity.doom.robot.entry");
    ASSERT_NE(robot, nullptr);
    const float robot_start_x = robot->position.x;
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.5f));
    EXPECT_GT(robot->position.x, robot_start_x);
    EXPECT_GT(slayer3d_properties_get_float(robot->props, "sprite_yaw", 0.0f), 1.0f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, DoomLevelNumberKeysSwitchRenderProfiles)
{
    const std::filesystem::path doom_path = doom_level_data_path();
    ASSERT_TRUE(std::filesystem::exists(doom_path)) << doom_path;

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(doom_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.doom_level.play"));

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    struct ProfileKeyCase
    {
        SDL_Scancode scancode;
        const char *expected_name;
        bool expected_vertex_snap;
        bool expected_quantize;
        slayer3d_tonemap_mode expected_tonemap;
    };
    const ProfileKeyCase cases[] = {
        {SDL_SCANCODE_1, "modern", false, false, SLAYER3D_TONEMAP_ACES},
        {SDL_SCANCODE_2, "ps1", true, true, SLAYER3D_TONEMAP_NONE},
        {SDL_SCANCODE_3, "n64", false, false, SLAYER3D_TONEMAP_NONE},
        {SDL_SCANCODE_4, "dos", false, true, SLAYER3D_TONEMAP_NONE},
        {SDL_SCANCODE_5, "snes", false, true, SLAYER3D_TONEMAP_NONE},
    };

    Uint64 tick = 1;
    for (const ProfileKeyCase &test_case : cases)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = test_case.scancode;
        slayer3d_input_process_event(input, &event);
        slayer3d_input_update(input, tick++);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));

        slayer3d_game_data_render_settings settings{};
        ASSERT_TRUE(slayer3d_game_data_get_render_settings(runtime, &settings));
        ASSERT_TRUE(settings.has_profile);
        EXPECT_STREQ(settings.profile_name, test_case.expected_name);
        EXPECT_EQ(settings.profile.vertex_snap, test_case.expected_vertex_snap) << test_case.expected_name;
        EXPECT_EQ(settings.profile.color_quantize, test_case.expected_quantize) << test_case.expected_name;
        EXPECT_EQ(settings.tonemap, test_case.expected_tonemap) << test_case.expected_name;

        event.type = SDL_EVENT_KEY_UP;
        slayer3d_input_process_event(input, &event);
        slayer3d_input_update(input, tick++);
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    }

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RunsAuthoredSectorHazardSensors)
{
    const std::filesystem::path dir = unique_test_dir("sector_hazard_sensors");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "entities": ["entity.player", "entity.bot"],
  "world": { "sector_levels": [{ "level": "sector.test", "variant": "unlit" }] }
})json");
    write_text(dir / "sector_hazard_sensors.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Sector Hazard Sensor Test" },
  "world": { "name": "world.test", "kind": "sector" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [1.0, 1.0, 1.0] },
      "properties": {
        "current_sector": { "type": "int", "value": 0 },
        "damage_taken": { "type": "int", "value": 0 },
        "last_damage_per_second": { "type": "float", "value": 0.0 },
        "entered_hazard": { "type": "bool", "value": false }
      }
    },
    {
      "name": "entity.bot",
      "active": true,
      "tags": ["hazard_subject"],
      "transform": { "position": [3.0, 1.0, 1.0] },
      "properties": {
        "tag_damage": { "type": "int", "value": 0 }
      }
    }
  ],
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }],
      "sectors": [
        {
          "name": "safe",
          "points": [[0, 0], [2, 0], [2, 2], [0, 2]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "wall_material": "floor"
        },
        {
          "name": "hazard",
          "points": [[2, 0], [4, 0], [4, 2], [2, 2]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "wall_material": "floor",
          "damage_per_second": 10.0,
          "ambient_sound_id": 3
        }
      ]
    }
  ],
  "logic": {
    "sensors": [
      {
        "name": "sensor.hazard.enter",
        "type": "sensor.sector",
        "actor": "entity.player",
        "sector_level": "sector.test",
        "sector": "hazard",
        "edge": "enter",
        "actions": [
          { "type": "property.set", "target_from_payload": "actor_name", "key": "entered_hazard", "value": true }
        ]
      },
      {
        "name": "sensor.hazard.stay",
        "type": "sensor.sector",
        "actor": "entity.player",
        "sector_level": "sector.test",
        "sector": "hazard",
        "edge": "stay",
        "actions": [
          { "type": "property.add", "target_from_payload": "actor_name", "key": "damage_taken", "value_from_payload": "sector_damage_delta" },
          { "type": "property.set", "target_from_payload": "actor_name", "key": "last_damage_per_second", "value_from_payload": "sector_damage_per_second" }
        ]
      },
      {
        "name": "sensor.hazard.tag_stay",
        "type": "sensor.sector",
        "actor_tag": "hazard_subject",
        "sector_level": "sector.test",
        "sector": "hazard",
        "edge": "stay",
        "actions": [
          { "type": "property.add", "target_from_payload": "actor_name", "key": "tag_damage", "value_from_payload": "sector_damage_delta" }
        ]
      },
      {
        "name": "sensor.hazard.exit",
        "type": "sensor.sector",
        "actor": "entity.player",
        "sector_level": "sector.test",
        "sector": "hazard",
        "edge": "exit",
        "actions": [
          { "type": "property.set", "target_from_payload": "actor_name", "key": "last_damage_per_second", "value": 0.0 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "sector_hazard_sensors.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    ASSERT_NE(player, nullptr);
    slayer3d_registered_actor *bot = slayer3d_game_data_find_actor(runtime, "entity.bot");
    ASSERT_NE(bot, nullptr);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_FALSE(slayer3d_properties_get_bool(player->props, "entered_hazard", true));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "damage_taken", -1), 0);
    EXPECT_NEAR(slayer3d_properties_get_float(bot->props, "tag_damage", 0.0f), 2.5f, 0.0001f);

    slayer3d_properties_set_int(player->props, "current_sector", 1);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "entered_hazard", false));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "damage_taken", 0.0f), 2.5f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "last_damage_per_second", 0.0f), 10.0f, 0.0001f);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.1f));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "damage_taken", 0.0f), 3.5f, 0.0001f);

    slayer3d_properties_set_int(player->props, "current_sector", 0);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "damage_taken", 0.0f), 3.5f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(player->props, "last_damage_per_second", -1.0f), 0.0f, 0.0001f);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RunsAuthoredPerceptionSensorsWithSectorLineOfSightAndTargetFilters)
{
    const std::filesystem::path dir = unique_test_dir("perception_sensors");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.player", "entity.enemy.visible", "entity.enemy.blocked", "entity.friend"]
})json");
    write_text(dir / "perception_sensors.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Perception Sensor Test" },
  "world": { "name": "world.test", "kind": "sector" },
  "factions": {
    "player": { "enemy": "hostile", "civilian": "friendly" },
    "enemy": { "player": "hostile" },
    "civilian": { "player": "friendly" }
  },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [2.0, 1.0, 2.0] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "faction": { "type": "string", "value": "player" }
      }
    },
    {
      "name": "entity.enemy.visible",
      "active": true,
      "tags": ["npc"],
      "transform": { "position": [2.0, 1.0, 0.75] },
      "properties": {
        "faction": { "type": "string", "value": "enemy" },
        "spotted": { "type": "int", "value": 0 },
        "last_distance": { "type": "float", "value": 0.0 },
        "lost": { "type": "bool", "value": false }
      }
    },
    {
      "name": "entity.enemy.blocked",
      "active": true,
      "tags": ["npc"],
      "transform": { "position": [6.0, 1.0, 2.0] },
      "properties": {
        "faction": { "type": "string", "value": "enemy" },
        "spotted": { "type": "int", "value": 0 }
      }
    },
    {
      "name": "entity.friend",
      "active": true,
      "tags": ["npc"],
      "transform": { "position": [2.5, 1.0, 0.75] },
      "properties": {
        "faction": { "type": "string", "value": "civilian" },
        "spotted": { "type": "int", "value": 0 }
      }
    }
  ],
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }],
      "sectors": [
        {
          "name": "room",
          "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
          "floor_y": 0.0,
          "ceil_y": 3.0,
          "wall_material": "floor"
        }
      ]
    }
  ],
  "logic": {
    "sensors": [
      {
        "name": "sensor.enemy.visible",
        "type": "sensor.perception",
        "observer": "entity.player",
        "target_tag": "npc",
        "sector_level": "sector.test",
        "range": 8.0,
        "fov_degrees": 120.0,
        "edge": "stay",
        "target_filter": { "relationship": "hostile" },
        "actions": [
          { "type": "property.add", "target_from_payload": "target_actor_name", "key": "spotted", "value": 1 },
          { "type": "property.set", "target_from_payload": "target_actor_name", "key": "last_distance", "value_from_payload": "distance" }
        ]
      },
      {
        "name": "sensor.enemy.lost",
        "type": "sensor.perception",
        "observer": "entity.player",
        "target": "entity.enemy.visible",
        "sector_level": "sector.test",
        "range": 8.0,
        "fov_degrees": 120.0,
        "edge": "exit",
        "target_filter": { "relationship": "hostile" },
        "actions": [
          { "type": "property.set", "target_from_payload": "target_actor_name", "key": "lost", "value": true }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "perception_sensors.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_registered_actor *visible = slayer3d_game_data_find_actor(runtime, "entity.enemy.visible");
    ASSERT_NE(visible, nullptr);
    slayer3d_registered_actor *blocked = slayer3d_game_data_find_actor(runtime, "entity.enemy.blocked");
    ASSERT_NE(blocked, nullptr);
    slayer3d_registered_actor *friendly = slayer3d_game_data_find_actor(runtime, "entity.friend");
    ASSERT_NE(friendly, nullptr);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(visible->props, "spotted", 0), 1);
    EXPECT_GT(slayer3d_properties_get_float(visible->props, "last_distance", 0.0f), 1.0f);
    EXPECT_EQ(slayer3d_properties_get_int(blocked->props, "spotted", 0), 0);
    EXPECT_EQ(slayer3d_properties_get_int(friendly->props, "spotted", 0), 0);
    EXPECT_FALSE(slayer3d_properties_get_bool(visible->props, "lost", false));

    visible->position = slayer3d_vec3_make(2.0f, 1.0f, 3.5f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(visible->props, "spotted", 0), 1);
    EXPECT_TRUE(slayer3d_properties_get_bool(visible->props, "lost", false));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RunsAuthoredHearingSensorsForNoiseEventsAndTargetFilters)
{
    const std::filesystem::path dir = unique_test_dir("hearing_sensors");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.listener", "entity.enemy", "entity.friend", "entity.far_enemy"]
})json");
    write_text(dir / "hearing_sensors.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Hearing Sensor Test" },
  "world": { "name": "world.test", "kind": "sector" },
  "signals": ["signal.enemy.noise", "signal.friend.noise", "signal.far.noise"],
  "factions": {
    "player": { "enemy": "hostile", "civilian": "friendly" },
    "enemy": { "player": "hostile" },
    "civilian": { "player": "friendly" }
  },
  "entities": [
    {
      "name": "entity.listener",
      "active": true,
      "transform": { "position": [0.0, 0.0, 0.0] },
      "properties": {
        "faction": { "type": "string", "value": "player" },
        "heard_count": { "type": "int", "value": 0 },
        "last_source": { "type": "string", "value": "" },
        "last_audibility": { "type": "float", "value": 0.0 }
      }
    },
    {
      "name": "entity.enemy",
      "active": true,
      "tags": ["noisy"],
      "transform": { "position": [2.0, 0.0, 0.0] },
      "properties": { "faction": { "type": "string", "value": "enemy" } }
    },
    {
      "name": "entity.friend",
      "active": true,
      "tags": ["noisy"],
      "transform": { "position": [1.0, 0.0, 0.0] },
      "properties": { "faction": { "type": "string", "value": "civilian" } }
    },
    {
      "name": "entity.far_enemy",
      "active": true,
      "tags": ["noisy"],
      "transform": { "position": [12.0, 0.0, 0.0] },
      "properties": { "faction": { "type": "string", "value": "enemy" } }
    }
  ],
  "logic": {
    "bindings": [
      {
        "signal": "signal.enemy.noise",
        "actions": [{ "type": "noise.emit", "source": "entity.enemy", "radius": 6.0, "duration": 0.25 }]
      },
      {
        "signal": "signal.friend.noise",
        "actions": [{ "type": "noise.emit", "source": "entity.friend", "radius": 6.0, "duration": 0.25 }]
      },
      {
        "signal": "signal.far.noise",
        "actions": [{ "type": "noise.emit", "source": "entity.far_enemy", "radius": 6.0, "duration": 0.25 }]
      }
    ],
    "sensors": [
      {
        "name": "sensor.listener.hears_hostile",
        "type": "sensor.hearing",
        "actor": "entity.listener",
        "target_tag": "noisy",
        "range": 8.0,
        "edge": "enter",
        "target_filter": { "relationship": "hostile" },
        "actions": [
          { "type": "property.add", "target_from_payload": "actor_name", "key": "heard_count", "value": 1 },
          { "type": "property.set", "target_from_payload": "actor_name", "key": "last_source", "value_from_payload": "source_actor_name" },
          { "type": "property.set", "target_from_payload": "actor_name", "key": "last_audibility", "value_from_payload": "audibility" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "hearing_sensors.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_registered_actor *listener = slayer3d_game_data_find_actor(runtime, "entity.listener");
    ASSERT_NE(listener, nullptr);
    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.enemy.noise"), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(listener->props, "heard_count", 0), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(listener->props, "last_source", ""), "entity.enemy");
    EXPECT_GT(slayer3d_properties_get_float(listener->props, "last_audibility", 0.0f), 0.0f);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.friend.noise"), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(listener->props, "heard_count", 0), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(listener->props, "last_source", ""), "entity.enemy");

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.far.noise"), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(listener->props, "heard_count", 0), 1);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.25f));
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.enemy.noise"), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(listener->props, "heard_count", 0), 2);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RunsAuthoredBrushContentsAndPerceptionSensors)
{
    const std::filesystem::path dir = unique_test_dir("brush_gameplay_sensors");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.player", "entity.visible", "entity.blocked"],
  "world": { "brush_worlds": [{ "world": "brush.sensor" }] }
})json");
    write_text(dir / "brush_gameplay_sensors.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush Gameplay Sensors Test" },
  "world": { "name": "world.test", "kind": "brush" },
  "entities": [
    {
      "name": "entity.player",
      "active": true,
      "transform": { "position": [0.0, 1.0, 1.0] },
      "properties": {
        "yaw": { "type": "float", "value": 0.0 },
        "in_trigger": { "type": "bool", "value": false },
        "left_trigger": { "type": "bool", "value": false },
        "trigger_ticks": { "type": "int", "value": 0 },
        "last_trigger": { "type": "string", "value": "" }
      }
    },
    {
      "name": "entity.visible",
      "active": true,
      "tags": ["npc"],
      "transform": { "position": [2.0, 1.0, -3.0] },
      "properties": {
        "spotted": { "type": "int", "value": 0 },
        "last_distance": { "type": "float", "value": 0.0 }
      }
    },
    {
      "name": "entity.blocked",
      "active": true,
      "tags": ["npc"],
      "transform": { "position": [0.0, 1.0, -3.0] },
      "properties": {
        "spotted": { "type": "int", "value": 0 }
      }
    }
  ],
  "brush_worlds": [
    {
      "name": "brush.sensor",
      "materials": [
        { "name": "mat.trigger", "albedo": [0.1, 0.7, 0.9, 0.35] },
        { "name": "mat.wall", "albedo": [0.3, 0.3, 0.35, 1.0] }
      ],
      "brushes": [
        {
          "name": "brush.trigger_zone",
          "contents": "trigger",
          "faces": [
            { "plane": { "normal": [ 1, 0, 0], "distance": 2.0 }, "material": "mat.trigger" },
            { "plane": { "normal": [-1, 0, 0], "distance": -1.0 }, "material": "mat.trigger" },
            { "plane": { "normal": [0,  1, 0], "distance": 2.0 }, "material": "mat.trigger" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.0 }, "material": "mat.trigger" },
            { "plane": { "normal": [0, 0,  1], "distance": 2.0 }, "material": "mat.trigger" },
            { "plane": { "normal": [0, 0, -1], "distance": 0.0 }, "material": "mat.trigger" }
          ]
        },
        {
          "name": "brush.los_wall",
          "contents": "solid",
          "faces": [
            { "plane": { "normal": [ 1, 0, 0], "distance": 0.5 }, "material": "mat.wall" },
            { "plane": { "normal": [-1, 0, 0], "distance": 0.5 }, "material": "mat.wall" },
            { "plane": { "normal": [0,  1, 0], "distance": 2.2 }, "material": "mat.wall" },
            { "plane": { "normal": [0, -1, 0], "distance": 0.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0,  1], "distance": -1.0 }, "material": "mat.wall" },
            { "plane": { "normal": [0, 0, -1], "distance": 2.0 }, "material": "mat.wall" }
          ]
        }
      ]
    }
  ],
  "logic": {
    "sensors": [
      {
        "name": "sensor.trigger.enter",
        "type": "sensor.brush_contents",
        "actor": "entity.player",
        "contents_mask": "trigger",
        "edge": "enter",
        "actions": [
          { "type": "property.set", "target_from_payload": "actor_name", "key": "in_trigger", "value": true },
          { "type": "property.set", "target_from_payload": "actor_name", "key": "last_trigger", "value_from_payload": "brush_name" }
        ]
      },
      {
        "name": "sensor.trigger.stay",
        "type": "sensor.brush_contents",
        "actor": "entity.player",
        "contents_mask": "trigger",
        "edge": "stay",
        "actions": [
          { "type": "property.add", "target_from_payload": "actor_name", "key": "trigger_ticks", "value": 1 }
        ]
      },
      {
        "name": "sensor.trigger.exit",
        "type": "sensor.brush_contents",
        "actor": "entity.player",
        "contents_mask": "trigger",
        "edge": "exit",
        "actions": [
          { "type": "property.set", "target_from_payload": "actor_name", "key": "left_trigger", "value": true }
        ]
      },
      {
        "name": "sensor.brush_los",
        "type": "sensor.brush_perception",
        "observer": "entity.player",
        "target_tag": "npc",
        "contents_mask": "solid",
        "range": 8.0,
        "fov_degrees": 180.0,
        "edge": "stay",
        "actions": [
          { "type": "property.add", "target_from_payload": "target_actor_name", "key": "spotted", "value": 1 },
          { "type": "property.set", "target_from_payload": "target_actor_name", "key": "last_distance", "value_from_payload": "distance" }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "brush_gameplay_sensors.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;
    slayer3d_registered_actor *player = slayer3d_game_data_find_actor(runtime, "entity.player");
    slayer3d_registered_actor *visible = slayer3d_game_data_find_actor(runtime, "entity.visible");
    slayer3d_registered_actor *blocked = slayer3d_game_data_find_actor(runtime, "entity.blocked");
    ASSERT_NE(player, nullptr);
    ASSERT_NE(visible, nullptr);
    ASSERT_NE(blocked, nullptr);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_FALSE(slayer3d_properties_get_bool(player->props, "in_trigger", true));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "trigger_ticks", -1), 0);
    EXPECT_EQ(slayer3d_properties_get_int(visible->props, "spotted", 0), 1);
    EXPECT_GT(slayer3d_properties_get_float(visible->props, "last_distance", 0.0f), 3.0f);
    EXPECT_EQ(slayer3d_properties_get_int(blocked->props, "spotted", 0), 0);

    player->position = slayer3d_vec3_make(1.5f, 1.0f, 1.0f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "in_trigger", false));
    EXPECT_STREQ(slayer3d_properties_get_string(player->props, "last_trigger", ""), "brush.trigger_zone");
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "trigger_ticks", 0), 1);

    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_EQ(slayer3d_properties_get_int(player->props, "trigger_ticks", 0), 2);

    player->position = slayer3d_vec3_make(3.0f, 1.0f, 1.0f);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_TRUE(slayer3d_properties_get_bool(player->props, "left_trigger", false));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidSectorDoorsAndActions)
{
    const std::filesystem::path dir = unique_test_dir("sector_doors_invalid");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play", "entities": ["entity.player"] })json");

    struct Case
    {
        const char *name;
        const char *door_json;
        const char *logic_json;
        const char *expected_error;
    };
    const Case cases[] = {
        {
            "bad_panel_bounds",
            R"json([
              {
                "name": "door.test",
                "panels": [
                  { "bounds": { "min": [1, 0], "max": [2, 2, 2] }, "open_offset": [0, 1, 0] }
                ]
              }
            ])json",
            R"json({ "bindings": [] })json",
            "requires bounds min and max vec3",
        },
        {
            "unknown_action_target",
            R"json([
              {
                "name": "door.test",
                "panels": [
                  { "bounds": { "min": [1, 0, 1], "max": [2, 2, 2] }, "open_offset": [0, 1, 0] }
                ]
              }
            ])json",
            R"json({
              "bindings": [
                {
                  "signal": "signal.run",
                  "actions": [{ "type": "sector_door.open", "target": "door.missing" }]
                }
              ]
            })json",
            "unknown sector door reference",
        },
        {
            "unknown_sector_sensor_sector",
            R"json([])json",
            R"json({
              "sensors": [
                {
                  "type": "sensor.sector",
                  "actor": "entity.player",
                  "sector_level": "sector.test",
                  "sector": "missing",
                  "edge": "stay",
                  "actions": [
                    { "type": "property.add", "target_from_payload": "actor_name", "key": "damage_taken", "value_from_payload": "sector_damage_delta" }
                  ]
                }
              ]
            })json",
            "unknown sensor.sector sector",
        },
        {
            "invalid_perception_sensor",
            R"json([])json",
            R"json({
              "sensors": [
                {
                  "type": "sensor.perception",
                  "observer": "entity.player",
                  "target_tag": "enemy",
                  "sector_level": "sector.test",
                  "fov_degrees": 0,
                  "actions": [
                    { "type": "property.set", "target_from_payload": "target_actor_name", "key": "alert", "value": true }
                  ]
                }
              ]
            })json",
            "sensor.perception fov_degrees",
        },
        {
            "invalid_hearing_sensor",
            R"json([])json",
            R"json({
              "sensors": [
                {
                  "type": "sensor.hearing",
                  "actor": "entity.player",
                  "range": 0,
                  "actions": [
                    { "type": "property.set", "target_from_payload": "actor_name", "key": "alert", "value": true }
                  ]
                }
              ]
            })json",
            "sensor.hearing range",
        },
        {
            "invalid_brush_contents_sensor",
            R"json([])json",
            R"json({
              "sensors": [
                {
                  "type": "sensor.brush_contents",
                  "actor": "entity.player",
                  "contents_mask": ["trigger", "missing_content"],
                  "actions": [
                    { "type": "property.set", "target_from_payload": "actor_name", "key": "alert", "value": true }
                  ]
                }
              ]
            })json",
            "brush content value is unknown",
        },
        {
            "invalid_brush_perception_sensor",
            R"json([])json",
            R"json({
              "sensors": [
                {
                  "type": "sensor.brush_perception",
                  "observer": "entity.player",
                  "range": -1,
                  "actions": [
                    { "type": "property.set", "target_from_payload": "target_actor_name", "key": "alert", "value": true }
                  ]
                }
              ]
            })json",
            "sensor.brush_perception requires exactly one of target or target_tag",
        },
        {
            "invalid_noise_action",
            R"json([])json",
            R"json({
              "bindings": [
                {
                  "signal": "signal.run",
                  "actions": [{ "type": "noise.emit", "source": "entity.player", "radius": 0 }]
                }
              ]
            })json",
            "noise.emit radius",
        },
        {
            "invalid_sector_lighting_action",
            R"json([])json",
            R"json({
              "bindings": [
                {
                  "signal": "signal.run",
                  "actions": [
                    {
                      "type": "sector_lighting.set",
                      "sector_level": "sector.test",
                      "sector": "room",
                      "level": 300,
                      "color": [1.0, 1.0, 1.0, 1.0]
                    }
                  ]
                }
              ]
            })json",
            "sector_lighting.set level",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path game_path = dir / (std::string(test_case.name) + ".game.json");
        const std::string game_json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Sector Door" },
  "signals": ["signal.run"],
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" }
        ]
      }
    ]
  },
  "entities": [
    {
      "name": "entity.player",
      "components": [
        {
          "type": "controller.fps_sector",
          "sector_level": "sector.test",
          "actions": {
            "forward": "action.move.forward",
            "back": "action.move.back",
            "left": "action.move.left",
            "right": "action.move.right"
          }
        }
      ]
    }
  ],
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }],
      "sectors": [{
        "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
        "floor_y": 0,
        "ceil_y": 3,
        "wall_material": "floor"
      }]
    }
  ],
  "sector_doors": )json") + test_case.door_json +
                                      R"json(,
  "logic": )json" + test_case.logic_json +
                                      R"json(,
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
        write_text(game_path, game_json.c_str());

        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(game_path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsFpsSectorControllerOnArchetypes)
{
    const std::filesystem::path dir = unique_test_dir("fps_sector_controller_archetype_invalid");
    write_text(dir / "fps_sector_archetype.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid FPS Sector Archetype Controller" },
  "input": {
    "contexts": [
      {
        "name": "input.play",
        "actions": [
          { "name": "action.move.forward" },
          { "name": "action.move.back" },
          { "name": "action.move.left" },
          { "name": "action.move.right" }
        ]
      }
    ]
  },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }],
      "sectors": [{
        "points": [[0, 0], [4, 0], [4, 4], [0, 4]],
        "floor_y": 0,
        "ceil_y": 3,
        "wall_material": "floor"
      }]
    }
  ],
  "actor_archetypes": [
    {
      "name": "archetype.player",
      "components": [
        {
          "type": "controller.fps_sector",
          "sector_level": "sector.test",
          "actions": {
            "forward": "action.move.forward",
            "back": "action.move.back",
            "left": "action.move.left",
            "right": "action.move.right"
          }
        }
      ]
    }
  ]
})json");

    char error[512]{};
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "fps_sector_archetype.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("only supported on static entities"), std::string::npos) << error;

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidSceneSectorLevelInstances)
{
    const std::filesystem::path dir = unique_test_dir("sector_level_scene_invalid");
    write_text(dir / "textures" / "sky.png", "placeholder");
    struct Case
    {
        const char *name;
        const char *scene_world_json;
        const char *expected_error;
    };
    const Case cases[] = {
        {
            "unknown_level",
            R"json({ "sector_levels": [{ "level": "sector.missing" }] })json",
            "unknown sector level",
        },
        {
            "bad_variant",
            R"json({ "sector_levels": [{ "level": "sector.test", "variant": "dynamic" }] })json",
            "variant",
        },
        {
            "bad_position",
            R"json({ "sector_levels": [{ "level": "sector.test", "position": [1.0, 2.0] }] })json",
            "position",
        },
        {
            "unknown_skybox_image",
            R"json({ "skybox": { "pos_x": "image.missing", "neg_x": "image.sky.nx", "pos_y": "image.sky.py", "neg_y": "image.sky.ny", "pos_z": "image.sky.pz", "neg_z": "image.sky.nz" } })json",
            "unknown image asset",
        },
        {
            "bad_skybox_size",
            R"json({ "skybox": { "pos_x": "image.sky.px", "neg_x": "image.sky.nx", "pos_y": "image.sky.py", "neg_y": "image.sky.ny", "pos_z": "image.sky.pz", "neg_z": "image.sky.nz", "size": 1.0 } })json",
            "skybox size",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path game_path = dir / (std::string(test_case.name) + ".game.json");
        write_text(dir / "scenes" / (std::string(test_case.name) + ".scene.json"), (std::string(R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": )json") + test_case.scene_world_json + R"json(
})json")
                                                                                       .c_str());
        const std::string game_json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Sector Scene" },
  "assets": {
    "images": [
      { "id": "image.sky.px", "path": "asset://textures/sky.png" },
      { "id": "image.sky.nx", "path": "asset://textures/sky.png" },
      { "id": "image.sky.py", "path": "asset://textures/sky.png" },
      { "id": "image.sky.ny", "path": "asset://textures/sky.png" },
      { "id": "image.sky.pz", "path": "asset://textures/sky.png" },
      { "id": "image.sky.nz", "path": "asset://textures/sky.png" }
    ]
  },
  "sector_levels": [
    {
      "name": "sector.test",
      "materials": [{ "name": "floor" }],
      "sectors": [{
        "points": [[0, 0], [2, 0], [2, 2], [0, 2]],
        "floor_y": 0,
        "ceil_y": 3,
        "wall_material": "floor"
      }]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/)json") +
                                      test_case.name + R"json(.scene.json"] }
})json";
        write_text(game_path, game_json.c_str());

        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(game_path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidSceneMouseCapturePolicy)
{
    const std::filesystem::path dir = unique_test_dir("scene_mouse_capture_invalid");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "input": { "mouse_capture": "relative" }
})json");
    write_text(dir / "game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Scene Mouse Capture" },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    char error[512]{};
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "game.json").string().c_str(), nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("scene input.mouse_capture must be never, unpaused, or always"),
              std::string::npos)
        << error;

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidAuthoredSectorLevels)
{
    const std::filesystem::path dir = unique_test_dir("sector_level_invalid");
    struct Case
    {
        const char *name;
        const char *sector_json;
        const char *expected_error;
    };
    const Case cases[] = {
        {
            "bad_material",
            R"json({
              "name": "sector.bad",
              "materials": [{ "name": "floor" }],
              "sectors": [{
                "points": [[0,0], [2,0], [2,2], [0,2]],
                "floor_y": 0,
                "ceil_y": 3,
                "floor_material": "floor",
                "ceil_material": "floor",
                "wall_material": "missing"
              }]
            })json",
            "material refs",
        },
        {
            "bad_points",
            R"json({
              "name": "sector.bad",
              "materials": [{ "name": "floor" }],
              "sectors": [{
                "points": [[0,0], [2,0]],
                "floor_y": 0,
                "ceil_y": 3,
                "wall_material": "floor"
              }]
            })json",
            "points",
        },
        {
            "bad_height",
            R"json({
              "name": "sector.bad",
              "materials": [{ "name": "floor" }],
              "sectors": [{
                "points": [[0,0], [2,0], [2,2], [0,2]],
                "floor_y": 4,
                "ceil_y": 3,
                "wall_material": "floor"
              }]
            })json",
            "ceil_y",
        },
        {
            "bad_lighting_level",
            R"json({
              "name": "sector.bad",
              "materials": [{ "name": "floor" }],
              "sectors": [{
                "points": [[0,0], [2,0], [2,2], [0,2]],
                "floor_y": 0,
                "ceil_y": 3,
                "wall_material": "floor",
                "lighting": { "level": 300, "color": [1.0, 1.0, 1.0, 1.0] }
              }]
            })json",
            "lighting level",
        },
        {
            "bad_lighting_color",
            R"json({
              "name": "sector.bad",
              "materials": [{ "name": "floor" }],
              "sectors": [{
                "points": [[0,0], [2,0], [2,2], [0,2]],
                "floor_y": 0,
                "ceil_y": 3,
                "wall_material": "floor",
                "lighting": { "level": 128, "color": [1.0, -0.1, 1.0, 1.0] }
              }]
            })json",
            "lighting color",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path path = dir / (std::string(test_case.name) + ".game.json");
        const std::string json = std::string(R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid Sector Level" },
  "sector_levels": [
)json") + test_case.sector_json +
                                 R"json(
  ]
})json";
        write_text(path, json.c_str());

        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.expected_error), std::string::npos)
            << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, PacmanDemoLoadsAndRunsMazeCollection)
{
    const std::filesystem::path pacman_path = pacman_data_path();
    ASSERT_TRUE(std::filesystem::exists(pacman_path)) << pacman_path;

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(pacman_path.string().c_str(), session, &runtime, error, sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.play"));
    const slayer3d_properties *state = slayer3d_game_data_scene_state(runtime);
    ASSERT_NE(state, nullptr);
    EXPECT_GT(slayer3d_properties_get_int(state, "pacman_spawned_collectibles", 0), 0);

    slayer3d_registered_actor *pac = slayer3d_game_data_find_actor(runtime, "entity.pacman");
    slayer3d_registered_actor *wall = slayer3d_game_data_find_actor(runtime, "pool.walls.0");
    slayer3d_registered_actor *game = slayer3d_game_data_find_actor(runtime, "entity.game");
    slayer3d_registered_actor *red_ghost = slayer3d_game_data_find_actor(runtime, "entity.ghost.red");
    ASSERT_NE(pac, nullptr);
    ASSERT_NE(wall, nullptr);
    ASSERT_NE(game, nullptr);
    ASSERT_NE(red_ghost, nullptr);
    EXPECT_TRUE(wall->active);
    EXPECT_TRUE(slayer3d_game_data_active_scene_has_entity(runtime, "pool.walls.0"));
    EXPECT_GT(slayer3d_properties_get_int(state, "pacman_spawned_wall_runs", 0), 0);
    EXPECT_LT(slayer3d_properties_get_int(state, "pacman_spawned_wall_runs", 999), 180);
    RenderPrimitiveCapture pickup_render{};
    ASSERT_TRUE(slayer3d_game_data_for_each_render_primitive(runtime, capture_render_primitive, &pickup_render));
    EXPECT_TRUE(pickup_render.saw_pickup_batch);
    EXPECT_GT(pickup_render.pickup_batch_instances, 0);

    const int spawned_collectibles = slayer3d_properties_get_int(state, "pacman_spawned_collectibles", 0);
    EXPECT_EQ(slayer3d_properties_get_int(game->props, "pellets_remaining", -1), spawned_collectibles);
    EXPECT_EQ(slayer3d_properties_get_int(pac->props, "grid_col", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(pac->props, "grid_row", -1), 1);
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.maze");

    slayer3d_camera3d player_camera{};
    ASSERT_TRUE(slayer3d_game_data_get_camera(runtime, "camera.player.first_person", &player_camera));
    EXPECT_EQ(player_camera.projection, SLAYER3D_CAMERA_PERSPECTIVE);
    EXPECT_NEAR(player_camera.position.x, pac->position.x, 0.0001f);
    EXPECT_NEAR(player_camera.position.y, pac->position.y, 0.0001f);
    EXPECT_GT(player_camera.target.x, player_camera.position.x);

    slayer3d_input_manager *input = slayer3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);
    const int camera_toggle = slayer3d_game_data_find_action(runtime, "action.camera.toggle");
    ASSERT_GE(camera_toggle, 0);
    EXPECT_TRUE(slayer3d_game_data_active_scene_allows_action(runtime, camera_toggle));
    slayer3d_input_set_action_override(input, camera_toggle, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 9100), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f / 60.0f));
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.player.first_person");
    slayer3d_input_set_action_override(input, camera_toggle, 0.0f);
    ASSERT_NE(slayer3d_input_update(input, 9101), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f / 60.0f));
    slayer3d_input_set_action_override(input, camera_toggle, 1.0f);
    ASSERT_NE(slayer3d_input_update(input, 9102), nullptr);
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f / 60.0f));
    EXPECT_STREQ(slayer3d_game_data_active_camera(runtime), "camera.maze");
    slayer3d_input_set_action_override(input, camera_toggle, 0.0f);
    ASSERT_NE(slayer3d_input_update(input, 9103), nullptr);

    for (int i = 0; i < 24; ++i)
    {
        ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f / 60.0f));
    }

    EXPECT_GT(slayer3d_properties_get_int(pac->props, "grid_col", -1), 1);
    EXPECT_LT(slayer3d_properties_get_int(game->props, "pellets_remaining", spawned_collectibles),
              spawned_collectibles);
    EXPECT_GT(slayer3d_properties_get_int(game->props, "score", 0), 0);
    EXPECT_TRUE(red_ghost->active);
    EXPECT_LT(slayer3d_properties_get_int(red_ghost->props, "grid_row", 99), 7);
    EXPECT_TRUE(slayer3d_game_data_find_actor(runtime, "entity.ghost.pink")->active);
    EXPECT_TRUE(slayer3d_game_data_find_actor(runtime, "entity.ghost.cyan")->active);
    EXPECT_TRUE(slayer3d_game_data_find_actor(runtime, "entity.ghost.orange")->active);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RejectsInvalidMazePrimitiveData)
{
    const std::filesystem::path dir = unique_test_dir("maze_primitive_validation");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({ "schema": "slayer3d.scene.v0", "name": "scene.play" })json");

    struct Case
    {
        const char *name;
        const char *json;
        const char *message;
    };

    const Case cases[] = {
        {
            "ragged_rows",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "world": { "name": "world.invalid", "kind": "fixed_screen" },
  "grid_maps": [
    { "name": "map.bad", "walkable": ["."], "rows": ["###", "##"] }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "grid map rows must have identical widths",
        },
        {
            "bad_grid_agent_map",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "world": { "name": "world.invalid", "kind": "fixed_screen" },
  "entities": [
    { "name": "entity.actor", "components": [{ "type": "motion.grid_agent", "map": "map.missing" }] }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown grid map",
        },
        {
            "bad_spawn_glyph",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "world": { "name": "world.invalid", "kind": "fixed_screen" },
  "grid_maps": [
    { "name": "map.maze", "walkable": ["."], "rows": [".."] }
  ],
  "actor_archetypes": [
    { "name": "archetype.pickup" }
  ],
  "actor_pools": [
    { "name": "pool.pickups", "archetype": "archetype.pickup", "capacity": 1 }
  ],
  "signals": ["signal.spawn"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.spawn",
        "actions": [
          {
            "type": "grid.spawn_from_glyphs",
            "map": "map.maze",
            "spawns": [{ "glyph": "too_long", "pool": "pool.pickups" }]
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "grid spawn glyph must be a single-byte string",
        },
        {
            "bad_pickup_layer_map",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "grid_pickup_layers": [
    { "name": "pickup.bad", "map": "map.missing", "kinds": [{ "glyph": ".", "kind": "pellet" }] }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown grid map",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path path = dir / (std::string(test_case.name) + ".game.json");
        write_text(path, test_case.json);
        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.message), std::string::npos) << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidActorPoolsAndSpawnActions)
{
    const std::filesystem::path dir = unique_test_dir("actor_pool_validation");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.game.v0",
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
            "bad_projectile_speed",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "entities": [
    { "name": "entity.player", "active": true }
  ],
  "actor_archetypes": [
    { "name": "archetype.shot" }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1 }
  ],
  "signals": ["signal.fire"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.fire",
        "actions": [
          {
            "type": "projectile.fire",
            "target": "entity.player",
            "pool": "pool.shots",
            "velocity_from_property": "camera_forward",
            "speed": -1.0
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "speed must be non-negative",
        },
        {
            "bad_sector_velocity_level",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "components": [
        { "type": "motion.sector_velocity_3d", "sector_level": "sector.missing" }
      ]
    }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1 }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "unknown sector level",
        },
        {
            "bad_brush_velocity_shape",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "actor_archetypes": [
    {
      "name": "archetype.shot",
      "components": [
        { "type": "motion.brush_velocity_3d", "shape": "capsule" }
      ]
    }
  ],
  "actor_pools": [
    { "name": "pool.shots", "archetype": "archetype.shot", "capacity": 1 }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "shape must be point, sphere, or aabb",
        },
        {
            "bad_spawn_from",
            R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.game.v0",
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
        {
            "bad_volume_bounds",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "entities": [
    { "name": "entity.player", "active": true }
  ],
  "logic": {
    "sensors": [
      {
        "type": "sensor.volume",
        "actor": "entity.player",
        "min": [2.0, 0.0, 0.0],
        "max": [1.0, 1.0, 1.0],
        "actions": [
          { "type": "property.add", "target": "entity.player", "key": "hits", "value": 1 }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "min must be less than or equal to max",
        },
        {
            "bad_fps_launch_velocity",
            R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Invalid", "id": "test.invalid", "version": "0.1.0" },
  "entities": [
    { "name": "entity.player", "active": true }
  ],
  "signals": ["signal.launch"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.launch",
        "actions": [
          {
            "type": "controller.fps_sector.launch",
            "target": "entity.player",
            "vertical_velocity": 0.0
          }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json",
            "positive vertical_velocity",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path path = dir / (std::string(test_case.name) + ".game.json");
        write_text(path, test_case.json);
        char error[512]{};
        EXPECT_FALSE(slayer3d_game_data_validate_file(path.string().c_str(), nullptr, error, sizeof(error)))
            << test_case.name;
        EXPECT_NE(std::string(error).find(test_case.message), std::string::npos) << test_case.name << ": " << error;
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, ValidatesStructuredJsonImportsAndFragments)
{
    const std::filesystem::path dir = unique_test_dir("json_imports_valid");
    write_text(dir / "game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "imports": [
    { "path": "fragments/assets.json", "sections": ["assets"] },
    { "path": "fragments/actors.json" }
  ]
})json");
    write_text(dir / "fragments" / "assets.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "assets": {
    "images": [
      { "id": "image.player", "path": "asset://images/player.png" }
    ]
  }
})json");
    write_text(dir / "fragments" / "actors.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "imports": [
    { "path": "shared/signals.json", "sections": ["signals"] }
  ],
  "actor_archetypes": [
    { "name": "archetype.enemy", "tags": ["enemy"] }
  ],
  "actor_pools": [
    { "name": "pool.enemies", "archetype": "archetype.enemy", "capacity": 32 }
  ]
})json");
    write_text(dir / "fragments" / "shared" / "signals.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "signals": ["signal.wave.start"]
})json");
    write_text(dir / "images" / "player.png", "placeholder");

    char error[512]{};
    EXPECT_TRUE(slayer3d_game_data_validate_file((dir / "game.json").string().c_str(), nullptr, error, sizeof(error)))
        << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, LoadsComposedStructuredJsonImports)
{
    const std::filesystem::path dir = unique_test_dir("json_imports_composed");
    write_text(dir / "game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Imports", "id": "test.imports", "version": "0.1.0" },
  "imports": [
    { "path": "fragments/entities.json", "sections": ["entities"] },
    { "path": "fragments/signals.json", "sections": ["signals"] },
    { "path": "fragments/logic.json", "sections": ["logic"] }
  ],
  "signals": ["signal.root"],
  "logic": {
    "bindings": [
      {
        "signal": "signal.imported",
        "actions": [
          { "type": "property.set", "target": "entity.imported", "key": "ready", "value": true }
        ]
      }
    ]
  },
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");
    write_text(dir / "fragments" / "entities.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "entities": [
    {
      "name": "entity.imported",
      "tags": ["imported"],
      "properties": {
        "ready": { "type": "bool", "value": false },
        "count": { "type": "int", "value": 0 }
      }
    }
  ]
})json");
    write_text(dir / "fragments" / "signals.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "signals": ["signal.imported"]
})json");
    write_text(dir / "fragments" / "logic.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "logic": {
    "bindings": [
      {
        "signal": "signal.root",
        "actions": [
          { "type": "property.set", "target": "entity.imported", "key": "count", "value": 7 }
        ]
      }
    ]
  }
})json");
    write_text(dir / "scenes" / "play.scene.json",
               R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "entities": ["entity.imported"]
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_data_runtime *runtime = nullptr;
    char error[512]{};
    ASSERT_TRUE(
        slayer3d_game_data_load_file((dir / "game.json").string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, "entity.imported");
    ASSERT_NE(actor, nullptr);
    EXPECT_NE(slayer3d_game_data_find_actor_with_tag(runtime, "imported"), nullptr);

    const int root_signal = slayer3d_game_data_find_signal(runtime, "signal.root");
    const int imported_signal = slayer3d_game_data_find_signal(runtime, "signal.imported");
    ASSERT_GE(root_signal, 0);
    ASSERT_GE(imported_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), root_signal, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(actor->props, "count", 0), 7);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), imported_signal, nullptr);
    EXPECT_TRUE(slayer3d_properties_get_bool(actor->props, "ready", false));

    destroy_runtime_session(session, runtime);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ImportDiagnosticsReportFragmentSource)
{
    const std::filesystem::path dir = unique_test_dir("json_imports_source_diagnostics");
    write_text(dir / "game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "imports": [
    { "path": "fragments/logic.json", "sections": ["logic"] }
  ],
  "signals": ["signal.root"]
})json");
    write_text(dir / "fragments" / "logic.json",
               R"json({
  "schema": "slayer3d.fragment.v0",
  "logic": {
    "bindings": [
      {
        "signal": "signal.root",
        "actions": [
          { "type": "property.set", "target": "entity.missing", "key": "ready", "value": true }
        ]
      }
    ]
  }
})json");

    char error[512]{};
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "game.json").string().c_str(), nullptr, error, sizeof(error)));
    const std::string message = error;
    EXPECT_NE(message.find("fragments/logic.json"), std::string::npos) << message;
    EXPECT_NE(message.find("$.logic.bindings[0].actions[0]"), std::string::npos) << message;
    EXPECT_NE(message.find("entity.missing"), std::string::npos) << message;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidStructuredJsonImports)
{
    const std::filesystem::path dir = unique_test_dir("json_imports_invalid");
    struct Case
    {
        const char *name;
        const char *root;
        const char *fragment_path;
        const char *fragment;
        const char *extra_path;
        const char *extra;
        const char *message;
    };

    const Case cases[] = {
        {
            "unsafe_path",
            R"json({ "schema": "slayer3d.game.v0", "imports": [{ "path": "../fragment.json" }] })json",
            NULL,
            NULL,
            NULL,
            NULL,
            "safe relative path",
        },
        {
            "bad_schema",
            R"json({ "schema": "slayer3d.game.v0", "imports": [{ "path": "fragment.json" }] })json",
            "fragment.json",
            R"json({ "schema": "slayer3d.game.v0" })json",
            NULL,
            NULL,
            "must use schema slayer3d.fragment.v0",
        },
        {
            "root_only_key",
            R"json({ "schema": "slayer3d.game.v0", "imports": [{ "path": "fragment.json" }] })json",
            "fragment.json",
            R"json({
  "schema": "slayer3d.fragment.v0",
  "metadata": { "name": "Bad Fragment" }
})json",
            NULL,
            NULL,
            "root-only or unsupported section",
        },
        {
            "bad_section",
            R"json({
  "schema": "slayer3d.game.v0",
  "imports": [{ "path": "fragment.json", "sections": ["metadata"] }]
})json",
            "fragment.json",
            R"json({ "schema": "slayer3d.fragment.v0", "assets": {} })json",
            NULL,
            NULL,
            "not mergeable",
        },
        {
            "missing_selected_section",
            R"json({
  "schema": "slayer3d.game.v0",
  "imports": [{ "path": "fragment.json", "sections": ["assets"] }]
})json",
            "fragment.json",
            R"json({ "schema": "slayer3d.fragment.v0", "signals": [] })json",
            NULL,
            NULL,
            "not present in fragment",
        },
        {
            "unselected_section",
            R"json({
  "schema": "slayer3d.game.v0",
  "imports": [{ "path": "fragment.json", "sections": ["assets"] }]
})json",
            "fragment.json",
            R"json({ "schema": "slayer3d.fragment.v0", "assets": {}, "signals": [] })json",
            NULL,
            NULL,
            "not selected by the import filter",
        },
        {
            "cycle",
            R"json({ "schema": "slayer3d.game.v0", "imports": [{ "path": "a.json" }] })json",
            "a.json",
            R"json({ "schema": "slayer3d.fragment.v0", "imports": [{ "path": "b.json" }], "signals": [] })json",
            "b.json",
            R"json({ "schema": "slayer3d.fragment.v0", "imports": [{ "path": "a.json" }], "signals": [] })json",
            "cycle",
        },
        {
            "merge_conflict",
            R"json({
  "schema": "slayer3d.game.v0",
  "imports": [
    { "path": "a.json", "sections": ["storage"] },
    { "path": "b.json", "sections": ["storage"] }
  ]
})json",
            "a.json",
            R"json({ "schema": "slayer3d.fragment.v0", "storage": { "organization": "A" } })json",
            "b.json",
            R"json({ "schema": "slayer3d.fragment.v0", "storage": { "organization": "B" } })json",
            "merge conflict",
        },
    };

    for (const Case &test_case : cases)
    {
        const std::filesystem::path case_dir = dir / test_case.name;
        write_text(case_dir / "game.json", test_case.root);
        if (test_case.fragment_path != NULL)
            write_text(case_dir / test_case.fragment_path, test_case.fragment);
        if (test_case.extra_path != NULL)
            write_text(case_dir / test_case.extra_path, test_case.extra);

        char error[512]{};
        EXPECT_FALSE(
            slayer3d_game_data_validate_file((case_dir / "game.json").string().c_str(), nullptr, error, sizeof(error)))
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
  "name": "scene.play"
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "lua_actor_pools.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn"), nullptr);

    slayer3d_registered_actor *shot0 = slayer3d_game_data_find_actor(runtime, "pool.projectiles.0");
    ASSERT_NE(shot0, nullptr);
    EXPECT_TRUE(shot0->active);
    expect_vec3_near(shot0->position, slayer3d_vec3_make(1.25f, 2.5f, 3.0f));
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 5);
    EXPECT_TRUE(slayer3d_properties_get_bool(shot0->props, "critical", false));
    EXPECT_STREQ(slayer3d_properties_get_string(shot0->props, "owner", ""), "player");
    expect_vec3_near(slayer3d_properties_get_vec3(shot0->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
                     slayer3d_vec3_make(1.0f, 2.0f, 0.0f));
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "touched", 0), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "spawn_name", ""),
                 "pool.projectiles.0");
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "spawn_actor_id", -1), shot0->id);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "spawn_pool_index", -1), 0);
    EXPECT_TRUE(slayer3d_properties_get_bool(slayer3d_game_data_scene_state(runtime), "spawn_active", false));
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "pool_capacity", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "pool_active", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "pool_available", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "active_projectiles", -1), 1);

    EXPECT_TRUE(slayer3d_game_data_update(runtime, 0.016f));
    EXPECT_FALSE(shot0->active);
    EXPECT_FALSE(slayer3d_properties_get_bool(slayer3d_game_data_scene_state(runtime), "sensor_during_active", true));
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "sensor_during_damage", -1), 5);
    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "sensor_during_lifecycle", ""),
                 "despawning");
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "sensor_during_available", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(shot0->props, "pool_lifecycle", ""), "inactive");

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.spawn"), nullptr);
    EXPECT_TRUE(shot0->active);
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 5);

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.despawn.first"), nullptr);
    EXPECT_FALSE(shot0->active);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "before_despawn", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "after_despawn", -1), 0);
    EXPECT_FALSE(slayer3d_properties_get_bool(slayer3d_game_data_scene_state(runtime), "during_despawn_active", true));
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "during_despawn_damage", -1), 5);
    EXPECT_STREQ(
        slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "during_despawn_lifecycle", ""),
        "despawning");
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "during_despawn_available", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "damage", 0), 1);
    EXPECT_EQ(slayer3d_properties_get_int(shot0->props, "touched", -1), 0);
    EXPECT_STREQ(slayer3d_properties_get_string(shot0->props, "pool_lifecycle", ""), "inactive");

    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.despawn.all"), nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_scene_state(runtime), "despawned_count", -1), 1);
    EXPECT_FALSE(shot0->active);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
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
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
  "name": "scene.play"
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "actor_pool_diagnostics.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    SDLLogOutputGuard log_guard;
    CapturedLogMessage captured_log;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetLogOutputFunction(capture_log_output, &captured_log);

    slayer3d_signal_bus *bus = slayer3d_game_session_get_signal_bus(session);
    ASSERT_NE(bus, nullptr);
    slayer3d_signal_emit(bus, slayer3d_game_data_find_signal(runtime, "signal.run"), nullptr);

    const slayer3d_properties *scene_state = slayer3d_game_data_scene_state(runtime);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_first_index", -1), 0);
    EXPECT_TRUE(slayer3d_properties_get_bool(scene_state, "fail_second_missing", false));
    EXPECT_NE(std::string(slayer3d_properties_get_string(scene_state, "fail_error", "")).find("exhausted"),
              std::string::npos);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_attempts", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_success", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_failures", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_exhaustion", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_peak", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "fail_last_failure", ""), "exhausted");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_active", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_available", -1), 0);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "fail_despawns", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "fail_last_despawn", ""), "hit_enemy");

    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_attempts", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_success", -1), 2);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_failures", -1), 0);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_exhaustion", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_count", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_despawns", -1), 1);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "reuse_last_despawn", ""), "reuse_oldest");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_peak", -1), 1);
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "reuse_active", -1), 1);
    slayer3d_registered_actor *reused = slayer3d_game_data_find_actor(runtime, "pool.reuse.0");
    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(slayer3d_properties_get_int(reused->props, "generation", -1), 2);

    EXPECT_EQ(captured_log.category, SDL_LOG_CATEGORY_APPLICATION);
    EXPECT_EQ(captured_log.priority, SDL_LOG_PRIORITY_WARN);
    EXPECT_NE(captured_log.message.find("SLAYER3D actor pool exhausted"), std::string::npos);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
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

    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    char error[512]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_memory_pack(assets, pack.data(), pack.size(), "module-fixture", error,
                                                          sizeof(error)))
        << error;
    EXPECT_TRUE(
        slayer3d_game_data_validate_asset(assets, "asset://module_success.game.json", nullptr, error, sizeof(error)))
        << error;

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_asset(assets, "asset://module_success.game.json", session, &runtime, error,
                                              sizeof(error)))
        << error;

    const int run_signal = slayer3d_game_data_find_signal(runtime, "signal.run");
    ASSERT_GE(run_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), run_signal, nullptr);

    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    EXPECT_FLOAT_EQ(target->position.x, 1.0f);
    EXPECT_FLOAT_EQ(target->position.y, 2.0f);
    EXPECT_TRUE(slayer3d_properties_get_bool(target->props, "ctx_ok", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(target->props, "state_ok", false));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    slayer3d_asset_resolver_destroy(assets);
}

TEST(GameDataRuntime, ReloadScriptsCommitsUpdatedLuaAdapters)
{
    const std::filesystem::path dir = unique_test_dir("reload_success");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 1);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(slayer3d_properties_get_int(target->props, "value", 0), 1);

    write_hot_reload_script(dir, 2);
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_game_data_reload_scripts(runtime, assets, error, sizeof(error))) << error;

    slayer3d_properties_set_int(target->props, "value", 0);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(slayer3d_properties_get_int(target->props, "value", 0), 2);

    slayer3d_asset_resolver_destroy(assets);
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReloadScriptsPreservesLastGoodAdapterOnSyntaxFailure)
{
    const std::filesystem::path dir = unique_test_dir("reload_syntax_failure");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 7);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(slayer3d_properties_get_int(target->props, "value", 0), 7);

    write_text(dir / "scripts" / "rules.lua", "local rules = \n");
    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    EXPECT_FALSE(slayer3d_game_data_reload_scripts(runtime, assets, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("script.rules"), std::string::npos);

    slayer3d_properties_set_int(target->props, "value", 0);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(slayer3d_properties_get_int(target->props, "value", 0), 7);

    slayer3d_asset_resolver_destroy(assets);
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReloadScriptsPreservesLastGoodAdapterOnMissingFunction)
{
    const std::filesystem::path dir = unique_test_dir("reload_missing_function");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 3);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    write_text(dir / "scripts" / "rules.lua", "return {}\n");

    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    EXPECT_FALSE(slayer3d_game_data_reload_scripts(runtime, assets, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("adapter.reload.run"), std::string::npos);

    slayer3d_properties_set_int(target->props, "value", 0);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(slayer3d_properties_get_int(target->props, "value", 0), 3);

    slayer3d_asset_resolver_destroy(assets);
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ReloadScriptsKeepsNativeAdapterOverrides)
{
    const std::filesystem::path dir = unique_test_dir("reload_native_override");
    write_hot_reload_json(dir);
    write_hot_reload_script(dir, 1);

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "reload.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    AdapterCapture capture{};
    ASSERT_TRUE(slayer3d_game_data_register_adapter(runtime, "adapter.reload.run", reload_native_adapter, &capture));
    write_hot_reload_script(dir, 2);

    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(assets, dir.string().c_str(), error, sizeof(error))) << error;
    ASSERT_TRUE(slayer3d_game_data_reload_scripts(runtime, assets, error, sizeof(error))) << error;

    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, "entity.target");
    ASSERT_NE(target, nullptr);
    emit_reload_signal(session, runtime);
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(slayer3d_properties_get_int(target->props, "value", 0), 99);

    slayer3d_asset_resolver_destroy(assets);
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, ValidatesPongDataWithoutDiagnostics)
{
    DiagnosticCapture capture;
    slayer3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    EXPECT_TRUE(slayer3d_game_data_validate_file(pong_data_path().string().c_str(), &options, error, sizeof(error)))
        << error;
    EXPECT_TRUE(capture.diagnostics.empty());
    EXPECT_EQ(error[0], '\0');
}

TEST(GameDataRuntime, RejectsInvalidHapticsPolicies)
{
    const std::filesystem::path dir = unique_test_dir("haptics_policy_validation");
    write_text(dir / "bad_haptics.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_haptics.game.json").string().c_str(), nullptr, error,
                                                  sizeof(error)));
    EXPECT_NE(std::string(error).find("haptics low_frequency must be a number from 0 to 1"), std::string::npos)
        << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsInvalidInputProfiles)
{
    const std::filesystem::path dir = unique_test_dir("input_profiles");
    write_text(dir / "bad_input_profile.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_input_profile.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0]"), std::string::npos);

    write_text(dir / "bad_input_assignment.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_input_assignment.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.device_assignment_sets[0].bindings[0]"), std::string::npos);

    write_text(dir / "mixed_input_profile.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "mixed_input_profile.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0]"), std::string::npos);

    write_text(dir / "extra_assignment_semantic.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "extra_assignment_semantic.game.json").string().c_str(),
                                                  nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0].assignments[0].actions.fire"), std::string::npos);

    write_text(dir / "nongamepad_assignment_slot.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "nongamepad_assignment_slot.game.json").string().c_str(),
                                                  nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("$.input.profiles[0].assignments[0]"), std::string::npos);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, StandardOptionsAdoptionFixtureLoadsReusablePackage)
{
    const std::string path = fixture_path("standard_options_minimal.game.json");
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_validate_file(path.c_str(), nullptr, error, sizeof(error))) << error;

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << error;

    EXPECT_STREQ(slayer3d_game_data_active_scene(runtime), "scene.title");
    EXPECT_EQ(slayer3d_game_data_scene_count(runtime), 7);
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 0), "scene.title");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 1), "scene.options");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 2), "scene.options.display");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 3), "scene.options.keyboard");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 4), "scene.options.mouse");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 5), "scene.options.gamepad");
    EXPECT_STREQ(slayer3d_game_data_scene_name_at(runtime, 6), "scene.options.audio");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options"));
    slayer3d_game_data_menu menu{};
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options");
    EXPECT_EQ(menu.item_count, 6);

    slayer3d_game_data_menu_item item{};
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Display");
    EXPECT_STREQ(item.scene, "scene.options.display");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 2, &item));
    EXPECT_STREQ(item.label, "Mouse");
    EXPECT_STREQ(item.scene, "scene.options.mouse");

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.keyboard"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.keyboard");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Up");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING);
    EXPECT_EQ(item.input_binding_count, 2);

    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.options.audio"));
    ASSERT_TRUE(slayer3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options.audio");
    ASSERT_TRUE(slayer3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Sound Effects");
    EXPECT_EQ(item.control_type, SLAYER3D_GAME_DATA_MENU_CONTROL_RANGE);
    EXPECT_STREQ(item.control_target, "entity.settings");
    EXPECT_STREQ(item.control_key, "sfx_volume");

    slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
    EXPECT_EQ(slayer3d_properties_get_int(settings->props, "music_volume", 0), 7);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ValidationReportsJsonPathAndMissingReference)
{
    DiagnosticCapture capture;
    slayer3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    const std::string path = fixture_path("bad_reference.game.json");
    EXPECT_FALSE(slayer3d_game_data_validate_file(path.c_str(), &options, error, sizeof(error)));
    ASSERT_FALSE(capture.diagnostics.empty());
    EXPECT_EQ(capture.diagnostics[0].severity, SLAYER3D_GAME_DATA_DIAGNOSTIC_ERROR);
    EXPECT_NE(capture.diagnostics[0].path.find("$.logic.bindings[0].actions[0]"), std::string::npos);
    EXPECT_NE(capture.diagnostics[0].message.find("entity.missing"), std::string::npos);
    EXPECT_NE(std::string(error).find("$.logic.bindings[0].actions[0]"), std::string::npos);
}

TEST(GameDataRuntime, ValidatesAuthoredStorageConfig)
{
    const std::filesystem::path dir = unique_test_dir("storage_validation");
    write_text(dir / "bad_storage.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Bad Storage", "id": "test.bad_storage", "version": "0.1.0" },
  "storage": { "organization": "Bad/Org", "application": "Pong" },
  "world": { "name": "world.bad_storage", "kind": "fixed_screen" },
  "entities": []
})json");

    char error[512]{};
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_storage.game.json").string().c_str(), nullptr, error,
                                                  sizeof(error)));
    EXPECT_NE(std::string(error).find("$.storage.organization"), std::string::npos);

    write_text(dir / "good_storage.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_TRUE(slayer3d_game_data_validate_file((dir / "good_storage.game.json").string().c_str(), nullptr, error,
                                                 sizeof(error)))
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
        slayer3d_game_data_validate_file((dir / "network_a.game.json").string().c_str(), nullptr, error, sizeof(error)))
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
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Local Only", "id": "test.local_only", "version": "0.1.0" },
  "world": { "name": "world.local_only", "kind": "fixed_screen" },
  "entities": []
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "local_only.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;
    ASSERT_NE(runtime, nullptr);

    std::array<Uint8, SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE> hash{};
    EXPECT_FALSE(slayer3d_game_data_has_network_schema(runtime));
    EXPECT_FALSE(slayer3d_game_data_get_network_schema_hash(runtime, hash.data()));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EncodesAndAppliesPongNetworkSnapshotFromAuthoredSchema)
{
    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(host));

    slayer3d_game_session *client_session = nullptr;
    slayer3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(client));

    slayer3d_registered_actor *host_player = slayer3d_game_data_find_actor(host, "entity.paddle.player");
    slayer3d_registered_actor *host_cpu = slayer3d_game_data_find_actor(host, "entity.paddle.cpu");
    slayer3d_registered_actor *host_ball = slayer3d_game_data_find_actor(host, "entity.ball");
    slayer3d_registered_actor *host_player_score = slayer3d_game_data_find_actor(host, "entity.score.player");
    slayer3d_registered_actor *host_cpu_score = slayer3d_game_data_find_actor(host, "entity.score.cpu");
    slayer3d_registered_actor *host_match = slayer3d_game_data_find_actor(host, "entity.match");
    slayer3d_registered_actor *host_presentation = slayer3d_game_data_find_actor(host, "entity.presentation");
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
    slayer3d_properties_set_vec3(host_ball->props, "velocity", {3.25f, -1.75f, 9.0f});
    slayer3d_properties_set_bool(host_ball->props, "active_motion", true);
    slayer3d_properties_set_bool(host_ball->props, "has_last_reflect_y", true);
    slayer3d_properties_set_float(host_ball->props, "last_reflect_y", 1.5f);
    slayer3d_properties_set_int(host_ball->props, "stagnant_reflect_count", 2);
    slayer3d_properties_set_int(host_player_score->props, "value", 4);
    slayer3d_properties_set_int(host_cpu_score->props, "value", 6);
    slayer3d_properties_set_bool(host_match->props, "finished", true);
    slayer3d_properties_set_int(host_match->props, "winner_id", 2);
    slayer3d_properties_set_bool(host_match->props, "paused", true);
    slayer3d_properties_set_float(host_presentation->props, "border_flash", 0.75f);
    slayer3d_properties_set_float(host_presentation->props, "paddle_flash", 0.5f);

    slayer3d_registered_actor *client_ball = slayer3d_game_data_find_actor(client, "entity.ball");
    ASSERT_NE(client_ball, nullptr);
    slayer3d_properties_set_vec3(client_ball->props, "velocity", {0.0f, 0.0f, 42.0f});

    std::array<Uint8, 512> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_encode_network_snapshot(host, "play_state", 12345U, packet.data(), packet.size(),
                                                           &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 0U);

    Uint32 tick = 0U;
    ASSERT_TRUE(
        slayer3d_game_data_apply_network_snapshot(client, packet.data(), packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 12345U);

    expect_vec3_near(slayer3d_game_data_find_actor(client, "entity.paddle.player")->position, host_player->position);
    expect_vec3_near(slayer3d_game_data_find_actor(client, "entity.paddle.cpu")->position, host_cpu->position);
    expect_vec3_near(client_ball->position, host_ball->position);
    const slayer3d_vec3 client_velocity =
        slayer3d_properties_get_vec3(client_ball->props, "velocity", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(client_velocity.x, 3.25f, 0.0001f);
    EXPECT_NEAR(client_velocity.y, -1.75f, 0.0001f);
    EXPECT_NEAR(client_velocity.z, 42.0f, 0.0001f);
    EXPECT_TRUE(slayer3d_properties_get_bool(client_ball->props, "active_motion", false));
    EXPECT_TRUE(slayer3d_properties_get_bool(client_ball->props, "has_last_reflect_y", false));
    EXPECT_NEAR(slayer3d_properties_get_float(client_ball->props, "last_reflect_y", 0.0f), 1.5f, 0.0001f);
    EXPECT_EQ(slayer3d_properties_get_int(client_ball->props, "stagnant_reflect_count", 0), 2);
    EXPECT_EQ(
        slayer3d_properties_get_int(slayer3d_game_data_find_actor(client, "entity.score.player")->props, "value", 0),
        4);
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_find_actor(client, "entity.score.cpu")->props, "value", 0),
              6);
    EXPECT_TRUE(
        slayer3d_properties_get_bool(slayer3d_game_data_find_actor(client, "entity.match")->props, "finished", false));
    EXPECT_EQ(slayer3d_properties_get_int(slayer3d_game_data_find_actor(client, "entity.match")->props, "winner_id", 0),
              2);
    EXPECT_TRUE(
        slayer3d_properties_get_bool(slayer3d_game_data_find_actor(client, "entity.match")->props, "paused", false));
    EXPECT_NEAR(slayer3d_properties_get_float(slayer3d_game_data_find_actor(client, "entity.presentation")->props,
                                              "border_flash", 0.0f),
                0.75f, 0.0001f);
    EXPECT_NEAR(slayer3d_properties_get_float(slayer3d_game_data_find_actor(client, "entity.presentation")->props,
                                              "paddle_flash", 0.0f),
                0.5f, 0.0001f);

    slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(host), "match_mode", "lan");
    slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(host), "network_role", "host");
    slayer3d_properties_set_string(slayer3d_game_data_mutable_scene_state(host), "network_flow", "direct");
    char description[4096]{};
    ASSERT_TRUE(slayer3d_game_data_describe_network_snapshot(host, "play_state", 12345U, description,
                                                             sizeof(description), error, sizeof(error)))
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
    EXPECT_FALSE(slayer3d_game_data_describe_network_snapshot(host, "play_state", 12345U, tiny_description,
                                                              sizeof(tiny_description), error, sizeof(error)));
    EXPECT_NE(std::string(error).find("buffer is too small"), std::string::npos);

    SDLLogOutputGuard log_guard;
    CapturedLogMessage captured_log;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetLogOutputFunction(capture_log_output, &captured_log);

    bool logged = false;
    ASSERT_TRUE(slayer3d_game_data_log_network_snapshot_diagnostic(host, "multiplayer_state", 12346U, "test_event",
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
    ASSERT_TRUE(slayer3d_game_data_log_network_snapshot_diagnostic(host, "multiplayer_state", 12347U, "test_event",
                                                                   "second", &logged, error, sizeof(error)))
        << error;
    EXPECT_FALSE(logged);
    EXPECT_TRUE(captured_log.message.empty());

    destroy_runtime_session(host_session, host);
    destroy_runtime_session(client_session, client);
}

TEST(GameDataRuntime, RejectsPongNetworkSnapshotsWithMismatchedSchemaOrTruncation)
{
    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    slayer3d_game_session *client_session = nullptr;
    slayer3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);

    std::array<Uint8, 512> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_encode_network_snapshot(host, "play_state", 10U, packet.data(), packet.size(),
                                                           &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 24U);

    std::array<Uint8, 8> too_small{};
    size_t too_small_size = 0U;
    EXPECT_FALSE(slayer3d_game_data_encode_network_snapshot(host, "play_state", 10U, too_small.data(), too_small.size(),
                                                            &too_small_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;
    EXPECT_EQ(too_small_size, 0U);

    std::array<Uint8, 512> corrupted = packet;
    corrupted[16] ^= 0xffU;
    EXPECT_FALSE(slayer3d_game_data_apply_network_snapshot(client, corrupted.data(), packet_size, nullptr, error,
                                                           sizeof(error)));
    EXPECT_NE(std::string(error).find("schema hash"), std::string::npos) << error;

    EXPECT_FALSE(slayer3d_game_data_apply_network_snapshot(client, packet.data(), packet_size - 1U, nullptr, error,
                                                           sizeof(error)));
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
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    slayer3d_game_session *host_session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &host_session));
    slayer3d_game_data_runtime *host = nullptr;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "pool.game.json").string().c_str(), host_session, &host, error,
                                             sizeof(error)))
        << error;

    slayer3d_game_session *client_session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &client_session));
    slayer3d_game_data_runtime *client = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "pool.game.json").string().c_str(), client_session, &client, error,
                                             sizeof(error)))
        << error;

    slayer3d_registered_actor *host_shot0 = slayer3d_game_data_find_actor(host, "pool.shots.0");
    slayer3d_registered_actor *host_shot1 = slayer3d_game_data_find_actor(host, "pool.shots.1");
    slayer3d_registered_actor *client_shot0 = slayer3d_game_data_find_actor(client, "pool.shots.0");
    slayer3d_registered_actor *client_shot1 = slayer3d_game_data_find_actor(client, "pool.shots.1");
    ASSERT_NE(host_shot0, nullptr);
    ASSERT_NE(host_shot1, nullptr);
    ASSERT_NE(client_shot0, nullptr);
    ASSERT_NE(client_shot1, nullptr);
    EXPECT_FALSE(host_shot0->active);
    EXPECT_FALSE(client_shot0->active);

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(host_session),
                         slayer3d_game_data_find_signal(host, "signal.spawn"), nullptr);
    ASSERT_TRUE(host_shot0->active);
    EXPECT_FALSE(host_shot1->active);
    host_shot0->position = slayer3d_vec3_make(5.0f, 6.0f, 7.0f);
    slayer3d_properties_set_int(host_shot0->props, "damage", 11);

    std::array<Uint8, 256> packet{};
    size_t packet_size = 0U;
    ASSERT_TRUE(slayer3d_game_data_encode_network_snapshot(host, "pool_state", 55U, packet.data(), packet.size(),
                                                           &packet_size, error, sizeof(error)))
        << error;
    Uint32 tick = 0U;
    ASSERT_TRUE(
        slayer3d_game_data_apply_network_snapshot(client, packet.data(), packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 55U);
    EXPECT_TRUE(client_shot0->active);
    EXPECT_FALSE(client_shot1->active);
    expect_vec3_near(client_shot0->position, host_shot0->position);
    EXPECT_EQ(slayer3d_properties_get_int(client_shot0->props, "damage", 0), 11);
    EXPECT_STREQ(slayer3d_properties_get_string(client_shot0->props, "pool_lifecycle", ""), "active");

    char description[1024]{};
    ASSERT_TRUE(slayer3d_game_data_describe_network_snapshot(host, "pool_state", 56U, description, sizeof(description),
                                                             error, sizeof(error)))
        << error;
    EXPECT_NE(std::string(description).find("pool.shots.0.active=true"), std::string::npos);
    EXPECT_NE(std::string(description).find("pool.shots.0.properties.damage=11"), std::string::npos);

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(host_session),
                         slayer3d_game_data_find_signal(host, "signal.despawn"), nullptr);
    ASSERT_FALSE(host_shot0->active);
    ASSERT_TRUE(slayer3d_game_data_encode_network_snapshot(host, "pool_state", 56U, packet.data(), packet.size(),
                                                           &packet_size, error, sizeof(error)))
        << error;
    ASSERT_TRUE(
        slayer3d_game_data_apply_network_snapshot(client, packet.data(), packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_FALSE(client_shot0->active);
    EXPECT_STREQ(slayer3d_properties_get_string(client_shot0->props, "pool_lifecycle", ""), "inactive");
    EXPECT_EQ(slayer3d_properties_get_int(client_shot0->props, "damage", 0), 1);
    expect_vec3_near(client_shot0->position, slayer3d_vec3_make(0.0f, 0.0f, 0.25f));

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
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "updates_game": true,
  "renders_world": true
})json");

    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_validate_file((dir / "capacity_2.game.json").string().c_str(), nullptr, error,
                                                 sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_validate_file((dir / "capacity_3.game.json").string().c_str(), nullptr, error,
                                                 sizeof(error)))
        << error;

    EXPECT_NE(load_network_schema_hash(dir / "capacity_2.game.json"),
              load_network_schema_hash(dir / "capacity_3.game.json"));
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsUnknownPooledActorNetworkReplicationRefs)
{
    const std::filesystem::path dir = unique_test_dir("bad_actor_pool_replication");
    const std::string network_json = R"json({
    "protocol": { "id": "slayer3d.test.pool.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_pool_ref.game.json").string().c_str(), nullptr, error,
                                                  sizeof(error)));
    EXPECT_NE(std::string(error).find("actor pool"), std::string::npos) << error;
    remove_test_dir(dir);
}

TEST(GameDataRuntime, EncodesAndAppliesPongNetworkInputFromAuthoredSchema)
{
    slayer3d_game_session *client_session = nullptr;
    slayer3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(client));

    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(host));

    slayer3d_input_manager *client_input = slayer3d_game_session_get_input(client_session);
    slayer3d_input_manager *host_input = slayer3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);

    const int up_action = slayer3d_game_data_find_action(client, "action.paddle.local.up");
    const int down_action = slayer3d_game_data_find_action(client, "action.paddle.local.down");
    ASSERT_GE(up_action, 0);
    ASSERT_GE(down_action, 0);

    slayer3d_input_set_action_override(client_input, up_action, 0.75f);
    slayer3d_input_set_action_override(client_input, down_action, -0.25f);
    ASSERT_NE(slayer3d_input_update(client_input, 44), nullptr);

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_encode_network_input(client, "client_input", client_input, 44U, packet.data(),
                                                        packet.size(), &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 0U);

    Uint32 tick = 0U;
    ASSERT_TRUE(slayer3d_game_data_apply_network_input(host, host_input, packet.data(), packet_size, &tick, error,
                                                       sizeof(error)))
        << error;
    EXPECT_EQ(tick, 44U);

    ASSERT_NE(slayer3d_input_update(host_input, 45), nullptr);
    const int host_up_action = slayer3d_game_data_find_action(host, "action.paddle.local.up");
    const int host_down_action = slayer3d_game_data_find_action(host, "action.paddle.local.down");
    ASSERT_GE(host_up_action, 0);
    ASSERT_GE(host_down_action, 0);
    EXPECT_NEAR(slayer3d_input_get_value(host_input, host_up_action), 0.75f, 0.0001f);
    EXPECT_NEAR(slayer3d_input_get_value(host_input, host_down_action), -0.25f, 0.0001f);

    ASSERT_TRUE(
        slayer3d_game_data_clear_network_input_overrides(host, "client_input", host_input, error, sizeof(error)))
        << error;
    ASSERT_NE(slayer3d_input_update(host_input, 46), nullptr);
    EXPECT_NEAR(slayer3d_input_get_value(host_input, host_up_action), 0.0f, 0.0001f);
    EXPECT_NEAR(slayer3d_input_get_value(host_input, host_down_action), 0.0f, 0.0001f);

    destroy_runtime_session(client_session, client);
    destroy_runtime_session(host_session, host);
}

TEST(GameDataRuntime, RuntimeReplicationBindingsEncodeAndApplyPongPackets)
{
    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(host));

    slayer3d_game_session *client_session = nullptr;
    slayer3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(client));

    slayer3d_registered_actor *host_ball = slayer3d_game_data_find_actor(host, "entity.ball");
    slayer3d_registered_actor *client_ball = slayer3d_game_data_find_actor(client, "entity.ball");
    ASSERT_NE(host_ball, nullptr);
    ASSERT_NE(client_ball, nullptr);
    host_ball->position = {2.0f, 3.0f, 0.25f};

    std::array<Uint8, 512> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_encode_network_runtime_snapshot(host, "state_snapshot", 123U, packet.data(),
                                                                   packet.size(), &packet_size, error, sizeof(error)))
        << error;

    Uint32 tick = 0U;
    ASSERT_TRUE(slayer3d_game_data_apply_network_runtime_snapshot(client, "state_snapshot", packet.data(), packet_size,
                                                                  &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 123U);
    expect_vec3_near(client_ball->position, host_ball->position);

    EXPECT_FALSE(slayer3d_game_data_apply_network_runtime_input(
        host, "client_input", slayer3d_game_session_get_input(host_session), packet.data(), packet_size, nullptr, error,
        sizeof(error)));
    EXPECT_NE(std::string(error).find("unsupported header"), std::string::npos) << error;

    slayer3d_input_manager *client_input = slayer3d_game_session_get_input(client_session);
    slayer3d_input_manager *host_input = slayer3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);
    const int client_up_action = slayer3d_game_data_find_action(client, "action.paddle.local.up");
    const int host_up_action = slayer3d_game_data_find_action(host, "action.paddle.local.up");
    ASSERT_GE(client_up_action, 0);
    ASSERT_GE(host_up_action, 0);
    slayer3d_input_set_action_override(client_input, client_up_action, 0.5f);
    ASSERT_NE(slayer3d_input_update(client_input, 321), nullptr);

    ASSERT_TRUE(slayer3d_game_data_encode_network_runtime_input(
        client, "client_input", client_input, 321U, packet.data(), packet.size(), &packet_size, error, sizeof(error)))
        << error;
    ASSERT_TRUE(slayer3d_game_data_apply_network_runtime_input(host, "client_input", host_input, packet.data(),
                                                               packet_size, &tick, error, sizeof(error)))
        << error;
    EXPECT_EQ(tick, 321U);
    ASSERT_NE(slayer3d_input_update(host_input, 322), nullptr);
    EXPECT_NEAR(slayer3d_input_get_value(host_input, host_up_action), 0.5f, 0.0001f);

    EXPECT_FALSE(slayer3d_game_data_apply_network_runtime_snapshot(client, "state_snapshot", packet.data(), packet_size,
                                                                   nullptr, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("unsupported header"), std::string::npos) << error;

    destroy_runtime_session(host_session, host);
    destroy_runtime_session(client_session, client);
}

TEST(GameDataRuntime, RejectsPongNetworkInputWithMismatchedSchemaOrTruncation)
{
    slayer3d_game_session *client_session = nullptr;
    slayer3d_game_data_runtime *client = nullptr;
    load_pong_runtime(&client_session, &client);
    slayer3d_game_session *host_session = nullptr;
    slayer3d_game_data_runtime *host = nullptr;
    load_pong_runtime(&host_session, &host);

    slayer3d_input_manager *client_input = slayer3d_game_session_get_input(client_session);
    slayer3d_input_manager *host_input = slayer3d_game_session_get_input(host_session);
    ASSERT_NE(client_input, nullptr);
    ASSERT_NE(host_input, nullptr);
    ASSERT_NE(slayer3d_input_update(client_input, 10), nullptr);

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_encode_network_input(client, "client_input", client_input, 10U, packet.data(),
                                                        packet.size(), &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 24U);

    std::array<Uint8, 8> too_small{};
    size_t too_small_size = 0U;
    EXPECT_FALSE(slayer3d_game_data_encode_network_input(client, "client_input", client_input, 10U, too_small.data(),
                                                         too_small.size(), &too_small_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;
    EXPECT_EQ(too_small_size, 0U);

    std::array<Uint8, 128> corrupted = packet;
    corrupted[16] ^= 0xffU;
    EXPECT_FALSE(slayer3d_game_data_apply_network_input(host, host_input, corrupted.data(), packet_size, nullptr, error,
                                                        sizeof(error)));
    EXPECT_NE(std::string(error).find("schema hash"), std::string::npos) << error;

    EXPECT_FALSE(slayer3d_game_data_apply_network_input(host, host_input, packet.data(), packet_size - 1U, nullptr,
                                                        error, sizeof(error)));
    EXPECT_NE(std::string(error).find("action data"), std::string::npos) << error;

    destroy_runtime_session(client_session, client);
    destroy_runtime_session(host_session, host);
}

TEST(GameDataRuntime, EncodesDecodesAndAppliesPongNetworkControlFromAuthoredSchema)
{
    slayer3d_game_session *sender_session = nullptr;
    slayer3d_game_data_runtime *sender = nullptr;
    load_pong_runtime(&sender_session, &sender);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(sender));

    slayer3d_game_session *receiver_session = nullptr;
    slayer3d_game_data_runtime *receiver = nullptr;
    load_pong_runtime(&receiver_session, &receiver);
    ASSERT_TRUE(slayer3d_game_data_has_network_schema(receiver));

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_encode_network_control(sender, "pause_request", 77U, packet.data(), packet.size(),
                                                          &packet_size, error, sizeof(error)))
        << error;
    ASSERT_GT(packet_size, 0U);

    slayer3d_game_data_network_control control{};
    ASSERT_TRUE(
        slayer3d_game_data_decode_network_control(receiver, packet.data(), packet_size, &control, error, sizeof(error)))
        << error;
    EXPECT_STREQ(control.name, "pause_request");
    EXPECT_EQ(control.direction, SLAYER3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL);
    EXPECT_EQ(control.signal_id, slayer3d_game_data_find_signal(receiver, "signal.network.pause_changed"));
    EXPECT_EQ(control.tick, 77U);

    NetworkSignalCapture capture;
    const int connection = slayer3d_signal_connect(slayer3d_game_session_get_signal_bus(receiver_session),
                                                   control.signal_id, capture_signal_payload, &capture);
    ASSERT_GT(connection, 0);
    slayer3d_game_data_network_control applied{};
    ASSERT_TRUE(
        slayer3d_game_data_apply_network_control(receiver, packet.data(), packet_size, &applied, error, sizeof(error)))
        << error;
    EXPECT_STREQ(applied.name, "pause_request");
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(capture.signal_id, control.signal_id);
    EXPECT_EQ(capture.network_control, "pause_request");
    EXPECT_EQ(capture.network_direction, "bidirectional");
    EXPECT_EQ(capture.network_tick, 77);
    slayer3d_signal_disconnect(slayer3d_game_session_get_signal_bus(receiver_session), connection);

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

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "network_runtime_controls.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    const char *control_name = nullptr;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_control(runtime, "semantic_pause", &control_name));
    EXPECT_STREQ(control_name, "pause");

    const char *binding_name = nullptr;
    ASSERT_TRUE(slayer3d_game_data_get_network_runtime_control_binding(runtime, "pause", &binding_name));
    EXPECT_STREQ(binding_name, "semantic_pause");

    std::array<Uint8, SLAYER3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE> packet{};
    size_t packet_size = 0U;
    ASSERT_TRUE(slayer3d_game_data_encode_network_runtime_control(runtime, "semantic_pause", 99U, packet.data(),
                                                                  packet.size(), &packet_size, error, sizeof(error)))
        << error;
    EXPECT_EQ(packet_size, SLAYER3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE);

    slayer3d_game_data_network_control control{};
    const char *decoded_binding = nullptr;
    ASSERT_TRUE(slayer3d_game_data_decode_network_runtime_control(runtime, packet.data(), packet_size, &decoded_binding,
                                                                  &control, error, sizeof(error)))
        << error;
    EXPECT_STREQ(decoded_binding, "semantic_pause");
    EXPECT_STREQ(control.name, "pause");
    EXPECT_EQ(control.tick, 99U);

    EXPECT_FALSE(slayer3d_game_data_encode_network_runtime_control(runtime, "missing", 1U, packet.data(), packet.size(),
                                                                   &packet_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("network runtime control binding 'missing' not found"), std::string::npos)
        << error;

    ASSERT_TRUE(slayer3d_game_data_encode_network_control(runtime, "start_game", 100U, packet.data(), packet.size(),
                                                          &packet_size, error, sizeof(error)))
        << error;
    EXPECT_FALSE(slayer3d_game_data_decode_network_runtime_control(runtime, packet.data(), packet_size,
                                                                   &decoded_binding, &control, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("network runtime control binding for 'start_game' not found"), std::string::npos)
        << error;

    destroy_runtime_session(session, runtime);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, RejectsPongNetworkControlWithMismatchedSchemaOrBadSize)
{
    slayer3d_game_session *sender_session = nullptr;
    slayer3d_game_data_runtime *sender = nullptr;
    load_pong_runtime(&sender_session, &sender);
    slayer3d_game_session *receiver_session = nullptr;
    slayer3d_game_data_runtime *receiver = nullptr;
    load_pong_runtime(&receiver_session, &receiver);

    std::array<Uint8, 128> packet{};
    size_t packet_size = 0U;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_encode_network_control(sender, "disconnect", 88U, packet.data(), packet.size(),
                                                          &packet_size, error, sizeof(error)))
        << error;
    ASSERT_EQ(packet_size, SLAYER3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE);
    ASSERT_GT(packet_size, 32U);

    std::array<Uint8, 8> too_small{};
    size_t too_small_size = 0U;
    EXPECT_FALSE(slayer3d_game_data_encode_network_control(sender, "disconnect", 88U, too_small.data(),
                                                           too_small.size(), &too_small_size, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;
    EXPECT_EQ(too_small_size, 0U);

    std::array<Uint8, 128> corrupted = packet;
    corrupted[16] ^= 0xffU;
    EXPECT_FALSE(slayer3d_game_data_decode_network_control(receiver, corrupted.data(), packet_size, nullptr, error,
                                                           sizeof(error)));
    EXPECT_NE(std::string(error).find("schema hash"), std::string::npos) << error;

    EXPECT_FALSE(slayer3d_game_data_decode_network_control(receiver, packet.data(), packet_size - 1U, nullptr, error,
                                                           sizeof(error)));
    EXPECT_NE(std::string(error).find("requires"), std::string::npos) << error;

    destroy_runtime_session(sender_session, sender);
    destroy_runtime_session(receiver_session, receiver);
}

TEST(GameDataRuntime, AuthoredDirectConnectActionsUpdateSceneState)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    slayer3d_properties_set_string(scene_state, "direct_connect_host", "");
    slayer3d_properties_set_string(scene_state, "direct_connect_port", "27183");
    slayer3d_properties_set_string(scene_state, "direct_connect_status", "Ready");
    slayer3d_properties_set_string(scene_state, "direct_connect_state", "disconnected");
    slayer3d_properties_set_bool(scene_state, "direct_connect_connected", true);

    const int connect_signal = slayer3d_game_data_find_signal(runtime, "signal.multiplayer.direct.connect");
    ASSERT_GE(connect_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(slayer3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""), "Invalid host");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    slayer3d_properties_set_string(scene_state, "direct_connect_host", "127.0.0.1");
    slayer3d_properties_set_string(scene_state, "direct_connect_port", "0");
    slayer3d_properties_set_bool(scene_state, "direct_connect_connected", true);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(slayer3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""), "Invalid port");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    const int disconnect_signal = slayer3d_game_data_find_signal(runtime, "signal.multiplayer.direct.disconnect");
    ASSERT_GE(disconnect_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), disconnect_signal, nullptr);

    EXPECT_EQ(slayer3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""), "Disconnected");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_state", ""), "disconnected");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RuntimeOwnedHostSessionPublishesSceneState)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);

    const ::testing::TestInfo *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_name =
        test_info != nullptr ? std::string(test_info->test_suite_name()) + "." + test_info->name() : "host";
    const int port = 30000 + (int)(std::hash<std::string>{}(test_name) % 20000U);
    if (!slayer3d_game_data_network_host_start(runtime, "test_host", port, "SLAYER3D Test", "host_status",
                                               "host_endpoint", "host_peer", "host_connected"))
    {
        slayer3d_game_data_destroy(runtime);
        slayer3d_game_session_destroy(session);
        GTEST_SKIP() << "network host session unavailable: " << SDL_GetError();
    }

    slayer3d_network_session *host = slayer3d_game_data_get_network_host_session(runtime, "test_host");
    ASSERT_NE(host, nullptr);
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "host_connected", true));
    EXPECT_NE(std::string(slayer3d_properties_get_string(scene_state, "host_endpoint", "")).find("UDP "),
              std::string::npos);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "host_peer", ""), "Waiting for client");

    ASSERT_TRUE(slayer3d_game_data_network_host_publish_status(runtime, "test_host", "host_status", "host_endpoint",
                                                               "host_peer", "host_connected"));
    ASSERT_TRUE(slayer3d_game_data_network_host_cancel(runtime, "test_host", "host_status", "host_endpoint",
                                                       "host_peer", "host_connected", "Not hosting"));
    EXPECT_EQ(slayer3d_game_data_get_network_host_session(runtime, "test_host"), nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "host_status", ""), "Not hosting");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "host_connected", true));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, AuthoredDiscoveryConnectActionsUpdateSceneState)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
    const int connect_signal = slayer3d_game_data_find_signal(runtime, "signal.multiplayer.discovery.connect");
    ASSERT_GE(connect_signal, 0);

    slayer3d_properties_set_int(scene_state, "local_match_index", 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(slayer3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""), "No session selected");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "direct_connect_connected", true));

    ASSERT_TRUE(
        slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "label", "Local Pong Host"));
    ASSERT_TRUE(
        slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "name", "Local Pong Host"));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "host", "127.0.0.1"));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_int(runtime, "local_matches", 0, "port", 0));
    ASSERT_TRUE(
        slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "endpoint", "127.0.0.1:0"));
    EXPECT_EQ(slayer3d_game_data_runtime_collection_count(runtime, "local_matches"), 1);

    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_EQ(slayer3d_game_data_get_network_direct_connect_session(runtime, "direct_connect"), nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_host", ""), "127.0.0.1");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_port", ""), "0");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""), "Invalid port");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_state", ""), "error");
    EXPECT_FALSE(slayer3d_properties_get_bool(scene_state, "direct_connect_connected", true));
    EXPECT_EQ(slayer3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);

    ASSERT_TRUE(
        slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "label", "Valid Pong Host"));
    ASSERT_TRUE(
        slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "name", "Valid Pong Host"));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "host", "127.0.0.1"));
    ASSERT_TRUE(slayer3d_game_data_runtime_collection_set_int(runtime, "local_matches", 0, "port", 65535));
    ASSERT_TRUE(
        slayer3d_game_data_runtime_collection_set_string(runtime, "local_matches", 0, "endpoint", "127.0.0.1:65535"));
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), connect_signal, nullptr);

    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_host", ""), "127.0.0.1");
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_port", ""), "65535");
    EXPECT_EQ(slayer3d_game_data_runtime_collection_count(runtime, "local_matches"), 0);
    if (slayer3d_game_data_get_network_direct_connect_session(runtime, "direct_connect") != nullptr)
    {
        EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""),
                     "Match found. Connecting...");
    }
    else
    {
        EXPECT_NE(std::string(slayer3d_properties_get_string(scene_state, "direct_connect_status", ""))
                      .find("networking is disabled"),
                  std::string::npos);
    }

    const int cancel_signal = slayer3d_game_data_find_signal(runtime, "signal.multiplayer.discovery.cancel");
    ASSERT_GE(cancel_signal, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), cancel_signal, nullptr);
    EXPECT_STREQ(slayer3d_properties_get_string(scene_state, "local_match_status", ""), "Discovery canceled");
    EXPECT_EQ(slayer3d_properties_get_int(scene_state, "local_match_count", -1), 0);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}

TEST(GameDataRuntime, RejectsInvalidDirectConnectActions)
{
    const std::filesystem::path dir = unique_test_dir("bad_direct_connect_actions");
    write_text(dir / "bad_direct_connect.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "bad_direct_connect.game.json").string().c_str(), nullptr,
                                                  error, sizeof(error)));
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
  "schema": "slayer3d.game.v0",
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
        slayer3d_game_data_validate_file((dir / "bad_host.game.json").string().c_str(), nullptr, error, sizeof(error)));
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
  "schema": "slayer3d.game.v0",
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
        EXPECT_FALSE(slayer3d_game_data_validate_file(
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
    "protocol": { "id": "slayer3d.test.network.v1", "version": 1, "transport": "udp", "tick_rate": 60 },
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
        EXPECT_FALSE(slayer3d_game_data_validate_file(path.string().c_str(), nullptr, error, sizeof(error)))
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
  "schema": "slayer3d.game.v0",
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

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "audio_cache.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    char materialized_path[4096]{};
    ASSERT_TRUE(slayer3d_game_data_prepare_audio_file(runtime, "asset://tone.wav", materialized_path,
                                                      sizeof(materialized_path)));
    const std::filesystem::path materialized(materialized_path);
    EXPECT_TRUE(std::filesystem::exists(materialized));
    EXPECT_EQ(materialized.parent_path().filename().string(), "audio");

    std::error_code ec;
    EXPECT_TRUE(std::filesystem::equivalent(materialized.parent_path().parent_path(), cache_root, ec));

    std::ifstream in(materialized, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "audio bytes");

    char cached_path[4096]{};
    ASSERT_TRUE(slayer3d_game_data_prepare_audio_file(runtime, "asset://tone.wav", cached_path, sizeof(cached_path)));
    EXPECT_STREQ(cached_path, materialized_path);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
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
    local gx, gy, gz = slayer3d.get_vec3(ghost, "velocity")
    if gx ~= nil or gy ~= nil or gz ~= nil then
        return false
    end

    local ok = ctx.storage.write("user://settings/options.json", "{\"difficulty\":\"hard\"}")
    if not ok or not ctx.storage.exists("user://settings/options.json") then
        return false
    end

    local data = ctx.storage.read("user://settings/options.json")
    local decoded, decode_error = slayer3d.json.decode(data)
    if decoded == nil or decode_error ~= nil or decoded.difficulty ~= "hard" then
        return false
    end
    local encoded = slayer3d.json.encode({ difficulty = decoded.difficulty, enabled = true, values = { 1, 2, 3 } })
    local roundtrip = slayer3d.json.decode(encoded)
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
  "schema": "slayer3d.game.v0",
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

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "lua_storage.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    const int signal_id = slayer3d_game_data_find_signal(runtime, "signal.storage.roundtrip");
    ASSERT_GE(signal_id, 0);
    slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), signal_id, nullptr);

    EXPECT_STREQ(slayer3d_properties_get_string(slayer3d_game_data_scene_state(runtime), "loaded_options", ""),
                 "hard:2:true");
    EXPECT_TRUE(std::filesystem::exists(user_root / "settings" / "options.json"));
    EXPECT_TRUE(std::filesystem::exists(cache_root / "script" / "status.txt"));
    EXPECT_FALSE(std::filesystem::exists(dir / "escape.txt"));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    remove_test_dir(dir);
}

TEST(GameDataRuntime, GenericPersistenceSavesOptionsAndPongLuaLoadsHighScores)
{
    const std::filesystem::path dir = unique_test_dir("pong_persistence");
    const std::filesystem::path user_root = dir / "user";
    const std::filesystem::path cache_root = dir / "cache";
    const std::filesystem::path game_path = copy_pong_data_with_storage_overrides(dir, user_root, cache_root);

    auto emit = [](slayer3d_game_session *session, slayer3d_game_data_runtime *runtime, const char *signal) {
        const int signal_id = slayer3d_game_data_find_signal(runtime, signal);
        EXPECT_GE(signal_id, 0) << signal;
        if (signal_id >= 0)
            slayer3d_signal_emit(slayer3d_game_session_get_signal_bus(session), signal_id, nullptr);
    };

    {
        slayer3d_game_session *session = nullptr;
        ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

        char error[512]{};
        slayer3d_game_data_runtime *runtime = nullptr;
        ASSERT_TRUE(slayer3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
            << error;

        slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
        ASSERT_NE(settings, nullptr);
        EXPECT_FALSE(slayer3d_properties_get_bool(settings->props, "options_persistence_enabled", true));
        EXPECT_TRUE(slayer3d_properties_get_bool(settings->props, "score_persistence_enabled", false));

        emit(session, runtime, "signal.persistence.save_options");
        EXPECT_FALSE(std::filesystem::exists(user_root / "settings" / "options.json"));

        emit(session, runtime, "signal.persistence.load");
        EXPECT_FALSE(slayer3d_properties_get_bool(settings->props, "options_persistence_enabled", true));

        slayer3d_properties_set_string(settings->props, "display_mode", "windowed");
        slayer3d_properties_set_bool(settings->props, "vsync", false);
        slayer3d_properties_set_string(settings->props, "renderer", "opengl");
        slayer3d_properties_set_string(settings->props, "gamepad_icons", "playstation");
        slayer3d_properties_set_bool(settings->props, "vibration", false);
        slayer3d_properties_set_int(settings->props, "sfx_volume", 4);
        slayer3d_properties_set_int(settings->props, "music_volume", 7);
        slayer3d_properties_set_bool(settings->props, "options_persistence_enabled", true);
        emit(session, runtime, "signal.persistence.save_options");

        emit(session, runtime, "signal.match.player_won");
        slayer3d_registered_actor *scores = slayer3d_game_data_find_actor(runtime, "entity.high_scores");
        ASSERT_NE(scores, nullptr);
        EXPECT_EQ(slayer3d_properties_get_int(scores->props, "player_wins", 0), 1);
        EXPECT_EQ(slayer3d_properties_get_int(scores->props, "matches_played", 0), 1);
        EXPECT_STREQ(slayer3d_properties_get_string(scores->props, "latest_winner", ""), "player");

        slayer3d_game_data_destroy(runtime);
        slayer3d_game_session_destroy(session);
    }

    EXPECT_TRUE(std::filesystem::exists(user_root / "settings" / "options.json"));
    EXPECT_TRUE(std::filesystem::exists(user_root / "scores" / "pong_scores.json"));
    const std::string options_text = read_text(user_root / "settings" / "options.json");
    EXPECT_NE(options_text.find("\"schema\": \"slayer3d.options.v1\""), std::string::npos);
    EXPECT_NE(options_text.find("\"version\": 1"), std::string::npos);
    EXPECT_NE(options_text.find("\"display_mode\": \"windowed\""), std::string::npos);
    EXPECT_NE(options_text.find("\"gamepad_icons\": \"playstation\""), std::string::npos);

    slayer3d_game_config persisted_config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_app_config_file(game_path.string().c_str(), &persisted_config, title,
                                                        sizeof(title), app_error, sizeof(app_error)))
        << app_error;
    EXPECT_EQ(persisted_config.display_mode, SLAYER3D_WINDOW_MODE_WINDOWED);
    EXPECT_EQ(persisted_config.backend, SLAYER3D_BACKEND_OPENGL);
    EXPECT_GT(persisted_config.vsync, 0);

    {
        slayer3d_game_session *session = nullptr;
        ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

        char error[512]{};
        slayer3d_game_data_runtime *runtime = nullptr;
        ASSERT_TRUE(slayer3d_game_data_load_file(game_path.string().c_str(), session, &runtime, error, sizeof(error)))
            << error;

        emit(session, runtime, "signal.persistence.load");

        slayer3d_registered_actor *settings = slayer3d_game_data_find_actor(runtime, "entity.settings");
        ASSERT_NE(settings, nullptr);
        EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
        EXPECT_TRUE(slayer3d_properties_get_bool(settings->props, "vsync", false));
        EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "renderer", ""), "opengl");
        EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "gamepad_icons", ""), "xbox");
        EXPECT_TRUE(slayer3d_properties_get_bool(settings->props, "vibration", false));
        EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 8);
        EXPECT_EQ(slayer3d_properties_get_int(settings->props, "music_volume", 0), 7);

        slayer3d_properties_set_bool(settings->props, "options_persistence_enabled", true);
        emit(session, runtime, "signal.persistence.load");
        EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "display_mode", ""), "windowed");
        EXPECT_FALSE(slayer3d_properties_get_bool(settings->props, "vsync", true));
        EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "renderer", ""), "opengl");
        EXPECT_STREQ(slayer3d_properties_get_string(settings->props, "gamepad_icons", ""), "playstation");
        EXPECT_FALSE(slayer3d_properties_get_bool(settings->props, "vibration", true));
        EXPECT_EQ(slayer3d_properties_get_int(settings->props, "sfx_volume", 0), 4);
        EXPECT_EQ(slayer3d_properties_get_int(settings->props, "music_volume", 0), 7);

        slayer3d_registered_actor *scores = slayer3d_game_data_find_actor(runtime, "entity.high_scores");
        ASSERT_NE(scores, nullptr);
        EXPECT_EQ(slayer3d_properties_get_int(scores->props, "player_wins", 0), 1);
        EXPECT_EQ(slayer3d_properties_get_int(scores->props, "cpu_wins", -1), 0);
        EXPECT_EQ(slayer3d_properties_get_int(scores->props, "matches_played", 0), 1);
        EXPECT_STREQ(slayer3d_properties_get_string(scores->props, "latest_winner", ""), "player");

        emit(session, runtime, "signal.match.cpu_won");
        EXPECT_EQ(slayer3d_properties_get_int(scores->props, "cpu_wins", 0), 1);
        EXPECT_EQ(slayer3d_properties_get_int(scores->props, "matches_played", 0), 2);
        EXPECT_STREQ(slayer3d_properties_get_string(scores->props, "latest_winner", ""), "cpu");

        slayer3d_game_data_destroy(runtime);
        slayer3d_game_session_destroy(session);
    }

    remove_test_dir(dir);
}

TEST(GameDataRuntime, ValidationReportsWarningsWithoutFailingByDefault)
{
    DiagnosticCapture capture;
    slayer3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    const std::string path = fixture_path("warning_unsupported_component.game.json");
    EXPECT_TRUE(slayer3d_game_data_validate_file(path.c_str(), &options, error, sizeof(error))) << error;
    ASSERT_EQ(capture.diagnostics.size(), 1u);
    EXPECT_EQ(capture.diagnostics[0].severity, SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING);
    EXPECT_NE(capture.diagnostics[0].message.find("unsupported component type"), std::string::npos);
    EXPECT_EQ(error[0], '\0');

    options.treat_warnings_as_errors = true;
    EXPECT_FALSE(slayer3d_game_data_validate_file(path.c_str(), &options, error, sizeof(error)));
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
        slayer3d_game_session *session = nullptr;
        ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

        char error[512]{};
        slayer3d_game_data_runtime *runtime = nullptr;
        const std::string path = fixture_path(file);
        EXPECT_FALSE(slayer3d_game_data_load_file(path.c_str(), session, &runtime, error, sizeof(error))) << file;
        EXPECT_NE(error[0], '\0') << file;
        EXPECT_EQ(runtime, nullptr);

        slayer3d_game_data_destroy(runtime);
        slayer3d_game_session_destroy(session);
    }
}

TEST(GameDataRuntime, RejectsLegacySplashSceneBlock)
{
    const std::filesystem::path dir = unique_test_dir("legacy_splash");
    write_text(dir / "legacy_splash.game.json",
               R"json({
  "schema": "slayer3d.game.v0",
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
  "schema": "slayer3d.scene.v0",
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
  "schema": "slayer3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false
})json");

    DiagnosticCapture capture;
    slayer3d_game_data_validation_options options{};
    options.diagnostic = capture_diagnostic;
    options.userdata = &capture;

    char error[512]{};
    EXPECT_FALSE(slayer3d_game_data_validate_file((dir / "legacy_splash.game.json").string().c_str(), &options, error,
                                                  sizeof(error)));
    ASSERT_FALSE(capture.diagnostics.empty());
    EXPECT_EQ(capture.diagnostics[0].severity, SLAYER3D_GAME_DATA_DIAGNOSTIC_ERROR);
    EXPECT_NE(capture.diagnostics[0].message.find("scene.timeline"), std::string::npos);
    EXPECT_NE(std::string(error).find("scene splash is no longer supported"), std::string::npos);

    remove_test_dir(dir);
}

TEST(GameDataRuntime, AuthoredGoalSensorDrivesScoreBinding)
{
    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));

    char error[512]{};
    slayer3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(
        slayer3d_game_data_load_file(pong_data_path().string().c_str(), session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_registered_actor *ball = slayer3d_game_data_find_actor(runtime, "entity.ball");
    slayer3d_registered_actor *cpu_score = slayer3d_game_data_find_actor(runtime, "entity.score.cpu");
    ASSERT_NE(ball, nullptr);
    ASSERT_NE(cpu_score, nullptr);

    ball->position.x = -10.0f;
    ASSERT_TRUE(slayer3d_game_data_set_active_scene(runtime, "scene.play"));
    ASSERT_TRUE(slayer3d_game_data_update(runtime, 1.0f / 60.0f));

    EXPECT_EQ(slayer3d_properties_get_int(cpu_score->props, "value", 0), 1);
    EXPECT_FLOAT_EQ(ball->position.x, 0.0f);
    EXPECT_FALSE(slayer3d_properties_get_bool(ball->props, "active_motion", true));

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
}
