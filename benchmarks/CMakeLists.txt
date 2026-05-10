add_executable(slayer3d_bench bench_render.c)

target_link_libraries(slayer3d_bench PRIVATE Slayer3D::slayer3d)

if(NOT WIN32)
    target_link_libraries(slayer3d_bench PRIVATE m)
endif()
target_compile_features(slayer3d_bench PRIVATE c_std_17)

if(WIN32)
    add_custom_command(
        TARGET slayer3d_bench
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:slayer3d_bench> $<TARGET_FILE_DIR:slayer3d_bench>
        COMMAND_EXPAND_LISTS
    )
endif()
