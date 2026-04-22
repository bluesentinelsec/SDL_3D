add_executable(sdl3d_profile_viewer main.c)
target_link_libraries(sdl3d_profile_viewer PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_profile_viewer PRIVATE c_std_17)
set_target_properties(sdl3d_profile_viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/profile_viewer")

if(NOT WIN32)
    target_link_libraries(sdl3d_profile_viewer PRIVATE m)
endif()

if(WIN32)
    add_custom_command(TARGET sdl3d_profile_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdl3d_profile_viewer>
            $<TARGET_FILE_DIR:sdl3d_profile_viewer>
        COMMAND_EXPAND_LISTS)
endif()
