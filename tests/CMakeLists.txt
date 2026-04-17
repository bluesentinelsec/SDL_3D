add_executable(test_greet test_greet.c)

target_link_libraries(test_greet PRIVATE SDL3D::sdl3d)

add_test(NAME sdl3d.test_greet COMMAND test_greet)
