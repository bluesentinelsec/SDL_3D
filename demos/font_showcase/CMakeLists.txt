add_executable(sdl3d_font_showcase main.c)
target_link_libraries(sdl3d_font_showcase PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_font_showcase PRIVATE c_std_17)
target_compile_definitions(sdl3d_font_showcase PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
set_target_properties(sdl3d_font_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/font_showcase")

if(NOT WIN32)
    target_link_libraries(sdl3d_font_showcase PRIVATE m)
endif()
