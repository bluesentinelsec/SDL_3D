# EmbedShaders.cmake
#
# Converts .glsl shader files into a C header with string constants.
# Each file shaders/foo.glsl becomes:
#   static const char sdl3d_shader_foo[] = "...";
#
# Usage:
#   sdl3d_embed_shaders(output_header shader1.glsl shader2.glsl ...)

function(sdl3d_embed_shaders OUTPUT_HEADER)
    set(SHADER_SOURCES ${ARGN})
    set(HEADER_CONTENT "/* Auto-generated from shader source files. Do not edit. */\n")
    string(APPEND HEADER_CONTENT "#ifndef SDL3D_EMBEDDED_SHADERS_H\n")
    string(APPEND HEADER_CONTENT "#define SDL3D_EMBEDDED_SHADERS_H\n\n")

    foreach(SHADER_FILE ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
        file(READ ${SHADER_FILE} SHADER_TEXT)
        # Escape backslashes, then quotes, then convert newlines to \n"
        string(REPLACE "\\" "\\\\" SHADER_TEXT "${SHADER_TEXT}")
        string(REPLACE "\"" "\\\"" SHADER_TEXT "${SHADER_TEXT}")
        string(REPLACE "\n" "\\n\"\n    \"" SHADER_TEXT "${SHADER_TEXT}")
        string(APPEND HEADER_CONTENT "static const char sdl3d_shader_${SHADER_NAME}[] =\n    \"${SHADER_TEXT}\";\n\n")
    endforeach()

    string(APPEND HEADER_CONTENT "#endif\n")
    file(WRITE ${OUTPUT_HEADER} "${HEADER_CONTENT}")
endfunction()
