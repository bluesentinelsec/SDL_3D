add_executable(sdl3d_ui_editor main.c)
target_link_libraries(sdl3d_ui_editor PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_ui_editor PRIVATE c_std_17)
target_compile_definitions(sdl3d_ui_editor PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
set_target_properties(sdl3d_ui_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/ui_editor")

if(NOT WIN32)
    target_link_libraries(sdl3d_ui_editor PRIVATE m)
endif()
