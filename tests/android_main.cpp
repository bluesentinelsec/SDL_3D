// Android test entry point.
//
// SDLActivity's native bridge invokes SDL_main after the Java side has
// created a surface, so we run GoogleTest from here and emit a sentinel
// line via __android_log_print that the CI runner tails from logcat to
// determine pass/fail.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/log.h>

namespace
{
constexpr const char *kLogTag = "SDL3D_TEST";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int rc = RUN_ALL_TESTS();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL3D_TEST_RESULT: %d", rc);
    SDL_Delay(500);
    return rc;
}
