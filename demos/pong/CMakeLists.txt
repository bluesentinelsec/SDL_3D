add_executable(pong_demo
    main.c
    pong_rules.c)
target_link_libraries(pong_demo PRIVATE SDL3D::sdl3d)
target_compile_features(pong_demo PRIVATE c_std_17)
target_compile_definitions(pong_demo PRIVATE
    SDL3D_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
set_target_properties(pong_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demos/pong")

if(NOT WIN32)
    target_link_libraries(pong_demo PRIVATE m)
endif()
