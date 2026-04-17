include(FetchContent)
include(GoogleTest)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(sdl3d_greet_test test_greet.cpp)

target_link_libraries(
    sdl3d_greet_test
    PRIVATE
        SDL3D::sdl3d
        GTest::gtest_main
)

target_compile_features(sdl3d_greet_test PRIVATE cxx_std_17)

sdl3d_enable_sanitizers(sdl3d_greet_test)

gtest_discover_tests(sdl3d_greet_test)
