add_executable(sdl3d_black_dragon main.c)
target_link_libraries(sdl3d_black_dragon PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_black_dragon PRIVATE c_std_17)
target_compile_definitions(sdl3d_black_dragon PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
set_target_properties(sdl3d_black_dragon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/black_dragon")

if(NOT WIN32)
    target_link_libraries(sdl3d_black_dragon PRIVATE m)
endif()

if(WIN32)
    add_custom_command(TARGET sdl3d_black_dragon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdl3d_black_dragon>
            $<TARGET_FILE_DIR:sdl3d_black_dragon>
        COMMAND_EXPAND_LISTS)
endif()
