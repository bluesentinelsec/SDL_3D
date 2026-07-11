cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for tooling")
endif()

project(
    Slayer3D
    VERSION 0.1.0
    DESCRIPTION "Data-driven 3D game engine powered by SDL and OpenGL"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
include(FetchContent)
find_package(Threads REQUIRED)

if(PROJECT_IS_TOP_LEVEL)
    option(SLAYER3D_BUILD_TESTS "Build Slayer 3D tests" ${BUILD_TESTING})
else()
    option(SLAYER3D_BUILD_TESTS "Build Slayer 3D tests" OFF)
endif()
option(SLAYER3D_FETCH_SDL3 "Fetch SDL3 if it is not already available" ON)
set(SLAYER3D_SDL_LINKAGE
    "auto"
    CACHE STRING "SDL3 link mode: static, shared, or auto")
set_property(CACHE SLAYER3D_SDL_LINKAGE PROPERTY STRINGS static shared auto)
option(SLAYER3D_ENABLE_NETWORKING "Enable SDL3_net-based network transport" ON)
option(SLAYER3D_COMPRESS_PACKS "Compress Slayer 3D asset packs by default" ON)
set(SLAYER3D_PACK_PASSWORD
    "password"
    CACHE STRING "Password used to obfuscate Slayer 3D asset packs (set to an empty string to disable obfuscation)")
option(SLAYER3D_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(SLAYER3D_BUILD_BENCHMARKS "Build Slayer 3D benchmarks" OFF)
option(SLAYER3D_BUILD_DEMOS "Build Slayer 3D demo programs" OFF)

set(SLAYER3D_USING_FETCHED_SDL3 OFF)
set(SLAYER3D_USING_FETCHED_SDL3_NET OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Slayer3DCompilerWarnings)
include(Slayer3DSanitizers)
include(Slayer3DAssets)

if(NOT SLAYER3D_SDL_LINKAGE STREQUAL "static" AND NOT SLAYER3D_SDL_LINKAGE STREQUAL "shared"
   AND NOT SLAYER3D_SDL_LINKAGE STREQUAL "auto")
    message(FATAL_ERROR "SLAYER3D_SDL_LINKAGE must be 'static', 'shared', or 'auto'.")
endif()

function(slayer3d_copy_runtime_dlls target_name)
    if(WIN32)
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${target_name}>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:${target_name}> $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-static AND NOT TARGET SDL3::SDL3-shared)
    if(SLAYER3D_SDL_LINKAGE STREQUAL "static")
        find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3-static)
    elseif(SLAYER3D_SDL_LINKAGE STREQUAL "shared")
        find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3-shared)
    else()
        find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3)
    endif()
endif()

if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-static AND NOT TARGET SDL3::SDL3-shared AND SLAYER3D_FETCH_SDL3)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.0
        GIT_SHALLOW TRUE
    )

    if(IOS AND SLAYER3D_SDL_LINKAGE STREQUAL "shared")
        message(FATAL_ERROR "iOS app bundles require SLAYER3D_SDL_LINKAGE=static or auto.")
    endif()
    if(SLAYER3D_SDL_LINKAGE STREQUAL "static" OR IOS)
        # iOS app bundles cannot rely on a build-tree SDL dylib at runtime.
        # Prefer static SDL by default so build-tree tools do not require
        # embedding extra runtime artifacts.
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    elseif(SLAYER3D_SDL_LINKAGE STREQUAL "shared")
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    endif()

    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
    set(SLAYER3D_USING_FETCHED_SDL3 ON)
endif()

if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-static AND NOT TARGET SDL3::SDL3-shared)
    message(FATAL_ERROR "SDL3 was not found. Provide SDL3 via a parent target, find_package(SDL3), or enable SLAYER3D_FETCH_SDL3.")
endif()

if(EMSCRIPTEN)
    set(SLAYER3D_ENABLE_NETWORKING OFF CACHE BOOL "Enable SDL3_net-based network transport" FORCE)
endif()

if(SLAYER3D_SDL_LINKAGE STREQUAL "static")
    if(NOT TARGET SDL3::SDL3-static)
        message(FATAL_ERROR "SLAYER3D_SDL_LINKAGE=static requires SDL3::SDL3-static.")
    endif()
    set(SLAYER3D_SDL_TARGET SDL3::SDL3-static)
elseif(SLAYER3D_SDL_LINKAGE STREQUAL "shared")
    if(NOT TARGET SDL3::SDL3-shared)
        message(FATAL_ERROR "SLAYER3D_SDL_LINKAGE=shared requires SDL3::SDL3-shared.")
    endif()
    set(SLAYER3D_SDL_TARGET SDL3::SDL3-shared)
else()
    set(SLAYER3D_SDL_TARGET SDL3::SDL3)
    if(IOS AND TARGET SDL3::SDL3-static)
        set(SLAYER3D_SDL_TARGET SDL3::SDL3-static)
    elseif(NOT TARGET ${SLAYER3D_SDL_TARGET} AND TARGET SDL3::SDL3-static)
        set(SLAYER3D_SDL_TARGET SDL3::SDL3-static)
    elseif(NOT TARGET ${SLAYER3D_SDL_TARGET} AND TARGET SDL3::SDL3-shared)
        set(SLAYER3D_SDL_TARGET SDL3::SDL3-shared)
    endif()
endif()

if(SLAYER3D_ENABLE_NETWORKING)
    if(NOT TARGET SDL3_net::SDL3_net AND NOT TARGET SDL3_net::SDL3_net-static)
        find_package(SDL3_net 3.0 CONFIG QUIET)
    endif()

    if(NOT TARGET SDL3_net::SDL3_net AND NOT TARGET SDL3_net::SDL3_net-static)
        FetchContent_Declare(
            SDL3_net
            GIT_REPOSITORY https://github.com/libsdl-org/SDL_net.git
            GIT_TAG main
            GIT_SHALLOW TRUE
        )

        set(_slayer3d_saved_build_shared_libs ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(SDLNET_INSTALL OFF CACHE BOOL "" FORCE)
        set(SDLNET_SAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3_net)
        set(BUILD_SHARED_LIBS ${_slayer3d_saved_build_shared_libs} CACHE BOOL "" FORCE)
        set(SLAYER3D_USING_FETCHED_SDL3_NET ON)
    endif()

    if(NOT TARGET SDL3_net::SDL3_net AND NOT TARGET SDL3_net::SDL3_net-static)
        message(FATAL_ERROR
            "SDL3_net was not found. Provide SDL3_net via a parent target, find_package(SDL3_net), or disable SLAYER3D_ENABLE_NETWORKING.")
    endif()

    set(SLAYER3D_SDLNET_TARGET SDL3_net::SDL3_net)
    if(NOT TARGET ${SLAYER3D_SDLNET_TARGET} AND TARGET SDL3_net::SDL3_net-static)
        set(SLAYER3D_SDLNET_TARGET SDL3_net::SDL3_net-static)
    endif()

    if(TARGET SDL3_net-static)
        target_link_libraries(SDL3_net-static INTERFACE ${SLAYER3D_SDL_TARGET})
    elseif(TARGET SDL3_net-shared)
        target_link_libraries(SDL3_net-shared INTERFACE ${SLAYER3D_SDL_TARGET})
    endif()
endif()

add_subdirectory(vendor)

add_library(slayer3d STATIC)
add_library(Slayer3D::slayer3d ALIAS slayer3d)

target_sources(
    slayer3d
    PRIVATE
        src/action.c
        src/actor_controller.c
        src/actor_registry.c
        src/app.c
        src/asset.c
        src/animation.c
        src/audio.c
        src/camera.c
        src/collision.c
        src/data_game.c
        src/door.c
        src/drawing3d.c
        src/effects.c
        src/fbx_loader.c
        src/game.c
        src/game_data_brush_artifacts.c
        src/game_data_brush_editor_export.c
        src/game_data_brush_editor_import.c
        src/game_data_brush_editor_mutation.c
        src/game_data_brush_source_enclosure.c
        src/game_data_brush_source_lifecycle.c
        src/game_data_brush_source_model.c
        src/game_data_brush_source_vertices.c
        src/game_data_brush_trace_queries.c
        src/game_data_brush_visibility_runtime.c
        src/game_data_brush.c
        src/game_data.c
        src/game_data_actions.c
        src/game_data_action_lookup.c
        src/game_data_actor_pool_runtime.c
        src/game_data_actors_input.c
        src/game_data_adapter_helpers.c
        src/game_data_combat_weapon_actions.c
        src/game_data_conditions_presentation.c
        src/game_data_controller_runtime.c
        src/game_data_controller_binding_helpers.c
        src/game_data_editor_active_selection.c
        src/game_data_editor_clip_tool.c
        src/game_data_editor_trace.c
        src/game_data_editor_create_box_actions.c
        src/game_data_editor_command_preview.c
        src/game_data_editor_command_transactions.c
        src/game_data_editor_output_actions.c
        src/game_data_editor_placement_preview.c
        src/game_data_editor_actor.c
        src/game_data_editor_player_start.c
        src/game_data_editor_selection_output.c
        src/game_data_editor_selection_helpers.c
        src/game_data_editor_selection_state.c
        src/game_data_editor_visibility.c
        src/game_data_editor_lock.c
        src/game_data_editor_stair.c
        src/game_data_editor_object_state.c
        src/game_data_editor_text_edit.c
        src/game_data_editor_console_ui.c
        src/game_data_editor_screen_selection.c
        src/game_data_editor_vertex_mode.c
        src/game_data_editor_edge_selection.c
        src/game_data_editor_vertex_selection.c
        src/game_data_editor_vertex_shared.c
        src/game_data_editor_selection_queries.c
        src/game_data_editor_debug_primitives.c
        src/game_data_editor_grid_widget.c
        src/game_data_editor_shape_widget.c
        src/game_data_editor_toolbar.c
        src/game_data_grid_actions.c
        src/game_data_grid_runtime.c
        src/game_data_world_model_instances.c
        src/game_data_world_diagnostics.c
        src/game_data_input_device_helpers.c
        src/game_data_interaction_effect_actions.c
        src/game_data_json_helpers.c
        src/game_data_motion_runtime.c
        src/game_data_network_runtime.c
        src/game_data_network_sessions.c
        src/game_data_persistence_audio_actions.c
        src/game_data_property_effect_runtime.c
        src/game_data_render_runtime.c
        src/game_data_replication_helpers.c
        src/game_data_runtime_collections.c
        src/game_data_load_runtime.c
        src/game_data_lua_api.c
        src/game_data_menu_ui.c
        src/game_data_ui_bindings.c
        src/game_data_ui_widgets.c
        src/game_data_scene_flow_runtime.c
        src/game_data_scene_load_runtime.c
        src/game_data_scene_lookup.c
        src/game_data_sensor_runtime.c
        src/game_data_sector_noise_actions.c
        src/game_data_scripts.c
        src/game_data_update_runtime.c
        src/game_data_ui_animation_runtime.c
        src/game_data_world_loaders.c
        src/game_data_world_lighting.c
        src/game_data_world_model_queries.c
        src/game_data_world_navigation.c
        src/game_data_world_queries.c
        src/game_data_standard_options.c
        src/game_presentation.c
        src/game_presentation_asset_warmup.c
        src/game_presentation_app_flow.c
        src/game_presentation_cache.c
        src/game_presentation_brush_worlds.c
        src/game_presentation_cache_lifecycle.c
        src/game_presentation_frame_state.c
        src/game_presentation_menus.c
        src/game_presentation_particles.c
        src/game_presentation_primitives.c
        src/game_presentation_scene_flow.c
        src/game_presentation_sector_levels.c
        src/game_presentation_ui.c
        src/game_data_validation_actions.c
        src/game_data_validation_actions_editor.c
        src/game_data_validation_brush_common.c
        src/game_data_validation_app_world.c
        src/game_data_validation_actors.c
        src/game_data_validation_assets.c
        src/game_data_validation_conditions.c
        src/game_data_validation_controller_components.c
        src/game_data_validation_factions.c
        src/game_data_validation_brush_worlds.c
        src/game_data_validation_components.c
        src/game_data_validation_editor_metadata.c
        src/game_data_validation_grid.c
        src/game_data_validation_haptics.c
        src/game_data_validation_input.c
        src/game_data_validation_imports.c
        src/game_data_validation_logic.c
        src/game_data_validation_network.c
        src/game_data_validation_persistence.c
        src/game_data_validation_render.c
        src/game_data_validation_render_components.c
        src/game_data_validation_scenes.c
        src/game_data_validation_scene_editor.c
        src/game_data_validation_sectors.c
        src/game_data_validation_ui.c
        src/game_data_validation.c
        src/gltf_loader.c
        src/gl_renderer.c
        src/gl_renderer_draw_lists.c
        src/gl_renderer_mesh_cache.c
        src/gl_renderer_postprocess.c
        src/gl_renderer_programs.c
        src/gl_renderer_shaders.c
        src/gl_renderer_targets.c
        src/gl_renderer_texture_cache.c
        src/image.c
        src/input.c
        src/lighting.c
        src/logic.c
        src/map.c
        src/math.c
        src/font.c
        src/fps_mover.c
        src/level.c
        src/model.c
        src/obj_loader.c
        src/network.c
        src/network_replication.c
        src/network_replication_schema.c
        src/properties.c
        src/rasterizer.c
        src/render_context.c
        src/scene.c
        src/script.c
        src/signal_bus.c
        src/trigger.c
        src/sprite_asset.c
        src/sprite_actor.c
        src/slayer3d.c
        src/shading.c
        src/shapes.c
        src/storage.c
        src/sw_backend.c
        src/texture.c
        src/teleporter.c
        src/time.c
        src/timer_pool.c
        src/transition.c
        src/transition_gl.c
        src/ui.c
        src/ui_layout.c
        $<TARGET_OBJECTS:slayer3d_cgltf>
        $<TARGET_OBJECTS:slayer3d_ufbx>
        $<TARGET_OBJECTS:slayer3d_stb>
        $<TARGET_OBJECTS:slayer3d_miniaudio>
        $<TARGET_OBJECTS:slayer3d_yyjson>
        $<TARGET_OBJECTS:slayer3d_miniz>
        $<TARGET_OBJECTS:slayer3d_crypto>
        $<TARGET_OBJECTS:slayer3d_lua>
)

target_compile_features(slayer3d PUBLIC c_std_17)

set_target_properties(
    slayer3d
    PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME slayer3d
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(
    slayer3d
    PRIVATE
        $<TARGET_PROPERTY:${SLAYER3D_SDL_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_cgltf,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_ufbx,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_stb,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_miniaudio,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_yyjson,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_miniz,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_crypto,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:slayer3d_lua,INCLUDE_DIRECTORIES>
)

if(SLAYER3D_ENABLE_NETWORKING)
    target_include_directories(slayer3d PRIVATE $<TARGET_PROPERTY:${SLAYER3D_SDLNET_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
endif()

target_include_directories(slayer3d PRIVATE $<TARGET_PROPERTY:slayer3d_crypto,INCLUDE_DIRECTORIES>)

string(REPLACE "\\" "\\\\" SLAYER3D_PACK_PASSWORD_ESCAPED "${SLAYER3D_PACK_PASSWORD}")
string(REPLACE "\"" "\\\"" SLAYER3D_PACK_PASSWORD_ESCAPED "${SLAYER3D_PACK_PASSWORD_ESCAPED}")
string(REPLACE ";" "\\;" SLAYER3D_PACK_PASSWORD_ESCAPED "${SLAYER3D_PACK_PASSWORD_ESCAPED}")
target_compile_definitions(slayer3d PRIVATE SLAYER3D_PACK_PASSWORD="${SLAYER3D_PACK_PASSWORD_ESCAPED}")

target_compile_definitions(
    slayer3d
    PRIVATE
        $<TARGET_PROPERTY:${SLAYER3D_SDL_TARGET},INTERFACE_COMPILE_DEFINITIONS>
)

target_compile_options(
    slayer3d
    PRIVATE
        $<TARGET_PROPERTY:${SLAYER3D_SDL_TARGET},INTERFACE_COMPILE_OPTIONS>
)

if(SLAYER3D_ENABLE_NETWORKING)
    target_compile_definitions(slayer3d PRIVATE SLAYER3D_NETWORKING_ENABLED=1)
else()
    target_compile_definitions(slayer3d PRIVATE SLAYER3D_NETWORKING_ENABLED=0)
endif()
if(SLAYER3D_COMPRESS_PACKS)
    target_compile_definitions(slayer3d PUBLIC SLAYER3D_PACK_COMPRESSION_ENABLED=1)
else()
    target_compile_definitions(slayer3d PUBLIC SLAYER3D_PACK_COMPRESSION_ENABLED=0)
endif()
target_compile_definitions(slayer3d PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

set_property(TARGET slayer3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${SLAYER3D_SDL_TARGET})
if(TARGET Threads::Threads)
    set_property(TARGET slayer3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads)
endif()
if(CMAKE_DL_LIBS)
    set_property(TARGET slayer3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${CMAKE_DL_LIBS})
endif()
if(SLAYER3D_ENABLE_NETWORKING)
    set_property(TARGET slayer3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${SLAYER3D_SDLNET_TARGET})
endif()
if(UNIX AND NOT EMSCRIPTEN)
    set_property(TARGET slayer3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES m)
endif()
if(APPLE)
    set_property(
        TARGET slayer3d
        APPEND
        PROPERTY INTERFACE_LINK_LIBRARIES "-framework AudioToolbox" "-framework AVFoundation" "-framework CoreAudio"
                 "-framework CoreFoundation"
    )
endif()

target_include_directories(
    slayer3d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

slayer3d_enable_warnings(slayer3d)
slayer3d_enable_sanitizers(slayer3d)

if(MSVC)
    target_compile_options(slayer3d PRIVATE /permissive-)
endif()

if(NOT CMAKE_CROSSCOMPILING AND NOT EMSCRIPTEN)
    add_executable(slayer3d_pack tools/slayer3d_pack.c tools/slayer3d_pack_cli.c)
    target_link_libraries(slayer3d_pack PRIVATE Slayer3D::slayer3d slayer3d_argtable3)
    target_compile_features(slayer3d_pack PRIVATE c_std_17)
    target_include_directories(slayer3d_pack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    slayer3d_enable_warnings(slayer3d_pack)
    slayer3d_enable_sanitizers(slayer3d_pack)
    slayer3d_copy_runtime_dlls(slayer3d_pack)

    add_executable(slayer3d_bundle tools/slayer3d_bundle.c tools/slayer3d_bundle_cli.c tools/slayer3d_fused.c)
    target_link_libraries(slayer3d_bundle PRIVATE Slayer3D::slayer3d slayer3d_argtable3)
    target_compile_features(slayer3d_bundle PRIVATE c_std_17)
    target_include_directories(slayer3d_bundle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    slayer3d_enable_warnings(slayer3d_bundle)
    slayer3d_enable_sanitizers(slayer3d_bundle)
    slayer3d_copy_runtime_dlls(slayer3d_bundle)

    add_executable(
        slayer3d_runner
        tools/slayer3d_runner.c
        tools/slayer3d_runner_cli.c
        tools/slayer3d_runner_manifest.c
        tools/slayer3d_runner_state.c
        tools/slayer3d_fused.c
    )
    target_link_libraries(slayer3d_runner PRIVATE Slayer3D::slayer3d slayer3d_argtable3)
    target_compile_features(slayer3d_runner PRIVATE c_std_17)
    target_include_directories(slayer3d_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_compile_definitions(slayer3d_runner PRIVATE SLAYER3D_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
    slayer3d_enable_warnings(slayer3d_runner)
    slayer3d_enable_sanitizers(slayer3d_runner)
    slayer3d_copy_runtime_dlls(slayer3d_runner)

    add_executable(
        slayer3d_editor
        tools/slayer3d_editor.c
        tools/slayer3d_editor_cli.c
        tools/slayer3d_runner.c
        tools/slayer3d_runner_cli.c
        tools/slayer3d_runner_manifest.c
        tools/slayer3d_runner_state.c
        tools/slayer3d_fused.c
    )
    target_link_libraries(slayer3d_editor PRIVATE Slayer3D::slayer3d slayer3d_argtable3)
    target_compile_features(slayer3d_editor PRIVATE c_std_17)
    target_include_directories(
        slayer3d_editor
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tools
    )
    target_compile_definitions(
        slayer3d_editor
        PRIVATE
            SLAYER3D_EDITOR_DEFAULT_PROJECT="${CMAKE_CURRENT_SOURCE_DIR}/apps/slayer3d_editor"
            SLAYER3D_RUNNER_NO_MAIN
    )
    slayer3d_enable_warnings(slayer3d_editor)
    slayer3d_enable_sanitizers(slayer3d_editor)
    slayer3d_copy_runtime_dlls(slayer3d_editor)
endif()

if(EMSCRIPTEN)
    # Browser build of the editor. Embedded asset packs require the native
    # slayer3d_pack tool, so the web editor instead preloads the canonical
    # editor project and the built-in media it references into the Emscripten
    # virtual filesystem and resolves the default project from there.
    add_executable(
        slayer3d_editor_web
        tools/slayer3d_editor.c
        tools/slayer3d_editor_cli.c
        tools/slayer3d_runner.c
        tools/slayer3d_runner_cli.c
        tools/slayer3d_runner_manifest.c
        tools/slayer3d_runner_state.c
        tools/slayer3d_fused.c
    )
    target_link_libraries(slayer3d_editor_web PRIVATE Slayer3D::slayer3d slayer3d_argtable3)
    target_compile_features(slayer3d_editor_web PRIVATE c_std_17)
    target_include_directories(slayer3d_editor_web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_compile_definitions(
        slayer3d_editor_web
        PRIVATE
            SLAYER3D_MEDIA_DIR="/media"
            SLAYER3D_EDITOR_DEFAULT_PROJECT="/apps/slayer3d_editor"
            SLAYER3D_RUNNER_NO_MAIN
    )
    set_target_properties(slayer3d_editor_web PROPERTIES SUFFIX ".html")
    target_link_options(
        slayer3d_editor_web
        PRIVATE
            # The GL renderer targets OpenGL ES 3.0, which requires a WebGL2
            # context; without these Emscripten defaults to WebGL1 and GL
            # context creation falls back to the software renderer.
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"
            "-sALLOW_MEMORY_GROWTH=1"
            # Data-action validation recurses through the authored editor
            # shell, so keep the wasm stack explicit like the game-data test
            # target does to avoid stack exhaustion before startup completes.
            "-sSTACK_SIZE=8388608"
            # The bundled editor textures include 8K sources that decode to
            # ~170MB of RGBA each, so give the heap room to grow.
            "-sINITIAL_MEMORY=268435456"
            "-sMAXIMUM_MEMORY=2147483648"
    )
    slayer3d_target_preload_asset_directory(slayer3d_editor_web "${CMAKE_CURRENT_SOURCE_DIR}/apps/slayer3d_editor"
                                            "/apps/slayer3d_editor")
    slayer3d_target_preload_asset_directory(slayer3d_editor_web "${CMAKE_CURRENT_SOURCE_DIR}/media/audio"
                                            "/media/audio")
    slayer3d_target_preload_asset_directory(slayer3d_editor_web "${CMAKE_CURRENT_SOURCE_DIR}/media/fonts"
                                            "/media/fonts")
    slayer3d_target_preload_asset_directory(slayer3d_editor_web "${CMAKE_CURRENT_SOURCE_DIR}/media/sprites"
                                            "/media/sprites")
    slayer3d_target_preload_asset_directory(slayer3d_editor_web "${CMAKE_CURRENT_SOURCE_DIR}/media/skyboxes"
                                            "/media/skyboxes")
    # The editor scans every session media child source (textures, models,
    # liquids, effects, plus the sprites/skyboxes above), so preload them all
    # for native/web testing parity. Empty directories pack no bytes but are
    # only mounted when they contain files, so guard each one.
    foreach(media_source_dir textures models liquids effects)
        file(GLOB_RECURSE media_source_files "${CMAKE_CURRENT_SOURCE_DIR}/media/${media_source_dir}/*")
        if(media_source_files)
            slayer3d_target_preload_asset_directory(slayer3d_editor_web
                                                    "${CMAKE_CURRENT_SOURCE_DIR}/media/${media_source_dir}"
                                                    "/media/${media_source_dir}")
        endif()
    endforeach()
    slayer3d_enable_warnings(slayer3d_editor_web)
endif()

if(SLAYER3D_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(SLAYER3D_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(SLAYER3D_BUILD_DEMOS)
    add_subdirectory(demos)
endif()

if(SLAYER3D_USING_FETCHED_SDL3)
    message(STATUS "SDL3 was fetched for this build; Slayer 3D install/export packaging is disabled.")
elseif(SLAYER3D_USING_FETCHED_SDL3_NET)
    message(STATUS "SDL3_net was fetched for this build; Slayer 3D install/export packaging is disabled.")
else()
    configure_package_config_file(
        cmake/Slayer3DConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/Slayer3DConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Slayer3D"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/Slayer3DConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        TARGETS slayer3d
        EXPORT Slayer3DTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    if(TARGET slayer3d_pack AND TARGET slayer3d_bundle AND TARGET slayer3d_runner AND TARGET slayer3d_editor)
        install(
            TARGETS slayer3d_pack slayer3d_bundle slayer3d_runner slayer3d_editor
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        if(TARGET slayer3d_slayermap_example)
            install(
                TARGETS slayer3d_slayermap_example
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
            )
        endif()

        install(
            DIRECTORY demos/slayermap_example/
            DESTINATION "${CMAKE_INSTALL_DATADIR}/slayer3d/demos/slayermap_example"
        )

        install(
            DIRECTORY media/
            DESTINATION "${CMAKE_INSTALL_DATADIR}/slayer3d/media"
        )
    endif()

    install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        EXPORT Slayer3DTargets
        FILE Slayer3DTargets.cmake
        NAMESPACE Slayer3D::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Slayer3D"
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/Slayer3DConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/Slayer3DConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Slayer3D"
    )
endif()
