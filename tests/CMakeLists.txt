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
    # Build GoogleTest as a static library regardless of BUILD_SHARED_LIBS
    # so we don't have to ship libgtest.so alongside the test binaries
    # (needed for Android APK packaging in particular).
    set(_sdl3d_saved_build_shared_libs ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(googletest)
    set(BUILD_SHARED_LIBS ${_sdl3d_saved_build_shared_libs})
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
    target_compile_definitions(
        ${target_name}
        PRIVATE
            SDL3D_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )

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

set(SDL3D_TEST_SOURCES
    test_api.cpp
    test_math.cpp
    test_rasterizer.cpp
    test_render_context_unit.cpp
    test_render_context.cpp
    test_drawing3d.cpp
    test_render_loop.cpp
    test_shapes.cpp
    test_vertex_color.cpp
    test_texture.cpp
    test_textured_rendering.cpp
    test_lighting.cpp
    test_scene.cpp
    test_collision.cpp
    test_animation.cpp
    test_effects.cpp
    test_level.cpp
)

# Tests that read filesystem fixtures via SDL3D_TEST_ASSETS_DIR are desktop-only.
# The Android APK, iOS .app bundle, and Emscripten virtual FS don't expose the
# host source tree, so we skip these tests on those platforms for now.
set(SDL3D_DESKTOP_ONLY_TEST_SOURCES
    test_image.cpp
    test_obj_loader.cpp
    test_m3_loaders.cpp
)

if(ANDROID)
    # SDLActivity expects libmain.so with an SDL_main entry point. Combine
    # all GoogleTest translation units into one shared library so a single
    # APK can run the entire suite.
    add_library(main SHARED android_main.cpp ${SDL3D_TEST_SOURCES})

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
elseif(IOS)
    # iOS apps must be bundled as .app directories and cannot launch bare
    # executables; as on Android we combine every test TU into a single
    # app bundle whose SDL_main runs the full suite. SDL's iOS bootstrap
    # creates UIApplication + a window and then calls into our SDL_main.
    set(SDL3D_IOS_BUNDLE_ID "com.sdl3d.tests")

    add_executable(sdl3d_ios_tests MACOSX_BUNDLE ios_main.cpp ${SDL3D_TEST_SOURCES})

    target_link_libraries(
        sdl3d_ios_tests
        PRIVATE
            SDL3D::sdl3d
            GTest::gtest
    )

    target_compile_features(sdl3d_ios_tests PRIVATE cxx_std_17)
    target_include_directories(sdl3d_ios_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_enable_sanitizers(sdl3d_ios_tests)

    set_target_properties(
        sdl3d_ios_tests
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${SDL3D_IOS_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_NAME "SDL3D Tests"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${SDL3D_IOS_BUNDLE_ID}"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
else()
    sdl3d_add_test(sdl3d_api_test test_api.cpp unit)
    sdl3d_add_test(sdl3d_app_test test_app.cpp unit)
    sdl3d_add_test(sdl3d_math_test test_math.cpp unit)
    sdl3d_add_test(sdl3d_rasterizer_test test_rasterizer.cpp unit)
    target_include_directories(sdl3d_rasterizer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_add_test(sdl3d_render_context_unit_test test_render_context_unit.cpp unit)
    target_include_directories(sdl3d_render_context_unit_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_add_test(sdl3d_render_context_test test_render_context.cpp render)
    sdl3d_add_test(sdl3d_drawing3d_test test_drawing3d.cpp render)
    sdl3d_add_test(sdl3d_render_loop_test test_render_loop.cpp render)
    sdl3d_add_test(sdl3d_shapes_test test_shapes.cpp render)
    sdl3d_add_test(sdl3d_vertex_color_test test_vertex_color.cpp render)
    sdl3d_add_test(sdl3d_texture_test test_texture.cpp unit)
    sdl3d_add_test(sdl3d_textured_rendering_test test_textured_rendering.cpp render)
    sdl3d_add_test(sdl3d_lighting_test test_lighting.cpp render)
    target_include_directories(sdl3d_lighting_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_add_test(sdl3d_scene_test test_scene.cpp unit)
    sdl3d_add_test(sdl3d_collision_test test_collision.cpp unit)
    sdl3d_add_test(sdl3d_animation_test test_animation.cpp unit)
    sdl3d_add_test(sdl3d_effects_test test_effects.cpp unit)
    sdl3d_add_test(sdl3d_level_test test_level.cpp unit)
    target_include_directories(sdl3d_level_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_add_test(sdl3d_parity_test test_parity.cpp render)

    if(NOT EMSCRIPTEN)
        sdl3d_add_test(sdl3d_image_test test_image.cpp unit)
        sdl3d_add_test(sdl3d_obj_loader_test test_obj_loader.cpp unit)
        sdl3d_add_test(sdl3d_m3_loaders_test test_m3_loaders.cpp unit)
        target_include_directories(sdl3d_m3_loaders_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        sdl3d_add_test(sdl3d_gl_renderer_test test_gl_renderer.cpp render)
        target_include_directories(sdl3d_gl_renderer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    endif()
endif()
