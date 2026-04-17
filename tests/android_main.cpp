// Android test entry point.
//
// SDLActivity's native bridge invokes SDL_main after the Java side has
// created a surface, so we run GoogleTest from here and emit a sentinel
// line via __android_log_print that the CI runner tails from logcat to
// determine pass/fail.
//
// gtest writes its pass/fail output to stdout via printf. On Android
// stdout is not wired to logcat, so we install a TestEventListener that
// mirrors per-test results into the SDL3D_TEST log tag — otherwise a CI
// failure would only surface as "exit code 1" with no indication of
// which test failed.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/log.h>

namespace
{
constexpr const char *kLogTag = "SDL3D_TEST";

class AndroidLogListener : public ::testing::EmptyTestEventListener
{
    void OnTestStart(const ::testing::TestInfo &info) override
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "RUN  %s.%s",
                            info.test_suite_name(), info.name());
    }

    void OnTestPartResult(const ::testing::TestPartResult &result) override
    {
        if (result.failed())
        {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "FAIL %s:%d %s",
                                result.file_name() ? result.file_name() : "?",
                                result.line_number(), result.summary());
        }
    }

    void OnTestEnd(const ::testing::TestInfo &info) override
    {
        const auto *r = info.result();
        __android_log_print(r && r->Failed() ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, kLogTag,
                            "%s %s.%s",
                            (r && r->Failed()) ? "FAIL" : "OK  ",
                            info.test_suite_name(), info.name());
    }
};
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new AndroidLogListener());
    int rc = RUN_ALL_TESTS();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL3D_TEST_RESULT: %d", rc);
    SDL_Delay(500);
    return rc;
}
