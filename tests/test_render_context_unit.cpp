#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

extern "C"
{
#include "backend.h"
#include "sdl3d/sdl3d.h"
}

#include <string>
#include <string_view>

namespace
{
bool SetBackendOverride(const char *value)
{
    if (value == nullptr)
    {
        return SDL_unsetenv_unsafe("SDL3D_BACKEND") == 0;
    }

    return SDL_setenv_unsafe("SDL3D_BACKEND", value, 1) == 0;
}

class SDL3DBackendEnvGuard
{
  public:
    explicit SDL3DBackendEnvGuard(const char *value)
    {
        const char *existing = SDL_getenv("SDL3D_BACKEND");
        if (existing != nullptr)
        {
            had_existing_ = true;
            previous_value_ = existing;
        }

        if (!SetBackendOverride(value))
        {
            ADD_FAILURE() << "Failed to set SDL3D_BACKEND test override";
        }
    }

    ~SDL3DBackendEnvGuard()
    {
        if (had_existing_)
        {
            SetBackendOverride(previous_value_.c_str());
        }
        else
        {
            SetBackendOverride(nullptr);
        }
    }

  private:
    bool had_existing_ = false;
    std::string previous_value_;
};
} // namespace

TEST(SDL3DRenderContextConfig, InitRenderContextConfigSetsDocumentedDefaults)
{
    sdl3d_render_context_config config{};
    config.backend = SDL3D_BACKEND_OPENGL;
    config.allow_backend_fallback = false;
    config.logical_width = 42;
    config.logical_height = 42;
    config.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    sdl3d_init_render_context_config(&config);

    EXPECT_EQ(SDL3D_BACKEND_AUTO, config.backend);
    EXPECT_TRUE(config.allow_backend_fallback);
    EXPECT_EQ(0, config.logical_width);
    EXPECT_EQ(0, config.logical_height);
    EXPECT_EQ(SDL_LOGICAL_PRESENTATION_STRETCH, config.logical_presentation);
}

TEST(SDL3DRenderContextConfig, InitRenderContextConfigRejectsNull)
{
    SDL_ClearError();
    sdl3d_init_render_context_config(nullptr);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'config' is invalid"), std::string_view::npos);
}

TEST(SDL3DRenderContextState, NullStateAccessorsAreRejected)
{
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_set_scissor_rect(nullptr, nullptr));
    EXPECT_FALSE(sdl3d_is_scissor_enabled(nullptr));
    EXPECT_FALSE(sdl3d_get_scissor_rect(nullptr, reinterpret_cast<SDL_Rect *>(0x1)));
    EXPECT_FALSE(sdl3d_clear_render_context_rect(nullptr, reinterpret_cast<const SDL_Rect *>(0x1), sdl3d_color{}));
}

TEST(SDL3DBackendName, MapsKnownBackendsToStableStrings)
{
    EXPECT_EQ(std::string_view("auto"), sdl3d_get_backend_name(SDL3D_BACKEND_AUTO));
    EXPECT_EQ(std::string_view("software"), sdl3d_get_backend_name(SDL3D_BACKEND_SOFTWARE));
    EXPECT_EQ(std::string_view("opengl"), sdl3d_get_backend_name(SDL3D_BACKEND_OPENGL));
}

TEST(SDL3DBackendName, UnknownBackendsMapToUnknown)
{
    const sdl3d_backend bogus = static_cast<sdl3d_backend>(0x4242);
    EXPECT_EQ(std::string_view("unknown"), sdl3d_get_backend_name(bogus));
}

TEST(SDL3DBackendEnvOverride, UnsetEnvReturnsFalseWithoutTouchingOutput)
{
    SDL3DBackendEnvGuard guard(nullptr);
    sdl3d_backend backend = SDL3D_BACKEND_OPENGL;

    EXPECT_FALSE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SDL3D_BACKEND_OPENGL, backend);
}

TEST(SDL3DBackendEnvOverride, EmptyEnvReturnsFalseWithoutTouchingOutput)
{
    SDL3DBackendEnvGuard guard("");
    sdl3d_backend backend = SDL3D_BACKEND_OPENGL;

    EXPECT_FALSE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SDL3D_BACKEND_OPENGL, backend);
}

TEST(SDL3DBackendEnvOverride, ParsesSoftware)
{
    SDL3DBackendEnvGuard guard("software");
    sdl3d_backend backend = SDL3D_BACKEND_AUTO;

    EXPECT_TRUE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SDL3D_BACKEND_SOFTWARE, backend);
}

TEST(SDL3DBackendEnvOverride, ParsesAuto)
{
    SDL3DBackendEnvGuard guard("AUTO");
    sdl3d_backend backend = SDL3D_BACKEND_OPENGL;

    EXPECT_TRUE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SDL3D_BACKEND_AUTO, backend);
}

TEST(SDL3DBackendEnvOverride, ParsesOpenGlAndGpuAliasesCaseInsensitively)
{
    {
        SDL3DBackendEnvGuard guard("OpenGL");
        sdl3d_backend backend = SDL3D_BACKEND_AUTO;
        EXPECT_TRUE(sdl3d_get_backend_override_from_environment(&backend));
        EXPECT_EQ(SDL3D_BACKEND_OPENGL, backend);
    }

    {
        SDL3DBackendEnvGuard guard("GPU");
        sdl3d_backend backend = SDL3D_BACKEND_AUTO;
        EXPECT_TRUE(sdl3d_get_backend_override_from_environment(&backend));
        EXPECT_EQ(SDL3D_BACKEND_OPENGL, backend);
    }
}

TEST(SDL3DBackendEnvOverride, InvalidValueReturnsFalseAndSetsError)
{
    SDL3DBackendEnvGuard guard("not-a-backend");
    sdl3d_backend backend = SDL3D_BACKEND_AUTO;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Unsupported SDL3D backend override"), std::string_view::npos);
}

TEST(SDL3DBackendEnvOverride, NullBackendOutputIsRejected)
{
    SDL3DBackendEnvGuard guard("software");

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_get_backend_override_from_environment(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'backend' is invalid"), std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, RejectsNullWindow)
{
    sdl3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(nullptr, reinterpret_cast<SDL_Renderer *>(0x1), nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'window' is invalid"), std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, RejectsNullRenderer)
{
    sdl3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), nullptr, nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'renderer' is invalid"), std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, RejectsNullOutputPointer)
{
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), reinterpret_cast<SDL_Renderer *>(0x1),
                                             nullptr, nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'out_context' is invalid"), std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, RejectsNegativeLogicalDimensions)
{
    sdl3d_render_context *context = nullptr;
    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.logical_width = -1;
    config.logical_height = 0;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), reinterpret_cast<SDL_Renderer *>(0x1),
                                             &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Logical dimensions must be zero or positive"),
              std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, RejectsMismatchedLogicalDimensions)
{
    sdl3d_render_context *context = nullptr;
    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.logical_width = 320;
    config.logical_height = 0;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), reinterpret_cast<SDL_Renderer *>(0x1),
                                             &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Logical width and height must both be zero or both be non-zero"),
              std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, InvalidEnvOverrideFailsBeforeTouchingRenderer)
{
    SDL3DBackendEnvGuard guard("nonsense");
    sdl3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), reinterpret_cast<SDL_Renderer *>(0x1),
                                             nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Unsupported SDL3D backend override"), std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, OpenGLBackendAcceptsRequest)
{
    SDL3DBackendEnvGuard guard(nullptr);
    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.backend = SDL3D_BACKEND_OPENGL;
    config.allow_backend_fallback = false;

    /* The GL backend is now implemented. Creating with a fake window will
     * fail at GL context creation, but the backend selection itself should
     * not produce a "not implemented" error. */
    sdl3d_render_context *context = nullptr;
    bool ok = sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), reinterpret_cast<SDL_Renderer *>(0x1),
                                          &config, &context);
    if (!ok)
    {
        std::string_view err = SDL_GetError();
        EXPECT_EQ(err.find("not implemented"), std::string_view::npos) << "Backend should be accepted, got: " << err;
    }
    if (context != nullptr)
    {
        sdl3d_destroy_render_context(context);
    }
}

TEST(SDL3DRenderContextAccessors, NullContextReturnsZeroSizedAuto)
{
    SDL_ClearError();
    EXPECT_EQ(SDL3D_BACKEND_AUTO, sdl3d_get_render_context_backend(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    SDL_ClearError();
    EXPECT_EQ(0, sdl3d_get_render_context_width(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    SDL_ClearError();
    EXPECT_EQ(0, sdl3d_get_render_context_height(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);
}

TEST(SDL3DRenderContextMutators, NullContextIsRejectedSafely)
{
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_clear_render_context(nullptr, sdl3d_color{0, 0, 0, 255}));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_present_render_context(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    sdl3d_destroy_render_context(nullptr);
}

TEST(SDL3DBackendInterface, SoftwareBackendPopulatesAllSlots)
{
    sdl3d_backend_interface iface{};
    sdl3d_sw_backend_init(&iface);

    EXPECT_NE(nullptr, iface.destroy);
    EXPECT_NE(nullptr, iface.clear);
    EXPECT_NE(nullptr, iface.present);
    EXPECT_NE(nullptr, iface.draw_mesh_unlit);
    EXPECT_NE(nullptr, iface.draw_mesh_lit);
}

TEST(SDL3DBackendInterface, GlBackendPopulatesAllSlots)
{
    sdl3d_backend_interface iface{};
    sdl3d_gl_backend_init(&iface);

    EXPECT_NE(nullptr, iface.destroy);
    EXPECT_NE(nullptr, iface.clear);
    EXPECT_NE(nullptr, iface.present);
    EXPECT_NE(nullptr, iface.draw_mesh_unlit);
    EXPECT_NE(nullptr, iface.draw_mesh_lit);
}

TEST(SDL3DBackendInterface, SoftwareDrawMeshUnlitReturnsFalseForFallthrough)
{
    sdl3d_backend_interface iface{};
    sdl3d_sw_backend_init(&iface);

    sdl3d_draw_params_unlit params{};
    /* Software draw returns false to signal "use inline software path". */
    EXPECT_FALSE(iface.draw_mesh_unlit(nullptr, &params));
}

TEST(SDL3DBackendInterface, SoftwareDrawMeshLitReturnsFalseForFallthrough)
{
    sdl3d_backend_interface iface{};
    sdl3d_sw_backend_init(&iface);

    sdl3d_draw_params_lit params{};
    EXPECT_FALSE(iface.draw_mesh_lit(nullptr, &params));
}

/* ------------------------------------------------------------------ */
/* High-level API tests                                                */
/* ------------------------------------------------------------------ */

TEST(SDL3DWindowConfig, DefaultsAreReasonable)
{
    sdl3d_window_config cfg;
    sdl3d_init_window_config(&cfg);
    EXPECT_EQ(cfg.width, 1280);
    EXPECT_EQ(cfg.height, 720);
    EXPECT_EQ(cfg.logical_width, 1280);
    EXPECT_EQ(cfg.logical_height, 720);
    EXPECT_NE(cfg.title, nullptr);
    EXPECT_EQ(cfg.icon_path, nullptr);
    EXPECT_EQ(cfg.backend, SDL3D_BACKEND_AUTO);
    EXPECT_TRUE(cfg.allow_backend_fallback);
    EXPECT_EQ(cfg.display_mode, SDL3D_WINDOW_MODE_WINDOWED);
    EXPECT_TRUE(cfg.vsync);
    EXPECT_FALSE(cfg.maximized);
    EXPECT_TRUE(cfg.resizable);
}

TEST(SDL3DWindowConfig, ApplyWindowConfigRejectsInvalidArguments)
{
    sdl3d_window_config cfg;
    sdl3d_init_window_config(&cfg);

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_apply_window_config(nullptr, nullptr, &cfg));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'window/context/config' is invalid"),
              std::string_view::npos);
}

TEST(SDL3DFeatureQuery, SoftwareHasNoPostProcessing)
{
    /* We can't create a real context in unit tests without a display,
     * but we can test the function with a NULL context. */
    EXPECT_FALSE(sdl3d_is_feature_available(nullptr, SDL3D_FEATURE_BLOOM));
    EXPECT_FALSE(sdl3d_is_feature_available(nullptr, SDL3D_FEATURE_SSAO));
    EXPECT_FALSE(sdl3d_is_feature_available(nullptr, SDL3D_FEATURE_SHADOWS));
}
