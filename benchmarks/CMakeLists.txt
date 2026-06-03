add_executable(slayer3d_bench bench_render.c)

target_link_libraries(slayer3d_bench PRIVATE Slayer3D::slayer3d)

if(NOT WIN32)
    target_link_libraries(slayer3d_bench PRIVATE m)
endif()
target_compile_features(slayer3d_bench PRIVATE c_std_17)
slayer3d_copy_runtime_dlls(slayer3d_bench)
