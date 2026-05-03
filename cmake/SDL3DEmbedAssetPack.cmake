function(_sdl3d_hex_to_c_array out_var hex)
    if("${hex}" STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1, " _out "${hex}")
    string(REGEX REPLACE "((0x[0-9A-Fa-f][0-9A-Fa-f], ){24})" "\\1\n    " _out "${_out}")
    set(${out_var} "    ${_out}\n" PARENT_SCOPE)
endfunction()

if(NOT INPUT_PACK OR NOT OUTPUT_C OR NOT OUTPUT_H OR NOT SYMBOL)
    message(FATAL_ERROR "SDL3DEmbedAssetPack.cmake requires INPUT_PACK, OUTPUT_C, OUTPUT_H, and SYMBOL")
endif()

if(NOT EXISTS "${INPUT_PACK}")
    message(FATAL_ERROR "Embedded pack file does not exist: ${INPUT_PACK}")
endif()

file(READ "${INPUT_PACK}" _pack_hex HEX)
_sdl3d_hex_to_c_array(_pack_array "${_pack_hex}")

get_filename_component(_output_c_dir "${OUTPUT_C}" DIRECTORY)
get_filename_component(_output_h_dir "${OUTPUT_H}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_c_dir}")
file(MAKE_DIRECTORY "${_output_h_dir}")

string(TOUPPER "${SYMBOL}_H" _guard)
string(REGEX REPLACE "[^A-Z0-9_]" "_" _guard "${_guard}")

file(WRITE "${OUTPUT_H}"
"#ifndef ${_guard}\n"
"#define ${_guard}\n\n"
"#include <stddef.h>\n\n"
"extern const unsigned char ${SYMBOL}[];\n"
"extern const size_t ${SYMBOL}_size;\n\n"
"#endif /* ${_guard} */\n")

file(WRITE "${OUTPUT_C}"
"#include \"${SYMBOL}.h\"\n\n"
"const unsigned char ${SYMBOL}[] = {\n"
"${_pack_array}"
"};\n\n"
"const size_t ${SYMBOL}_size = sizeof(${SYMBOL});\n")
