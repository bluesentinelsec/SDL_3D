// iOS test entry point.
//
// SDL's iOS bootstrap invokes UIApplicationMain and then calls back into
// SDL_main (our main() below is renamed to SDL_main via SDL_main.h). We
// run GoogleTest from here and emit per-test and sentinel lines via
// os_log to the "com.sdl3d.tests" subsystem so the CI runner can tail
// them through `log stream` / `log show` on the simulator.
//
// gtest writes its pass/fail to stdout; the simulator console can pick
// that up with `simctl launch --console`, but os_log is the reliable
// channel because it survives process exit timing and we can filter by
// subsystem without noise from the rest of iOS.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <os/log.h>

namespace
{
os_log_t g_log()
{
    static os_log_t log = os_log_create("com.sdl3d.tests", "tests");
    return log;
}

class IOSLogListener : public ::testing::EmptyTestEventListener
{
    void OnTestStart(const ::testing::TestInfo &info) override
    {
        os_log_info(g_log(), "SDL3D_TEST RUN  %{public}s.%{public}s", info.test_suite_name(), info.name());
    }

    void OnTestPartResult(const ::testing::TestPartResult &result) override
    {
        if (result.failed())
        {
            os_log_error(g_log(), "SDL3D_TEST FAIL %{public}s:%d %{public}s",
                         result.file_name() ? result.file_name() : "?", result.line_number(), result.summary());
        }
    }

    void OnTestEnd(const ::testing::TestInfo &info) override
    {
        const auto *r = info.result();
        const bool failed = r && r->Failed();
        if (failed)
        {
            os_log_error(g_log(), "SDL3D_TEST FAIL %{public}s.%{public}s", info.test_suite_name(), info.name());
        }
        else
        {
            os_log_info(g_log(), "SDL3D_TEST OK   %{public}s.%{public}s", info.test_suite_name(), info.name());
        }
    }
};
} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new IOSLogListener());
    int rc = RUN_ALL_TESTS();
    os_log_info(g_log(), "SDL3D_TEST_RESULT: %d", rc);
    SDL_Delay(500);
    return rc;
}
