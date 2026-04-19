add_executable(sdl3d_bench bench_render.c)

target_link_libraries(sdl3d_bench PRIVATE SDL3D::sdl3d)

if(NOT WIN32)
    target_link_libraries(sdl3d_bench PRIVATE m)
endif()
target_compile_features(sdl3d_bench PRIVATE c_std_17)

if(WIN32)
    add_custom_command(
        TARGET sdl3d_bench
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sdl3d_bench> $<TARGET_FILE_DIR:sdl3d_bench>
        COMMAND_EXPAND_LISTS
    )
endif()
