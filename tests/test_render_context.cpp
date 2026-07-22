#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

extern "C"
{
#include "slayer3d/slayer3d.h"
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
        return SDL_unsetenv_unsafe("SLAYER3D_BACKEND") == 0;
    }

    return SDL_setenv_unsafe("SLAYER3D_BACKEND", value, 1) == 0;
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
        SDL_SetMainReady();
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

        if (!SDL_CreateWindowAndRenderer("SLAYER3D Context Test", 128, 72, 0, &raw_window, &raw_renderer))
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

class SLAYER3DBackendOverrideGuard
{
  public:
    explicit SLAYER3DBackendOverrideGuard(const char *value)
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

    ~SLAYER3DBackendOverrideGuard()
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

    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(pair.window(), pair.renderer(), nullptr, &context)) << SDL_GetError();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(SLAYER3D_BACKEND_SOFTWARE, slayer3d_get_render_context_backend(context));
    EXPECT_GT(slayer3d_get_render_context_width(context), 0);
    EXPECT_GT(slayer3d_get_render_context_height(context), 0);

    slayer3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, LogicalPresentationConfigIsApplied)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.logical_width = 320;
    config.logical_height = 180;
    config.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(pair.window(), pair.renderer(), &config, &context)) << SDL_GetError();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(320, slayer3d_get_render_context_width(context));
    EXPECT_EQ(180, slayer3d_get_render_context_height(context));

    int logical_width = 0;
    int logical_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    ASSERT_TRUE(SDL_GetRenderLogicalPresentation(pair.renderer(), &logical_width, &logical_height, &mode))
        << SDL_GetError();
    EXPECT_EQ(320, logical_width);
    EXPECT_EQ(180, logical_height);
    EXPECT_EQ(SDL_LOGICAL_PRESENTATION_LETTERBOX, mode);
    ASSERT_TRUE(slayer3d_clear_render_context(context, {16, 32, 64, 255})) << SDL_GetError();
    ASSERT_TRUE(slayer3d_present_render_context(context)) << SDL_GetError();

    slayer3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, HighLevelSoftwareWindowLetterboxesLogicalPresentation)
{
    SLAYER3DBackendOverrideGuard backend_override("software");
    slayer3d_window_config config;
    slayer3d_init_window_config(&config);
    config.width = 640;
    config.height = 480;
    config.logical_width = 320;
    config.logical_height = 180;
    config.backend = SLAYER3D_BACKEND_SOFTWARE;
    config.allow_backend_fallback = false;
    config.vsync = false;

    SDL_Window *window = nullptr;
    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_window(&config, &window, &context)) << SDL_GetError();
    ASSERT_NE(window, nullptr);
    ASSERT_NE(context, nullptr);
    SDL_Renderer *renderer = SDL_GetRenderer(window);
    ASSERT_NE(renderer, nullptr);

    int logical_width = 0;
    int logical_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    ASSERT_TRUE(SDL_GetRenderLogicalPresentation(renderer, &logical_width, &logical_height, &mode)) << SDL_GetError();
    EXPECT_EQ(320, logical_width);
    EXPECT_EQ(180, logical_height);
    EXPECT_EQ(SDL_LOGICAL_PRESENTATION_LETTERBOX, mode);

    slayer3d_destroy_window(window, context);
}

TEST_F(SDLVideoFixture, HighLevelSoftwareWindowExpandsLogicalCanvasToOutputAspect)
{
    SLAYER3DBackendOverrideGuard backend_override("software");
    slayer3d_window_config config;
    slayer3d_init_window_config(&config);
    config.width = 640;
    config.height = 480;
    config.logical_width = 320;
    config.logical_height = 180;
    config.logical_size_policy = SLAYER3D_LOGICAL_SIZE_EXPAND;
    config.backend = SLAYER3D_BACKEND_SOFTWARE;
    config.allow_backend_fallback = false;
    config.vsync = false;

    SDL_Window *window = nullptr;
    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_window(&config, &window, &context)) << SDL_GetError();
    ASSERT_NE(window, nullptr);
    ASSERT_NE(context, nullptr);

    int output_width = 0;
    int output_height = 0;
    ASSERT_TRUE(SDL_GetWindowSizeInPixels(window, &output_width, &output_height)) << SDL_GetError();
    int expected_width = 0;
    int expected_height = 0;
    ASSERT_TRUE(slayer3d_resolve_logical_size(320, 180, output_width, output_height, SLAYER3D_LOGICAL_SIZE_EXPAND,
                                              &expected_width, &expected_height));
    EXPECT_EQ(expected_width, slayer3d_get_render_context_width(context));
    EXPECT_EQ(expected_height, slayer3d_get_render_context_height(context));
    EXPECT_EQ(SLAYER3D_LOGICAL_SIZE_EXPAND, slayer3d_get_render_context_logical_size_policy(context));

    SDL_FRect safe_area{};
    ASSERT_TRUE(slayer3d_get_render_context_safe_area(context, &safe_area)) << SDL_GetError();
    EXPECT_GE(safe_area.x, 0.0f);
    EXPECT_GE(safe_area.y, 0.0f);
    EXPECT_GT(safe_area.w, 0.0f);
    EXPECT_GT(safe_area.h, 0.0f);
    EXPECT_LE(safe_area.x + safe_area.w, static_cast<float>(expected_width));
    EXPECT_LE(safe_area.y + safe_area.h, static_cast<float>(expected_height));

    SDL_Renderer *renderer = SDL_GetRenderer(window);
    ASSERT_NE(renderer, nullptr);
    int logical_width = 0;
    int logical_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    ASSERT_TRUE(SDL_GetRenderLogicalPresentation(renderer, &logical_width, &logical_height, &mode)) << SDL_GetError();
    EXPECT_EQ(expected_width, logical_width);
    EXPECT_EQ(expected_height, logical_height);
    EXPECT_EQ(SDL_LOGICAL_PRESENTATION_STRETCH, mode);
    ASSERT_TRUE(slayer3d_clear_render_context(context, {16, 32, 64, 255})) << SDL_GetError();
    ASSERT_TRUE(slayer3d_present_render_context(context)) << SDL_GetError();

    slayer3d_destroy_window(window, context);
}

TEST_F(SDLVideoFixture, AdaptiveSoftwareCanvasTracksWindowResizes)
{
    SLAYER3DBackendOverrideGuard backend_override("software");
    slayer3d_window_config config;
    slayer3d_init_window_config(&config);
    config.width = 640;
    config.height = 480;
    config.logical_width = 320;
    config.logical_height = 180;
    config.logical_size_policy = SLAYER3D_LOGICAL_SIZE_EXPAND;
    config.backend = SLAYER3D_BACKEND_SOFTWARE;
    config.allow_backend_fallback = false;
    config.vsync = false;

    SDL_Window *window = nullptr;
    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_window(&config, &window, &context)) << SDL_GetError();
    ASSERT_TRUE(SDL_SetWindowSize(window, 800, 400)) << SDL_GetError();
    ASSERT_TRUE(SDL_SyncWindow(window)) << SDL_GetError();

    int output_width = 0;
    int output_height = 0;
    ASSERT_TRUE(SDL_GetWindowSizeInPixels(window, &output_width, &output_height)) << SDL_GetError();
    int expected_width = 0;
    int expected_height = 0;
    ASSERT_TRUE(slayer3d_resolve_logical_size(320, 180, output_width, output_height, SLAYER3D_LOGICAL_SIZE_EXPAND,
                                              &expected_width, &expected_height));
    ASSERT_TRUE(slayer3d_sync_render_context_logical_size(context)) << SDL_GetError();
    EXPECT_EQ(expected_width, slayer3d_get_render_context_width(context));
    EXPECT_EQ(expected_height, slayer3d_get_render_context_height(context));
    ASSERT_TRUE(slayer3d_clear_render_context(context, {16, 32, 64, 255})) << SDL_GetError();
    ASSERT_TRUE(slayer3d_present_render_context(context)) << SDL_GetError();

    slayer3d_destroy_window(window, context);
}

TEST_F(SDLVideoFixture, HighLevelSoftwareWindowAppliesVSyncAtRuntime)
{
    SLAYER3DBackendOverrideGuard backend_override("software");
    slayer3d_window_config config;
    slayer3d_init_window_config(&config);
    config.backend = SLAYER3D_BACKEND_SOFTWARE;
    config.allow_backend_fallback = false;
    config.vsync = false;

    SDL_Window *window = nullptr;
    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_window(&config, &window, &context)) << SDL_GetError();
    SDL_Renderer *renderer = SDL_GetRenderer(window);
    ASSERT_NE(renderer, nullptr);

    int vsync = -1;
    ASSERT_TRUE(SDL_GetRenderVSync(renderer, &vsync)) << SDL_GetError();
    EXPECT_EQ(SDL_RENDERER_VSYNC_DISABLED, vsync);

    config.vsync = true;
    ASSERT_TRUE(slayer3d_apply_window_config(&window, &context, &config)) << SDL_GetError();
    renderer = SDL_GetRenderer(window);
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(SDL_GetRenderVSync(renderer, &vsync)) << SDL_GetError();
    EXPECT_EQ(1, vsync);

    config.vsync = false;
    ASSERT_TRUE(slayer3d_apply_window_config(&window, &context, &config)) << SDL_GetError();
    renderer = SDL_GetRenderer(window);
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(SDL_GetRenderVSync(renderer, &vsync)) << SDL_GetError();
    EXPECT_EQ(SDL_RENDERER_VSYNC_DISABLED, vsync);

    slayer3d_destroy_window(window, context);
}

TEST_F(SDLVideoFixture, OpenGLBackendAcceptsRequest)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.backend = SLAYER3D_BACKEND_OPENGL;
    config.allow_backend_fallback = false;

    slayer3d_render_context *context = nullptr;
    bool ok = slayer3d_create_render_context(pair.window(), pair.renderer(), &config, &context);
    /* GL context creation may fail in headless CI, but should not say "not implemented". */
    if (!ok)
    {
        std::string_view err = SDL_GetError();
        EXPECT_EQ(err.find("not implemented"), std::string_view::npos);
    }
    if (context != nullptr)
    {
        slayer3d_destroy_render_context(context);
    }
}

TEST_F(SDLVideoFixture, EnvironmentOverrideCanForceSoftwareBackend)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    SLAYER3DBackendOverrideGuard backend_override("software");
    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.backend = SLAYER3D_BACKEND_OPENGL;
    config.allow_backend_fallback = false;

    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(pair.window(), pair.renderer(), &config, &context)) << SDL_GetError();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(SLAYER3D_BACKEND_SOFTWARE, slayer3d_get_render_context_backend(context));

    slayer3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, InvalidEnvironmentOverrideFails)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid()) << SDL_GetError();

    SLAYER3DBackendOverrideGuard backend_override("bogus");
    slayer3d_render_context *context = nullptr;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_create_render_context(pair.window(), pair.renderer(), nullptr, &context));
    EXPECT_EQ(nullptr, context);
    EXPECT_NE(std::string_view(SDL_GetError()).find("Unsupported SLAYER3D backend override"), std::string_view::npos);
}

/* ------------------------------------------------------------------ */
/* Backend parity tests                                                */
/* ------------------------------------------------------------------ */

TEST_F(SDLVideoFixture, SoftwareBackendClearAndPresentSucceed)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid());

    SLAYER3DBackendOverrideGuard guard("software");
    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(pair.window(), pair.renderer(), nullptr, &context)) << SDL_GetError();
    ASSERT_NE(nullptr, context);

    slayer3d_color black = {0, 0, 0, 255};
    EXPECT_TRUE(slayer3d_clear_render_context(context, black));
    EXPECT_TRUE(slayer3d_present_render_context(context));

    slayer3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, SoftwareBackendVtableIsFullyPopulated)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid());

    SLAYER3DBackendOverrideGuard guard("software");
    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(pair.window(), pair.renderer(), nullptr, &context)) << SDL_GetError();
    ASSERT_NE(nullptr, context);

    EXPECT_EQ(SLAYER3D_BACKEND_SOFTWARE, slayer3d_get_render_context_backend(context));

    slayer3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, DynamicWorldRenderScaleAdjustsConservatively)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid());

    SLAYER3DBackendOverrideGuard guard("software");
    slayer3d_render_context *context = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(pair.window(), pair.renderer(), nullptr, &context)) << SDL_GetError();
    ASSERT_NE(nullptr, context);

    ASSERT_TRUE(slayer3d_set_world_render_scale(context, 1.0f)) << SDL_GetError();
    ASSERT_TRUE(slayer3d_configure_dynamic_world_render_scale(context, true, 0.5f, 1.0f, 60.0f)) << SDL_GetError();
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(slayer3d_update_dynamic_world_render_scale(context, 25.0f)) << SDL_GetError();
    }
    EXPECT_LT(slayer3d_get_world_render_scale(context), 1.0f);
    const float reduced_scale = slayer3d_get_world_render_scale(context);

    for (int i = 0; i < 120; ++i)
    {
        ASSERT_TRUE(slayer3d_update_dynamic_world_render_scale(context, 10.0f)) << SDL_GetError();
    }
    EXPECT_GT(slayer3d_get_world_render_scale(context), reduced_scale);

    slayer3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, OpenGLBackendClearAndPresentSucceedWhenAvailable)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid());

    slayer3d_render_context_config config;
    slayer3d_init_render_context_config(&config);
    config.backend = SLAYER3D_BACKEND_OPENGL;
    config.allow_backend_fallback = false;

    slayer3d_render_context *context = nullptr;
    bool ok = slayer3d_create_render_context(pair.window(), pair.renderer(), &config, &context);
    if (!ok)
    {
        /* GL not available in this environment — skip. */
        GTEST_SKIP() << "GL backend not available: " << SDL_GetError();
    }
    ASSERT_NE(nullptr, context);

    slayer3d_color red = {255, 0, 0, 255};
    EXPECT_TRUE(slayer3d_clear_render_context(context, red));
    EXPECT_TRUE(slayer3d_present_render_context(context));

    EXPECT_EQ(SLAYER3D_BACKEND_OPENGL, slayer3d_get_render_context_backend(context));

    slayer3d_destroy_render_context(context);
}

TEST_F(SDLVideoFixture, BothBackendsReportCorrectDimensions)
{
    SDLWindowRendererPair pair;
    ASSERT_TRUE(pair.is_valid());

    /* Software backend. */
    {
        SLAYER3DBackendOverrideGuard guard("software");
        slayer3d_render_context *ctx = nullptr;
        ASSERT_TRUE(slayer3d_create_render_context(pair.window(), pair.renderer(), nullptr, &ctx)) << SDL_GetError();
        EXPECT_GT(slayer3d_get_render_context_width(ctx), 0);
        EXPECT_GT(slayer3d_get_render_context_height(ctx), 0);
        slayer3d_destroy_render_context(ctx);
    }
}
