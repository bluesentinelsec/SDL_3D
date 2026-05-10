#define SDL_MAIN_HANDLED

#include <gtest/gtest.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

extern "C"
{
#include "slayer3d/slayer3d.h"
}

#include <memory>

namespace
{
constexpr int kDoneSignal = 4242;

struct SignalState
{
    int count = 0;
    int last_signal = 0;
};

void count_signal(void *userdata, int signal_id, const slayer3d_properties *payload)
{
    (void)payload;
    auto *state = static_cast<SignalState *>(userdata);
    state->count++;
    state->last_signal = signal_id;
}

class WindowRenderer
{
  public:
    WindowRenderer(int width = 64, int height = 64)
        : window_(nullptr, SDL_DestroyWindow), renderer_(nullptr, SDL_DestroyRenderer)
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        if (!SDL_CreateWindowAndRenderer("SLAYER3D Transition Test", width, height, SDL_WINDOW_HIDDEN, &window,
                                         &renderer))
        {
            ADD_FAILURE() << SDL_GetError();
            return;
        }
        window_.reset(window);
        renderer_.reset(renderer);
    }

    bool ok() const
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

class TransitionRenderTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        SDL_SetMainReady();
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    }

    void TearDown() override
    {
        SDL_Quit();
    }
};

slayer3d_color black()
{
    return slayer3d_color{0, 0, 0, 255};
}

slayer3d_color white()
{
    return slayer3d_color{255, 255, 255, 255};
}
} // namespace

TEST(Transition, StartSetsActive)
{
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_IN, black(), 1.0f, -1);

    EXPECT_EQ(SLAYER3D_TRANSITION_FADE, transition.type);
    EXPECT_EQ(SLAYER3D_TRANSITION_IN, transition.direction);
    EXPECT_FLOAT_EQ(1.0f, transition.duration);
    EXPECT_FLOAT_EQ(0.0f, transition.elapsed);
    EXPECT_TRUE(transition.active);
    EXPECT_FALSE(transition.finished);
    EXPECT_EQ(-1, transition.done_signal_id);
}

TEST(Transition, UpdateAdvancesElapsed)
{
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);

    slayer3d_transition_update(&transition, nullptr, 0.25f);

    EXPECT_FLOAT_EQ(0.25f, transition.elapsed);
    EXPECT_TRUE(transition.active);
}

TEST(Transition, FinishSetsFlags)
{
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);

    slayer3d_transition_update(&transition, nullptr, 2.0f);

    EXPECT_FLOAT_EQ(1.0f, transition.elapsed);
    EXPECT_FALSE(transition.active);
    EXPECT_TRUE(transition.finished);
}

TEST(Transition, FinishEmitsSignalOnce)
{
    slayer3d_transition transition{};
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    ASSERT_NE(nullptr, bus);

    SignalState state;
    ASSERT_NE(0, slayer3d_signal_connect(bus, kDoneSignal, count_signal, &state));
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_OUT, black(), 1.0f,
                              kDoneSignal);

    slayer3d_transition_update(&transition, bus, 1.0f);
    slayer3d_transition_update(&transition, bus, 1.0f);

    EXPECT_EQ(1, state.count);
    EXPECT_EQ(kDoneSignal, state.last_signal);
    slayer3d_signal_bus_destroy(bus);
}

TEST(Transition, NoSignalWhenMinusOne)
{
    slayer3d_transition transition{};
    slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
    ASSERT_NE(nullptr, bus);

    SignalState state;
    ASSERT_NE(0, slayer3d_signal_connect(bus, kDoneSignal, count_signal, &state));
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);

    slayer3d_transition_update(&transition, bus, 2.0f);

    EXPECT_EQ(0, state.count);
    slayer3d_signal_bus_destroy(bus);
}

TEST(Transition, ProgressClamped)
{
    slayer3d_transition transition{};
    EXPECT_FLOAT_EQ(0.0f, slayer3d_transition_progress(nullptr));
    EXPECT_FLOAT_EQ(0.0f, slayer3d_transition_progress(&transition));

    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);
    transition.elapsed = 2.0f;
    EXPECT_FLOAT_EQ(1.0f, slayer3d_transition_progress(&transition));
}

TEST(Transition, ResetClearsState)
{
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_MELT, SLAYER3D_TRANSITION_OUT, black(), 1.0f,
                              kDoneSignal);

    slayer3d_transition_reset(&transition);

    EXPECT_FALSE(transition.active);
    EXPECT_FALSE(transition.finished);
    EXPECT_FLOAT_EQ(0.0f, transition.elapsed);
    EXPECT_FLOAT_EQ(0.0f, transition.duration);
    EXPECT_EQ(-1, transition.done_signal_id);
}

TEST(Transition, InactiveUpdateIsNoOp)
{
    slayer3d_transition transition{};
    slayer3d_transition_update(&transition, nullptr, 1.0f);

    EXPECT_FLOAT_EQ(0.0f, transition.elapsed);
    EXPECT_FALSE(transition.finished);
}

TEST(Transition, MeltOffsetsSeeded)
{
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_MELT, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);

    bool saw_nonzero = false;
    bool saw_distinct = false;
    for (int i = 0; i < SLAYER3D_TRANSITION_MELT_COLUMNS; ++i)
    {
        EXPECT_GE(transition.melt_offsets[i], 0.0f);
        EXPECT_LE(transition.melt_offsets[i], 1.0f);
        saw_nonzero = saw_nonzero || transition.melt_offsets[i] > 0.0f;
        if (i > 0 && transition.melt_offsets[i] != transition.melt_offsets[0])
        {
            saw_distinct = true;
        }
    }

    EXPECT_TRUE(saw_nonzero);
    EXPECT_TRUE(saw_distinct);
}

TEST(Transition, NullSafety)
{
    slayer3d_transition_start(nullptr, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_IN, black(), 1.0f, -1);
    slayer3d_transition_update(nullptr, nullptr, 1.0f);
    slayer3d_transition_draw(nullptr, nullptr);
    slayer3d_transition_reset(nullptr);
    EXPECT_FLOAT_EQ(0.0f, slayer3d_transition_progress(nullptr));
}

TEST_F(TransitionRenderTest, FadeDrawBlendsSoftwareBuffer)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, white()));
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_FADE, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);
    slayer3d_transition_update(&transition, nullptr, 0.5f);
    slayer3d_transition_draw(&transition, ctx);

    slayer3d_color pixel{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(ctx, 32, 32, &pixel));
    EXPECT_NEAR(128, pixel.r, 1);
    EXPECT_NEAR(128, pixel.g, 1);
    EXPECT_NEAR(128, pixel.b, 1);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(TransitionRenderTest, CircleDrawDoesNotCrash)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, white()));
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_CIRCLE, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);
    slayer3d_transition_update(&transition, nullptr, 0.5f);
    slayer3d_transition_draw(&transition, ctx);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(TransitionRenderTest, MeltDrawDoesNotCrash)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, white()));
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_MELT, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);
    slayer3d_transition_update(&transition, nullptr, 0.5f);
    slayer3d_transition_draw(&transition, ctx);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(TransitionRenderTest, PixelateDrawDoesNotCrash)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, white()));
    slayer3d_transition transition{};
    slayer3d_transition_start(&transition, SLAYER3D_TRANSITION_PIXELATE, SLAYER3D_TRANSITION_OUT, black(), 1.0f, -1);
    slayer3d_transition_update(&transition, nullptr, 0.5f);
    slayer3d_transition_draw(&transition, ctx);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(TransitionRenderTest, InactiveDrawIsNoOp)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, white()));
    slayer3d_transition transition{};
    slayer3d_transition_draw(&transition, ctx);

    slayer3d_color pixel{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(ctx, 32, 32, &pixel));
    EXPECT_EQ(255, pixel.r);
    EXPECT_EQ(255, pixel.g);
    EXPECT_EQ(255, pixel.b);

    slayer3d_destroy_render_context(ctx);
}
