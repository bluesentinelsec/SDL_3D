function(slayer3d_add_asset_pack target_name)
    set(options)
    set(one_value_args ROOT OUTPUT)
    set(multi_value_args FILES)
    cmake_parse_arguments(SLAYER3D_PACK "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT SLAYER3D_PACK_ROOT OR NOT SLAYER3D_PACK_OUTPUT OR NOT SLAYER3D_PACK_FILES)
        message(FATAL_ERROR "slayer3d_add_asset_pack requires ROOT, OUTPUT, and FILES")
    endif()
    if(NOT TARGET slayer3d_pack)
        message(FATAL_ERROR "slayer3d_add_asset_pack requires the native slayer3d_pack tool target")
    endif()

    set(_pack_args)
    set(_pack_deps)
    foreach(_file IN LISTS SLAYER3D_PACK_FILES)
        list(APPEND _pack_args --file "${_file}")
        list(APPEND _pack_deps "${SLAYER3D_PACK_ROOT}/${_file}")
    endforeach()
    get_filename_component(_pack_output_dir "${SLAYER3D_PACK_OUTPUT}" DIRECTORY)

    add_custom_command(
        OUTPUT "${SLAYER3D_PACK_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_pack_output_dir}"
        COMMAND $<TARGET_FILE:slayer3d_pack> --output "${SLAYER3D_PACK_OUTPUT}" --root "${SLAYER3D_PACK_ROOT}" ${_pack_args}
        DEPENDS slayer3d_pack ${_pack_deps}
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Packing SLAYER3D assets: ${SLAYER3D_PACK_OUTPUT}"
    )
    add_custom_target(${target_name} DEPENDS "${SLAYER3D_PACK_OUTPUT}")
endfunction()

function(slayer3d_add_embedded_asset_pack target_name)
    set(options)
    set(one_value_args ROOT SYMBOL OUTPUT_DIR)
    set(multi_value_args FILES)
    cmake_parse_arguments(SLAYER3D_EMBED "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT SLAYER3D_EMBED_ROOT OR NOT SLAYER3D_EMBED_SYMBOL OR NOT SLAYER3D_EMBED_FILES)
        message(FATAL_ERROR "slayer3d_add_embedded_asset_pack requires ROOT, SYMBOL, and FILES")
    endif()
    if(NOT SLAYER3D_EMBED_OUTPUT_DIR)
        set(SLAYER3D_EMBED_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_generated")
    endif()

    set(_embed_pack "${SLAYER3D_EMBED_OUTPUT_DIR}/${SLAYER3D_EMBED_SYMBOL}.slayer3dpak")
    set(_embed_c "${SLAYER3D_EMBED_OUTPUT_DIR}/${SLAYER3D_EMBED_SYMBOL}.c")
    set(_embed_h "${SLAYER3D_EMBED_OUTPUT_DIR}/${SLAYER3D_EMBED_SYMBOL}.h")
    set(_embed_args)
    set(_embed_deps)
    foreach(_file IN LISTS SLAYER3D_EMBED_FILES)
        list(APPEND _embed_args --file "${_file}")
        list(APPEND _embed_deps "${SLAYER3D_EMBED_ROOT}/${_file}")
    endforeach()

    add_custom_command(
        OUTPUT "${_embed_pack}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SLAYER3D_EMBED_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:slayer3d_pack> --output "${_embed_pack}" --root "${SLAYER3D_EMBED_ROOT}" ${_embed_args}
        DEPENDS slayer3d_pack ${_embed_deps}
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Packing SLAYER3D embedded assets: ${SLAYER3D_EMBED_SYMBOL}"
    )

    add_custom_command(
        OUTPUT "${_embed_c}" "${_embed_h}"
        COMMAND
            ${CMAKE_COMMAND}
            "-DINPUT_PACK=${_embed_pack}"
            "-DOUTPUT_C=${_embed_c}"
            "-DOUTPUT_H=${_embed_h}"
            "-DSYMBOL=${SLAYER3D_EMBED_SYMBOL}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Slayer3DEmbedAssetPack.cmake"
        DEPENDS "${_embed_pack}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Slayer3DEmbedAssetPack.cmake"
        VERBATIM
        COMMENT "Embedding SLAYER3D assets: ${SLAYER3D_EMBED_SYMBOL}"
    )

    add_library(${target_name} OBJECT "${_embed_c}")
    target_include_directories(${target_name} PUBLIC "${SLAYER3D_EMBED_OUTPUT_DIR}")
endfunction()

function(slayer3d_target_preload_asset_directory target_name source_dir mount_dir)
    if(EMSCRIPTEN)
        target_link_options(${target_name} PRIVATE "--preload-file=${source_dir}@${mount_dir}")
    endif()
endfunction()
