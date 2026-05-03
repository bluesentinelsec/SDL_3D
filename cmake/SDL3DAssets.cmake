function(sdl3d_add_asset_pack target_name)
    set(options)
    set(one_value_args ROOT OUTPUT)
    set(multi_value_args FILES)
    cmake_parse_arguments(SDL3D_PACK "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT SDL3D_PACK_ROOT OR NOT SDL3D_PACK_OUTPUT OR NOT SDL3D_PACK_FILES)
        message(FATAL_ERROR "sdl3d_add_asset_pack requires ROOT, OUTPUT, and FILES")
    endif()
    if(NOT TARGET sdl3d_pack)
        message(FATAL_ERROR "sdl3d_add_asset_pack requires the native sdl3d_pack tool target")
    endif()

    set(_pack_args)
    set(_pack_deps)
    foreach(_file IN LISTS SDL3D_PACK_FILES)
        list(APPEND _pack_args --file "${_file}")
        list(APPEND _pack_deps "${SDL3D_PACK_ROOT}/${_file}")
    endforeach()
    get_filename_component(_pack_output_dir "${SDL3D_PACK_OUTPUT}" DIRECTORY)

    add_custom_command(
        OUTPUT "${SDL3D_PACK_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_pack_output_dir}"
        COMMAND $<TARGET_FILE:sdl3d_pack> --output "${SDL3D_PACK_OUTPUT}" --root "${SDL3D_PACK_ROOT}" ${_pack_args}
        DEPENDS sdl3d_pack ${_pack_deps}
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Packing SDL3D assets: ${SDL3D_PACK_OUTPUT}"
    )
    add_custom_target(${target_name} DEPENDS "${SDL3D_PACK_OUTPUT}")
endfunction()

function(sdl3d_add_embedded_asset_pack target_name)
    set(options)
    set(one_value_args ROOT SYMBOL OUTPUT_DIR)
    set(multi_value_args FILES)
    cmake_parse_arguments(SDL3D_EMBED "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT SDL3D_EMBED_ROOT OR NOT SDL3D_EMBED_SYMBOL OR NOT SDL3D_EMBED_FILES)
        message(FATAL_ERROR "sdl3d_add_embedded_asset_pack requires ROOT, SYMBOL, and FILES")
    endif()
    if(NOT SDL3D_EMBED_OUTPUT_DIR)
        set(SDL3D_EMBED_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_generated")
    endif()

    set(_embed_pack "${SDL3D_EMBED_OUTPUT_DIR}/${SDL3D_EMBED_SYMBOL}.sdl3dpak")
    set(_embed_c "${SDL3D_EMBED_OUTPUT_DIR}/${SDL3D_EMBED_SYMBOL}.c")
    set(_embed_h "${SDL3D_EMBED_OUTPUT_DIR}/${SDL3D_EMBED_SYMBOL}.h")
    set(_embed_args)
    set(_embed_deps)
    foreach(_file IN LISTS SDL3D_EMBED_FILES)
        list(APPEND _embed_args --file "${_file}")
        list(APPEND _embed_deps "${SDL3D_EMBED_ROOT}/${_file}")
    endforeach()

    add_custom_command(
        OUTPUT "${_embed_pack}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SDL3D_EMBED_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:sdl3d_pack> --output "${_embed_pack}" --root "${SDL3D_EMBED_ROOT}" ${_embed_args}
        DEPENDS sdl3d_pack ${_embed_deps}
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Packing SDL3D embedded assets: ${SDL3D_EMBED_SYMBOL}"
    )

    add_custom_command(
        OUTPUT "${_embed_c}" "${_embed_h}"
        COMMAND
            ${CMAKE_COMMAND}
            "-DINPUT_PACK=${_embed_pack}"
            "-DOUTPUT_C=${_embed_c}"
            "-DOUTPUT_H=${_embed_h}"
            "-DSYMBOL=${SDL3D_EMBED_SYMBOL}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SDL3DEmbedAssetPack.cmake"
        DEPENDS "${_embed_pack}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SDL3DEmbedAssetPack.cmake"
        VERBATIM
        COMMENT "Embedding SDL3D assets: ${SDL3D_EMBED_SYMBOL}"
    )

    add_library(${target_name} OBJECT "${_embed_c}")
    target_include_directories(${target_name} PUBLIC "${SDL3D_EMBED_OUTPUT_DIR}")
endfunction()

function(sdl3d_target_preload_asset_directory target_name source_dir mount_dir)
    if(EMSCRIPTEN)
        target_link_options(${target_name} PRIVATE "--preload-file=${source_dir}@${mount_dir}")
    endif()
endfunction()
