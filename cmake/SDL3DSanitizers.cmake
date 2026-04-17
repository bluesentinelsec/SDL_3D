function(sdl3d_enable_sanitizers target_name)
    if(NOT SDL3D_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        message(FATAL_ERROR "SDL3D_ENABLE_SANITIZERS is not supported with MSVC.")
    endif()

    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "SDL3D_ENABLE_SANITIZERS requires Clang or GCC.")
    endif()

    target_compile_options(
        ${target_name}
        PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
    )

    target_link_options(
        ${target_name}
        PRIVATE
            -fsanitize=address,undefined
    )
endfunction()
