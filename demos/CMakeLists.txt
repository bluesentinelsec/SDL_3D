add_subdirectory(gui_demo)

add_executable(slayer3d_slayermap_example slayermap_example/main.c)
target_link_libraries(slayer3d_slayermap_example PRIVATE Slayer3D::slayer3d)
target_compile_features(slayer3d_slayermap_example PRIVATE c_std_17)
if(EMSCRIPTEN)
    target_compile_definitions(
        slayer3d_slayermap_example
        PRIVATE
            SLAYER3D_SLAYERMAP_EXAMPLE_MAP="/slayermap_example/maps/training_room.slayermap.json"
    )
    slayer3d_target_preload_asset_directory(
        slayer3d_slayermap_example
        "${CMAKE_CURRENT_SOURCE_DIR}/slayermap_example"
        "/slayermap_example"
    )
else()
    target_compile_definitions(
        slayer3d_slayermap_example
        PRIVATE
            SLAYER3D_SLAYERMAP_EXAMPLE_MAP="${CMAKE_CURRENT_SOURCE_DIR}/slayermap_example/maps/training_room.slayermap.json"
    )
endif()
set_target_properties(
    slayer3d_slayermap_example
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/slayermap_example"
)

slayer3d_enable_warnings(slayer3d_slayermap_example)
slayer3d_enable_sanitizers(slayer3d_slayermap_example)
slayer3d_copy_runtime_dlls(slayer3d_slayermap_example)
