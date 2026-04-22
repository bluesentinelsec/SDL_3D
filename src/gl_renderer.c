/*
 * OpenGL 3.3 Core renderer for SDL3D.
 *
 * Implements the sdl3d_backend_interface using streaming VAO/VBO draws,
 * a scene UBO for per-frame data, and an offscreen FBO with a
 * fullscreen-triangle copy pass for letterboxed presentation.
 */

#include "gl_renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include "gl_funcs.h"
#include "render_context_internal.h"
#include "sdl3d/lighting.h"
#include "sdl3d/texture.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Texture cache                                                       */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_gl_tex_entry
{
    const sdl3d_texture2d *key;
    GLuint gl_tex;
    struct sdl3d_gl_tex_entry *next;
} sdl3d_gl_tex_entry;

/* ------------------------------------------------------------------ */
/* Scene UBO (std140 layout, must match GLSL SceneUBO)                 */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_scene_ubo_data
{
    float view_projection[16];
    float camera_pos[3];
    float _pad0;
    float ambient[3];
    int light_count;
    struct
    {
        int type;
        float _pad[3];
        float position[3];
        float _pad1;
        float direction[3];
        float _pad2;
        float color[3];
        float intensity;
        float range;
        float inner_cutoff;
        float outer_cutoff;
        float _pad3;
    } lights[8];
    int fog_mode;
    float fog_start;
    float fog_end;
    float fog_density;
    float fog_color[3];
    int tonemap_mode;
} sdl3d_scene_ubo_data;

/* ------------------------------------------------------------------ */
/* Draw list entry                                                     */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_draw_entry
{
    float *positions;
    float *normals;
    float *uvs;
    float *colors;
    unsigned int *indices;
    int vertex_count;
    int index_count;
    float model_matrix[16];
    float normal_matrix[9];
    float tint[4];
    float metallic;
    float roughness;
    float emissive[3];
    const sdl3d_texture2d *texture;
    bool lit;
    float mvp[16];
} sdl3d_draw_entry;

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

#define SDL3D_MAX_POINT_SHADOWS 2
#define SDL3D_POINT_SHADOWS_ENABLED 1

struct sdl3d_gl_context
{
    SDL_Window *window;
    SDL_GLContext gl_context;
    sdl3d_gl_funcs gl;
    bool is_es;
    Uint64 frame_index;
    bool ubo_dirty;

    GLuint pbr_program;
    GLuint unlit_program;
    GLuint copy_program;

    /* PBR uniform locations */
    GLint pbr_model_loc;
    GLint pbr_normal_matrix_loc;
    GLint pbr_texture_loc;
    GLint pbr_has_texture_loc;
    GLint pbr_tint_loc;
    GLint pbr_metallic_loc;
    GLint pbr_roughness_loc;
    GLint pbr_emissive_loc;

    /* Unlit uniform locations */
    GLint unlit_mvp_loc;
    GLint unlit_texture_loc;
    GLint unlit_has_texture_loc;
    GLint unlit_tint_loc;

    /* Copy uniform locations */
    GLint copy_texture_loc;

    /* Scene UBO */
    GLuint scene_ubo;

    /* Lit streaming buffers */
    GLuint lit_vao;
    GLuint lit_position_vbo;
    GLuint lit_normal_vbo;
    GLuint lit_uv_vbo;
    GLuint lit_color_vbo;
    GLuint lit_ebo;

    /* Unlit streaming buffers */
    GLuint unlit_vao;
    GLuint unlit_position_vbo;
    GLuint unlit_uv_vbo;
    GLuint unlit_color_vbo;
    GLuint unlit_ebo;

    GLuint fullscreen_vao;

    /* Shadow mapping */
    GLuint shadow_fbo;
    GLuint shadow_depth_tex;
    GLuint shadow_program;
    GLint shadow_light_vp_loc;
    GLint shadow_model_loc;
    GLuint shadow_vao;
    GLuint shadow_position_vbo;
    GLuint shadow_ebo;
    bool in_shadow_pass;
    float shadow_light_vp[16];
    float shadow_bias;

#define SDL3D_CSM_CASCADE_COUNT 4
    float csm_light_vp[SDL3D_CSM_CASCADE_COUNT][16];
    float csm_split_depths[SDL3D_CSM_CASCADE_COUNT];
    bool csm_fragment_enabled;

    /* PBR shadow uniform locations */
    GLint pbr_shadow_map_loc;
    GLint pbr_shadow_vp_loc;
    GLint pbr_shadow_enabled_loc;
    GLint pbr_shadow_bias_loc;

    /* CSM uniform locations */
    GLint pbr_csm_vp_loc[4];
    GLint pbr_csm_splits_loc;
    GLint pbr_view_matrix_loc;
    GLint pbr_csm_enabled_loc;

    /* Point light shadows */
    GLuint point_shadow_fbo;
    GLuint point_shadow_cubemap[SDL3D_MAX_POINT_SHADOWS];
    GLuint point_shadow_program;
    GLint point_shadow_model_loc;
    GLint point_shadow_light_vp_loc;
    GLint point_shadow_light_pos_loc;
    GLint point_shadow_far_loc;
    int point_shadow_light_index[SDL3D_MAX_POINT_SHADOWS];
    float point_shadow_far_plane[SDL3D_MAX_POINT_SHADOWS];
    float point_shadow_vp[SDL3D_MAX_POINT_SHADOWS][6][16];
    int point_shadow_count;

    GLint pbr_point_shadow_map_loc[SDL3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_light_pos_loc[SDL3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_far_loc[SDL3D_MAX_POINT_SHADOWS];
    GLint pbr_point_shadow_count_loc;

    /* Deferred draw list */
    sdl3d_draw_entry *draw_list;
    int draw_count;
    int draw_capacity;

    GLuint white_texture;

    sdl3d_gl_tex_entry *tex_cache;

    float *white_colors;
    int white_colors_capacity;

    GLuint fbo;
    GLuint fbo_color;
    GLuint fbo_depth;
    int logical_w;
    int logical_h;

    /* Post-process */
    GLuint pp_fbo_a, pp_fbo_b;
    GLuint pp_tex_a, pp_tex_b;
    GLuint bloom_program;
    GLuint bloom_blur_program;
    GLuint composite_program;
    GLint bloom_scene_loc, bloom_threshold_loc;
    GLint blur_image_loc, blur_horizontal_loc;
    GLint comp_scene_loc, comp_bloom_loc, comp_vignette_loc, comp_contrast_loc, comp_saturation_loc;
    GLuint final_color_tex;

    /* Retro profile post-process */
    GLuint retro_program;
    GLint retro_scene_loc;
    GLint retro_profile_loc;
    GLint retro_resolution_loc;
    int active_retro_profile; /* 0=modern, 1=PS1, 2=N64, 3=DOS, 4=SNES */

    /* SSAO post-process */
    GLuint ssao_program;
    GLint ssao_scene_loc, ssao_depth_loc, ssao_texel_size_loc, ssao_near_loc, ssao_far_loc;

    /* Cached render context pointer for lazy UBO upload */
    sdl3d_render_context *current_ctx;
};

/* ------------------------------------------------------------------ */
/* Embedded shader source (without #version line)                      */
/* ------------------------------------------------------------------ */

static const char k_pbr_vert[] = "layout(location = 0) in vec3 aPosition;\n"
                                 "layout(location = 1) in vec3 aNormal;\n"
                                 "layout(location = 2) in vec2 aTexCoord;\n"
                                 "layout(location = 3) in vec4 aColor;\n"
                                 "\n"
                                 "#define MAX_LIGHTS 8\n"
                                 "struct Light {\n"
                                 "    int type;\n"
                                 "    vec3 position;\n"
                                 "    vec3 direction;\n"
                                 "    vec3 color;\n"
                                 "    float intensity;\n"
                                 "    float range;\n"
                                 "    float innerCutoff;\n"
                                 "    float outerCutoff;\n"
                                 "};\n"
                                 "layout(std140) uniform SceneUBO {\n"
                                 "    mat4 uViewProjection;\n"
                                 "    vec3 uCameraPos;\n"
                                 "    float _pad0;\n"
                                 "    vec3 uAmbient;\n"
                                 "    int uLightCount;\n"
                                 "    Light uLights[MAX_LIGHTS];\n"
                                 "    int uFogMode;\n"
                                 "    float uFogStart;\n"
                                 "    float uFogEnd;\n"
                                 "    float uFogDensity;\n"
                                 "    vec3 uFogColor;\n"
                                 "    int uTonemapMode;\n"
                                 "};\n"
                                 "\n"
                                 "uniform mat4 uModel;\n"
                                 "uniform mat3 uNormalMatrix;\n"
                                 "\n"
                                 "out vec3 vWorldPos;\n"
                                 "out vec3 vWorldNormal;\n"
                                 "out vec2 vTexCoord;\n"
                                 "out vec4 vColor;\n"
                                 "\n"
                                 "void main() {\n"
                                 "    vec4 worldPos = uModel * vec4(aPosition, 1.0);\n"
                                 "    vWorldPos = worldPos.xyz;\n"
                                 "    vWorldNormal = normalize(uNormalMatrix * aNormal);\n"
                                 "    vTexCoord = aTexCoord;\n"
                                 "    vColor = aColor;\n"
                                 "    gl_Position = uViewProjection * worldPos;\n"
                                 "}\n";

static const char k_pbr_frag_decl[] = "#define MAX_LIGHTS 8\n"
                                      "#define PI 3.14159265\n"
                                      "\n"
                                      "struct Light {\n"
                                      "    int type;\n"
                                      "    vec3 position;\n"
                                      "    vec3 direction;\n"
                                      "    vec3 color;\n"
                                      "    float intensity;\n"
                                      "    float range;\n"
                                      "    float innerCutoff;\n"
                                      "    float outerCutoff;\n"
                                      "};\n"
                                      "\n"
                                      "layout(std140) uniform SceneUBO {\n"
                                      "    mat4 uViewProjection;\n"
                                      "    vec3 uCameraPos;\n"
                                      "    float _pad0;\n"
                                      "    vec3 uAmbient;\n"
                                      "    int uLightCount;\n"
                                      "    Light uLights[MAX_LIGHTS];\n"
                                      "    int uFogMode;\n"
                                      "    float uFogStart;\n"
                                      "    float uFogEnd;\n"
                                      "    float uFogDensity;\n"
                                      "    vec3 uFogColor;\n"
                                      "    int uTonemapMode;\n"
                                      "};\n"
                                      "\n"
                                      "in vec3 vWorldPos;\n"
                                      "in vec3 vWorldNormal;\n"
                                      "in vec2 vTexCoord;\n"
                                      "in vec4 vColor;\n"
                                      "\n"
                                      "uniform sampler2D uTexture;\n"
                                      "uniform int uHasTexture;\n"
                                      "uniform vec4 uTint;\n"
                                      "uniform float uMetallic;\n"
                                      "uniform float uRoughness;\n"
                                      "uniform vec3 uEmissive;\n"
                                      "\n"
                                      "uniform sampler2DArray uShadowMap;\n"
                                      "uniform mat4 uShadowVP;\n"
                                      "uniform int uShadowEnabled;\n"
                                      "uniform float uShadowBias;\n"
                                      "uniform mat4 uCSMVP[4];\n"
                                      "uniform float uCSMSplits[4];\n"
                                      "uniform mat4 uViewMatrix;\n"
                                      "uniform int uCSMEnabled;\n"
                                      "\n"
                                      "uniform samplerCube uPointShadowMap[2];\n"
                                      "uniform vec3 uPointShadowLightPos[2];\n"
                                      "uniform float uPointShadowFar[2];\n"
                                      "uniform int uPointShadowCount;\n"
                                      "\n"
                                      "out vec4 fragColor;\n"
                                      "\n"
                                      "float DistributionGGX(float NdotH, float r) {\n"
                                      "    float a = r * r;\n"
                                      "    float a2 = a * a;\n"
                                      "    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
                                      "    return a2 / (PI * d * d + 0.0001);\n"
                                      "}\n"
                                      "\n"
                                      "float GeometrySchlickGGX(float NdotV, float r) {\n"
                                      "    float k = (r + 1.0) * (r + 1.0) / 8.0;\n"
                                      "    return NdotV / (NdotV * (1.0 - k) + k + 0.0001);\n"
                                      "}\n"
                                      "\n"
                                      "vec3 FresnelSchlick(float cosTheta, vec3 F0) {\n"
                                      "    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
                                      "}\n";

static const char k_pbr_frag_main[] =
    "void main() {\n"
    "    vec2 uv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);\n"
    "    vec4 texel = (uHasTexture != 0) ? texture(uTexture, uv) : vec4(1.0);\n"
    "    vec3 albedo = texel.rgb * vColor.rgb * uTint.rgb;\n"
    "    float alpha = texel.a * vColor.a * uTint.a;\n"
    "    if (alpha <= 0.0) discard;\n"
    "\n"
    "    vec3 N = normalize(vWorldNormal);\n"
    "    vec3 V = normalize(uCameraPos - vWorldPos);\n"
    "    float NdotV = max(dot(N, V), 0.0);\n"
    "    vec3 F0 = mix(vec3(0.04), albedo, uMetallic);\n"
    "\n"
    "    vec3 Lo = vec3(0.0);\n"
    "    for (int i = 0; i < uLightCount && i < MAX_LIGHTS; i++) {\n"
    "        vec3 L;\n"
    "        float attenuation = 1.0;\n"
    "\n"
    "        if (uLights[i].type == 0) {\n"
    "            L = normalize(-uLights[i].direction);\n"
    "        } else {\n"
    "            vec3 toLight = uLights[i].position - vWorldPos;\n"
    "            float dist = length(toLight);\n"
    "            L = toLight / max(dist, 0.0001);\n"
    "            if (uLights[i].range > 0.0) {\n"
    "                float r = dist / uLights[i].range;\n"
    "                attenuation = max(1.0 - r * r, 0.0);\n"
    "                attenuation *= attenuation;\n"
    "            }\n"
    "            if (uLights[i].type == 2) {\n"
    "                float cosA = dot(-L, normalize(uLights[i].direction));\n"
    "                float eps = uLights[i].innerCutoff - uLights[i].outerCutoff;\n"
    "                attenuation *= clamp((cosA - uLights[i].outerCutoff) / max(eps, 0.0001), 0.0, 1.0);\n"
    "            }\n"
    "        }\n"
    "\n"
    "        vec3 radiance = uLights[i].color * uLights[i].intensity * attenuation;\n"
    "        float NdotL = max(dot(N, L), 0.0);\n"
    "        if (NdotL <= 0.0) continue;\n"
    "\n"
    "        vec3 H = normalize(L + V);\n"
    "        float NdotH = max(dot(N, H), 0.0);\n"
    "        float HdotV = max(dot(H, V), 0.0);\n"
    "\n"
    "        float NDF = DistributionGGX(NdotH, uRoughness);\n"
    "        float G = GeometrySchlickGGX(NdotV, uRoughness) * GeometrySchlickGGX(NdotL, uRoughness);\n"
    "        vec3 F = FresnelSchlick(HdotV, F0);\n"
    "\n"
    "        vec3 spec = (NDF * G * F) / (4.0 * NdotV * NdotL + 0.0001);\n"
    "        vec3 kD = (1.0 - F) * (1.0 - uMetallic);\n"
    "        vec3 diff = kD * albedo / PI;\n"
    "\n"
    "        Lo += (diff + spec) * radiance * NdotL;\n"
    "    }\n";

static const char k_pbr_frag_post[] =
    "\n"
    "    /* Shadow (CSM cascade selection with PCF 3x3). */\n"
    "    if (uShadowEnabled != 0) {\n"
    "        int layer = 0;\n"
    "        if (uCSMEnabled != 0) {\n"
    "            float fragDepth = abs((uViewMatrix * vec4(vWorldPos, 1.0)).z);\n"
    "            layer = 3;\n"
    "            for (int i = 0; i < 4; ++i) {\n"
    "                if (fragDepth < uCSMSplits[i]) { layer = i; break; }\n"
    "            }\n"
    "        }\n"
    "        mat4 shadowVP = (uCSMEnabled != 0) ? uCSMVP[layer] : uShadowVP;\n"
    "        vec4 lpos = shadowVP * vec4(vWorldPos, 1.0);\n"
    "        vec3 projCoords = lpos.xyz / lpos.w * 0.5 + 0.5;\n"
    "        float currentDepth = projCoords.z;\n"
    "        vec3 lightDir = normalize(-uLights[0].direction);\n"
    "        float bias = max(0.05 * (1.0 - dot(N, lightDir)), 0.005);\n"
    "        float shadow = 0.0;\n"
    "        vec2 texelSize = 1.0 / vec2(2048.0);\n"
    "        for (int x = -1; x <= 1; ++x) {\n"
    "            for (int y = -1; y <= 1; ++y) {\n"
    "                float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, "
    "float(layer))).r;\n"
    "                shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;\n"
    "            }\n"
    "        }\n"
    "        shadow /= 9.0;\n"
    "        if (projCoords.z > 1.0) shadow = 0.0;\n"
    "        Lo *= (1.0 - shadow);\n"
    "    }\n"
    "\n"
    "    /* Point light shadows. */\n"
    "    for (int ps = 0; ps < uPointShadowCount && ps < 2; ps++) {\n"
    "        vec3 fragToLight = vWorldPos - uPointShadowLightPos[ps];\n"
    "        float closestDepth = texture(uPointShadowMap[ps], fragToLight).r * uPointShadowFar[ps];\n"
    "        float currentDepth = length(fragToLight);\n"
    "        float pbias = 0.15;\n"
    "        if (currentDepth - pbias > closestDepth) {\n"
    "            Lo *= 0.5;\n"
    "        }\n"
    "    }\n"
    "\n"
    "    /* Hemisphere ambient: sky color from above, ground bounce from below. */\n"
    "    vec3 skyColor = uAmbient * 1.2;\n"
    "    vec3 groundColor = uAmbient * vec3(0.6, 0.5, 0.4);\n"
    "    float hemi = dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;\n"
    "    vec3 ambient = mix(groundColor, skyColor, hemi) * albedo;\n"
    "    vec3 color = ambient + Lo + uEmissive;\n"
    "\n"
    "    if (uFogMode > 0) {\n"
    "        float dist = length(uCameraPos - vWorldPos);\n"
    "        float fogFactor = 0.0;\n"
    "        if (uFogMode == 1) fogFactor = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);\n"
    "        else if (uFogMode == 2) fogFactor = 1.0 - exp(-uFogDensity * dist);\n"
    "        else if (uFogMode == 3) { float d = uFogDensity * dist; fogFactor = 1.0 - exp(-d * d); }\n"
    "        color = mix(color, uFogColor, fogFactor);\n"
    "    }\n"
    "\n"
    "    if (uTonemapMode == 1) color = color / (1.0 + color);\n"
    "    else if (uTonemapMode == 2) {\n"
    "        float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;\n"
    "        color = clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);\n"
    "    }\n"
    "\n"
    "    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));\n"
    "\n"
    "    fragColor = vec4(color, alpha);\n"
    "}\n";

static const char k_unlit_vert[] = "layout(location = 0) in vec3 aPosition;\n"
                                   "layout(location = 1) in vec2 aTexCoord;\n"
                                   "layout(location = 2) in vec4 aColor;\n"
                                   "\n"
                                   "uniform mat4 uMVP;\n"
                                   "\n"
                                   "out vec2 vTexCoord;\n"
                                   "out vec4 vColor;\n"
                                   "\n"
                                   "void main() {\n"
                                   "    vTexCoord = aTexCoord;\n"
                                   "    vColor = aColor;\n"
                                   "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
                                   "}\n";

static const char k_unlit_frag[] = "in vec2 vTexCoord;\n"
                                   "in vec4 vColor;\n"
                                   "\n"
                                   "uniform sampler2D uTexture;\n"
                                   "uniform int uHasTexture;\n"
                                   "uniform vec4 uTint;\n"
                                   "\n"
                                   "out vec4 fragColor;\n"
                                   "\n"
                                   "void main() {\n"
                                   "    vec2 uv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);\n"
                                   "    vec4 texel = (uHasTexture != 0) ? texture(uTexture, uv) : vec4(1.0);\n"
                                   "    fragColor = texel * vColor * uTint;\n"
                                   "    if (fragColor.a <= 0.0) discard;\n"
                                   "}\n";

static const char k_fullscreen_vert[] = "out vec2 vTexCoord;\n"
                                        "\n"
                                        "void main() {\n"
                                        "    float x = float((gl_VertexID & 1) << 2) - 1.0;\n"
                                        "    float y = float((gl_VertexID & 2) << 1) - 1.0;\n"
                                        "    vTexCoord = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);\n"
                                        "    gl_Position = vec4(x, y, 0.0, 1.0);\n"
                                        "}\n";

static const char k_copy_frag[] = "in vec2 vTexCoord;\n"
                                  "uniform sampler2D uScene;\n"
                                  "out vec4 fragColor;\n"
                                  "\n"
                                  "void main() {\n"
                                  "    fragColor = texture(uScene, vTexCoord);\n"
                                  "}\n";

static const char k_shadow_vert[] = "layout(location = 0) in vec3 aPosition;\n"
                                    "uniform mat4 uLightVP;\n"
                                    "uniform mat4 uModel;\n"
                                    "void main() {\n"
                                    "    gl_Position = uLightVP * uModel * vec4(aPosition, 1.0);\n"
                                    "}\n";

static const char k_shadow_frag[] = "out vec4 fragColor;\nvoid main() { fragColor = vec4(1.0); }\n";

static const char k_point_shadow_vert[] = "layout(location = 0) in vec3 aPos;\n"
                                          "uniform mat4 model;\n"
                                          "uniform mat4 lightVP;\n"
                                          "out vec3 vWorldPos;\n"
                                          "void main() {\n"
                                          "    vec4 wp = model * vec4(aPos, 1.0);\n"
                                          "    vWorldPos = wp.xyz;\n"
                                          "    gl_Position = lightVP * wp;\n"
                                          "}\n";

static const char k_point_shadow_frag[] = "in vec3 vWorldPos;\n"
                                          "uniform vec3 lightPos;\n"
                                          "uniform float farPlane;\n"
                                          "void main() {\n"
                                          "    float dist = length(vWorldPos - lightPos);\n"
                                          "    gl_FragDepth = dist / farPlane;\n"
                                          "}\n";

/* ---- Post-process fragment shaders ---- */

static const char k_bloom_threshold_frag[] =
    "in vec2 vTexCoord;\n"
    "uniform sampler2D uScene;\n"
    "uniform float uThreshold;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 color = texture(uScene, vTexCoord).rgb;\n"
    "    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "    fragColor = (brightness > uThreshold) ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char k_blur_frag[] =
    "in vec2 vTexCoord;\n"
    "uniform sampler2D uImage;\n"
    "uniform int uHorizontal;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
    "    vec2 texOffset = 1.0 / vec2(textureSize(uImage, 0));\n"
    "    vec3 result = texture(uImage, vTexCoord).rgb * weights[0];\n"
    "    if (uHorizontal == 1) {\n"
    "        for (int i = 1; i < 5; ++i) {\n"
    "            result += texture(uImage, vTexCoord + vec2(texOffset.x * float(i), 0.0)).rgb * weights[i];\n"
    "            result += texture(uImage, vTexCoord - vec2(texOffset.x * float(i), 0.0)).rgb * weights[i];\n"
    "        }\n"
    "    } else {\n"
    "        for (int i = 1; i < 5; ++i) {\n"
    "            result += texture(uImage, vTexCoord + vec2(0.0, texOffset.y * float(i))).rgb * weights[i];\n"
    "            result += texture(uImage, vTexCoord - vec2(0.0, texOffset.y * float(i))).rgb * weights[i];\n"
    "        }\n"
    "    }\n"
    "    fragColor = vec4(result, 1.0);\n"
    "}\n";

static const char k_composite_frag[] = "in vec2 vTexCoord;\n"
                                       "uniform sampler2D uScene;\n"
                                       "uniform sampler2D uBloom;\n"
                                       "uniform float uVignetteStrength;\n"
                                       "uniform float uContrast;\n"
                                       "uniform float uSaturation;\n"
                                       "out vec4 fragColor;\n"
                                       "void main() {\n"
                                       "    vec3 color = texture(uScene, vTexCoord).rgb;\n"
                                       "    vec3 bloom = texture(uBloom, vTexCoord).rgb;\n"
                                       "    color += bloom * 0.3;\n"
                                       "    vec2 uv = vTexCoord * 2.0 - 1.0;\n"
                                       "    float vig = 1.0 - dot(uv, uv) * uVignetteStrength;\n"
                                       "    color *= clamp(vig, 0.0, 1.0);\n"
                                       "    color = (color - 0.5) * uContrast + 0.5;\n"
                                       "    float grey = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
                                       "    color = mix(vec3(grey), color, uSaturation);\n"
                                       "    fragColor = vec4(max(color, 0.0), 1.0);\n"
                                       "}\n";

static const char k_retro_frag[] = "in vec2 vTexCoord;\n"
                                   "uniform sampler2D uScene;\n"
                                   "uniform int uProfile;\n"
                                   "uniform vec2 uResolution;\n"
                                   "out vec4 fragColor;\n"
                                   "void main() {\n"
                                   "    vec2 uv = vTexCoord;\n"
                                   "    vec3 color;\n"
                                   "    if (uProfile == 1) {\n"
                                   "        vec2 lowRes = vec2(320.0, 240.0);\n"
                                   "        vec2 pixelUV = floor(uv * lowRes) / lowRes;\n"
                                   "        color = texture(uScene, pixelUV).rgb;\n"
                                   "        ivec2 px = ivec2(gl_FragCoord.xy);\n"
                                   "        float bayer[16] = float[16](0.0/16.0, 8.0/16.0, 2.0/16.0, 10.0/16.0,\n"
                                   "                                    12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0,\n"
                                   "                                    3.0/16.0, 11.0/16.0, 1.0/16.0, 9.0/16.0,\n"
                                   "                                    15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0);\n"
                                   "        float dither = (bayer[(px.y % 4) * 4 + (px.x % 4)] - 0.5) / 24.0;\n"
                                   "        color = floor((color + dither) * 32.0 + 0.5) / 32.0;\n"
                                   "        float lum = dot(color, vec3(0.299, 0.587, 0.114));\n"
                                   "        color = mix(vec3(lum), color, 0.8);\n"
                                   "    } else if (uProfile == 2) {\n"
                                   "        vec2 lowRes = vec2(320.0, 240.0);\n"
                                   "        vec2 pixelUV = floor(uv * lowRes + 0.5) / lowRes;\n"
                                   "        color = texture(uScene, pixelUV).rgb;\n"
                                   "        color *= vec3(1.05, 1.0, 0.92);\n"
                                   "        color = (color - 0.5) * 1.08 + 0.5;\n"
                                   "        color = clamp(color, 0.0, 1.0);\n"
                                   "    } else if (uProfile == 3) {\n"
                                   "        vec2 lowRes = vec2(320.0, 200.0);\n"
                                   "        vec2 pixelUV = floor(uv * lowRes) / lowRes;\n"
                                   "        color = texture(uScene, pixelUV).rgb;\n"
                                   "        ivec2 px = ivec2(gl_FragCoord.xy);\n"
                                   "        float bayer[16] = float[16](0.0/16.0, 8.0/16.0, 2.0/16.0, 10.0/16.0,\n"
                                   "                                    12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0,\n"
                                   "                                    3.0/16.0, 11.0/16.0, 1.0/16.0, 9.0/16.0,\n"
                                   "                                    15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0);\n"
                                   "        float dither = (bayer[(px.y % 4) * 4 + (px.x % 4)] - 0.5) / 5.0;\n"
                                   "        color = floor((color + dither) * 6.0 + 0.5) / 6.0;\n"
                                   "        float lum = dot(color, vec3(0.299, 0.587, 0.114));\n"
                                   "        color = mix(vec3(lum), color, 0.65);\n"
                                   "        color *= vec3(1.08, 0.98, 0.88);\n"
                                   "    } else if (uProfile == 4) {\n"
                                   "        vec2 lowRes = vec2(256.0, 224.0);\n"
                                   "        vec2 pixelUV = floor(uv * lowRes) / lowRes;\n"
                                   "        color = texture(uScene, pixelUV).rgb;\n"
                                   "        color = floor(color * 32.0 + 0.5) / 32.0;\n"
                                   "        int row = int(gl_FragCoord.y);\n"
                                   "        if ((row & 1) == 0) color *= 0.8;\n"
                                   "        color *= vec3(0.93, 0.96, 1.08);\n"
                                   "    } else {\n"
                                   "        color = texture(uScene, uv).rgb;\n"
                                   "    }\n"
                                   "    fragColor = vec4(max(color, 0.0), 1.0);\n"
                                   "}\n";

static const char k_ssao_frag[] = "in vec2 vTexCoord;\n"
                                  "uniform sampler2D uScene;\n"
                                  "uniform sampler2D uDepth;\n"
                                  "uniform vec2 uTexelSize;\n"
                                  "uniform float uNear;\n"
                                  "uniform float uFar;\n"
                                  "out vec4 fragColor;\n"
                                  "float linearizeDepth(float d) {\n"
                                  "    float z = d * 2.0 - 1.0;\n"
                                  "    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));\n"
                                  "}\n"
                                  "void main() {\n"
                                  "    float rawDepth = texture(uDepth, vTexCoord).r;\n"
                                  "    vec3 color = texture(uScene, vTexCoord).rgb;\n"
                                  "    if (rawDepth > 0.999) { fragColor = vec4(color, 1.0); return; }\n"
                                  "    float depth = linearizeDepth(rawDepth);\n"
                                  "    /* Scattered sample offsets (avoids grid banding). */\n"
                                  "    const vec2 offsets[8] = vec2[8](\n"
                                  "        vec2(-0.94, -0.34), vec2( 0.76,  0.65),\n"
                                  "        vec2(-0.09, -0.93), vec2( 0.97, -0.22),\n"
                                  "        vec2(-0.71,  0.71), vec2( 0.33,  0.94),\n"
                                  "        vec2( 0.52, -0.85), vec2(-0.86,  0.12)\n"
                                  "    );\n"
                                  "    float radius = clamp(1.0 / depth, 1.0, 8.0);\n"
                                  "    float ao = 0.0;\n"
                                  "    for (int i = 0; i < 8; i++) {\n"
                                  "        vec2 sampleUV = vTexCoord + offsets[i] * uTexelSize * radius;\n"
                                  "        float sd = linearizeDepth(texture(uDepth, sampleUV).r);\n"
                                  "        float diff = depth - sd;\n"
                                  "        if (diff > 0.1 && diff < 2.0) ao += 1.0;\n"
                                  "    }\n"
                                  "    ao /= 8.0;\n"
                                  "    color *= (1.0 - ao * 0.35);\n"
                                  "    fragColor = vec4(color, 1.0);\n"
                                  "}\n";

static GLuint compile_shader(sdl3d_gl_funcs *gl, GLenum type, const char *version, const char *body)
{
    GLuint s = gl->CreateShader(type);
    const char *srcs[2] = {version, body};
    gl->ShaderSource(s, 2, srcs, NULL);
    gl->CompileShader(s);

    GLint ok = 0;
    gl->GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        gl->GetShaderInfoLog(s, sizeof(buf), NULL, buf);
        SDL_Log("SDL3D GL shader compile error: %s", buf);
        gl->DeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint compile_shader_multi(sdl3d_gl_funcs *gl, GLenum type, int count, const char **srcs)
{
    GLuint s = gl->CreateShader(type);
    gl->ShaderSource(s, count, srcs, NULL);
    gl->CompileShader(s);

    GLint ok = 0;
    gl->GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        gl->GetShaderInfoLog(s, sizeof(buf), NULL, buf);
        SDL_Log("SDL3D GL shader compile error: %s", buf);
        gl->DeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(sdl3d_gl_funcs *gl, GLuint vert, GLuint frag)
{
    GLuint p = gl->CreateProgram();
    gl->AttachShader(p, vert);
    gl->AttachShader(p, frag);
    gl->LinkProgram(p);

    GLint ok = 0;
    gl->GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        gl->GetProgramInfoLog(p, sizeof(buf), NULL, buf);
        SDL_Log("SDL3D GL program link error: %s", buf);
        gl->DeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint build_program(sdl3d_gl_funcs *gl, const char *version, const char *vert_body, const char *frag_body)
{
    GLuint vs = compile_shader(gl, GL_VERTEX_SHADER, version, vert_body);
    if (!vs)
        return 0;
    GLuint fs = compile_shader(gl, GL_FRAGMENT_SHADER, version, frag_body);
    if (!fs)
    {
        gl->DeleteShader(vs);
        return 0;
    }
    GLuint prog = link_program(gl, vs, fs);
    gl->DeleteShader(vs);
    gl->DeleteShader(fs);
    return prog;
}

/* ------------------------------------------------------------------ */
/* Texture cache                                                       */
/* ------------------------------------------------------------------ */

static GLuint tex_cache_lookup(sdl3d_gl_context *ctx, const sdl3d_texture2d *tex)
{
    for (sdl3d_gl_tex_entry *e = ctx->tex_cache; e; e = e->next)
    {
        if (e->key == tex)
            return e->gl_tex;
    }
    return 0;
}

static GLuint tex_cache_upload(sdl3d_gl_context *ctx, const sdl3d_texture2d *tex)
{
    sdl3d_gl_funcs *gl = &ctx->gl;
    GLuint id;
    gl->GenTextures(1, &id);
    gl->BindTexture(GL_TEXTURE_2D, id);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex->pixels);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    sdl3d_gl_tex_entry *entry = SDL_calloc(1, sizeof(*entry));
    entry->key = tex;
    entry->gl_tex = id;
    entry->next = ctx->tex_cache;
    ctx->tex_cache = entry;
    return id;
}

static GLuint resolve_texture(sdl3d_gl_context *ctx, const sdl3d_texture2d *tex)
{
    if (!tex)
        return ctx->white_texture;
    GLuint id = tex_cache_lookup(ctx, tex);
    return id ? id : tex_cache_upload(ctx, tex);
}

static void tex_cache_free(sdl3d_gl_context *ctx)
{
    sdl3d_gl_funcs *gl = &ctx->gl;
    sdl3d_gl_tex_entry *e = ctx->tex_cache;
    while (e)
    {
        sdl3d_gl_tex_entry *next = e->next;
        gl->DeleteTextures(1, &e->gl_tex);
        SDL_free(e);
        e = next;
    }
    ctx->tex_cache = NULL;
}

/* ------------------------------------------------------------------ */
/* White color buffer                                                  */
/* ------------------------------------------------------------------ */

static const float *ensure_white_colors(sdl3d_gl_context *ctx, int vertex_count)
{
    int need = vertex_count * 4;
    if (need <= ctx->white_colors_capacity)
        return ctx->white_colors;
    SDL_free(ctx->white_colors);
    ctx->white_colors = SDL_malloc((size_t)need * sizeof(float));
    ctx->white_colors_capacity = need;
    for (int i = 0; i < need; i++)
        ctx->white_colors[i] = 1.0f;
    return ctx->white_colors;
}

/* ------------------------------------------------------------------ */
/* Draw list helpers                                                   */
/* ------------------------------------------------------------------ */

static void free_draw_list(sdl3d_gl_context *ctx)
{
    for (int i = 0; i < ctx->draw_count; i++)
    {
        sdl3d_draw_entry *e = &ctx->draw_list[i];
        SDL_free(e->positions);
        SDL_free(e->normals);
        SDL_free(e->uvs);
        SDL_free(e->colors);
        SDL_free(e->indices);
    }
    ctx->draw_count = 0;
}

static sdl3d_draw_entry *append_draw_entry(sdl3d_gl_context *ctx)
{
    if (ctx->draw_count == ctx->draw_capacity)
    {
        int cap = ctx->draw_capacity ? ctx->draw_capacity * 2 : 64;
        sdl3d_draw_entry *buf = SDL_realloc(ctx->draw_list, (size_t)cap * sizeof(sdl3d_draw_entry));
        if (!buf)
            return NULL;
        ctx->draw_list = buf;
        ctx->draw_capacity = cap;
    }
    sdl3d_draw_entry *e = &ctx->draw_list[ctx->draw_count++];
    SDL_memset(e, 0, sizeof(*e));
    return e;
}

/* Check for potential z-fighting: warn if two lit draw entries have
 * overlapping axis-aligned bounding boxes with coplanar faces. */
static void check_z_fighting(sdl3d_gl_context *ctx, const sdl3d_draw_entry *new_entry)
{
    if (!new_entry->lit || new_entry->vertex_count == 0)
        return;

    /* Compute AABB of the new entry from its vertex positions + model matrix translation. */
    float tx = new_entry->model_matrix[12];
    float ty = new_entry->model_matrix[13];
    float tz = new_entry->model_matrix[14];

    float min_x = 1e30f, max_x = -1e30f;
    float min_y = 1e30f, max_y = -1e30f;
    float min_z = 1e30f, max_z = -1e30f;
    for (int i = 0; i < new_entry->vertex_count; i++)
    {
        float x = new_entry->positions[i * 3 + 0] + tx;
        float y = new_entry->positions[i * 3 + 1] + ty;
        float z = new_entry->positions[i * 3 + 2] + tz;
        if (x < min_x)
            min_x = x;
        if (x > max_x)
            max_x = x;
        if (y < min_y)
            min_y = y;
        if (y > max_y)
            max_y = y;
        if (z < min_z)
            min_z = z;
        if (z > max_z)
            max_z = z;
    }

    /* Compare against all previous lit entries. */
    for (int i = 0; i < ctx->draw_count - 1; i++)
    {
        const sdl3d_draw_entry *other = &ctx->draw_list[i];
        if (!other->lit || other->vertex_count == 0)
            continue;

        float otx = other->model_matrix[12];
        float oty = other->model_matrix[13];
        float otz = other->model_matrix[14];

        float o_min_x = 1e30f, o_max_x = -1e30f;
        float o_min_y = 1e30f, o_max_y = -1e30f;
        float o_min_z = 1e30f, o_max_z = -1e30f;
        for (int j = 0; j < other->vertex_count; j++)
        {
            float x = other->positions[j * 3 + 0] + otx;
            float y = other->positions[j * 3 + 1] + oty;
            float z = other->positions[j * 3 + 2] + otz;
            if (x < o_min_x)
                o_min_x = x;
            if (x > o_max_x)
                o_max_x = x;
            if (y < o_min_y)
                o_min_y = y;
            if (y > o_max_y)
                o_max_y = y;
            if (z < o_min_z)
                o_min_z = z;
            if (z > o_max_z)
                o_max_z = z;
        }

        /* Check if AABBs overlap. */
        if (max_x < o_min_x || min_x > o_max_x)
            continue;
        if (max_y < o_min_y || min_y > o_max_y)
            continue;
        if (max_z < o_min_z || min_z > o_max_z)
            continue;

        /* Z-fighting: two thin surfaces overlapping on the same thin axis.
         * A "thin" axis is one where the extent is < 0.5 units (wall thickness).
         * Both entries must be thin on the same axis AND overlap on that axis. */
        float eps = 0.02f;
        float thin = 0.5f;
        float min_thin = 0.05f; /* ignore zero-thickness geometry (skybox, planes) */
        bool zfight = false;
        float ext_x = max_x - min_x, ext_y = max_y - min_y, ext_z = max_z - min_z;
        float o_ext_x = o_max_x - o_min_x, o_ext_y = o_max_y - o_min_y, o_ext_z = o_max_z - o_min_z;

        /* Both thin on X and overlapping on X */
        if (ext_x > min_thin && ext_x < thin && o_ext_x > min_thin && o_ext_x < thin &&
            SDL_fabsf((min_x + max_x) * 0.5f - (o_min_x + o_max_x) * 0.5f) < eps)
            zfight = true;
        /* Both thin on Y and overlapping on Y */
        if (ext_y > min_thin && ext_y < thin && o_ext_y > min_thin && o_ext_y < thin &&
            SDL_fabsf((min_y + max_y) * 0.5f - (o_min_y + o_max_y) * 0.5f) < eps)
            zfight = true;
        /* Both thin on Z and overlapping on Z */
        if (ext_z > min_thin && ext_z < thin && o_ext_z > min_thin && o_ext_z < thin &&
            SDL_fabsf((min_z + max_z) * 0.5f - (o_min_z + o_max_z) * 0.5f) < eps)
            zfight = true;

        if (zfight)
        {
            char msg[512];
            SDL_snprintf(msg, sizeof(msg),
                         "Z-FIGHTING DETECTED\n\n"
                         "Two draw calls have overlapping coplanar geometry:\n\n"
                         "Entry A: (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n"
                         "Entry B: (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n\n"
                         "Fix: offset one surface by at least 0.05 units.",
                         min_x, min_y, min_z, max_x, max_y, max_z, o_min_x, o_min_y, o_min_z, o_max_x, o_max_y,
                         o_max_z);
            if (ctx->current_ctx && ctx->current_ctx->zfight_callback)
            {
                ctx->current_ctx->zfight_callback(msg, ctx->current_ctx->zfight_userdata);
            }
        }
    }
}

static float *copy_floats(const float *src, size_t count)
{
    if (!src || count == 0)
        return NULL;
    float *dst = SDL_malloc(count * sizeof(float));
    if (dst)
        SDL_memcpy(dst, src, count * sizeof(float));
    return dst;
}

static unsigned int *copy_indices(const unsigned int *src, size_t count)
{
    if (!src || count == 0)
        return NULL;
    unsigned int *dst = SDL_malloc(count * sizeof(unsigned int));
    if (dst)
        SDL_memcpy(dst, src, count * sizeof(unsigned int));
    return dst;
}

/* ------------------------------------------------------------------ */
/* 4x4 matrix inverse (Cramer's rule, column-major float[16])          */
/* ------------------------------------------------------------------ */

static bool mat4_inverse(const float *m, float *out)
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f)
        return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * det;
    return true;
}

/* ------------------------------------------------------------------ */
/* CSM cascade computation                                             */
/* ------------------------------------------------------------------ */

static void compute_csm_matrices(sdl3d_gl_context *ctx, const sdl3d_render_context *rc)
{
    float near_p = rc->near_plane;
    float far_p = rc->far_plane;
    /* Use the camera's far plane so shadows cover the entire visible scene. */

    /* Practical split scheme: lambda=0.5 blend of log and uniform. */
    for (int i = 0; i < SDL3D_CSM_CASCADE_COUNT; i++)
    {
        float p = (float)(i + 1) / (float)SDL3D_CSM_CASCADE_COUNT;
        float log_split = near_p * SDL_powf(far_p / near_p, p);
        float uni_split = near_p + (far_p - near_p) * p;
        ctx->csm_split_depths[i] = 0.5f * log_split + 0.5f * uni_split;
    }

    /* Extract FOV and aspect from the projection matrix (column-major).
     * proj[5] = m[1][1] = f = 1/tan(fovy/2)
     * proj[0] = m[0][0] = f/aspect                                    */
    float f = rc->projection.m[5];
    float aspect = f / rc->projection.m[0];
    float fovy = 2.0f * SDL_atanf(1.0f / f);

    /* Light direction (normalized). */
    sdl3d_vec3 light_dir = sdl3d_vec3_normalize(rc->lights[0].direction);

    for (int i = 0; i < SDL3D_CSM_CASCADE_COUNT; i++)
    {
        float c_near = (i == 0) ? rc->near_plane : ctx->csm_split_depths[i - 1];
        float c_far = ctx->csm_split_depths[i];

        /* Build sub-frustum projection and invert (proj * view). */
        sdl3d_mat4 sub_proj;
        sdl3d_mat4_perspective(fovy, aspect, c_near, c_far, &sub_proj);
        sdl3d_mat4 pv = sdl3d_mat4_multiply(sub_proj, rc->view);

        float inv_pv[16];
        mat4_inverse(pv.m, inv_pv);

        /* 8 NDC corners -> world space. */
        float corners[8][3];
        int ci = 0;
        for (int z = 0; z < 2; z++)
        {
            for (int y = 0; y < 2; y++)
            {
                for (int x = 0; x < 2; x++)
                {
                    sdl3d_vec4 ndc =
                        sdl3d_vec4_make((float)x * 2.0f - 1.0f, (float)y * 2.0f - 1.0f, (float)z * 2.0f - 1.0f, 1.0f);
                    sdl3d_mat4 inv_m;
                    SDL_memcpy(inv_m.m, inv_pv, sizeof(inv_pv));
                    sdl3d_vec4 ws = sdl3d_mat4_transform_vec4(inv_m, ndc);
                    corners[ci][0] = ws.x / ws.w;
                    corners[ci][1] = ws.y / ws.w;
                    corners[ci][2] = ws.z / ws.w;
                    ci++;
                }
            }
        }

        /* Frustum center. */
        float cx = 0, cy = 0, cz = 0;
        for (int j = 0; j < 8; j++)
        {
            cx += corners[j][0];
            cy += corners[j][1];
            cz += corners[j][2];
        }
        cx /= 8.0f;
        cy /= 8.0f;
        cz /= 8.0f;

        /* Light view: lookAt(center + lightDir, center, up). */
        sdl3d_vec3 center = sdl3d_vec3_make(cx, cy, cz);
        sdl3d_vec3 eye = sdl3d_vec3_add(center, light_dir);
        sdl3d_vec3 up = sdl3d_vec3_make(0, 1, 0);
        /* If light is nearly vertical, use alternative up. */
        if (SDL_fabsf(sdl3d_vec3_dot(sdl3d_vec3_normalize(light_dir), up)) > 0.99f)
            up = sdl3d_vec3_make(1, 0, 0);

        sdl3d_mat4 light_view;
        sdl3d_mat4_look_at(eye, center, up, &light_view);

        /* Transform corners to light view space, find AABB. */
        float minX = 1e30f, maxX = -1e30f;
        float minY = 1e30f, maxY = -1e30f;
        float minZ = 1e30f, maxZ = -1e30f;
        for (int j = 0; j < 8; j++)
        {
            sdl3d_vec4 lv = sdl3d_mat4_transform_vec4(
                light_view, sdl3d_vec4_make(corners[j][0], corners[j][1], corners[j][2], 1.0f));
            if (lv.x < minX)
                minX = lv.x;
            if (lv.x > maxX)
                maxX = lv.x;
            if (lv.y < minY)
                minY = lv.y;
            if (lv.y > maxY)
                maxY = lv.y;
            if (lv.z < minZ)
                minZ = lv.z;
            if (lv.z > maxZ)
                maxZ = lv.z;
        }

        /* Extend Z range to catch shadow casters behind the frustum. */
        float z_mult = 10.0f;
        if (minZ < 0)
            minZ *= z_mult;
        else
            minZ /= z_mult;
        if (maxZ < 0)
            maxZ /= z_mult;
        else
            maxZ *= z_mult;

        sdl3d_mat4 light_proj;
        sdl3d_mat4_orthographic(minX, maxX, minY, maxY, minZ, maxZ, &light_proj);

        sdl3d_mat4 lp_lv = sdl3d_mat4_multiply(light_proj, light_view);
        SDL_memcpy(ctx->csm_light_vp[i], lp_lv.m, 16 * sizeof(float));
    }

    /* Use cascade 0 VP as the main shadow VP for the fragment shader (layer 0).
     * NOTE: ctx->shadow_light_vp is already set from sdl3d_enable_shadow in gl_clear.
     * We keep that original VP as fallback. CSM fragment path is now active. */
    ctx->csm_fragment_enabled = true;
}

static void compute_point_shadow_matrices(sdl3d_gl_context *ctx, const sdl3d_render_context *rc)
{
    ctx->point_shadow_count = 0;
    for (int i = 0; i < rc->light_count && ctx->point_shadow_count < SDL3D_MAX_POINT_SHADOWS; i++)
    {
        if (rc->lights[i].type != SDL3D_LIGHT_POINT)
            continue;
        int s = ctx->point_shadow_count;
        ctx->point_shadow_light_index[s] = i;

        const sdl3d_light *l = &rc->lights[i];
        float far = l->range > 0 ? l->range : 25.0f;
        ctx->point_shadow_far_plane[s] = far;

        sdl3d_mat4 proj;
        sdl3d_mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f, far, &proj);

        sdl3d_vec3 pos = l->position;
        sdl3d_vec3 targets[6] = {{pos.x + 1, pos.y, pos.z}, {pos.x - 1, pos.y, pos.z}, {pos.x, pos.y + 1, pos.z},
                                 {pos.x, pos.y - 1, pos.z}, {pos.x, pos.y, pos.z + 1}, {pos.x, pos.y, pos.z - 1}};
        sdl3d_vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
        for (int f = 0; f < 6; f++)
        {
            sdl3d_mat4 view;
            sdl3d_mat4_look_at(pos, targets[f], ups[f], &view);
            sdl3d_mat4 vp = sdl3d_mat4_multiply(proj, view);
            SDL_memcpy(ctx->point_shadow_vp[s][f], vp.m, 16 * sizeof(float));
        }
        ctx->point_shadow_count++;
    }
}

/* ------------------------------------------------------------------ */
/* Draw list replay                                                    */
/* ------------------------------------------------------------------ */

static void replay_draw_list_shadow(sdl3d_gl_context *ctx)
{
    sdl3d_gl_funcs *gl = &ctx->gl;
    gl->UseProgram(ctx->shadow_program);
    gl->BindVertexArray(ctx->shadow_vao);

    /* lightVP uniform is set by the caller per cascade. */

    for (int i = 0; i < ctx->draw_count; i++)
    {
        sdl3d_draw_entry *e = &ctx->draw_list[i];
        if (!e->lit)
            continue;
        /* Only shadow-cast entries with a non-identity model matrix
         * (placed models / dynamic actors). Shape primitives have
         * identity model matrix with position baked into vertices. */
        if (e->model_matrix[12] == 0.0f && e->model_matrix[13] == 0.0f && e->model_matrix[14] == 0.0f &&
            e->model_matrix[0] == 1.0f && e->model_matrix[5] == 1.0f && e->model_matrix[10] == 1.0f)
            continue;

        gl->UniformMatrix4fv(ctx->shadow_model_loc, 1, GL_FALSE, e->model_matrix);

        gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                       GL_DYNAMIC_DRAW);

        if (e->indices && e->index_count > 0)
        {
            gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->shadow_ebo);
            gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)),
                           e->indices, GL_DYNAMIC_DRAW);
            gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
        }
        else
        {
            gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
        }
    }
    gl->BindVertexArray(0);
}

static void replay_draw_list_geometry(sdl3d_gl_context *ctx)
{
    sdl3d_gl_funcs *gl = &ctx->gl;

    for (int i = 0; i < ctx->draw_count; i++)
    {
        sdl3d_draw_entry *e = &ctx->draw_list[i];
        GLuint tex = resolve_texture(ctx, e->texture);

        if (e->lit)
        {
            gl->UseProgram(ctx->pbr_program);
            gl->UniformMatrix4fv(ctx->pbr_model_loc, 1, GL_FALSE, e->model_matrix);
            gl->UniformMatrix3fv(ctx->pbr_normal_matrix_loc, 1, GL_FALSE, e->normal_matrix);
            gl->Uniform4f(ctx->pbr_tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);
            gl->Uniform1f(ctx->pbr_metallic_loc, e->metallic);
            gl->Uniform1f(ctx->pbr_roughness_loc, e->roughness);
            gl->Uniform3f(ctx->pbr_emissive_loc, e->emissive[0], e->emissive[1], e->emissive[2]);

            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, tex);
            gl->Uniform1i(ctx->pbr_texture_loc, 0);
            gl->Uniform1i(ctx->pbr_has_texture_loc, e->texture ? 1 : 0);

            /* Shadow uniforms — always bind the texture array to prevent
             * undefined sampler behavior on some drivers. */
            gl->ActiveTexture(GL_TEXTURE0 + 1);
            gl->BindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadow_depth_tex);
            gl->Uniform1i(ctx->pbr_shadow_map_loc, 1);
            if (ctx->shadow_depth_tex && ctx->shadow_bias > 0.0f)
            {
                gl->UniformMatrix4fv(ctx->pbr_shadow_vp_loc, 1, GL_FALSE, ctx->shadow_light_vp);
                gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 0); /* CSM disabled */
                gl->Uniform1f(ctx->pbr_shadow_bias_loc, ctx->shadow_bias);
                if (ctx->csm_fragment_enabled)
                {
                    gl->UniformMatrix4fv(ctx->pbr_csm_vp_loc[0], 1, GL_FALSE, ctx->shadow_light_vp);
                    for (int c = 1; c < 4; c++)
                        gl->UniformMatrix4fv(ctx->pbr_csm_vp_loc[c], 1, GL_FALSE, ctx->csm_light_vp[c]);
                    gl->Uniform1fv(ctx->pbr_csm_splits_loc, 4, ctx->csm_split_depths);
                    gl->UniformMatrix4fv(ctx->pbr_view_matrix_loc, 1, GL_FALSE, ctx->current_ctx->view.m);
                    gl->Uniform1i(ctx->pbr_csm_enabled_loc, 1);
                }
                else
                {
                    gl->Uniform1i(ctx->pbr_csm_enabled_loc, 0);
                }
            }
            else
            {
                gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 0);
                gl->Uniform1i(ctx->pbr_csm_enabled_loc, 0);
            }
            gl->ActiveTexture(GL_TEXTURE0);

            /* Point shadow uniforms. */
            for (int ps = 0; ps < SDL3D_MAX_POINT_SHADOWS; ps++)
            {
                gl->ActiveTexture(GL_TEXTURE0 + 2 + (GLenum)ps);
                gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->point_shadow_cubemap[ps]);
                gl->Uniform1i(ctx->pbr_point_shadow_map_loc[ps], 2 + ps);
                if (ps < ctx->point_shadow_count)
                {
                    const sdl3d_light *pl = &ctx->current_ctx->lights[ctx->point_shadow_light_index[ps]];
                    gl->Uniform3f(ctx->pbr_point_shadow_light_pos_loc[ps], pl->position.x, pl->position.y,
                                  pl->position.z);
                    gl->Uniform1f(ctx->pbr_point_shadow_far_loc[ps], ctx->point_shadow_far_plane[ps]);
                }
            }
            gl->Uniform1i(ctx->pbr_point_shadow_count_loc, ctx->point_shadow_count);
            gl->ActiveTexture(GL_TEXTURE0);

            gl->BindVertexArray(ctx->lit_vao);
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_position_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                           GL_DYNAMIC_DRAW);
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_normal_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->normals,
                           GL_DYNAMIC_DRAW);
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_uv_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 2 * sizeof(float)), e->uvs,
                           GL_DYNAMIC_DRAW);
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_color_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 4 * sizeof(float)), e->colors,
                           GL_DYNAMIC_DRAW);

            if (e->indices && e->index_count > 0)
            {
                gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->lit_ebo);
                gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)),
                               e->indices, GL_DYNAMIC_DRAW);
                gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
            }
            else
            {
                gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
            }
        }
        else
        {
            gl->UseProgram(ctx->unlit_program);
            gl->UniformMatrix4fv(ctx->unlit_mvp_loc, 1, GL_FALSE, e->mvp);
            gl->Uniform4f(ctx->unlit_tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);

            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, tex);
            gl->Uniform1i(ctx->unlit_texture_loc, 0);
            gl->Uniform1i(ctx->unlit_has_texture_loc, e->texture ? 1 : 0);

            gl->BindVertexArray(ctx->unlit_vao);
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_position_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                           GL_DYNAMIC_DRAW);
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_uv_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 2 * sizeof(float)), e->uvs,
                           GL_DYNAMIC_DRAW);
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_color_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 4 * sizeof(float)), e->colors,
                           GL_DYNAMIC_DRAW);

            if (e->indices && e->index_count > 0)
            {
                gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->unlit_ebo);
                gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)),
                               e->indices, GL_DYNAMIC_DRAW);
                gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
            }
            else
            {
                gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* FBO helpers                                                         */
/* ------------------------------------------------------------------ */

static bool create_fbo(sdl3d_gl_context *ctx, int w, int h)
{
    sdl3d_gl_funcs *gl = &ctx->gl;

    gl->GenTextures(1, &ctx->fbo_color);
    gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl->GenTextures(1, &ctx->fbo_depth);
    gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_depth);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    gl->GenFramebuffers(1, &ctx->fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->fbo_color, 0);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->fbo_depth, 0);

    GLenum status = gl->CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("SDL3D GL FBO incomplete: 0x%x", status);
        return false;
    }

    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    ctx->logical_w = w;
    ctx->logical_h = h;
    return true;
}

/* ------------------------------------------------------------------ */
/* Lazy UBO upload                                                     */
/* ------------------------------------------------------------------ */

static void flush_scene_ubo(sdl3d_gl_context *ctx)
{
    if (!ctx->ubo_dirty)
        return;
    ctx->ubo_dirty = false;

    sdl3d_render_context *rc = ctx->current_ctx;
    sdl3d_scene_ubo_data ubo;
    SDL_memset(&ubo, 0, sizeof(ubo));

    SDL_memcpy(ubo.view_projection, rc->view_projection.m, 16 * sizeof(float));

    /* Extract camera position from view matrix inverse translation. */
    const sdl3d_mat4 v = rc->view;
    ubo.camera_pos[0] = -(v.m[0] * v.m[12] + v.m[1] * v.m[13] + v.m[2] * v.m[14]);
    ubo.camera_pos[1] = -(v.m[4] * v.m[12] + v.m[5] * v.m[13] + v.m[6] * v.m[14]);
    ubo.camera_pos[2] = -(v.m[8] * v.m[12] + v.m[9] * v.m[13] + v.m[10] * v.m[14]);

    ubo.ambient[0] = rc->ambient[0];
    ubo.ambient[1] = rc->ambient[1];
    ubo.ambient[2] = rc->ambient[2];

    int lc = rc->light_count;
    if (lc > 8)
        lc = 8;
    ubo.light_count = lc;
    for (int i = 0; i < lc; i++)
    {
        const sdl3d_light *l = &rc->lights[i];
        ubo.lights[i].type = (int)l->type;
        ubo.lights[i].position[0] = l->position.x;
        ubo.lights[i].position[1] = l->position.y;
        ubo.lights[i].position[2] = l->position.z;
        ubo.lights[i].direction[0] = l->direction.x;
        ubo.lights[i].direction[1] = l->direction.y;
        ubo.lights[i].direction[2] = l->direction.z;
        ubo.lights[i].color[0] = l->color[0];
        ubo.lights[i].color[1] = l->color[1];
        ubo.lights[i].color[2] = l->color[2];
        ubo.lights[i].intensity = l->intensity;
        ubo.lights[i].range = l->range;
        ubo.lights[i].inner_cutoff = l->inner_cutoff;
        ubo.lights[i].outer_cutoff = l->outer_cutoff;
    }

    ubo.fog_mode = (int)rc->fog.mode;
    ubo.fog_start = rc->fog.start;
    ubo.fog_end = rc->fog.end;
    ubo.fog_density = rc->fog.density;
    ubo.fog_color[0] = rc->fog.color[0];
    ubo.fog_color[1] = rc->fog.color[1];
    ubo.fog_color[2] = rc->fog.color[2];
    ubo.tonemap_mode = (int)rc->tonemap_mode;

    sdl3d_gl_funcs *gl = &ctx->gl;
    gl->BindBuffer(GL_UNIFORM_BUFFER, ctx->scene_ubo);
    gl->BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(ubo), &ubo, GL_DYNAMIC_DRAW);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->scene_ubo);

    if (ctx->frame_index <= 2)
    {
        SDL_Log("SDL3D GL UBO flush frame=%llu lights=%d vp[0]=%.3f vp[5]=%.3f cam=(%.1f,%.1f,%.1f)",
                (unsigned long long)ctx->frame_index, ubo.light_count, ubo.view_projection[0], ubo.view_projection[5],
                ubo.camera_pos[0], ubo.camera_pos[1], ubo.camera_pos[2]);
    }
}

/* ------------------------------------------------------------------ */
/* Create / Destroy                                                    */
/* ------------------------------------------------------------------ */

sdl3d_gl_context *sdl3d_gl_create(SDL_Window *window, int width, int height)
{
    /* Request GL 3.3 Core. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx)
    {
        SDL_Log("SDL3D GL: failed to create context: %s", SDL_GetError());
        return NULL;
    }
    SDL_GL_MakeCurrent(window, glctx);

    /* Adaptive vsync: allows tearing only when a frame is late,
     * preventing stutter. Falls back to regular vsync if unsupported. */
    if (!SDL_GL_SetSwapInterval(-1))
    {
        SDL_GL_SetSwapInterval(1);
    }

    sdl3d_gl_context *ctx = SDL_calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        SDL_GL_DestroyContext(glctx);
        return NULL;
    }

    ctx->window = window;
    ctx->gl_context = glctx;
    ctx->frame_index = 1;

    if (!sdl3d_gl_load_funcs(&ctx->gl))
    {
        SDL_Log("SDL3D GL: failed to load GL functions");
        SDL_GL_DestroyContext(glctx);
        SDL_free(ctx);
        return NULL;
    }

    sdl3d_gl_funcs *gl = &ctx->gl;

    /* Detect ES. */
    ctx->is_es = false;

    const char *version_prefix = ctx->is_es ? "#version 300 es\nprecision highp float;\n" : "#version 330\n";

    /* Compile shader programs. */
    /* PBR frag is split into two arrays to stay under C99 string length limits. */
    {
        GLuint vs = compile_shader(gl, GL_VERTEX_SHADER, version_prefix, k_pbr_vert);
        const char *frag_srcs[4] = {version_prefix, k_pbr_frag_decl, k_pbr_frag_main, k_pbr_frag_post};
        GLuint fs = vs ? compile_shader_multi(gl, GL_FRAGMENT_SHADER, 4, frag_srcs) : 0;
        ctx->pbr_program = (vs && fs) ? link_program(gl, vs, fs) : 0;
        if (vs)
            gl->DeleteShader(vs);
        if (fs)
            gl->DeleteShader(fs);
    }
    ctx->unlit_program = build_program(gl, version_prefix, k_unlit_vert, k_unlit_frag);
    ctx->copy_program = build_program(gl, version_prefix, k_fullscreen_vert, k_copy_frag);

    if (!ctx->pbr_program || !ctx->unlit_program || !ctx->copy_program)
    {
        SDL_Log("SDL3D GL: shader compilation failed");
        sdl3d_gl_destroy(ctx);
        return NULL;
    }

    /* PBR uniform locations. */
    ctx->pbr_model_loc = gl->GetUniformLocation(ctx->pbr_program, "uModel");
    ctx->pbr_normal_matrix_loc = gl->GetUniformLocation(ctx->pbr_program, "uNormalMatrix");
    ctx->pbr_texture_loc = gl->GetUniformLocation(ctx->pbr_program, "uTexture");
    ctx->pbr_has_texture_loc = gl->GetUniformLocation(ctx->pbr_program, "uHasTexture");
    ctx->pbr_tint_loc = gl->GetUniformLocation(ctx->pbr_program, "uTint");
    ctx->pbr_metallic_loc = gl->GetUniformLocation(ctx->pbr_program, "uMetallic");
    ctx->pbr_roughness_loc = gl->GetUniformLocation(ctx->pbr_program, "uRoughness");
    ctx->pbr_emissive_loc = gl->GetUniformLocation(ctx->pbr_program, "uEmissive");

    /* PBR shadow uniform locations. */
    ctx->pbr_shadow_map_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowMap");
    ctx->pbr_shadow_vp_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowVP");
    ctx->pbr_shadow_enabled_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowEnabled");
    ctx->pbr_shadow_bias_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowBias");

    /* CSM uniform locations. */
    {
        char name[32];
        for (int i = 0; i < 4; i++)
        {
            SDL_snprintf(name, sizeof(name), "uCSMVP[%d]", i);
            ctx->pbr_csm_vp_loc[i] = gl->GetUniformLocation(ctx->pbr_program, name);
        }
    }
    ctx->pbr_csm_splits_loc = gl->GetUniformLocation(ctx->pbr_program, "uCSMSplits");
    ctx->pbr_view_matrix_loc = gl->GetUniformLocation(ctx->pbr_program, "uViewMatrix");
    ctx->pbr_csm_enabled_loc = gl->GetUniformLocation(ctx->pbr_program, "uCSMEnabled");

    /* Point shadow PBR uniform locations. */
    {
        char name[64];
        for (int s = 0; s < SDL3D_MAX_POINT_SHADOWS; s++)
        {
            SDL_snprintf(name, sizeof(name), "uPointShadowMap[%d]", s);
            ctx->pbr_point_shadow_map_loc[s] = gl->GetUniformLocation(ctx->pbr_program, name);
            SDL_snprintf(name, sizeof(name), "uPointShadowLightPos[%d]", s);
            ctx->pbr_point_shadow_light_pos_loc[s] = gl->GetUniformLocation(ctx->pbr_program, name);
            SDL_snprintf(name, sizeof(name), "uPointShadowFar[%d]", s);
            ctx->pbr_point_shadow_far_loc[s] = gl->GetUniformLocation(ctx->pbr_program, name);
        }
    }
    ctx->pbr_point_shadow_count_loc = gl->GetUniformLocation(ctx->pbr_program, "uPointShadowCount");

    /* Unlit uniform locations. */
    ctx->unlit_mvp_loc = gl->GetUniformLocation(ctx->unlit_program, "uMVP");
    ctx->unlit_texture_loc = gl->GetUniformLocation(ctx->unlit_program, "uTexture");
    ctx->unlit_has_texture_loc = gl->GetUniformLocation(ctx->unlit_program, "uHasTexture");
    ctx->unlit_tint_loc = gl->GetUniformLocation(ctx->unlit_program, "uTint");

    /* Copy uniform locations. */
    ctx->copy_texture_loc = gl->GetUniformLocation(ctx->copy_program, "uScene");

    /* Scene UBO — create, allocate, bind to point 0. */
    gl->GenBuffers(1, &ctx->scene_ubo);
    gl->BindBuffer(GL_UNIFORM_BUFFER, ctx->scene_ubo);
    gl->BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(sdl3d_scene_ubo_data), NULL, GL_DYNAMIC_DRAW);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->scene_ubo);

    /* Bind SceneUBO block in PBR program to binding point 0. */
    GLuint block_idx = gl->GetUniformBlockIndex(ctx->pbr_program, "SceneUBO");
    if (block_idx != 0xFFFFFFFFu)
    {
        gl->UniformBlockBinding(ctx->pbr_program, block_idx, 0);
    }

    /* ---- Lit streaming VAO/VBOs ---- */
    gl->GenVertexArrays(1, &ctx->lit_vao);
    gl->BindVertexArray(ctx->lit_vao);

    gl->GenBuffers(1, &ctx->lit_position_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_position_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_normal_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_normal_vbo);
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_uv_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_uv_vbo);
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_color_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_color_vbo);
    gl->EnableVertexAttribArray(3);
    gl->VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_ebo);

    /* ---- Unlit streaming VAO/VBOs ---- */
    gl->GenVertexArrays(1, &ctx->unlit_vao);
    gl->BindVertexArray(ctx->unlit_vao);

    gl->GenBuffers(1, &ctx->unlit_position_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_position_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->unlit_uv_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_uv_vbo);
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->unlit_color_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_color_vbo);
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->unlit_ebo);

    /* ---- Fullscreen VAO (empty, vertex ID driven) ---- */
    gl->GenVertexArrays(1, &ctx->fullscreen_vao);

    /* Shadow map: 2048x2048 depth-only FBO with its own VAO. */
    gl->GenFramebuffers(1, &ctx->shadow_fbo);
    gl->GenTextures(1, &ctx->shadow_depth_tex);
    gl->BindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadow_depth_tex);
    gl->TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, 2048, 2048, 4, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    {
        float border_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
        gl->TexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border_color);
    }
    /* Attach layer 0 for now — CSM will render to each layer separately. */
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    gl->FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, ctx->shadow_depth_tex, 0, 0);
    gl->DrawBuffer(GL_NONE);
    gl->ReadBuffer(GL_NONE);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo); /* restore */

    /* Shadow VAO: position only (simple depth shader). */
    gl->GenVertexArrays(1, &ctx->shadow_vao);
    gl->BindVertexArray(ctx->shadow_vao);
    gl->GenBuffers(1, &ctx->shadow_position_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    gl->GenBuffers(1, &ctx->shadow_ebo);
    gl->BindVertexArray(0);

    /* Compile shadow program. */
    ctx->shadow_program = build_program(gl, version_prefix, k_shadow_vert, k_shadow_frag);
    if (ctx->shadow_program)
    {
        ctx->shadow_light_vp_loc = gl->GetUniformLocation(ctx->shadow_program, "uLightVP");
        ctx->shadow_model_loc = gl->GetUniformLocation(ctx->shadow_program, "uModel");
    }

    /* Point light shadow: 512x512 depth cubemaps. */
    for (int s = 0; s < SDL3D_MAX_POINT_SHADOWS; s++)
        ctx->point_shadow_light_index[s] = -1;
    gl->GenFramebuffers(1, &ctx->point_shadow_fbo);
    for (int s = 0; s < SDL3D_MAX_POINT_SHADOWS; s++)
    {
        gl->GenTextures(1, &ctx->point_shadow_cubemap[s]);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->point_shadow_cubemap[s]);
        for (int i = 0; i < 6; i++)
        {
            gl->TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, GL_DEPTH_COMPONENT24, 512, 512, 0,
                           GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        }
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    ctx->point_shadow_program = build_program(gl, version_prefix, k_point_shadow_vert, k_point_shadow_frag);
    if (ctx->point_shadow_program)
    {
        ctx->point_shadow_model_loc = gl->GetUniformLocation(ctx->point_shadow_program, "model");
        ctx->point_shadow_light_vp_loc = gl->GetUniformLocation(ctx->point_shadow_program, "lightVP");
        ctx->point_shadow_light_pos_loc = gl->GetUniformLocation(ctx->point_shadow_program, "lightPos");
        ctx->point_shadow_far_loc = gl->GetUniformLocation(ctx->point_shadow_program, "farPlane");
    }

    gl->BindVertexArray(0);

    /* ---- 1×1 white texture ---- */
    {
        Uint8 white[4] = {255, 255, 255, 255};
        gl->GenTextures(1, &ctx->white_texture);
        gl->BindTexture(GL_TEXTURE_2D, ctx->white_texture);
        gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    /* ---- Main FBO ---- */
    if (!create_fbo(ctx, width, height))
    {
        SDL_Log("SDL3D GL: FBO creation failed");
        sdl3d_gl_destroy(ctx);
        return NULL;
    }

    /* ---- Post-process ping-pong FBOs ---- */
    {
        GLuint *fbos[2] = {&ctx->pp_fbo_a, &ctx->pp_fbo_b};
        GLuint *texs[2] = {&ctx->pp_tex_a, &ctx->pp_tex_b};
        for (int i = 0; i < 2; i++)
        {
            gl->GenTextures(1, texs[i]);
            gl->BindTexture(GL_TEXTURE_2D, *texs[i]);
            gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
            gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            gl->GenFramebuffers(1, fbos[i]);
            gl->BindFramebuffer(GL_FRAMEBUFFER, *fbos[i]);
            gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texs[i], 0);
            GLenum status = gl->CheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                SDL_Log("SDL3D GL: post-process FBO %d incomplete: 0x%x", i, status);
                sdl3d_gl_destroy(ctx);
                return NULL;
            }
        }
        gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* ---- Post-process shaders ---- */
    ctx->bloom_program = build_program(gl, version_prefix, k_fullscreen_vert, k_bloom_threshold_frag);
    ctx->bloom_blur_program = build_program(gl, version_prefix, k_fullscreen_vert, k_blur_frag);
    ctx->composite_program = build_program(gl, version_prefix, k_fullscreen_vert, k_composite_frag);
    if (!ctx->bloom_program || !ctx->bloom_blur_program || !ctx->composite_program)
    {
        SDL_Log("SDL3D GL: post-process shader compilation failed");
        sdl3d_gl_destroy(ctx);
        return NULL;
    }
    ctx->bloom_scene_loc = gl->GetUniformLocation(ctx->bloom_program, "uScene");
    ctx->bloom_threshold_loc = gl->GetUniformLocation(ctx->bloom_program, "uThreshold");
    ctx->blur_image_loc = gl->GetUniformLocation(ctx->bloom_blur_program, "uImage");
    ctx->blur_horizontal_loc = gl->GetUniformLocation(ctx->bloom_blur_program, "uHorizontal");
    ctx->comp_scene_loc = gl->GetUniformLocation(ctx->composite_program, "uScene");
    ctx->comp_bloom_loc = gl->GetUniformLocation(ctx->composite_program, "uBloom");
    ctx->comp_vignette_loc = gl->GetUniformLocation(ctx->composite_program, "uVignetteStrength");
    ctx->comp_contrast_loc = gl->GetUniformLocation(ctx->composite_program, "uContrast");
    ctx->comp_saturation_loc = gl->GetUniformLocation(ctx->composite_program, "uSaturation");

    /* ---- Retro profile shader ---- */
    ctx->retro_program = build_program(gl, version_prefix, k_fullscreen_vert, k_retro_frag);
    if (!ctx->retro_program)
    {
        SDL_Log("SDL3D GL: retro shader compilation failed");
        sdl3d_gl_destroy(ctx);
        return NULL;
    }
    ctx->retro_scene_loc = gl->GetUniformLocation(ctx->retro_program, "uScene");
    ctx->retro_profile_loc = gl->GetUniformLocation(ctx->retro_program, "uProfile");
    ctx->retro_resolution_loc = gl->GetUniformLocation(ctx->retro_program, "uResolution");

    /* ---- SSAO shader ---- */
    ctx->ssao_program = build_program(gl, version_prefix, k_fullscreen_vert, k_ssao_frag);
    if (!ctx->ssao_program)
    {
        SDL_Log("SDL3D GL: SSAO shader compilation failed");
        sdl3d_gl_destroy(ctx);
        return NULL;
    }
    ctx->ssao_scene_loc = gl->GetUniformLocation(ctx->ssao_program, "uScene");
    ctx->ssao_depth_loc = gl->GetUniformLocation(ctx->ssao_program, "uDepth");
    ctx->ssao_texel_size_loc = gl->GetUniformLocation(ctx->ssao_program, "uTexelSize");
    ctx->ssao_near_loc = gl->GetUniformLocation(ctx->ssao_program, "uNear");
    ctx->ssao_far_loc = gl->GetUniformLocation(ctx->ssao_program, "uFar");

    /* ---- Initial GL state ---- */
    gl->Enable(GL_DEPTH_TEST);
    gl->DepthFunc(GL_LEQUAL);
    gl->Enable(GL_CULL_FACE);
    gl->CullFace(GL_BACK);
    gl->FrontFace(GL_CCW);
    gl->Enable(GL_CULL_FACE);

    SDL_Log("SDL3D GL create: ctx=%p logical=%dx%d fbo=%u", (void *)ctx, width, height, ctx->fbo);
    SDL_Log("SDL3D GL renderer created: %dx%d pbr=%u unlit=%u copy=%u fbo=%u ubo=%u", width, height, ctx->pbr_program,
            ctx->unlit_program, ctx->copy_program, ctx->fbo, ctx->scene_ubo);

    return ctx;
}

void sdl3d_gl_destroy(sdl3d_gl_context *ctx)
{
    if (!ctx)
        return;
    sdl3d_gl_funcs *gl = &ctx->gl;

    free_draw_list(ctx);
    SDL_free(ctx->draw_list);

    tex_cache_free(ctx);
    SDL_free(ctx->white_colors);

    if (ctx->pbr_program)
        gl->DeleteProgram(ctx->pbr_program);
    if (ctx->unlit_program)
        gl->DeleteProgram(ctx->unlit_program);
    if (ctx->copy_program)
        gl->DeleteProgram(ctx->copy_program);

    if (ctx->scene_ubo)
        gl->DeleteBuffers(1, &ctx->scene_ubo);

    GLuint lit_bufs[] = {ctx->lit_position_vbo, ctx->lit_normal_vbo, ctx->lit_uv_vbo, ctx->lit_color_vbo, ctx->lit_ebo};
    gl->DeleteBuffers(5, lit_bufs);
    if (ctx->lit_vao)
        gl->DeleteVertexArrays(1, &ctx->lit_vao);

    GLuint unlit_bufs[] = {ctx->unlit_position_vbo, ctx->unlit_uv_vbo, ctx->unlit_color_vbo, ctx->unlit_ebo};
    gl->DeleteBuffers(4, unlit_bufs);
    if (ctx->unlit_vao)
        gl->DeleteVertexArrays(1, &ctx->unlit_vao);

    if (ctx->fullscreen_vao)
        gl->DeleteVertexArrays(1, &ctx->fullscreen_vao);

    /* Shadow resources. */
    if (ctx->shadow_program)
        gl->DeleteProgram(ctx->shadow_program);
    if (ctx->shadow_fbo)
        gl->DeleteFramebuffers(1, &ctx->shadow_fbo);
    if (ctx->shadow_depth_tex)
        gl->DeleteTextures(1, &ctx->shadow_depth_tex);
    {
        GLuint shadow_bufs[] = {ctx->shadow_position_vbo, ctx->shadow_ebo};
        gl->DeleteBuffers(2, shadow_bufs);
    }
    if (ctx->shadow_vao)
        gl->DeleteVertexArrays(1, &ctx->shadow_vao);

    /* Point shadow resources. */
    if (ctx->point_shadow_program)
        gl->DeleteProgram(ctx->point_shadow_program);
    if (ctx->point_shadow_fbo)
        gl->DeleteFramebuffers(1, &ctx->point_shadow_fbo);
    gl->DeleteTextures(SDL3D_MAX_POINT_SHADOWS, ctx->point_shadow_cubemap);

    /* Post-process resources. */
    if (ctx->retro_program)
        gl->DeleteProgram(ctx->retro_program);
    if (ctx->ssao_program)
        gl->DeleteProgram(ctx->ssao_program);
    if (ctx->bloom_program)
        gl->DeleteProgram(ctx->bloom_program);
    if (ctx->bloom_blur_program)
        gl->DeleteProgram(ctx->bloom_blur_program);
    if (ctx->composite_program)
        gl->DeleteProgram(ctx->composite_program);
    if (ctx->pp_fbo_a)
        gl->DeleteFramebuffers(1, &ctx->pp_fbo_a);
    if (ctx->pp_fbo_b)
        gl->DeleteFramebuffers(1, &ctx->pp_fbo_b);
    if (ctx->pp_tex_a)
        gl->DeleteTextures(1, &ctx->pp_tex_a);
    if (ctx->pp_tex_b)
        gl->DeleteTextures(1, &ctx->pp_tex_b);

    if (ctx->white_texture)
        gl->DeleteTextures(1, &ctx->white_texture);

    if (ctx->fbo)
        gl->DeleteFramebuffers(1, &ctx->fbo);
    if (ctx->fbo_color)
        gl->DeleteTextures(1, &ctx->fbo_color);
    if (ctx->fbo_depth)
        gl->DeleteTextures(1, &ctx->fbo_depth);

    if (ctx->gl_context)
        SDL_GL_DestroyContext(ctx->gl_context);
    SDL_free(ctx);
}

/* ================================================================== */
/* Backend interface implementations                                   */
/* ================================================================== */

static bool gl_clear(sdl3d_render_context *context, sdl3d_color color)
{
    sdl3d_gl_context *ctx = context->gl;
    sdl3d_gl_funcs *gl = &ctx->gl;

    free_draw_list(ctx);

    ctx->frame_index++;
    ctx->current_ctx = context;
    ctx->ubo_dirty = true;

    /* Map shading mode to retro profile. */
    if (context->shading_mode == SDL3D_SHADING_GOURAUD && context->vertex_snap)
        ctx->active_retro_profile = 1; /* PS1 */
    else if (context->shading_mode == SDL3D_SHADING_GOURAUD && context->uv_mode == SDL3D_UV_AFFINE)
        ctx->active_retro_profile = 3; /* DOS */
    else if (context->shading_mode == SDL3D_SHADING_GOURAUD)
        ctx->active_retro_profile = 2; /* N64 */
    else if (context->shading_mode == SDL3D_SHADING_FLAT)
        ctx->active_retro_profile = 4; /* SNES */
    else
        ctx->active_retro_profile = 0; /* Modern */

    /* Sync shadow state from render context so deferred replay works. */
    if (context->shadow_enabled[0])
    {
        SDL_memcpy(ctx->shadow_light_vp, context->shadow_vp[0].m, 16 * sizeof(float));
        ctx->shadow_bias = context->shadow_bias > 0 ? context->shadow_bias : 0.005f;
    }

    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);

    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
    gl->ClearColor((float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f, (float)color.a / 255.0f);
    gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

static bool gl_draw_mesh_unlit(sdl3d_render_context *context, const sdl3d_draw_params_unlit *params)
{
    sdl3d_gl_context *ctx = context->gl;

    const float *colors = params->colors ? params->colors : ensure_white_colors(ctx, params->vertex_count);

    sdl3d_draw_entry *e = append_draw_entry(ctx);
    if (!e)
        return false;

    e->lit = false;
    e->vertex_count = params->vertex_count;
    e->index_count = (params->indices && params->index_count > 0) ? params->index_count : 0;
    e->positions = copy_floats(params->positions, (size_t)params->vertex_count * 3);
    e->uvs = copy_floats(params->uvs, (size_t)params->vertex_count * 2);
    e->colors = copy_floats(colors, (size_t)params->vertex_count * 4);
    e->indices = copy_indices(params->indices, (size_t)e->index_count);
    e->texture = params->texture;
    SDL_memcpy(e->mvp, params->mvp, 16 * sizeof(float));
    SDL_memcpy(e->tint, params->tint, 4 * sizeof(float));

    return true;
}

static bool gl_draw_mesh_lit(sdl3d_render_context *context, const sdl3d_draw_params_lit *params)
{
    sdl3d_gl_context *ctx = context->gl;

    const float *colors = params->colors ? params->colors : ensure_white_colors(ctx, params->vertex_count);

    sdl3d_draw_entry *e = append_draw_entry(ctx);
    if (!e)
        return false;

    e->lit = true;
    e->vertex_count = params->vertex_count;
    e->index_count = (params->indices && params->index_count > 0) ? params->index_count : 0;
    e->positions = copy_floats(params->positions, (size_t)params->vertex_count * 3);
    e->normals = copy_floats(params->normals, (size_t)params->vertex_count * 3);
    e->uvs = copy_floats(params->uvs, (size_t)params->vertex_count * 2);
    e->colors = copy_floats(colors, (size_t)params->vertex_count * 4);
    e->indices = copy_indices(params->indices, (size_t)e->index_count);
    e->texture = params->texture;
    SDL_memcpy(e->model_matrix, params->model_matrix, 16 * sizeof(float));
    SDL_memcpy(e->normal_matrix, params->normal_matrix, 9 * sizeof(float));
    SDL_memcpy(e->tint, params->tint, 4 * sizeof(float));
    e->metallic = params->metallic;
    e->roughness = params->roughness;
    SDL_memcpy(e->emissive, params->emissive, 3 * sizeof(float));

    check_z_fighting(ctx, e);

    return true;
}

/* ------------------------------------------------------------------ */
/* Shadow pass                                                         */
/* ------------------------------------------------------------------ */

void sdl3d_gl_begin_shadow_pass(sdl3d_gl_context *ctx, const float *light_vp, float bias)
{
    if (!ctx || !light_vp || !ctx->shadow_fbo)
        return;
    SDL_memcpy(ctx->shadow_light_vp, light_vp, 16 * sizeof(float));
    ctx->shadow_bias = bias;
    ctx->in_shadow_pass = true;

    sdl3d_gl_funcs *gl = &ctx->gl;
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    gl->Viewport(0, 0, 2048, 2048);
    gl->Clear(GL_DEPTH_BUFFER_BIT);
    gl->Enable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
}

void sdl3d_gl_end_shadow_pass(sdl3d_gl_context *ctx)
{
    if (!ctx)
        return;
    ctx->in_shadow_pass = false;

    sdl3d_gl_funcs *gl = &ctx->gl;
    /* Restore main FBO and normal culling. */
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
    gl->Enable(GL_CULL_FACE);
    gl->CullFace(GL_BACK);
}

static bool gl_present(sdl3d_render_context *context)
{
    sdl3d_gl_context *ctx = context->gl;
    sdl3d_gl_funcs *gl = &ctx->gl;

    /* Flush UBO before any replay. */
    flush_scene_ubo(ctx);

    /* Shadow pass: render original VP into layer 0 (backward compatible),
     * then CSM cascades 1-3 into layers 1-3 for Slice 3. */
    if (0 && ctx->shadow_program)
    {
        compute_csm_matrices(ctx, context);

        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
        gl->Viewport(0, 0, 2048, 2048);
        gl->Enable(GL_DEPTH_TEST);
        gl->Disable(GL_CULL_FACE);

        for (int cascade = 0; cascade < SDL3D_CSM_CASCADE_COUNT; cascade++)
        {
            gl->FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, ctx->shadow_depth_tex, 0, cascade);
            gl->Clear(GL_DEPTH_BUFFER_BIT);
            gl->UseProgram(ctx->shadow_program);
            gl->UniformMatrix4fv(ctx->shadow_light_vp_loc, 1, GL_FALSE,
                                 (cascade == 0) ? ctx->shadow_light_vp : ctx->csm_light_vp[cascade]);
            replay_draw_list_shadow(ctx);
        }

        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
        gl->Enable(GL_CULL_FACE);
        gl->CullFace(GL_BACK);
    }

    /* Point shadow pass: 6-face cubemap per light. */
    compute_point_shadow_matrices(ctx, context);
    if (ctx->current_ctx->point_shadows_enabled && ctx->point_shadow_program && ctx->point_shadow_count > 0)
    {
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->point_shadow_fbo);
        gl->Viewport(0, 0, 512, 512);
        gl->Enable(GL_DEPTH_TEST);
        gl->Disable(GL_CULL_FACE);
        gl->UseProgram(ctx->point_shadow_program);

        for (int s = 0; s < ctx->point_shadow_count; s++)
        {
            const sdl3d_light *pl = &context->lights[ctx->point_shadow_light_index[s]];
            gl->Uniform3f(ctx->point_shadow_light_pos_loc, pl->position.x, pl->position.y, pl->position.z);
            gl->Uniform1f(ctx->point_shadow_far_loc, ctx->point_shadow_far_plane[s]);

            for (int face = 0; face < 6; face++)
            {
                gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                         GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, ctx->point_shadow_cubemap[s],
                                         0);
                gl->Clear(GL_DEPTH_BUFFER_BIT);
                gl->UniformMatrix4fv(ctx->point_shadow_light_vp_loc, 1, GL_FALSE, ctx->point_shadow_vp[s][face]);
                for (int i = 0; i < ctx->draw_count; i++)
                {
                    sdl3d_draw_entry *e = &ctx->draw_list[i];
                    if (!e->lit)
                        continue;
                    /* Only dynamic actors cast point shadows. */
                    if (e->model_matrix[12] == 0.0f && e->model_matrix[13] == 0.0f && e->model_matrix[14] == 0.0f &&
                        e->model_matrix[0] == 1.0f && e->model_matrix[5] == 1.0f && e->model_matrix[10] == 1.0f)
                        continue;
                    gl->UniformMatrix4fv(ctx->point_shadow_model_loc, 1, GL_FALSE, e->model_matrix);
                    gl->BindVertexArray(ctx->shadow_vao);
                    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
                    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)),
                                   e->positions, GL_DYNAMIC_DRAW);
                    if (e->indices && e->index_count > 0)
                    {
                        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->shadow_ebo);
                        gl->BufferData(GL_ELEMENT_ARRAY_BUFFER,
                                       (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)), e->indices,
                                       GL_DYNAMIC_DRAW);
                        gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
                    }
                    else
                    {
                        gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
                    }
                }
            }
        }
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
        gl->Enable(GL_CULL_FACE);
        gl->CullFace(GL_BACK);
    }

    /* Geometry pass: replay all entries into main FBO. */
    replay_draw_list_geometry(ctx);

    /* ---- Post-process pipeline ---- */
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->BindVertexArray(ctx->fullscreen_vao);

    /* Retro profile pass: runs before bloom so bloom operates on the
     * retro-styled scene. Renders fbo_color through the retro uber-shader
     * into pp_fbo_a, then bloom reads from pp_tex_a instead of fbo_color. */
    if (ctx->active_retro_profile > 0)
    {
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_a);
        gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
        gl->Clear(GL_COLOR_BUFFER_BIT);
        gl->UseProgram(ctx->retro_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
        gl->Uniform1i(ctx->retro_scene_loc, 0);
        gl->Uniform1i(ctx->retro_profile_loc, ctx->active_retro_profile);
        gl->Uniform2f(ctx->retro_resolution_loc, (float)ctx->logical_w, (float)ctx->logical_h);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);

        /* Copy retro result back to fbo_color so the rest of the pipeline
         * (bloom threshold, composite scene read) works unchanged. */
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
        gl->UseProgram(ctx->copy_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_a);
        gl->Uniform1i(ctx->copy_texture_loc, 0);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);
    }

    /* SSAO pass: darken pixels where nearby depth samples indicate occlusion.
     * Reads fbo_color + fbo_depth, writes to pp_fbo_a, then copies back. */
    if (context->ssao_enabled)
    {
        float near_p = ctx->current_ctx->near_plane;
        float far_p = ctx->current_ctx->far_plane;
        if (near_p > 0.0f && far_p > near_p)
        {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_a);
            gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
            gl->Clear(GL_COLOR_BUFFER_BIT);
            gl->UseProgram(ctx->ssao_program);
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
            gl->Uniform1i(ctx->ssao_scene_loc, 0);
            gl->ActiveTexture(GL_TEXTURE0 + 1);
            gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_depth);
            gl->Uniform1i(ctx->ssao_depth_loc, 1);
            gl->Uniform2f(ctx->ssao_texel_size_loc, 1.0f / (float)ctx->logical_w, 1.0f / (float)ctx->logical_h);
            gl->Uniform1f(ctx->ssao_near_loc, near_p);
            gl->Uniform1f(ctx->ssao_far_loc, far_p);
            gl->DrawArrays(GL_TRIANGLES, 0, 3);
            gl->ActiveTexture(GL_TEXTURE0);

            /* Copy SSAO result back to fbo_color. */
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
            gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
            gl->UseProgram(ctx->copy_program);
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_a);
            gl->Uniform1i(ctx->copy_texture_loc, 0);
            gl->DrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    /* Step 1: Extract bright pixels from main FBO into pp_tex_a. */
    if (context->bloom_enabled)
    {
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_a);
        gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
        gl->Clear(GL_COLOR_BUFFER_BIT);
        gl->UseProgram(ctx->bloom_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
        gl->Uniform1i(ctx->bloom_scene_loc, 0);
        gl->Uniform1f(ctx->bloom_threshold_loc, 1.2f);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);

        /* Step 2: Blur bright pixels (ping-pong, 10 passes = 5 iterations). */
        gl->UseProgram(ctx->bloom_blur_program);
        for (int i = 0; i < 6; i++)
        {
            bool horizontal = (i % 2 == 0);
            gl->BindFramebuffer(GL_FRAMEBUFFER, horizontal ? ctx->pp_fbo_b : ctx->pp_fbo_a);
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, horizontal ? ctx->pp_tex_a : ctx->pp_tex_b);
            gl->Uniform1i(ctx->blur_image_loc, 0);
            gl->Uniform1i(ctx->blur_horizontal_loc, horizontal ? 1 : 0);
            gl->DrawArrays(GL_TRIANGLES, 0, 3);
        }
        /* After 10 passes (even count), last write was to pp_fbo_b with horizontal=false reading pp_tex_b...
         * Actually: i=9 -> horizontal=false -> writes to pp_fbo_a, reads pp_tex_b.
         * So final blurred result is in pp_tex_a. */

        /* Step 3: Copy fbo_color to pp_tex_b to avoid read-write conflict. */
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_b);
        gl->UseProgram(ctx->copy_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
        gl->Uniform1i(ctx->copy_texture_loc, 0);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);

        /* Step 4: Composite (bloom + vignette + grading) into main FBO.
         * Read scene from pp_tex_b, bloom from pp_tex_a, write to fbo. */
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->UseProgram(ctx->composite_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_b);
        gl->ActiveTexture(GL_TEXTURE0 + 1);
        gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_a);
        gl->Uniform1i(ctx->comp_scene_loc, 0);
        gl->Uniform1i(ctx->comp_bloom_loc, 1);
        gl->Uniform1f(ctx->comp_vignette_loc, 0.4f);
        gl->Uniform1f(ctx->comp_contrast_loc, 1.1f);
        gl->Uniform1f(ctx->comp_saturation_loc, 1.15f);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);
        gl->ActiveTexture(GL_TEXTURE0);
    } /* bloom_enabled */

    gl->Enable(GL_CULL_FACE);
    gl->Enable(GL_DEPTH_TEST);

    /* Compute letterbox viewport. */
    int win_w, win_h;
    SDL_GetWindowSizeInPixels(ctx->window, &win_w, &win_h);

    float scale_x = (float)win_w / (float)ctx->logical_w;
    float scale_y = (float)win_h / (float)ctx->logical_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int vp_w = (int)((float)ctx->logical_w * scale);
    int vp_h = (int)((float)ctx->logical_h * scale);
    int vp_x = (win_w - vp_w) / 2;
    int vp_y = (win_h - vp_h) / 2;

    /* Bind default framebuffer, clear to black, set letterbox viewport. */
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl->Clear(GL_COLOR_BUFFER_BIT);
    gl->Viewport(vp_x, vp_y, vp_w, vp_h);

    /* Draw fullscreen triangle sampling the FBO color attachment. */
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->UseProgram(ctx->copy_program);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
    gl->Uniform1i(ctx->copy_texture_loc, 0);
    gl->BindVertexArray(ctx->fullscreen_vao);
    gl->DrawArrays(GL_TRIANGLES, 0, 3);
    gl->Enable(GL_CULL_FACE);
    gl->Enable(GL_DEPTH_TEST);

    SDL_GL_SwapWindow(ctx->window);
    return true;
}

/* ------------------------------------------------------------------ */
/* Lifecycle adapter                                                   */
/* ------------------------------------------------------------------ */

static void gl_destroy_adapter(sdl3d_render_context *context)
{
    sdl3d_gl_destroy(context->gl);
    context->gl = NULL;
}

/* ------------------------------------------------------------------ */
/* Backend interface initializer                                       */
/* ------------------------------------------------------------------ */

void sdl3d_gl_post_process(sdl3d_gl_context *ctx, int effects, float bloom_threshold, float bloom_intensity,
                           float vignette_intensity, float contrast, float brightness, float saturation)
{
    (void)ctx;
    (void)effects;
    (void)bloom_threshold;
    (void)bloom_intensity;
    (void)vignette_intensity;
    (void)contrast;
    (void)brightness;
    (void)saturation;
}

void sdl3d_gl_read_pixel(sdl3d_gl_context *ctx, int x, int y, unsigned char *rgba)
{
    if (ctx == NULL || rgba == NULL)
    {
        return;
    }
    /* Flush any pending draw list so pixels are up to date. */
    if (ctx->draw_count > 0)
    {
        sdl3d_gl_funcs *gl = &ctx->gl;
        flush_scene_ubo(ctx);
        if (0 && ctx->shadow_program)
        {
            compute_csm_matrices(ctx, ctx->current_ctx);

            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
            gl->Viewport(0, 0, 2048, 2048);
            gl->Enable(GL_DEPTH_TEST);
            gl->Disable(GL_CULL_FACE);

            for (int cascade = 0; cascade < SDL3D_CSM_CASCADE_COUNT; cascade++)
            {
                gl->FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, ctx->shadow_depth_tex, 0, cascade);
                gl->Clear(GL_DEPTH_BUFFER_BIT);
                gl->UseProgram(ctx->shadow_program);
                gl->UniformMatrix4fv(ctx->shadow_light_vp_loc, 1, GL_FALSE,
                                     (cascade == 0) ? ctx->shadow_light_vp : ctx->csm_light_vp[cascade]);
                replay_draw_list_shadow(ctx);
            }

            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
            gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
            gl->Enable(GL_CULL_FACE);
            gl->CullFace(GL_BACK);
        }
        compute_point_shadow_matrices(ctx, ctx->current_ctx);
        if (ctx->current_ctx->point_shadows_enabled && ctx->point_shadow_program && ctx->point_shadow_count > 0)
        {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->point_shadow_fbo);
            gl->Viewport(0, 0, 512, 512);
            gl->Enable(GL_DEPTH_TEST);
            gl->Disable(GL_CULL_FACE);
            gl->UseProgram(ctx->point_shadow_program);
            for (int s = 0; s < ctx->point_shadow_count; s++)
            {
                const sdl3d_light *pl = &ctx->current_ctx->lights[ctx->point_shadow_light_index[s]];
                gl->Uniform3f(ctx->point_shadow_light_pos_loc, pl->position.x, pl->position.y, pl->position.z);
                gl->Uniform1f(ctx->point_shadow_far_loc, ctx->point_shadow_far_plane[s]);
                for (int face = 0; face < 6; face++)
                {
                    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                             GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face,
                                             ctx->point_shadow_cubemap[s], 0);
                    gl->Clear(GL_DEPTH_BUFFER_BIT);
                    gl->UniformMatrix4fv(ctx->point_shadow_light_vp_loc, 1, GL_FALSE, ctx->point_shadow_vp[s][face]);
                    for (int i = 0; i < ctx->draw_count; i++)
                    {
                        sdl3d_draw_entry *e = &ctx->draw_list[i];
                        if (!e->lit)
                            continue;
                        if (e->model_matrix[12] == 0.0f && e->model_matrix[13] == 0.0f && e->model_matrix[14] == 0.0f &&
                            e->model_matrix[0] == 1.0f && e->model_matrix[5] == 1.0f && e->model_matrix[10] == 1.0f)
                            continue;
                        gl->UniformMatrix4fv(ctx->point_shadow_model_loc, 1, GL_FALSE, e->model_matrix);
                        gl->BindVertexArray(ctx->shadow_vao);
                        gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
                        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)),
                                       e->positions, GL_DYNAMIC_DRAW);
                        if (e->indices && e->index_count > 0)
                        {
                            gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->shadow_ebo);
                            gl->BufferData(GL_ELEMENT_ARRAY_BUFFER,
                                           (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)), e->indices,
                                           GL_DYNAMIC_DRAW);
                            gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
                        }
                        else
                        {
                            gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
                        }
                    }
                }
            }
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
            gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
            gl->Enable(GL_CULL_FACE);
            gl->CullFace(GL_BACK);
        }
        replay_draw_list_geometry(ctx);
    }
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    ctx->gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void sdl3d_gl_backend_init(sdl3d_backend_interface *iface)
{
    iface->destroy = gl_destroy_adapter;
    iface->clear = gl_clear;
    iface->present = gl_present;
    iface->draw_mesh_unlit = gl_draw_mesh_unlit;
    iface->draw_mesh_lit = gl_draw_mesh_lit;
}
