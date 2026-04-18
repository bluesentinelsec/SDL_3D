# Vendored third-party libraries, all built from source and linked statically
# into SDL3D. No additional runtime dependencies beyond SDL3 itself.
#
# Each library is compiled as an OBJECT library so that its translation units
# land inside libsdl3d and callers never need to link against it separately.

add_library(sdl3d_cgltf OBJECT cgltf/cgltf_impl.c)
target_include_directories(sdl3d_cgltf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
set_target_properties(sdl3d_cgltf PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sdl3d_cgltf PRIVATE -w)
elseif(MSVC)
    target_compile_options(sdl3d_cgltf PRIVATE /w)
endif()

add_library(sdl3d_ufbx OBJECT ufbx/ufbx.c)
target_include_directories(sdl3d_ufbx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ufbx)
set_target_properties(sdl3d_ufbx PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sdl3d_ufbx PRIVATE -w)
elseif(MSVC)
    target_compile_options(sdl3d_ufbx PRIVATE /w)
endif()
