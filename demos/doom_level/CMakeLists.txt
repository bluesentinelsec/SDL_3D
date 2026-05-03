add_executable(doom_level
    main.c
    backend.c
    doom_doors.c
    entities.c
    hazard_effects.c
    level_data.c
    player.c
    renderer.c
    surveillance.c)
target_link_libraries(doom_level PRIVATE SDL3D::sdl3d)
target_compile_features(doom_level PRIVATE c_std_17)
target_compile_definitions(doom_level PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media"
    SDL3D_DOOM_LEVEL_DATA_DIR="${CMAKE_SOURCE_DIR}/demos/doom_level/data")
set_target_properties(doom_level PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/doom_level")

if(NOT WIN32)
    target_link_libraries(doom_level PRIVATE m)
endif()
