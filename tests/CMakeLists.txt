include(FetchContent)
include(GoogleTest)

find_package(GTest CONFIG QUIET)

if(NOT TARGET GTest::gtest)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )

    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

function(sdl3d_add_test target_name source_file test_label)
    add_executable(${target_name} ${source_file})

    target_link_libraries(
        ${target_name}
        PRIVATE
            SDL3D::sdl3d
            GTest::gtest
            GTest::gtest_main
    )

    target_compile_features(${target_name} PRIVATE cxx_std_17)

    sdl3d_enable_sanitizers(${target_name})

    if(EMSCRIPTEN)
        # Produce an .html shell so emrun can host the page and capture
        # stdout + exit code via the Emscripten runtime hooks enabled by
        # the --emrun link flag.
        set_target_properties(${target_name} PROPERTIES SUFFIX ".html")
        target_link_options(${target_name} PRIVATE "--emrun")
    endif()

    if(WIN32)
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${target_name}> $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
        )
    endif()

    gtest_discover_tests(
        ${target_name}
        DISCOVERY_TIMEOUT 10
        DISCOVERY_MODE PRE_TEST
        PROPERTIES
            LABELS ${test_label}
            TIMEOUT 15
    )
endfunction()

if(ANDROID)
    # SDLActivity expects libmain.so with an SDL_main entry point. Combine
    # all GoogleTest translation units into one shared library so a single
    # APK can run the entire suite.
    add_library(
        main SHARED
        android_main.cpp
        test_api.cpp
        test_math.cpp
        test_rasterizer.cpp
        test_render_context_unit.cpp
        test_render_context.cpp
        test_drawing3d.cpp
        test_render_loop.cpp
    )

    target_link_libraries(
        main
        PRIVATE
            SDL3D::sdl3d
            GTest::gtest
            log
            android
    )

    target_compile_features(main PRIVATE cxx_std_17)
    target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_enable_sanitizers(main)
else()
    sdl3d_add_test(sdl3d_api_test test_api.cpp unit)
    sdl3d_add_test(sdl3d_math_test test_math.cpp unit)
    sdl3d_add_test(sdl3d_rasterizer_test test_rasterizer.cpp unit)
    target_include_directories(sdl3d_rasterizer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_add_test(sdl3d_render_context_unit_test test_render_context_unit.cpp unit)
    sdl3d_add_test(sdl3d_render_context_test test_render_context.cpp render)
    sdl3d_add_test(sdl3d_drawing3d_test test_drawing3d.cpp render)
    sdl3d_add_test(sdl3d_render_loop_test test_render_loop.cpp render)
endif()
