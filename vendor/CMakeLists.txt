# Vendored third-party libraries, all built from source and linked statically
# into SLAYER3D. No additional runtime dependencies beyond SDL3 itself.
#
# Each library is compiled as an OBJECT library so that its translation units
# land inside libslayer3d and callers never need to link against it separately.

include(FetchContent)

FetchContent_Declare(
    slayer3d_lua_source
    GIT_REPOSITORY https://github.com/walterschell/Lua.git
    GIT_TAG v5.4.7
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR slayer3d_no_upstream_cmake
)

FetchContent_Declare(
    slayer3d_miniz_source
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.1.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR slayer3d_no_upstream_cmake
)

FetchContent_MakeAvailable(slayer3d_lua_source slayer3d_miniz_source)

set(SLAYER3D_MINIZ_HEADER "${slayer3d_miniz_source_SOURCE_DIR}/miniz.h")
if(EXISTS "${SLAYER3D_MINIZ_HEADER}")
    file(READ "${SLAYER3D_MINIZ_HEADER}" SLAYER3D_MINIZ_HEADER_CONTENTS)
    if(NOT SLAYER3D_MINIZ_HEADER_CONTENTS MATCHES "\n$")
        file(APPEND "${SLAYER3D_MINIZ_HEADER}" "\n")
    endif()
endif()

set(SLAYER3D_LUA_ROOT "${slayer3d_lua_source_SOURCE_DIR}/lua-5.4.7")
add_library(
    slayer3d_lua
    OBJECT
        "${SLAYER3D_LUA_ROOT}/src/lapi.c"
        "${SLAYER3D_LUA_ROOT}/src/lcode.c"
        "${SLAYER3D_LUA_ROOT}/src/lctype.c"
        "${SLAYER3D_LUA_ROOT}/src/ldebug.c"
        "${SLAYER3D_LUA_ROOT}/src/ldo.c"
        "${SLAYER3D_LUA_ROOT}/src/ldump.c"
        "${SLAYER3D_LUA_ROOT}/src/lfunc.c"
        "${SLAYER3D_LUA_ROOT}/src/lgc.c"
        "${SLAYER3D_LUA_ROOT}/src/llex.c"
        "${SLAYER3D_LUA_ROOT}/src/lmem.c"
        "${SLAYER3D_LUA_ROOT}/src/lobject.c"
        "${SLAYER3D_LUA_ROOT}/src/lopcodes.c"
        "${SLAYER3D_LUA_ROOT}/src/lparser.c"
        "${SLAYER3D_LUA_ROOT}/src/lstate.c"
        "${SLAYER3D_LUA_ROOT}/src/lstring.c"
        "${SLAYER3D_LUA_ROOT}/src/ltable.c"
        "${SLAYER3D_LUA_ROOT}/src/ltm.c"
        "${SLAYER3D_LUA_ROOT}/src/lundump.c"
        "${SLAYER3D_LUA_ROOT}/src/lvm.c"
        "${SLAYER3D_LUA_ROOT}/src/lzio.c"
        "${SLAYER3D_LUA_ROOT}/src/lauxlib.c"
        "${SLAYER3D_LUA_ROOT}/src/lbaselib.c"
        "${SLAYER3D_LUA_ROOT}/src/lmathlib.c"
        "${SLAYER3D_LUA_ROOT}/src/lstrlib.c"
        "${SLAYER3D_LUA_ROOT}/src/ltablib.c"
        "${SLAYER3D_LUA_ROOT}/src/lutf8lib.c"
)
target_include_directories(slayer3d_lua PUBLIC "${SLAYER3D_LUA_ROOT}/include")
set_target_properties(slayer3d_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX AND NOT EMSCRIPTEN)
    target_compile_definitions(slayer3d_lua PRIVATE LUA_USE_POSIX)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_lua PRIVATE -w)
endif()

add_library(slayer3d_cgltf OBJECT cgltf/cgltf_impl.c)
target_include_directories(slayer3d_cgltf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
set_target_properties(slayer3d_cgltf PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_cgltf PRIVATE -w)
endif()

add_library(slayer3d_ufbx OBJECT ufbx/ufbx.c)
target_include_directories(slayer3d_ufbx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ufbx)
set_target_properties(slayer3d_ufbx PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_ufbx PRIVATE -w)
endif()

add_library(slayer3d_stb OBJECT stb/stb_impl.c stb/stb_truetype_impl.c)
target_include_directories(slayer3d_stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
set_target_properties(slayer3d_stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_stb PRIVATE -w)
endif()

if(APPLE)
    enable_language(OBJC)
    set_source_files_properties(miniaudio/miniaudio.c PROPERTIES LANGUAGE OBJC)
endif()

add_library(slayer3d_miniaudio OBJECT miniaudio/miniaudio.c)
target_include_directories(slayer3d_miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio ${CMAKE_CURRENT_SOURCE_DIR}/stb)
set_target_properties(slayer3d_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_miniaudio PRIVATE -w)
endif()

add_library(slayer3d_yyjson OBJECT yyjson/yyjson.c)
target_include_directories(slayer3d_yyjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/yyjson)
set_target_properties(slayer3d_yyjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_yyjson PRIVATE -w)
endif()

add_library(
    slayer3d_miniz
    OBJECT
        "${slayer3d_miniz_source_SOURCE_DIR}/miniz.c"
        "${slayer3d_miniz_source_SOURCE_DIR}/miniz_tdef.c"
        "${slayer3d_miniz_source_SOURCE_DIR}/miniz_tinfl.c"
)
target_include_directories(
    slayer3d_miniz
    PUBLIC
        ${slayer3d_miniz_source_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/miniz
)
set_target_properties(slayer3d_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(slayer3d_miniz PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO MINIZ_NO_TIME
                                                MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_miniz PRIVATE -w)
endif()

add_library(slayer3d_crypto OBJECT crypto/slayer3d_crypto.c)
target_include_directories(slayer3d_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/crypto)
set_target_properties(slayer3d_crypto PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(
    slayer3d_argtable3
    STATIC
        argtable3/arg_cmd.c
        argtable3/arg_date.c
        argtable3/arg_dbl.c
        argtable3/arg_dstr.c
        argtable3/arg_end.c
        argtable3/arg_file.c
        argtable3/arg_getopt_long.c
        argtable3/arg_hashtable.c
        argtable3/arg_int.c
        argtable3/arg_lit.c
        argtable3/arg_rem.c
        argtable3/arg_rex.c
        argtable3/arg_str.c
        argtable3/arg_utils.c
        argtable3/argtable3.c
)
target_include_directories(slayer3d_argtable3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/argtable3)
set_target_properties(slayer3d_argtable3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(slayer3d_argtable3 PRIVATE -w)
endif()
