add_executable(gui_demo main.c)
target_link_libraries(gui_demo PRIVATE SDL3D::sdl3d)
target_compile_features(gui_demo PRIVATE c_std_17)
target_compile_definitions(gui_demo PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
set_target_properties(gui_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/gui_demo")

if(NOT WIN32)
    target_link_libraries(gui_demo PRIVATE m)
endif()
