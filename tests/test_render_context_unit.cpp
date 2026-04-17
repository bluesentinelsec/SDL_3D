#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

extern "C"
{
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
    config.backend = SDL3D_BACKEND_SDLGPU;
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

TEST(SDL3DBackendName, MapsKnownBackendsToStableStrings)
{
    EXPECT_EQ(std::string_view("auto"), sdl3d_get_backend_name(SDL3D_BACKEND_AUTO));
    EXPECT_EQ(std::string_view("software"), sdl3d_get_backend_name(SDL3D_BACKEND_SOFTWARE));
    EXPECT_EQ(std::string_view("sdlgpu"), sdl3d_get_backend_name(SDL3D_BACKEND_SDLGPU));
}

TEST(SDL3DBackendName, UnknownBackendsMapToUnknown)
{
    const sdl3d_backend bogus = static_cast<sdl3d_backend>(0x4242);
    EXPECT_EQ(std::string_view("unknown"), sdl3d_get_backend_name(bogus));
}

TEST(SDL3DBackendEnvOverride, UnsetEnvReturnsFalseWithoutTouchingOutput)
{
    SDL3DBackendEnvGuard guard(nullptr);
    sdl3d_backend backend = SDL3D_BACKEND_SDLGPU;

    EXPECT_FALSE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SDL3D_BACKEND_SDLGPU, backend);
}

TEST(SDL3DBackendEnvOverride, EmptyEnvReturnsFalseWithoutTouchingOutput)
{
    SDL3DBackendEnvGuard guard("");
    sdl3d_backend backend = SDL3D_BACKEND_SDLGPU;

    EXPECT_FALSE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SDL3D_BACKEND_SDLGPU, backend);
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
    sdl3d_backend backend = SDL3D_BACKEND_SDLGPU;

    EXPECT_TRUE(sdl3d_get_backend_override_from_environment(&backend));
    EXPECT_EQ(SDL3D_BACKEND_AUTO, backend);
}

TEST(SDL3DBackendEnvOverride, ParsesSdlGpuAndGpuAliasCaseInsensitively)
{
    {
        SDL3DBackendEnvGuard guard("SdlGpu");
        sdl3d_backend backend = SDL3D_BACKEND_AUTO;
        EXPECT_TRUE(sdl3d_get_backend_override_from_environment(&backend));
        EXPECT_EQ(SDL3D_BACKEND_SDLGPU, backend);
    }

    {
        SDL3DBackendEnvGuard guard("GPU");
        sdl3d_backend backend = SDL3D_BACKEND_AUTO;
        EXPECT_TRUE(sdl3d_get_backend_override_from_environment(&backend));
        EXPECT_EQ(SDL3D_BACKEND_SDLGPU, backend);
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
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                             reinterpret_cast<SDL_Renderer *>(0x1), nullptr, nullptr));
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
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                             reinterpret_cast<SDL_Renderer *>(0x1), &config, &context));
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
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                             reinterpret_cast<SDL_Renderer *>(0x1), &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Logical width and height must both be zero or both be non-zero"),
              std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, InvalidEnvOverrideFailsBeforeTouchingRenderer)
{
    SDL3DBackendEnvGuard guard("nonsense");
    sdl3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                             reinterpret_cast<SDL_Renderer *>(0x1), nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Unsupported SDL3D backend override"), std::string_view::npos);
}

TEST(SDL3DCreateRenderContext, SdlGpuWithoutFallbackFailsBeforeTouchingRenderer)
{
    SDL3DBackendEnvGuard guard(nullptr);
    sdl3d_render_context *context = nullptr;
    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.backend = SDL3D_BACKEND_SDLGPU;
    config.allow_backend_fallback = false;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(reinterpret_cast<SDL_Window *>(0x1),
                                             reinterpret_cast<SDL_Renderer *>(0x1), &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("not implemented"), std::string_view::npos);
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
