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
};

struct UiTextCapture
{
    int count = 0;
    bool saw_score = false;
    bool saw_pause = false;
};

struct ParticleCapture
{
    int count = 0;
    bool saw_ambient = false;
};

struct EvaluatedPrimitiveCapture
{
    bool saw_border = false;
    bool saw_ball = false;
};

struct ScenePayloadCapture
{
    bool called = false;
    std::string from_scene;
    std::string to_scene;
    std::string selected_level;
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

std::string fixture_path(const char *filename)
{
    return std::string(SDL3D_GAME_DATA_FIXTURE_DIR) + "/" + filename;
}

std::string read_fixture_file(const char *filename)
{
    std::ifstream in(fixture_path(filename), std::ios::binary);
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
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.paddle.up"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.scene.title"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.scene.options"), 0);
    EXPECT_GE(sdl3d_game_data_find_action(runtime, "action.scene.play"), 0);
    EXPECT_STREQ(sdl3d_game_data_active_camera(runtime), "camera.overhead");
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");
    EXPECT_EQ(sdl3d_game_data_scene_count(runtime), 3);
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 0), "scene.title");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 1), "scene.options");
    EXPECT_STREQ(sdl3d_game_data_scene_name_at(runtime, 2), "scene.play");
    EXPECT_EQ(sdl3d_game_data_scene_name_at(runtime, -1), nullptr);
    EXPECT_EQ(sdl3d_game_data_scene_name_at(runtime, 3), nullptr);
    EXPECT_FALSE(sdl3d_game_data_active_scene_updates_game(runtime));
    EXPECT_FALSE(sdl3d_game_data_active_scene_renders_world(runtime));
    EXPECT_FALSE(sdl3d_game_data_active_scene_has_entity(runtime, "entity.ball"));
    EXPECT_EQ(sdl3d_timer_pool_active_count(sdl3d_game_session_get_timer_pool(session)), 0);

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ExposesAuthoredPongPresentationData)
{
    sdl3d_game_config config{};
    char title[128]{};
    char app_error[512]{};
    ASSERT_TRUE(sdl3d_game_data_load_app_config_file(SDL3D_PONG_DATA_PATH, &config, title, sizeof(title), app_error,
                                                     sizeof(app_error)))
        << app_error;
    EXPECT_STREQ(config.title, "SDL3D Pong");
    EXPECT_EQ(config.width, 1280);
    EXPECT_EQ(config.height, 720);
    EXPECT_EQ(config.backend, SDL3D_BACKEND_AUTO);
    EXPECT_NEAR(config.tick_rate, 1.0f / 120.0f, 0.00001f);
    EXPECT_EQ(config.max_ticks_per_frame, 12);

    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

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
    EXPECT_EQ(ui.count, 8);
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

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}

TEST(GameDataRuntime, ExposesDataDrivenScenesAndMenus)
{
    sdl3d_game_session *session = nullptr;
    ASSERT_TRUE(sdl3d_game_session_create(nullptr, &session));

    char error[512]{};
    sdl3d_game_data_runtime *runtime = nullptr;
    ASSERT_TRUE(sdl3d_game_data_load_file(SDL3D_PONG_DATA_PATH, session, &runtime, error, sizeof(error))) << error;

    sdl3d_game_data_menu menu{};
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.title");
    EXPECT_EQ(menu.item_count, 3);
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
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 1, &item));
    EXPECT_STREQ(item.label, "Options");
    EXPECT_STREQ(item.scene, "scene.options");
    EXPECT_FALSE(item.quit);

    ASSERT_TRUE(sdl3d_game_data_menu_move(runtime, menu.name, -1));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_EQ(menu.selected_index, 2);
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
            EXPECT_EQ(text->align, SDL3D_GAME_DATA_UI_ALIGN_CENTER);
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

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.options"));
    ASSERT_TRUE(sdl3d_game_data_get_active_menu(runtime, &menu));
    EXPECT_STREQ(menu.name, "menu.options");
    EXPECT_EQ(menu.item_count, 5);
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 0, &item));
    EXPECT_STREQ(item.label, "Difficulty");
    EXPECT_EQ(item.control_type, SDL3D_GAME_DATA_MENU_CONTROL_CHOICE);
    EXPECT_STREQ(item.control_target, "entity.settings");
    EXPECT_STREQ(item.control_key, "difficulty");
    EXPECT_EQ(item.choice_count, 3);
    sdl3d_registered_actor *settings = sdl3d_game_data_find_actor(runtime, "entity.settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "difficulty", ""), "normal");
    ASSERT_TRUE(sdl3d_game_data_apply_menu_item_control(runtime, &item));
    EXPECT_STREQ(sdl3d_properties_get_string(settings->props, "difficulty", ""), "hard");
    ASSERT_TRUE(sdl3d_game_data_get_menu_item(runtime, menu.name, 4, &item));
    EXPECT_STREQ(item.scene, "scene.title");

    bool saw_options_value = false;
    auto find_options_value = [](void *userdata, const sdl3d_game_data_ui_text *text) -> bool {
        auto *saw = static_cast<bool *>(userdata);
        const std::string name = text->name != nullptr ? text->name : "";
        const std::string value = text->text != nullptr ? text->text : "";
        if (name == "ui.options.menu" && value == "Difficulty: Hard")
        {
            *saw = true;
            return false;
        }
        return true;
    };
    ASSERT_TRUE(sdl3d_game_data_for_each_ui_text(runtime, find_options_value, &saw_options_value));
    EXPECT_TRUE(saw_options_value);

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
    EXPECT_STREQ(result.scene, "scene.options");
    EXPECT_EQ(result.signal_id, -1);

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

    sdl3d_input_manager *input = sdl3d_game_session_get_input(session);
    ASSERT_NE(input, nullptr);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_3;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 1);

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(sdl3d_game_data_app_flow_is_transitioning(&flow));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.title");

    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_STREQ(sdl3d_game_data_active_scene(runtime), "scene.play");
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.29f));
    EXPECT_FALSE(sdl3d_game_data_app_flow_is_transitioning(&flow));

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 2);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 3);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_TRUE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 4);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 5);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    sdl3d_registered_actor *match = sdl3d_game_data_find_actor(runtime, "entity.match");
    ASSERT_NE(match, nullptr);
    sdl3d_properties_set_bool(match->props, "finished", true);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 6);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 7);
    ASSERT_TRUE(sdl3d_game_data_app_flow_update(&flow, &ctx, runtime, 0.0f));
    EXPECT_FALSE(ctx.paused);

    key.type = SDL_EVENT_KEY_UP;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 8);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_ESCAPE;
    sdl3d_input_process_event(input, &key);
    sdl3d_input_update(input, 9);
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

    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 0.1f));

    EXPECT_GT(cpu->position.y, 0.0f);
    EXPECT_LE(cpu->position.y, 0.55f);

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
    ASSERT_TRUE(sdl3d_game_data_set_active_scene(runtime, "scene.play"));
    ASSERT_TRUE(sdl3d_game_data_update(runtime, 1.0f / 60.0f));

    EXPECT_EQ(sdl3d_properties_get_int(cpu_score->props, "value", 0), 1);
    EXPECT_FLOAT_EQ(ball->position.x, 0.0f);
    EXPECT_FALSE(sdl3d_properties_get_bool(ball->props, "active_motion", true));

    sdl3d_game_data_destroy(runtime);
    sdl3d_game_session_destroy(session);
}
