# Vendored third-party libraries, all built from source and linked statically
# into SDL3D. No additional runtime dependencies beyond SDL3 itself.
#
# Each library is compiled as an OBJECT library so that its translation units
# land inside libsdl3d and callers never need to link against it separately.

include(FetchContent)

FetchContent_Declare(
    sdl3d_lua_source
    GIT_REPOSITORY https://github.com/walterschell/Lua.git
    GIT_TAG v5.4.7
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(sdl3d_lua_source)
if(NOT sdl3d_lua_source_POPULATED)
    FetchContent_Populate(sdl3d_lua_source)
endif()

set(SDL3D_LUA_ROOT "${sdl3d_lua_source_SOURCE_DIR}/lua-5.4.7")
add_library(
    sdl3d_lua
    OBJECT
        "${SDL3D_LUA_ROOT}/src/lapi.c"
        "${SDL3D_LUA_ROOT}/src/lcode.c"
        "${SDL3D_LUA_ROOT}/src/lctype.c"
        "${SDL3D_LUA_ROOT}/src/ldebug.c"
        "${SDL3D_LUA_ROOT}/src/ldo.c"
        "${SDL3D_LUA_ROOT}/src/ldump.c"
        "${SDL3D_LUA_ROOT}/src/lfunc.c"
        "${SDL3D_LUA_ROOT}/src/lgc.c"
        "${SDL3D_LUA_ROOT}/src/llex.c"
        "${SDL3D_LUA_ROOT}/src/lmem.c"
        "${SDL3D_LUA_ROOT}/src/lobject.c"
        "${SDL3D_LUA_ROOT}/src/lopcodes.c"
        "${SDL3D_LUA_ROOT}/src/lparser.c"
        "${SDL3D_LUA_ROOT}/src/lstate.c"
        "${SDL3D_LUA_ROOT}/src/lstring.c"
        "${SDL3D_LUA_ROOT}/src/ltable.c"
        "${SDL3D_LUA_ROOT}/src/ltm.c"
        "${SDL3D_LUA_ROOT}/src/lundump.c"
        "${SDL3D_LUA_ROOT}/src/lvm.c"
        "${SDL3D_LUA_ROOT}/src/lzio.c"
        "${SDL3D_LUA_ROOT}/src/lauxlib.c"
        "${SDL3D_LUA_ROOT}/src/lbaselib.c"
        "${SDL3D_LUA_ROOT}/src/lmathlib.c"
        "${SDL3D_LUA_ROOT}/src/lstrlib.c"
        "${SDL3D_LUA_ROOT}/src/ltablib.c"
        "${SDL3D_LUA_ROOT}/src/lutf8lib.c"
)
target_include_directories(sdl3d_lua PUBLIC "${SDL3D_LUA_ROOT}/include")
set_target_properties(sdl3d_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX AND NOT EMSCRIPTEN)
    target_compile_definitions(sdl3d_lua PRIVATE LUA_USE_POSIX)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sdl3d_lua PRIVATE -w)
elseif(MSVC)
    target_compile_options(sdl3d_lua PRIVATE /w)
endif()

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

add_library(sdl3d_stb OBJECT stb/stb_impl.c stb/stb_truetype_impl.c)
target_include_directories(sdl3d_stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
set_target_properties(sdl3d_stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sdl3d_stb PRIVATE -w)
elseif(MSVC)
    target_compile_options(sdl3d_stb PRIVATE /w)
endif()

if(APPLE)
    enable_language(OBJC)
    set_source_files_properties(miniaudio/miniaudio.c PROPERTIES LANGUAGE OBJC)
endif()

add_library(sdl3d_miniaudio OBJECT miniaudio/miniaudio.c)
target_include_directories(sdl3d_miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)
set_target_properties(sdl3d_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sdl3d_miniaudio PRIVATE -w)
elseif(MSVC)
    target_compile_options(sdl3d_miniaudio PRIVATE /w)
endif()

add_library(sdl3d_yyjson OBJECT yyjson/yyjson.c)
target_include_directories(sdl3d_yyjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/yyjson)
set_target_properties(sdl3d_yyjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sdl3d_yyjson PRIVATE -w)
elseif(MSVC)
    target_compile_options(sdl3d_yyjson PRIVATE /w)
endif()
