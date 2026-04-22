add_executable(sdl3d_showcase_1080p main.c)
target_link_libraries(sdl3d_showcase_1080p PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_showcase_1080p PRIVATE c_std_17)
target_compile_definitions(sdl3d_showcase_1080p PRIVATE
    SDL3D_DEMO_ASSETS_DIR="${CMAKE_SOURCE_DIR}/demos/assets"
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
set_target_properties(sdl3d_showcase_1080p PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/showcase_1080p")

if(NOT WIN32)
    target_link_libraries(sdl3d_showcase_1080p PRIVATE m)
endif()

if(WIN32)
    add_custom_command(TARGET sdl3d_showcase_1080p POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdl3d_showcase_1080p>
            $<TARGET_FILE_DIR:sdl3d_showcase_1080p>
        COMMAND_EXPAND_LISTS)
endif()
