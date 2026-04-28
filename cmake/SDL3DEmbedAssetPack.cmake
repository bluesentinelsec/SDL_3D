function(_sdl3d_append_le out_var value byte_count)
    set(_bytes ${${out_var}})
    math(EXPR _last "${byte_count} - 1")
    foreach(_i RANGE 0 ${_last})
        math(EXPR _byte "(${value} >> (${_i} * 8)) & 255")
        list(APPEND _bytes "${_byte}")
    endforeach()
    set(${out_var} ${_bytes} PARENT_SCOPE)
endfunction()

function(_sdl3d_append_hex out_var hex)
    set(_bytes ${${out_var}})
    string(LENGTH "${hex}" _hex_len)
    if(_hex_len GREATER 0)
        math(EXPR _last "${_hex_len} - 2")
        foreach(_i RANGE 0 ${_last} 2)
            string(SUBSTRING "${hex}" ${_i} 2 _pair)
            list(APPEND _bytes "0x${_pair}")
        endforeach()
    endif()
    set(${out_var} ${_bytes} PARENT_SCOPE)
endfunction()

function(_sdl3d_append_ascii out_var text)
    string(HEX "${text}" _hex)
    _sdl3d_append_hex(${out_var} "${_hex}")
    set(${out_var} ${${out_var}} PARENT_SCOPE)
endfunction()

function(_sdl3d_bytes_to_c_array out_var)
    set(_line "    ")
    set(_out "")
    set(_column 0)
    foreach(_byte IN LISTS ARGN)
        if(_byte MATCHES "^0x")
            set(_literal "${_byte}")
        else()
            set(_literal "${_byte}")
        endif()
        string(APPEND _line "${_literal}, ")
        math(EXPR _column "${_column} + 1")
        if(_column EQUAL 12)
            string(APPEND _out "${_line}\n")
            set(_line "    ")
            set(_column 0)
        endif()
    endforeach()
    if(NOT _line STREQUAL "    ")
        string(APPEND _out "${_line}\n")
    endif()
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

if(NOT OUTPUT_C OR NOT OUTPUT_H OR NOT SYMBOL OR NOT ROOT OR NOT FILES)
    message(FATAL_ERROR "SDL3DEmbedAssetPack.cmake requires OUTPUT_C, OUTPUT_H, SYMBOL, ROOT, and FILES")
endif()

string(REPLACE "|" ";" _files "${FILES}")
list(SORT _files)

set(_last_file "")
set(_table_size 0)
foreach(_file IN LISTS _files)
    if(_file MATCHES "://" OR _file MATCHES "^/" OR _file MATCHES "^[A-Za-z]:" OR _file MATCHES "(^|/|\\\\)\\.\\.($|/|\\\\)")
        message(FATAL_ERROR "Unsafe embedded asset path: ${_file}")
    endif()
    if(_last_file STREQUAL _file)
        message(FATAL_ERROR "Duplicate embedded asset path: ${_file}")
    endif()
    set(_last_file "${_file}")

    string(HEX "${_file}" _path_hex)
    string(LENGTH "${_path_hex}" _path_hex_len)
    math(EXPR _path_len "${_path_hex_len} / 2")
    if(_path_len EQUAL 0 OR _path_len GREATER 65535)
        message(FATAL_ERROR "Invalid embedded asset path: ${_file}")
    endif()
    math(EXPR _table_size "${_table_size} + 18 + ${_path_len}")
endforeach()

set(_bytes)
_sdl3d_append_ascii(_bytes "S3DPAK1")
list(APPEND _bytes 0)
_sdl3d_append_le(_bytes 1 4)
list(LENGTH _files _entry_count)
_sdl3d_append_le(_bytes ${_entry_count} 4)
_sdl3d_append_le(_bytes 24 8)

math(EXPR _data_offset "24 + ${_table_size}")
set(_data_chunks)
foreach(_file IN LISTS _files)
    string(HEX "${_file}" _path_hex)
    string(LENGTH "${_path_hex}" _path_hex_len)
    math(EXPR _path_len "${_path_hex_len} / 2")
    set(_source "${ROOT}/${_file}")
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "Embedded asset source does not exist: ${_source}")
    endif()
    file(SIZE "${_source}" _file_size)
    file(READ "${_source}" _file_hex HEX)

    _sdl3d_append_le(_bytes ${_path_len} 2)
    _sdl3d_append_le(_bytes ${_data_offset} 8)
    _sdl3d_append_le(_bytes ${_file_size} 8)
    _sdl3d_append_hex(_bytes "${_path_hex}")

    list(APPEND _data_chunks "${_file_hex}")
    math(EXPR _data_offset "${_data_offset} + ${_file_size}")
endforeach()

foreach(_chunk IN LISTS _data_chunks)
    _sdl3d_append_hex(_bytes "${_chunk}")
endforeach()

_sdl3d_bytes_to_c_array(_array ${_bytes})
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
"${_array}"
"};\n\n"
"const size_t ${SYMBOL}_size = sizeof(${SYMBOL});\n")
