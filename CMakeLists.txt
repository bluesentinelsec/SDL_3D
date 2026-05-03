cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for tooling")
endif()

project(
    SDL3D
    VERSION 0.1.0
    DESCRIPTION "High-level 3D graphics API powered by SDL3 GPU"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
include(FetchContent)
find_package(Threads REQUIRED)

if(PROJECT_IS_TOP_LEVEL)
    option(SDL3D_BUILD_TESTS "Build SDL3D tests" ${BUILD_TESTING})
else()
option(SDL3D_BUILD_TESTS "Build SDL3D tests" OFF)
endif()
option(SDL3D_FETCH_SDL3 "Fetch SDL3 if it is not already available" ON)
option(SDL3D_ENABLE_NETWORKING "Enable SDL3_net-based network transport" ON)
option(SDL3D_COMPRESS_PACKS "Compress SDL3D asset packs by default" ON)
set(SDL3D_PACK_PASSWORD
    "password"
    CACHE STRING "Password used to obfuscate SDL3D asset packs (set to an empty string to disable obfuscation)")
option(SDL3D_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(SDL3D_BUILD_BENCHMARKS "Build SDL3D benchmarks" OFF)
option(SDL3D_BUILD_DEMOS "Build SDL3D demo programs" OFF)

set(SDL3D_USING_FETCHED_SDL3 OFF)
set(SDL3D_USING_FETCHED_SDL3_NET OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SDL3DCompilerWarnings)
include(SDL3DSanitizers)
include(SDL3DAssets)

if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3)
endif()

if(NOT TARGET SDL3::SDL3 AND SDL3D_FETCH_SDL3)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.0
        GIT_SHALLOW TRUE
    )

    if(IOS)
        # iOS app bundles cannot rely on a build-tree SDL dylib at runtime.
        # Force a static SDL so the simulator/device app can launch without
        # embedding extra runtime artifacts into the .app.
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()

    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
    set(SDL3D_USING_FETCHED_SDL3 ON)
endif()

if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-static)
    message(FATAL_ERROR "SDL3 was not found. Provide SDL3 via a parent target, find_package(SDL3), or enable SDL3D_FETCH_SDL3.")
endif()

if(EMSCRIPTEN)
    set(SDL3D_ENABLE_NETWORKING OFF CACHE BOOL "Enable SDL3_net-based network transport" FORCE)
endif()

set(SDL3D_SDL_TARGET SDL3::SDL3)
if(NOT TARGET ${SDL3D_SDL_TARGET} AND TARGET SDL3::SDL3-static)
    set(SDL3D_SDL_TARGET SDL3::SDL3-static)
elseif(IOS AND TARGET SDL3::SDL3-static)
    set(SDL3D_SDL_TARGET SDL3::SDL3-static)
endif()

if(SDL3D_ENABLE_NETWORKING)
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

        set(_sdl3d_saved_build_shared_libs ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(SDLNET_INSTALL OFF CACHE BOOL "" FORCE)
        set(SDLNET_SAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3_net)
        set(BUILD_SHARED_LIBS ${_sdl3d_saved_build_shared_libs} CACHE BOOL "" FORCE)
        set(SDL3D_USING_FETCHED_SDL3_NET ON)
    endif()

    if(NOT TARGET SDL3_net::SDL3_net AND NOT TARGET SDL3_net::SDL3_net-static)
        message(FATAL_ERROR
            "SDL3_net was not found. Provide SDL3_net via a parent target, find_package(SDL3_net), or disable SDL3D_ENABLE_NETWORKING.")
    endif()

    set(SDL3D_SDLNET_TARGET SDL3_net::SDL3_net)
    if(NOT TARGET ${SDL3D_SDLNET_TARGET} AND TARGET SDL3_net::SDL3_net-static)
        set(SDL3D_SDLNET_TARGET SDL3_net::SDL3_net-static)
    endif()

    if(TARGET SDL3_net-static)
        target_link_libraries(SDL3_net-static INTERFACE ${SDL3D_SDL_TARGET})
    elseif(TARGET SDL3_net-shared)
        target_link_libraries(SDL3_net-shared INTERFACE ${SDL3D_SDL_TARGET})
    endif()
endif()

add_subdirectory(vendor)

add_library(sdl3d STATIC)
add_library(SDL3D::sdl3d ALIAS sdl3d)

target_sources(
    sdl3d
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
        src/door.c
        src/drawing3d.c
        src/effects.c
        src/fbx_loader.c
        src/game.c
        src/game_data.c
        src/game_data_standard_options.c
        src/game_presentation.c
        src/game_data_validation.c
        src/gltf_loader.c
        src/gl_renderer.c
        src/image.c
        src/input.c
        src/lighting.c
        src/logic.c
        src/math.c
        src/font.c
        src/fps_mover.c
        src/level.c
        src/model.c
        src/obj_loader.c
        src/network.c
        src/properties.c
        src/rasterizer.c
        src/render_context.c
        src/scene.c
        src/script.c
        src/signal_bus.c
        src/trigger.c
        src/sprite_actor.c
        src/sdl3d.c
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
        $<TARGET_OBJECTS:sdl3d_cgltf>
        $<TARGET_OBJECTS:sdl3d_ufbx>
        $<TARGET_OBJECTS:sdl3d_stb>
        $<TARGET_OBJECTS:sdl3d_miniaudio>
        $<TARGET_OBJECTS:sdl3d_yyjson>
        $<TARGET_OBJECTS:sdl3d_miniz>
        $<TARGET_OBJECTS:sdl3d_crypto>
        $<TARGET_OBJECTS:sdl3d_lua>
)

target_compile_features(sdl3d PUBLIC c_std_17)

set_target_properties(
    sdl3d
    PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME sdl3d
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(
    sdl3d
    PRIVATE
        $<TARGET_PROPERTY:${SDL3D_SDL_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_cgltf,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_ufbx,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_stb,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_miniaudio,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_yyjson,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_miniz,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_crypto,INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:sdl3d_lua,INCLUDE_DIRECTORIES>
)

if(SDL3D_ENABLE_NETWORKING)
    target_include_directories(sdl3d PRIVATE $<TARGET_PROPERTY:${SDL3D_SDLNET_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
endif()

target_include_directories(sdl3d PRIVATE $<TARGET_PROPERTY:sdl3d_crypto,INCLUDE_DIRECTORIES>)

string(REPLACE "\\" "\\\\" SDL3D_PACK_PASSWORD_ESCAPED "${SDL3D_PACK_PASSWORD}")
string(REPLACE "\"" "\\\"" SDL3D_PACK_PASSWORD_ESCAPED "${SDL3D_PACK_PASSWORD_ESCAPED}")
string(REPLACE ";" "\\;" SDL3D_PACK_PASSWORD_ESCAPED "${SDL3D_PACK_PASSWORD_ESCAPED}")
target_compile_definitions(sdl3d PRIVATE SDL3D_PACK_PASSWORD="${SDL3D_PACK_PASSWORD_ESCAPED}")

target_compile_definitions(
    sdl3d
    PRIVATE
        $<TARGET_PROPERTY:${SDL3D_SDL_TARGET},INTERFACE_COMPILE_DEFINITIONS>
)

target_compile_options(
    sdl3d
    PRIVATE
        $<TARGET_PROPERTY:${SDL3D_SDL_TARGET},INTERFACE_COMPILE_OPTIONS>
)

if(SDL3D_ENABLE_NETWORKING)
    target_compile_definitions(sdl3d PRIVATE SDL3D_NETWORKING_ENABLED=1)
else()
    target_compile_definitions(sdl3d PRIVATE SDL3D_NETWORKING_ENABLED=0)
endif()
if(SDL3D_COMPRESS_PACKS)
    target_compile_definitions(sdl3d PUBLIC SDL3D_PACK_COMPRESSION_ENABLED=1)
else()
    target_compile_definitions(sdl3d PUBLIC SDL3D_PACK_COMPRESSION_ENABLED=0)
endif()
target_compile_definitions(sdl3d PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

set_property(TARGET sdl3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${SDL3D_SDL_TARGET})
if(TARGET Threads::Threads)
    set_property(TARGET sdl3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads)
endif()
if(CMAKE_DL_LIBS)
    set_property(TARGET sdl3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${CMAKE_DL_LIBS})
endif()
if(SDL3D_ENABLE_NETWORKING)
    set_property(TARGET sdl3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${SDL3D_SDLNET_TARGET})
endif()
if(UNIX AND NOT EMSCRIPTEN)
    set_property(TARGET sdl3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES m)
endif()
if(APPLE)
    set_property(
        TARGET sdl3d
        APPEND
        PROPERTY INTERFACE_LINK_LIBRARIES "-framework AudioToolbox" "-framework AVFoundation" "-framework CoreAudio"
                 "-framework CoreFoundation"
    )
endif()

target_include_directories(
    sdl3d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

sdl3d_enable_warnings(sdl3d)
sdl3d_enable_sanitizers(sdl3d)

if(MSVC)
    target_compile_options(sdl3d PRIVATE /permissive-)
endif()

if(NOT CMAKE_CROSSCOMPILING AND NOT EMSCRIPTEN)
    add_executable(sdl3d_pack tools/sdl3d_pack.c)
    target_link_libraries(sdl3d_pack PRIVATE SDL3D::sdl3d)
    target_compile_features(sdl3d_pack PRIVATE c_std_17)
    sdl3d_enable_warnings(sdl3d_pack)
    sdl3d_enable_sanitizers(sdl3d_pack)
endif()

if(SDL3D_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(SDL3D_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(SDL3D_BUILD_DEMOS)
    add_subdirectory(demos)
endif()

if(SDL3D_USING_FETCHED_SDL3)
    message(STATUS "SDL3 was fetched for this build; SDL3D install/export packaging is disabled.")
elseif(SDL3D_USING_FETCHED_SDL3_NET)
    message(STATUS "SDL3_net was fetched for this build; SDL3D install/export packaging is disabled.")
else()
    configure_package_config_file(
        cmake/SDL3DConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/SDL3DConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SDL3D"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/SDL3DConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        TARGETS sdl3d
        EXPORT SDL3DTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        EXPORT SDL3DTargets
        FILE SDL3DTargets.cmake
        NAMESPACE SDL3D::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SDL3D"
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/SDL3DConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/SDL3DConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SDL3D"
    )
endif()
