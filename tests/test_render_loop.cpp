#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <memory>
#include <string_view>

namespace
{
struct SDLQuitGuard
{
    ~SDLQuitGuard()
    {
        SDL_Quit();
    }
};

struct DeterministicLoopConfig
{
    int max_frames;
    Uint64 timeout_ms;
};

using WindowPtr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using RendererPtr = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;

::testing::AssertionResult RunDeterministicRenderLoop(SDL_Renderer *renderer, const DeterministicLoopConfig &config)
{
    if (config.max_frames <= 0)
    {
        return ::testing::AssertionFailure() << "max_frames must be positive";
    }

    const Uint64 start_ticks = SDL_GetTicks();

    for (int frame = 0; frame < config.max_frames; ++frame)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                return ::testing::AssertionFailure() << "unexpected SDL_EVENT_QUIT during render loop";
            }
        }

        if ((SDL_GetTicks() - start_ticks) > config.timeout_ms)
        {
            return ::testing::AssertionFailure() << "render loop exceeded timeout after " << frame << " frames";
        }

        const Uint8 red = static_cast<Uint8>((frame * 53) % 255);
        const Uint8 green = static_cast<Uint8>((frame * 97) % 255);
        const Uint8 blue = static_cast<Uint8>((frame * 193) % 255);
        const SDL_FPoint point = {static_cast<float>(frame * 11), static_cast<float>(frame * 7)};

        if (!SDL_SetRenderDrawColor(renderer, red, green, blue, 255U))
        {
            return ::testing::AssertionFailure() << "SDL_SetRenderDrawColor failed: " << SDL_GetError();
        }

        if (!SDL_RenderClear(renderer))
        {
            return ::testing::AssertionFailure() << "SDL_RenderClear failed: " << SDL_GetError();
        }

        if (!SDL_RenderPoint(renderer, point.x, point.y))
        {
            return ::testing::AssertionFailure() << "SDL_RenderPoint failed: " << SDL_GetError();
        }

        if (!SDL_RenderPresent(renderer))
        {
            return ::testing::AssertionFailure() << "SDL_RenderPresent failed: " << SDL_GetError();
        }
    }

    return ::testing::AssertionSuccess();
}
} // namespace

TEST(SDL3DRenderLoop, MinimalRendererLoopCompletesDeterministically)
{
    SDL_ClearError();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        const std::string_view error = SDL_GetError();
        if (error.find("did not add any displays") != std::string_view::npos)
        {
            GTEST_SKIP() << error;
        }

        FAIL() << error;
    }
    SDLQuitGuard quit_guard;

    SDL_Window *raw_window = nullptr;
    SDL_Renderer *raw_renderer = nullptr;
    ASSERT_TRUE(SDL_CreateWindowAndRenderer("SDL3D Render Loop Test", 96, 96, 0, &raw_window, &raw_renderer))
        << SDL_GetError();

    WindowPtr window(raw_window, SDL_DestroyWindow);
    RendererPtr renderer(raw_renderer, SDL_DestroyRenderer);

    ASSERT_NE(SDL_GetRendererName(renderer.get()), nullptr);
    ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer.get(), 96, 96, SDL_LOGICAL_PRESENTATION_STRETCH))
        << SDL_GetError();

    int logical_width = 0;
    int logical_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    ASSERT_TRUE(SDL_GetRenderLogicalPresentation(renderer.get(), &logical_width, &logical_height, &mode))
        << SDL_GetError();
    EXPECT_EQ(96, logical_width);
    EXPECT_EQ(96, logical_height);
    EXPECT_EQ(SDL_LOGICAL_PRESENTATION_STRETCH, mode);

    EXPECT_TRUE(RunDeterministicRenderLoop(renderer.get(), {.max_frames = 3, .timeout_ms = 2000U}));
}
