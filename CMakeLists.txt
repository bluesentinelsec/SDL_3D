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

if(PROJECT_IS_TOP_LEVEL)
    option(SDL3D_BUILD_TESTS "Build SDL3D tests" ${BUILD_TESTING})
else()
    option(SDL3D_BUILD_TESTS "Build SDL3D tests" OFF)
endif()
option(SDL3D_FETCH_SDL3 "Fetch SDL3 if it is not already available" ON)
option(SDL3D_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

set(SDL3D_USING_FETCHED_SDL3 OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SDL3DCompilerWarnings)
include(SDL3DSanitizers)

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

    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
    set(SDL3D_USING_FETCHED_SDL3 ON)
endif()

if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "SDL3::SDL3 was not found. Provide SDL3 via a parent target, find_package(SDL3), or enable SDL3D_FETCH_SDL3.")
endif()

add_library(sdl3d STATIC)
add_library(SDL3D::sdl3d ALIAS sdl3d)

target_sources(
    sdl3d
    PRIVATE
        src/camera.c
        src/drawing3d.c
        src/math.c
        src/rasterizer.c
        src/render_context.c
        src/sdl3d.c
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
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_definitions(
    sdl3d
    PRIVATE
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_COMPILE_DEFINITIONS>
)

target_compile_options(
    sdl3d
    PRIVATE
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_COMPILE_OPTIONS>
)

set_property(TARGET sdl3d APPEND PROPERTY INTERFACE_LINK_LIBRARIES SDL3::SDL3)

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

if(SDL3D_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(SDL3D_USING_FETCHED_SDL3)
    message(STATUS "SDL3 was fetched for this build; SDL3D install/export packaging is disabled.")
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
