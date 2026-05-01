#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

extern "C"
{
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/asset.h"
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

struct RenderPrimitiveCapture
{
    int cubes = 0;
    int spheres = 0;
    bool saw_player_paddle = false;
    bool saw_ball = false;
    bool saw_options_background = false;
    bool saw_options_glow = false;
};

struct UiTextCapture
{
    int count = 0;
    bool saw_score = false;
    bool saw_pause = false;
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
    }
  ],
  "scenes": {
    "initial": "scene.title",
    "files": ["scenes/title.scene.json"]
  }
})json");
    write_text(dir / "scenes" / "title.scene.json",
               R"json({
  "schema": "sdl3d.scene.v0",
  "name": "scene.title",
  "updates_game": false,
  "renders_world": false,
  "camera": "camera.overhead",
  "entities": ["entity.state"],
  "activity": {
    "enabled": true,
    "input": "any",
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
  }
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
        EXPECT_NEAR(primitive->position.z, 0.12f, 0.0001f);
        EXPECT_NEAR(primitive->radius, 0.22f, 0.0001f);
        EXPECT_EQ(primitive->rings, 12);
        EXPECT_EQ(primitive->slices, 18);
        EXPECT_TRUE(primitive->emissive);
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
        EXPECT_GT(primitive->emissive_color.x, 0.7f);
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
    EXPECT_STREQ(app.startup_transition, "startup");
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

    EXPECT_EQ(sdl3d_game_data_world_light_count(runtime), 4);
    sdl3d_light ball_light{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light(runtime, 3, &ball_light));
    EXPECT_EQ(ball_light.type, SDL3D_LIGHT_POINT);
    EXPECT_NEAR(ball_light.position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(ball_light.position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(ball_light.position.z, 1.32f, 0.0001f);

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

    sdl3d_registered_actor *presentation = sdl3d_game_data_find_actor(runtime, "entity.presentation");
    ASSERT_NE(presentation, nullptr);
    EXPECT_NEAR(sdl3d_properties_get_float(presentation->props, "border_flash_decay", 0.0f), 2.8f, 0.0001f);
    sdl3d_properties_set_float(presentation->props, "border_flash", 1.0f);
    ASSERT_TRUE(sdl3d_game_data_update_property_effects(runtime, 0.25f));
    EXPECT_NEAR(sdl3d_properties_get_float(presentation->props, "border_flash", -1.0f), 0.3f, 0.0001f);
    sdl3d_properties_set_float(presentation->props, "border_flash", 1.0f);

    sdl3d_light base_light{};
    sdl3d_light flashed_light{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light(runtime, 0, &base_light));
    sdl3d_game_data_render_eval light_eval{};
    ASSERT_TRUE(sdl3d_game_data_get_world_light_evaluated(runtime, 0, &light_eval, &flashed_light));
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
    EXPECT_EQ(ui.count, 7);
    EXPECT_TRUE(ui.saw_score);
    EXPECT_TRUE(ui.saw_pause);

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
    EXPECT_STREQ(image_asset.path, "asset://images/splash-logo.jpg");

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
    EXPECT_STREQ(menu.name, "menu.multiplayer.lobby");
    EXPECT_EQ(menu.item_count, 2);
    const int lobby_start_signal = sdl3d_game_data_find_signal(runtime, "signal.multiplayer.lobby.start");
    ASSERT_GE(lobby_start_signal, 0);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Start Game");
    EXPECT_EQ(item.signal_id, lobby_start_signal);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Back");
    EXPECT_STREQ(item.scene, "scene.multiplayer.lan");

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer.join"));
    EXPECT_TRUE(sdl3d_game_data_active_scene_renders_world(runtime));
    EXPECT_TRUE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.multiplayer.discovery"));
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
    EXPECT_FALSE(sdl3d_game_data_get_active_menu(runtime, &menu));

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.multiplayer.discovery"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.multiplayer.discovery");
    EXPECT_EQ(menu.item_count, 1);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
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
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));
    ASSERT_TRUE(sdl3d_game_data_set_active_scene_with_payload(runtime, "scene.play", payload));
    EXPECT_TRUE(payload_capture.called);
    EXPECT_EQ(payload_capture.from_scene, "scene.options");
    EXPECT_EQ(payload_capture.to_scene, "scene.play");
    EXPECT_EQ(payload_capture.selected_level, "level.test");
    sdl3d_properties_destroy(payload);

    sdl3d_properties *scene_state = sdl3d_game_data_mutable_scene_state(runtime);
    ASSERT_NE(scene_state, nullptr);
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
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));

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
