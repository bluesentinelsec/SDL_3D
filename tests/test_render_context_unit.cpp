#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

extern "C"
{
#include "backend.h"
#include "render_context_internal.h"
#include "slayer3d/slayer3d.h"
}

#include <string>
#include <string_view>

namespace
{
bool SetBackendOverride(const char *value)
{
    if (value == nullptr)
    {
        return SDL_unsetenv_unsafe("SLAYER3D_BACKEND") == 0;
    }

    return SDL_setenv_unsafe("SLAYER3D_BACKEND", value, 1) == 0;
}

class SLAYER3DBackendEnvGuard
{
  public:
    explicit SLAYER3DBackendEnvGuard(const char *value)
    {
        const char *existing = SDL_getenv("SLAYER3D_BACKEND");
        if (existing != nullptr)
        {
            had_existing_ = true;
            previous_value_ = existing;
        }

        if (!SetBackendOverride(value))
        {
            ADD_FAILURE() << "Failed to set SLAYER3D_BACKEND test override";
        }
    }

    ~SLAYER3DBackendEnvGuard()
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

TEST(SLAYER3DRenderContextConfig, InitRenderContextConfigSetsDocumentedDefaults)
{
    slayer3d_render_context_config config{};
    config.backend = SLAYER3D_BACKEND_OPENGL;
    config.allow_backend_fallback = false;
    config.logical_width = 42;
    config.logical_height = 42;
    config.logical_size_policy = SLAYER3D_LOGICAL_SIZE_EXPAND;
    config.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    slayer3d_init_render_context_config(&config);

    EXPECT_EQ(SLAYER3D_BACKEND_AUTO, config.backend);
    EXPECT_TRUE(config.allow_backend_fallback);
    EXPECT_EQ(0, config.logical_width);
    EXPECT_EQ(0, config.logical_height);
    EXPECT_EQ(SLAYER3D_LOGICAL_SIZE_FIXED, config.logical_size_policy);
    EXPECT_EQ(SDL_LOGICAL_PRESENTATION_LETTERBOX, config.logical_presentation);
}

TEST(SLAYER3DWindowConfig, InitWindowConfigSetsHighDpiDefault)
{
    slayer3d_window_config config{};
    config.high_pixel_density = false;

    slayer3d_init_window_config(&config);

    EXPECT_EQ(config.width, 1280);
    EXPECT_EQ(config.height, 720);
    EXPECT_EQ(config.logical_width, 1280);
    EXPECT_EQ(config.logical_height, 720);
    EXPECT_EQ(SLAYER3D_LOGICAL_SIZE_FIXED, config.logical_size_policy);
    EXPECT_TRUE(config.high_pixel_density);
}

TEST(SLAYER3DWindowConfig, WindowedCreationFlagsPreserveDesktopWindowState)
{
    slayer3d_window_config config{};
    slayer3d_init_window_config(&config);
    config.display_mode = SLAYER3D_WINDOW_MODE_WINDOWED;
    config.maximized = true;
    config.resizable = true;

    const SDL_WindowFlags flags = slayer3d_internal_window_flags(&config, SLAYER3D_BACKEND_SOFTWARE);

    EXPECT_NE(0u, flags & SDL_WINDOW_MAXIMIZED);
    EXPECT_NE(0u, flags & SDL_WINDOW_RESIZABLE);
    EXPECT_EQ(0u, flags & SDL_WINDOW_FULLSCREEN);
}

TEST(SLAYER3DWindowConfig, BorderlessFullscreenIsEstablishedDuringWindowCreation)
{
    slayer3d_window_config config{};
    slayer3d_init_window_config(&config);
    config.display_mode = SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS;
    config.maximized = true;

    const SDL_WindowFlags flags = slayer3d_internal_window_flags(&config, SLAYER3D_BACKEND_OPENGL);

    EXPECT_NE(0u, flags & SDL_WINDOW_FULLSCREEN);
    EXPECT_NE(0u, flags & SDL_WINDOW_OPENGL);
    EXPECT_EQ(0u, flags & SDL_WINDOW_MAXIMIZED);
}

TEST(SLAYER3DWindowConfig, ExclusiveFullscreenDoesNotRequestWindowedMaximization)
{
    slayer3d_window_config config{};
    slayer3d_init_window_config(&config);
    config.display_mode = SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE;
    config.maximized = true;

    const SDL_WindowFlags flags = slayer3d_internal_window_flags(&config, SLAYER3D_BACKEND_SOFTWARE);

    EXPECT_EQ(0u, flags & SDL_WINDOW_FULLSCREEN);
    EXPECT_EQ(0u, flags & SDL_WINDOW_MAXIMIZED);
}

TEST(SLAYER3DLogicalSize, FixedPolicyPreservesAuthoredCanvas)
{
    int width = 0;
    int height = 0;
    ASSERT_TRUE(slayer3d_resolve_logical_size(1280, 720, 1600, 1000, SLAYER3D_LOGICAL_SIZE_FIXED, &width, &height));
    EXPECT_EQ(1280, width);
    EXPECT_EQ(720, height);
}

TEST(SLAYER3DLogicalSize, ExpandPolicyFillsCommonAndTallAspects)
{
    int width = 0;
    int height = 0;

    ASSERT_TRUE(slayer3d_resolve_logical_size(1280, 720, 1920, 1080, SLAYER3D_LOGICAL_SIZE_EXPAND, &width, &height));
    EXPECT_EQ(1280, width);
    EXPECT_EQ(720, height);

    ASSERT_TRUE(slayer3d_resolve_logical_size(1280, 720, 1600, 1000, SLAYER3D_LOGICAL_SIZE_EXPAND, &width, &height));
    EXPECT_EQ(1280, width);
    EXPECT_EQ(800, height);

    ASSERT_TRUE(slayer3d_resolve_logical_size(1280, 720, 3456, 2234, SLAYER3D_LOGICAL_SIZE_EXPAND, &width, &height));
    EXPECT_EQ(1280, width);
    EXPECT_EQ(828, height);
}

TEST(SLAYER3DLogicalSize, ExpandPolicyGrowsWidthForWideOutputs)
{
    int width = 0;
    int height = 0;
    ASSERT_TRUE(slayer3d_resolve_logical_size(1280, 720, 3440, 1440, SLAYER3D_LOGICAL_SIZE_EXPAND, &width, &height));
    EXPECT_EQ(1720, width);
    EXPECT_EQ(720, height);
}

TEST(SLAYER3DLogicalSize, RejectsInvalidDimensionsAndPolicies)
{
    int width = 0;
    int height = 0;
    EXPECT_FALSE(slayer3d_resolve_logical_size(0, 720, 1920, 1080, SLAYER3D_LOGICAL_SIZE_EXPAND, &width, &height));
    EXPECT_FALSE(slayer3d_resolve_logical_size(1280, 720, 1920, 1080, static_cast<slayer3d_logical_size_policy>(99),
                                               &width, &height));
    EXPECT_FALSE(slayer3d_resolve_logical_size(1280, 720, 1920, 1080, SLAYER3D_LOGICAL_SIZE_EXPAND, nullptr, &height));
}

TEST(SLAYER3DLogicalSize, AspectFitViewportPreservesGeometryAfterAdaptiveRounding)
{
    SDL_FRect viewport{};
    ASSERT_TRUE(slayer3d_resolve_aspect_fit_viewport(1280, 828, 3456, 2234, &viewport));

    EXPECT_FLOAT_EQ(0.0f, viewport.y);
    EXPECT_NEAR(1.237f, viewport.x, 0.01f);
    EXPECT_NEAR(3453.53f, viewport.w, 0.01f);
    EXPECT_FLOAT_EQ(2234.0f, viewport.h);
    EXPECT_NEAR(viewport.w / 1280.0f, viewport.h / 828.0f, 0.0001f);
}

TEST(SLAYER3DLogicalSize, AspectFitViewportRejectsInvalidArguments)
{
    SDL_FRect viewport{};
    EXPECT_FALSE(slayer3d_resolve_aspect_fit_viewport(0, 720, 1920, 1080, &viewport));
    EXPECT_FALSE(slayer3d_resolve_aspect_fit_viewport(1280, 720, 1920, 1080, nullptr));
}

TEST(SLAYER3DRenderContextConfig, InitRenderContextConfigRejectsNull)
{
    SDL_ClearError();
    slayer3d_init_render_context_config(nullptr);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'config' is invalid"), std::string_view::npos);
}

TEST(SLAYER3DRenderContextState, NullStateAccessorsAreRejected)
{
    SDL_ClearError();
    EXPECT_FALSE(slayer3d_set_scissor_rect(nullptr, nullptr));
    EXPECT_FALSE(slayer3d_is_scissor_enabled(nullptr));
    EXPECT_FALSE(slayer3d_get_scissor_rect(nullptr, reinterpret_cast<SDL_Rect *>(0x1)));
    EXPECT_FALSE(
        slayer3d_clear_render_context_rect(nullptr, reinterpret_cast<const SDL_Rect *>(0x1), slayer3d_color{}));
}

TEST(SLAYER3DBackendName, MapsKnownBackendsToStableStrings)
{
    EXPECT_EQ(std::string_view("auto"), slayer3d_get_backend_name(SLAYER3D_BACKEND_AUTO));
    EXPECT_EQ(std::string_view("software"), slayer3d_get_backend_name(SLAYER3D_BACKEND_SOFTWARE));
    EXPECT_EQ(std::string_view("opengl"), slayer3d_get_backend_name(SLAYER3D_BACKEND_OPENGL));
}

TEST(SLAYER3DBackendName, UnknownBackendsMapToUnknown)
{
    const slayer3d_backend bogus = static_cast<slayer3d_backend>(0x4242);
    EXPECT_EQ(std::string_view("unknown"), slayer3d_get_backend_name(bogus));
}

TEST(SLAYER3DBackendEnvOverride, UnsetEnvReturnsFalseWithoutTouchingOutput)
{
    SLAYER3DBackendEnvGuard guard(nullptr);
    slayer3d_backend backend = SLAYER3D_BACKEND_OPENGL;

    EXPECT_FALSE(slayer3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SLAYER3D_BACKEND_OPENGL, backend);
}

TEST(SLAYER3DBackendEnvOverride, EmptyEnvReturnsFalseWithoutTouchingOutput)
{
    SLAYER3DBackendEnvGuard guard("");
    slayer3d_backend backend = SLAYER3D_BACKEND_OPENGL;

    EXPECT_FALSE(slayer3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SLAYER3D_BACKEND_OPENGL, backend);
}

TEST(SLAYER3DBackendEnvOverride, ParsesSoftware)
{
    SLAYER3DBackendEnvGuard guard("software");
    slayer3d_backend backend = SLAYER3D_BACKEND_AUTO;

    EXPECT_TRUE(slayer3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SLAYER3D_BACKEND_SOFTWARE, backend);
}

TEST(SLAYER3DBackendEnvOverride, ParsesAuto)
{
    SLAYER3DBackendEnvGuard guard("AUTO");
    slayer3d_backend backend = SLAYER3D_BACKEND_OPENGL;

    EXPECT_TRUE(slayer3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SLAYER3D_BACKEND_AUTO, backend);
}

TEST(SLAYER3DBackendEnvOverride, ParsesOpenGlAndGpuAliasesCaseInsensitively)
{
    {
        SLAYER3DBackendEnvGuard guard("OpenGL");
        slayer3d_backend backend = SLAYER3D_BACKEND_AUTO;
        EXPECT_TRUE(slayer3d_get_backend_override_from_environment(&backend));
        EXPECT_EQ(SLAYER3D_BACKEND_OPENGL, backend);
    }

    {
        SLAYER3DBackendEnvGuard guard("GPU");
        slayer3d_backend backend = SLAYER3D_BACKEND_AUTO;
        EXPECT_TRUE(slayer3d_get_backend_override_from_environment(&backend));
        EXPECT_EQ(SLAYER3D_BACKEND_OPENGL, backend);
    }
}

TEST(SLAYER3DBackendEnvOverride, InvalidValueReturnsFalseAndSetsError)
{
    SLAYER3DBackendEnvGuard guard("not-a-backend");
    slayer3d_backend backend = SLAYER3D_BACKEND_AUTO;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_get_backend_override_from_environment(&backend));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Unsupported SLAYER3D backend override"), std::string_view::npos);
}

TEST(SLAYER3DBackendEnvOverride, NullBackendOutputIsRejected)
{
    SLAYER3DBackendEnvGuard guard("software");

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_get_backend_override_from_environment(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'backend' is invalid"), std::string_view::npos);
}

TEST(SLAYER3DCreateRenderContext, RejectsNullWindow)
{
    slayer3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_create_render_context(nullptr, reinterpret_cast<SDL_Renderer *>(0x1), nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'window' is invalid"), std::string_view::npos);
}

TEST(SLAYER3DCreateRenderContext, RejectsNullRenderer)
{
    slayer3d_render_context *context = nullptr;
    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.backend = SLAYER3D_BACKEND_SOFTWARE;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), nullptr, &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'renderer' is invalid"), std::string_view::npos);
}

TEST(SLAYER3DCreateRenderContext, RejectsNullOutputPointer)
{
    SDL_ClearError();
    EXPECT_FALSE(slayer3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                                reinterpret_cast<SDL_Renderer *>(0x1), nullptr, nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'out_context' is invalid"), std::string_view::npos);
}

TEST(SLAYER3DCreateRenderContext, RejectsNegativeLogicalDimensions)
{
    slayer3d_render_context *context = nullptr;
    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.logical_width = -1;
    config.logical_height = 0;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                                reinterpret_cast<SDL_Renderer *>(0x1), &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Logical dimensions must be zero or positive"),
              std::string_view::npos);
}

TEST(SLAYER3DCreateRenderContext, RejectsMismatchedLogicalDimensions)
{
    slayer3d_render_context *context = nullptr;
    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.logical_width = 320;
    config.logical_height = 0;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                                reinterpret_cast<SDL_Renderer *>(0x1), &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Logical width and height must both be zero or both be non-zero"),
              std::string_view::npos);
}

TEST(SLAYER3DCreateRenderContext, InvalidEnvOverrideFailsBeforeTouchingRenderer)
{
    SLAYER3DBackendEnvGuard guard("nonsense");
    slayer3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                                reinterpret_cast<SDL_Renderer *>(0x1), nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Unsupported SLAYER3D backend override"), std::string_view::npos);
}

TEST(SLAYER3DCreateRenderContext, OpenGLBackendAcceptsRequest)
{
    SLAYER3DBackendEnvGuard guard(nullptr);
    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.backend = SLAYER3D_BACKEND_OPENGL;
    config.allow_backend_fallback = false;

    /* The GL backend is now implemented. Creating with a fake window will
     * fail at GL context creation, but the backend selection itself should
     * not produce a "not implemented" error. */
    slayer3d_render_context *context = nullptr;
    bool ok = slayer3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1), reinterpret_cast<SDL_Renderer *>(0x1),
                                             &config, &context);
    if (!ok)
    {
        std::string_view err = SDL_GetError();
        EXPECT_EQ(err.find("not implemented"), std::string_view::npos) << "Backend should be accepted, got: " << err;
    }
    if (context != nullptr)
    {
        slayer3d_destroy_render_context(context);
    }
}

TEST(SLAYER3DRenderContextAccessors, NullContextReturnsZeroSizedAuto)
{
    SDL_ClearError();
    EXPECT_EQ(SLAYER3D_BACKEND_AUTO, slayer3d_get_render_context_backend(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    SDL_ClearError();
    EXPECT_EQ(0, slayer3d_get_render_context_width(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    SDL_ClearError();
    EXPECT_EQ(0, slayer3d_get_render_context_height(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);
}

TEST(SLAYER3DRenderContextMutators, NullContextIsRejectedSafely)
{
    SDL_ClearError();
    EXPECT_FALSE(slayer3d_clear_render_context(nullptr, slayer3d_color{0, 0, 0, 255}));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_present_render_context(nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'context' is invalid"), std::string_view::npos);

    slayer3d_destroy_render_context(nullptr);
}

TEST(SLAYER3DBackendInterface, SoftwareBackendPopulatesAllSlots)
{
    slayer3d_backend_interface iface{};
    slayer3d_sw_backend_init(&iface);

    EXPECT_NE(nullptr, iface.destroy);
    EXPECT_NE(nullptr, iface.clear);
    EXPECT_NE(nullptr, iface.present);
    EXPECT_NE(nullptr, iface.draw_mesh_unlit);
    EXPECT_NE(nullptr, iface.draw_mesh_lit);
}

TEST(SLAYER3DBackendInterface, GlBackendPopulatesAllSlots)
{
    slayer3d_backend_interface iface{};
    slayer3d_gl_backend_init(&iface);

    EXPECT_NE(nullptr, iface.destroy);
    EXPECT_NE(nullptr, iface.clear);
    EXPECT_NE(nullptr, iface.present);
    EXPECT_NE(nullptr, iface.draw_mesh_unlit);
    EXPECT_NE(nullptr, iface.draw_mesh_lit);
}

TEST(SLAYER3DBackendInterface, SoftwareDrawMeshUnlitReturnsFalseForFallthrough)
{
    slayer3d_backend_interface iface{};
    slayer3d_sw_backend_init(&iface);

    slayer3d_draw_params_unlit params{};
    /* Software draw returns false to signal "use inline software path". */
    EXPECT_FALSE(iface.draw_mesh_unlit(nullptr, &params));
}

TEST(SLAYER3DBackendInterface, SoftwareDrawMeshLitReturnsFalseForFallthrough)
{
    slayer3d_backend_interface iface{};
    slayer3d_sw_backend_init(&iface);

    slayer3d_draw_params_lit params{};
    EXPECT_FALSE(iface.draw_mesh_lit(nullptr, &params));
}

/* ------------------------------------------------------------------ */
/* High-level API tests                                                */
/* ------------------------------------------------------------------ */

TEST(SLAYER3DWindowConfig, DefaultsAreReasonable)
{
    slayer3d_window_config cfg;
    slayer3d_init_window_config(&cfg);
    EXPECT_EQ(cfg.width, 1280);
    EXPECT_EQ(cfg.height, 720);
    EXPECT_EQ(cfg.logical_width, 1280);
    EXPECT_EQ(cfg.logical_height, 720);
    EXPECT_NE(cfg.title, nullptr);
    EXPECT_EQ(cfg.icon_path, nullptr);
    EXPECT_EQ(cfg.backend, SLAYER3D_BACKEND_OPENGL);
    EXPECT_TRUE(cfg.allow_backend_fallback);
    EXPECT_EQ(cfg.display_mode, SLAYER3D_WINDOW_MODE_WINDOWED);
    EXPECT_TRUE(cfg.vsync);
    EXPECT_FALSE(cfg.maximized);
    EXPECT_TRUE(cfg.resizable);
}

TEST(SLAYER3DWindowConfig, ApplyWindowConfigRejectsInvalidArguments)
{
    slayer3d_window_config cfg;
    slayer3d_init_window_config(&cfg);

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_apply_window_config(nullptr, nullptr, &cfg));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'window/context/config' is invalid"),
              std::string_view::npos);
}

TEST(SLAYER3DFeatureQuery, SoftwareHasNoPostProcessing)
{
    /* We can't create a real context in unit tests without a display,
     * but we can test the function with a NULL context. */
    EXPECT_FALSE(slayer3d_is_feature_available(nullptr, SLAYER3D_FEATURE_BLOOM));
    EXPECT_FALSE(slayer3d_is_feature_available(nullptr, SLAYER3D_FEATURE_SSAO));
    EXPECT_FALSE(slayer3d_is_feature_available(nullptr, SLAYER3D_FEATURE_SHADOWS));
}
