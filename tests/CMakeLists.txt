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

sdl3d_add_test(sdl3d_api_test test_api.cpp unit)
sdl3d_add_test(sdl3d_render_loop_test test_render_loop.cpp render)
