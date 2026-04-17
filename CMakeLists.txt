cmake_minimum_required(VERSION 3.21)

project(
    SDL3D
    VERSION 0.1.0
    DESCRIPTION "High-level 3D graphics API powered by SDL3 GPU"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

if(PROJECT_IS_TOP_LEVEL)
    option(SDL3D_BUILD_TESTS "Build SDL3D tests" ${BUILD_TESTING})
else()
    option(SDL3D_BUILD_TESTS "Build SDL3D tests" OFF)
endif()
option(SDL3D_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SDL3DCompilerWarnings)
include(SDL3DSanitizers)

add_library(sdl3d)
add_library(SDL3D::sdl3d ALIAS sdl3d)

target_sources(
    sdl3d
    PRIVATE
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
