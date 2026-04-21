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
    GLint shadow_light_mvp_loc;
    GLuint shadow_vao;
    GLuint shadow_position_vbo;
    GLuint shadow_ebo;
    bool in_shadow_pass;
    float shadow_light_vp[16];
    float shadow_bias;

    /* PBR shadow uniform locations */
    GLint pbr_shadow_map_loc;
    GLint pbr_shadow_vp_loc;
    GLint pbr_shadow_enabled_loc;
    GLint pbr_shadow_bias_loc;

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
                                      "uniform sampler2D uShadowMap;\n"
                                      "uniform mat4 uShadowVP;\n"
                                      "uniform int uShadowEnabled;\n"
                                      "uniform float uShadowBias;\n"
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
    "    }\n"
    "\n"
    "    /* Shadow (PCF 3x3 for soft edges). */\n"
    "    if (uShadowEnabled != 0) {\n"
    "        vec4 lpos = uShadowVP * vec4(vWorldPos, 1.0);\n"
    "        vec3 proj = lpos.xyz / lpos.w * 0.5 + 0.5;\n"
    "        if (proj.x >= 0.0 && proj.x <= 1.0 && proj.y >= 0.0 && proj.y <= 1.0 && proj.z <= 1.0) {\n"
    "            float shadow = 0.0;\n"
    "            vec2 texelSize = vec2(1.0 / 2048.0);\n"
    "            for (int x = -1; x <= 1; x++) {\n"
    "                for (int y = -1; y <= 1; y++) {\n"
    "                    float d = texture(uShadowMap, proj.xy + vec2(x, y) * texelSize).r;\n"
    "                    shadow += (proj.z - uShadowBias > d) ? 1.0 : 0.0;\n"
    "                }\n"
    "            }\n"
    "            shadow /= 9.0;\n"
    "            Lo *= (1.0 - shadow);\n"
    "        }\n"
    "    }\n"
    "\n"
    "    vec3 color = uAmbient * albedo + Lo + uEmissive;\n"
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
                                    "uniform mat4 uLightMVP;\n"
                                    "void main() {\n"
                                    "    gl_Position = uLightMVP * vec4(aPosition, 1.0);\n"
                                    "}\n";

static const char k_shadow_frag[] = "out vec4 fragColor;\nvoid main() { fragColor = vec4(1.0); }\n";

/* ------------------------------------------------------------------ */
/* Shader helpers                                                      */
/* ------------------------------------------------------------------ */

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
/* Draw list replay                                                    */
/* ------------------------------------------------------------------ */

static void replay_draw_list_shadow(sdl3d_gl_context *ctx)
{
    sdl3d_gl_funcs *gl = &ctx->gl;
    gl->UseProgram(ctx->shadow_program);
    gl->BindVertexArray(ctx->shadow_vao);

    for (int i = 0; i < ctx->draw_count; i++)
    {
        sdl3d_draw_entry *e = &ctx->draw_list[i];
        if (!e->lit)
            continue;

        float light_mvp[16];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
            {
                float sum = 0;
                for (int k = 0; k < 4; k++)
                    sum += ctx->shadow_light_vp[r + k * 4] * e->model_matrix[k + c * 4];
                light_mvp[r + c * 4] = sum;
            }

        gl->UniformMatrix4fv(ctx->shadow_light_mvp_loc, 1, GL_FALSE, light_mvp);
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

            /* Shadow uniforms */
            if (ctx->shadow_depth_tex && ctx->shadow_bias > 0.0f)
            {
                gl->ActiveTexture(GL_TEXTURE0 + 1);
                gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_depth_tex);
                gl->Uniform1i(ctx->pbr_shadow_map_loc, 1);
                gl->UniformMatrix4fv(ctx->pbr_shadow_vp_loc, 1, GL_FALSE, ctx->shadow_light_vp);
                gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 1);
                gl->Uniform1f(ctx->pbr_shadow_bias_loc, ctx->shadow_bias);
                gl->ActiveTexture(GL_TEXTURE0);
            }
            else
            {
                gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 0);
            }

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

    gl->GenRenderbuffers(1, &ctx->fbo_depth);
    gl->BindRenderbuffer(GL_RENDERBUFFER, ctx->fbo_depth);
    gl->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    gl->GenFramebuffers(1, &ctx->fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->fbo_color, 0);
    gl->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx->fbo_depth);

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
        const char *frag_srcs[3] = {version_prefix, k_pbr_frag_decl, k_pbr_frag_main};
        GLuint fs = vs ? compile_shader_multi(gl, GL_FRAGMENT_SHADER, 3, frag_srcs) : 0;
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
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_depth_tex);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->shadow_depth_tex, 0);
    gl->DrawBuffer(GL_NONE);
    gl->ReadBuffer(GL_NONE);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo); /* restore */

    /* Shadow VAO: position-only (lesson #2: never share VAOs between passes). */
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
        ctx->shadow_light_mvp_loc = gl->GetUniformLocation(ctx->shadow_program, "uLightMVP");
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

    if (ctx->white_texture)
        gl->DeleteTextures(1, &ctx->white_texture);

    if (ctx->fbo)
        gl->DeleteFramebuffers(1, &ctx->fbo);
    if (ctx->fbo_color)
        gl->DeleteTextures(1, &ctx->fbo_color);
    if (ctx->fbo_depth)
        gl->DeleteRenderbuffers(1, &ctx->fbo_depth);

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
    /* Lesson #6: cull front faces so back faces write depth. */
    gl->Enable(GL_CULL_FACE);
    gl->CullFace(GL_FRONT);
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
    gl->CullFace(GL_BACK);
}

static bool gl_present(sdl3d_render_context *context)
{
    sdl3d_gl_context *ctx = context->gl;
    sdl3d_gl_funcs *gl = &ctx->gl;

    /* Flush UBO before any replay. */
    flush_scene_ubo(ctx);

    /* Shadow pass: replay lit entries into shadow FBO. */
    if (ctx->shadow_program && ctx->shadow_fbo && ctx->shadow_bias > 0.0f)
    {
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
        gl->Viewport(0, 0, 2048, 2048);
        gl->Clear(GL_DEPTH_BUFFER_BIT);
        gl->Enable(GL_DEPTH_TEST);
        gl->Enable(GL_CULL_FACE);
        gl->CullFace(GL_FRONT);

        replay_draw_list_shadow(ctx);

        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
        gl->CullFace(GL_BACK);
    }

    /* Geometry pass: replay all entries into main FBO. */
    replay_draw_list_geometry(ctx);

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
        if (ctx->shadow_program && ctx->shadow_fbo && ctx->shadow_bias > 0.0f)
        {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
            gl->Viewport(0, 0, 2048, 2048);
            gl->Clear(GL_DEPTH_BUFFER_BIT);
            gl->Enable(GL_DEPTH_TEST);
            gl->Enable(GL_CULL_FACE);
            gl->CullFace(GL_FRONT);
            replay_draw_list_shadow(ctx);
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
            gl->Viewport(0, 0, ctx->logical_w, ctx->logical_h);
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
