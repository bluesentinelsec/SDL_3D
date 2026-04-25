add_executable(doom_level main.c)
target_link_libraries(doom_level PRIVATE SDL3D::sdl3d)
target_compile_features(doom_level PRIVATE c_std_17)
target_compile_definitions(doom_level PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
set_target_properties(doom_level PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/doom_level")

if(NOT WIN32)
    target_link_libraries(doom_level PRIVATE m)
endif()
