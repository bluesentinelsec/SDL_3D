#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_version.h>

extern "C"
{
#include "sdl3d/sdl3d.h"
}

#include <array>
#include <string>
#include <string_view>

namespace
{
struct CapturedLogMessage
{
    int category = -1;
    SDL_LogPriority priority = SDL_LOG_PRIORITY_INVALID;
    std::string message;
};

void SDLCALL CaptureLogOutput(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    auto *capture = static_cast<CapturedLogMessage *>(userdata);
    capture->category = category;
    capture->priority = priority;
    capture->message = message;
}

class SDLLogOutputGuard
{
  public:
    SDLLogOutputGuard()
    {
        SDL_GetLogOutputFunction(&callback_, &userdata_);
    }

    ~SDLLogOutputGuard()
    {
        SDL_SetLogOutputFunction(callback_, userdata_);
    }

  private:
    SDL_LogOutputFunction callback_ = nullptr;
    void *userdata_ = nullptr;
};
} // namespace

TEST(SDL3D, GreetReturnsExpectedMessage)
{
    EXPECT_EQ(std::string_view("Hello from SDL3D."), sdl3d_greet());
}

TEST(SDL3D, CopyGreetingWritesExpectedMessage)
{
    std::array<char, 32> buffer{};

    SDL_ClearError();
    ASSERT_TRUE(sdl3d_copy_greeting(buffer.data(), buffer.size())) << SDL_GetError();
    EXPECT_EQ(std::string_view("Hello from SDL3D."), buffer.data());
    EXPECT_TRUE(std::string_view(SDL_GetError()).empty());
}

TEST(SDL3D, CopyGreetingRejectsNullBuffer)
{
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_copy_greeting(nullptr, 32U));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'buffer' is invalid"), std::string_view::npos);
}

TEST(SDL3D, CopyGreetingRejectsSmallBuffer)
{
    std::array<char, 4> buffer{};

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_copy_greeting(buffer.data(), buffer.size()));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Buffer is too small"), std::string_view::npos);
}

TEST(SDL3D, ReportsLinkedSDLVersion)
{
    EXPECT_GE(sdl3d_linked_sdl_version(), SDL_VERSIONNUM(3, 2, 0));
}

TEST(SDL3D, LoggingUsesDedicatedCategoryAndCallerControlledPriority)
{
    CapturedLogMessage capture;
    SDLLogOutputGuard log_output_guard;

    SDL_SetLogOutputFunction(CaptureLogOutput, &capture);
    sdl3d_set_log_priority(SDL_LOG_PRIORITY_INFO);

    EXPECT_EQ(SDL_LOG_PRIORITY_INFO, sdl3d_get_log_priority());
    ASSERT_TRUE(sdl3d_log_message(SDL_LOG_PRIORITY_INFO, "SDL3D info message"));
    EXPECT_EQ(sdl3d_log_category(), capture.category);
    EXPECT_EQ(SDL_LOG_PRIORITY_INFO, capture.priority);
    EXPECT_EQ(std::string("SDL3D info message"), capture.message);
}

TEST(SDL3D, LoggingRespectsPriorityFiltering)
{
    CapturedLogMessage capture;
    SDLLogOutputGuard log_output_guard;

    SDL_SetLogOutputFunction(CaptureLogOutput, &capture);
    sdl3d_set_log_priority(SDL_LOG_PRIORITY_ERROR);

    ASSERT_TRUE(sdl3d_log_message(SDL_LOG_PRIORITY_INFO, "suppressed"));
    EXPECT_TRUE(capture.message.empty());

    ASSERT_TRUE(sdl3d_log_message(SDL_LOG_PRIORITY_ERROR, "visible"));
    EXPECT_EQ(std::string("visible"), capture.message);
    EXPECT_EQ(SDL_LOG_PRIORITY_ERROR, capture.priority);
}

TEST(SDL3D, LoggingRejectsNullMessage)
{
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_log_message(SDL_LOG_PRIORITY_INFO, nullptr));
    EXPECT_NE(std::string_view(SDL_GetError()).find("Parameter 'message' is invalid"), std::string_view::npos);
}
