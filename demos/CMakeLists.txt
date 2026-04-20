add_executable(sdl3d_profile_viewer profile_viewer.c)

target_link_libraries(sdl3d_profile_viewer PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_profile_viewer PRIVATE c_std_17)

if(NOT WIN32)
    target_link_libraries(sdl3d_profile_viewer PRIVATE m)
endif()

if(WIN32)
    add_custom_command(
        TARGET sdl3d_profile_viewer
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sdl3d_profile_viewer> $<TARGET_FILE_DIR:sdl3d_profile_viewer>
        COMMAND_EXPAND_LISTS
    )
endif()

add_executable(sdl3d_showcase showcase.c)

target_link_libraries(sdl3d_showcase PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_showcase PRIVATE c_std_17)
target_compile_definitions(sdl3d_showcase PRIVATE SDL3D_DEMO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

if(NOT WIN32)
    target_link_libraries(sdl3d_showcase PRIVATE m)
endif()

if(WIN32)
    add_custom_command(
        TARGET sdl3d_showcase
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sdl3d_showcase> $<TARGET_FILE_DIR:sdl3d_showcase>
        COMMAND_EXPAND_LISTS
    )
endif()

add_executable(sdl3d_showcase_1080p showcase_1080p.c)

target_link_libraries(sdl3d_showcase_1080p PRIVATE SDL3D::sdl3d)
target_compile_features(sdl3d_showcase_1080p PRIVATE c_std_17)
target_compile_definitions(sdl3d_showcase_1080p PRIVATE SDL3D_DEMO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

if(NOT WIN32)
    target_link_libraries(sdl3d_showcase_1080p PRIVATE m)
endif()

if(WIN32)
    add_custom_command(
        TARGET sdl3d_showcase_1080p
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sdl3d_showcase_1080p> $<TARGET_FILE_DIR:sdl3d_showcase_1080p>
        COMMAND_EXPAND_LISTS
    )
endif()
