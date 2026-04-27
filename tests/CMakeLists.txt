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
            SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media"
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

    set(_sdl3d_test_timeout 15)
    if(test_label STREQUAL "render")
        # GPU/windowed tests are reliable locally but can stall briefly on
        # hosted macOS runners when creating hidden GL contexts.
        set(_sdl3d_test_timeout 30)
    endif()

    gtest_discover_tests(
        ${target_name}
        DISCOVERY_TIMEOUT 10
        DISCOVERY_MODE PRE_TEST
        PROPERTIES
            LABELS ${test_label}
            TIMEOUT ${_sdl3d_test_timeout}
    )
endfunction()

set(SDL3D_TEST_SOURCES
    test_api.cpp
    test_actor_controller.cpp
    test_math.cpp
    test_rasterizer.cpp
    test_render_context_unit.cpp
    test_render_context.cpp
    test_drawing3d.cpp
    test_door.cpp
    test_render_loop.cpp
    test_shapes.cpp
    test_vertex_color.cpp
    test_texture.cpp
    test_textured_rendering.cpp
    test_lighting.cpp
    test_logic.cpp
    test_scene.cpp
    test_collision.cpp
    test_action.cpp
    test_actor_registry.cpp
    test_animation.cpp
    test_audio.cpp
    test_effects.cpp
    test_game.cpp
    test_game_session.cpp
    test_input.cpp
    test_transition.cpp
    test_level.cpp
    test_properties.cpp
    test_signal_bus.cpp
    test_trigger.cpp
    test_timer_pool.cpp
    test_fps_mover.cpp
    test_sprite_actor.cpp
    test_teleporter.cpp
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
    sdl3d_add_test(sdl3d_door_test test_door.cpp unit)
    sdl3d_add_test(sdl3d_render_loop_test test_render_loop.cpp render)
    sdl3d_add_test(sdl3d_shapes_test test_shapes.cpp render)
    sdl3d_add_test(sdl3d_vertex_color_test test_vertex_color.cpp render)
    sdl3d_add_test(sdl3d_texture_test test_texture.cpp unit)
    sdl3d_add_test(sdl3d_textured_rendering_test test_textured_rendering.cpp render)
    sdl3d_add_test(sdl3d_lighting_test test_lighting.cpp render)
    target_include_directories(sdl3d_lighting_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_add_test(sdl3d_logic_test test_logic.cpp unit)
    sdl3d_add_test(sdl3d_scene_test test_scene.cpp unit)
    sdl3d_add_test(sdl3d_collision_test test_collision.cpp unit)
    sdl3d_add_test(sdl3d_action_test test_action.cpp unit)
    sdl3d_add_test(sdl3d_actor_controller_test test_actor_controller.cpp unit)
    sdl3d_add_test(sdl3d_actor_registry_test test_actor_registry.cpp unit)
    sdl3d_add_test(sdl3d_animation_test test_animation.cpp unit)
    sdl3d_add_test(sdl3d_audio_test test_audio.cpp unit)
    sdl3d_add_test(sdl3d_effects_test test_effects.cpp unit)
    sdl3d_add_test(sdl3d_game_test test_game.cpp render)
    sdl3d_add_test(sdl3d_game_session_test test_game_session.cpp unit)
    sdl3d_add_test(sdl3d_game_data_test test_game_data.cpp unit)
    if(EMSCRIPTEN)
        target_compile_definitions(
            sdl3d_game_data_test
            PRIVATE
                SDL3D_PONG_DATA_PATH="/pong/pong.game.json"
                SDL3D_GAME_DATA_FIXTURE_DIR="/game_data"
        )
        sdl3d_target_preload_asset_directory(sdl3d_game_data_test "${CMAKE_SOURCE_DIR}/demos/pong/data" "/pong")
        sdl3d_target_preload_asset_directory(sdl3d_game_data_test "${CMAKE_SOURCE_DIR}/tests/assets/game_data"
                                             "/game_data")
    else()
        target_compile_definitions(
            sdl3d_game_data_test
            PRIVATE
                SDL3D_PONG_DATA_PATH="${CMAKE_SOURCE_DIR}/demos/pong/data/pong.game.json"
                SDL3D_GAME_DATA_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/assets/game_data"
        )
    endif()
    sdl3d_add_test(sdl3d_input_test test_input.cpp unit)
    sdl3d_add_test(sdl3d_transition_test test_transition.cpp render)
    sdl3d_add_test(sdl3d_level_test test_level.cpp unit)
    target_include_directories(sdl3d_level_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    sdl3d_add_test(sdl3d_properties_test test_properties.cpp unit)
    sdl3d_add_test(sdl3d_signal_bus_test test_signal_bus.cpp unit)
    sdl3d_add_test(sdl3d_trigger_test test_trigger.cpp unit)
    sdl3d_add_test(sdl3d_timer_pool_test test_timer_pool.cpp unit)
    sdl3d_add_test(sdl3d_fps_mover_test test_fps_mover.cpp unit)
    sdl3d_add_test(doom_backend_test test_doom_backend.cpp unit)
    target_sources(doom_backend_test PRIVATE ${CMAKE_SOURCE_DIR}/demos/doom_level/backend.c)
    target_include_directories(doom_backend_test PRIVATE ${CMAKE_SOURCE_DIR}/demos/doom_level)
    sdl3d_add_test(doom_surveillance_test test_doom_surveillance.cpp unit)
    target_sources(doom_surveillance_test PRIVATE ${CMAKE_SOURCE_DIR}/demos/doom_level/surveillance.c)
    target_include_directories(doom_surveillance_test PRIVATE ${CMAKE_SOURCE_DIR}/demos/doom_level)
    sdl3d_add_test(game_data_json_test test_game_data_json.cpp unit)
    if(EMSCRIPTEN)
        target_compile_definitions(game_data_json_test PRIVATE SDL3D_PONG_DATA_PATH="/pong/pong.game.json")
        sdl3d_target_preload_asset_directory(game_data_json_test "${CMAKE_SOURCE_DIR}/demos/pong/data" "/pong")
    else()
        target_compile_definitions(
            game_data_json_test
            PRIVATE
                SDL3D_PONG_DATA_PATH="${CMAKE_SOURCE_DIR}/demos/pong/data/pong.game.json"
        )
    endif()
    target_include_directories(game_data_json_test PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
    sdl3d_add_test(sdl3d_sprite_actor_test test_sprite_actor.cpp unit)
    sdl3d_add_test(sdl3d_teleporter_test test_teleporter.cpp unit)
    sdl3d_add_test(sdl3d_ui_test test_ui.cpp unit)
    sdl3d_add_test(sdl3d_parity_test test_parity.cpp render)

    if(NOT EMSCRIPTEN)
        sdl3d_add_test(sdl3d_asset_test test_asset.cpp unit)
        sdl3d_add_test(sdl3d_image_test test_image.cpp unit)
        sdl3d_add_test(sdl3d_font_test test_font.cpp unit)
        sdl3d_add_test(sdl3d_time_test test_time.cpp unit)
        sdl3d_add_test(sdl3d_obj_loader_test test_obj_loader.cpp unit)
        sdl3d_add_test(sdl3d_m3_loaders_test test_m3_loaders.cpp unit)
        target_include_directories(sdl3d_m3_loaders_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        sdl3d_add_test(sdl3d_gl_renderer_test test_gl_renderer.cpp render)
        target_include_directories(sdl3d_gl_renderer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    endif()
endif()
