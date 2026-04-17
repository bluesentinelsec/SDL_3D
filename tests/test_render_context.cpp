#include <gtest/gtest.h>

#include <SDL3/SDL.h>

extern "C"
{
#include "sdl3d/sdl3d.h"
}

#include <memory>
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

struct SDLQuitGuard
{
    ~SDLQuitGuard()
    {
        SDL_Quit();
    }
};

class SDLVideoFixture : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        SDL_ClearError();
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    }

    void TearDown() override
    {
        SDL_Quit();
    }
};

class SDLWindowRendererPair
{
  public:
    SDLWindowRendererPair() : window_(nullptr, SDL_DestroyWindow), renderer_(nullptr, SDL_DestroyRenderer)
    {
        SDL_Window *raw_window = nullptr;
        SDL_Renderer *raw_renderer = nullptr;

        if (!SDL_CreateWindowAndRenderer("SDL3D Context Test", 128, 72, 0, &raw_window, &raw_renderer))
        {
            ADD_FAILURE() << SDL_GetError();
            return;
        }

        window_.reset(raw_window);
        renderer_.reset(raw_renderer);
    }

    bool is_valid() const
    {
        return window_ != nullptr && renderer_ != nullptr;
    }

    SDL_Window *window() const
    {
        return window_.get();
    }

    SDL_Renderer *renderer() const
    {
        return renderer_.get();
    }

  private:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer_;
};

class SDL3DBackendOverrideGuard
{
  public:
    explicit SDL3DBackendOverrideGuard(const char *value)
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

    ~SDL3DBackendOverrideGuard()
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

TEST_F(SDLVideoFixture, DefaultConfigUsesSoftwareBackend)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(pair.window(), pair.renderer(), nullptr, &context)) << SDL_GetError();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(SDL3D_BACKEND_SOFTWARE, sdl3d_get_render_context_backend(context));
    EXPECT_GT(sdl3d_get_render_context_width(context), 0);
    EXPECT_GT(sdl3d_get_render_context_height(context), 0);

    sdl3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, LogicalPresentationConfigIsApplied)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.logical_width = 320;
    config.logical_height = 180;
    config.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(pair.window(), pair.renderer(), &config, &context)) << SDL_GetError();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(320, sdl3d_get_render_context_width(context));
    EXPECT_EQ(180, sdl3d_get_render_context_height(context));

    int logical_width = 0;
    int logical_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    ASSERT_TRUE(SDL_GetRenderLogicalPresentation(pair.renderer(), &logical_width, &logical_height, &mode))
        << SDL_GetError();
    EXPECT_EQ(320, logical_width);
    EXPECT_EQ(180, logical_height);
    EXPECT_EQ(SDL_LOGICAL_PRESENTATION_LETTERBOX, mode);
    ASSERT_TRUE(sdl3d_clear_render_context(context, {16, 32, 64, 255})) << SDL_GetError();
    ASSERT_TRUE(sdl3d_present_render_context(context)) << SDL_GetError();

    sdl3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, SdlGpuWithoutFallbackFails)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.backend = SDL3D_BACKEND_SDLGPU;
    config.allow_backend_fallback = false;

    sdl3d_render_context *context = nullptr;
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(pair.window(), pair.renderer(), &config, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("not implemented"), std::string_view::npos);
}

TEST_F(SDLVideoFixture, EnvironmentOverrideCanForceSoftwareBackend)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    SDL3DBackendOverrideGuard backend_override("software");
    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.backend = SDL3D_BACKEND_SDLGPU;
    config.allow_backend_fallback = false;

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(pair.window(), pair.renderer(), &config, &context)) << SDL_GetError();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(SDL3D_BACKEND_SOFTWARE, sdl3d_get_render_context_backend(context));

    sdl3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, InvalidEnvironmentOverrideFails)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    SDL3DBackendOverrideGuard backend_override("bogus");
    sdl3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_create_render_context(pair.window(), pair.renderer(), nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Unsupported SDL3D backend override"), std::string_view::npos);
}
