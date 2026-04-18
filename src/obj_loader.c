#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <limits.h>

#include "model_internal.h"

/* --------------------------------------------------------------------
 * Dynamic arrays. Written as a single generic growable buffer so each
 * call site stays readable (no macro expansion noise, no per-type
 * boilerplate). Sizes are byte-precise; callers do the stride math.
 * -------------------------------------------------------------------- */

typedef struct sdl3d_obj_buffer
{
    void *data;
    size_t count;    /* number of elements */
    size_t capacity; /* capacity in elements */
    size_t stride;
} sdl3d_obj_buffer;

static void sdl3d_obj_buffer_init(sdl3d_obj_buffer *buf, size_t stride)
{
    buf->data = NULL;
    buf->count = 0;
    buf->capacity = 0;
    buf->stride = stride;
}

static bool sdl3d_obj_buffer_reserve(sdl3d_obj_buffer *buf, size_t required)
{
    if (buf->capacity >= required)
    {
        return true;
    }
    size_t cap = buf->capacity == 0 ? 16 : buf->capacity;
    while (cap < required)
    {
        cap *= 2;
    }
    void *next = SDL_realloc(buf->data, cap * buf->stride);
    if (next == NULL)
    {
        return SDL_OutOfMemory();
    }
    buf->data = next;
    buf->capacity = cap;
    return true;
}

static bool sdl3d_obj_buffer_push(sdl3d_obj_buffer *buf, const void *element)
{
    if (!sdl3d_obj_buffer_reserve(buf, buf->count + 1))
    {
        return false;
    }
    SDL_memcpy((char *)buf->data + buf->count * buf->stride, element, buf->stride);
    buf->count += 1;
    return true;
}

static void sdl3d_obj_buffer_free(sdl3d_obj_buffer *buf)
{
    SDL_free(buf->data);
    buf->data = NULL;
    buf->count = 0;
    buf->capacity = 0;
}

/* --------------------------------------------------------------------
 * Small string helpers. All OBJ/MTL tokens are bounded — keep the API
 * minimal and use SDL allocators throughout so the public free function
 * can release every string uniformly.
 * -------------------------------------------------------------------- */

static char *sdl3d_obj_strdup_range(const char *begin, const char *end)
{
    const size_t len = (size_t)(end - begin);
    char *out = (char *)SDL_malloc(len + 1);
    if (out == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }
    if (len > 0)
    {
        SDL_memcpy(out, begin, len);
    }
    out[len] = '\0';
    return out;
}

static char *sdl3d_obj_strdup(const char *s)
{
    return sdl3d_obj_strdup_range(s, s + SDL_strlen(s));
}

static bool sdl3d_obj_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static const char *sdl3d_obj_skip_spaces(const char *p, const char *end)
{
    while (p < end && sdl3d_obj_is_space(*p))
    {
        ++p;
    }
    return p;
}

static const char *sdl3d_obj_find_space(const char *p, const char *end)
{
    while (p < end && !sdl3d_obj_is_space(*p) && *p != '\n')
    {
        ++p;
    }
    return p;
}

/* --------------------------------------------------------------------
 * Filesystem helpers. OBJ files refer to .mtl siblings and texture
 * files by path relative to the OBJ's own directory; we reproduce that
 * rule without dragging in a full path library.
 * -------------------------------------------------------------------- */

static char *sdl3d_obj_dirname_dup(const char *path)
{
    const char *slash = NULL;
    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            slash = p;
        }
    }
    if (slash == NULL)
    {
        char *empty = (char *)SDL_malloc(1);
        if (empty != NULL)
        {
            empty[0] = '\0';
        }
        return empty;
    }
    return sdl3d_obj_strdup_range(path, slash + 1);
}

static char *sdl3d_obj_join_path(const char *dir, const char *relative)
{
    const size_t dl = SDL_strlen(dir);
    const size_t rl = SDL_strlen(relative);
    char *out = (char *)SDL_malloc(dl + rl + 1);
    if (out == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }
    SDL_memcpy(out, dir, dl);
    SDL_memcpy(out + dl, relative, rl);
    out[dl + rl] = '\0';
    return out;
}

static char *sdl3d_obj_load_text(const char *path, size_t *out_size)
{
    size_t bytes = 0;
    void *data = SDL_LoadFile(path, &bytes);
    if (data == NULL)
    {
        return NULL;
    }

    /*
     * SDL_LoadFile already null-terminates its output, so we can use
     * the pointer directly as a C string. The caller frees via SDL_free.
     */
    if (out_size != NULL)
    {
        *out_size = bytes;
    }
    return (char *)data;
}

/* --------------------------------------------------------------------
 * OBJ face vertex + working state.
 * -------------------------------------------------------------------- */

typedef struct sdl3d_obj_face_vertex
{
    int v;  /* 1-based into positions; required */
    int vt; /* 1-based into uvs; 0 when absent */
    int vn; /* 1-based into normals; 0 when absent */
} sdl3d_obj_face_vertex;

typedef struct sdl3d_obj_face
{
    int first;           /* index into face_vertices */
    int count;           /* number of vertices */
    int material;        /* into materials, or -1 */
    int group_signature; /* changes trigger a new mesh */
} sdl3d_obj_face;

typedef struct sdl3d_obj_material_scratch
{
    sdl3d_material m;
} sdl3d_obj_material_scratch;

typedef struct sdl3d_obj_state
{
    sdl3d_obj_buffer positions;     /* vec3 */
    sdl3d_obj_buffer normals;       /* vec3 */
    sdl3d_obj_buffer uvs;           /* vec2 */
    sdl3d_obj_buffer face_vertices; /* sdl3d_obj_face_vertex */
    sdl3d_obj_buffer faces;         /* sdl3d_obj_face */
    sdl3d_obj_buffer materials;     /* sdl3d_material */
    int current_material;
    int current_group;
    char *group_name;
    char *dirname;
} sdl3d_obj_state;

static void sdl3d_obj_material_init(sdl3d_material *mat, const char *name)
{
    SDL_zerop(mat);
    mat->name = name != NULL ? sdl3d_obj_strdup(name) : NULL;
    mat->albedo[0] = 1.0f;
    mat->albedo[1] = 1.0f;
    mat->albedo[2] = 1.0f;
    mat->albedo[3] = 1.0f;
    mat->metallic = 0.0f;
    mat->roughness = 1.0f;
}

static int sdl3d_obj_find_material(sdl3d_obj_state *s, const char *name)
{
    const sdl3d_material *mats = (const sdl3d_material *)s->materials.data;
    for (size_t i = 0; i < s->materials.count; ++i)
    {
        if (mats[i].name != NULL && SDL_strcmp(mats[i].name, name) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

/* --------------------------------------------------------------------
 * MTL parser. Tolerates missing maps and unknown directives; warns
 * rather than failing so a bad .mtl never blocks geometry loading.
 * -------------------------------------------------------------------- */

static bool sdl3d_mtl_parse_floats(const char *p, const char *end, float *out, int count)
{
    for (int i = 0; i < count; ++i)
    {
        p = sdl3d_obj_skip_spaces(p, end);
        if (p >= end)
        {
            return false;
        }
        char *tail = NULL;
        const double v = SDL_strtod(p, &tail);
        if (tail == p)
        {
            return false;
        }
        out[i] = (float)v;
        p = tail;
    }
    return true;
}

static char *sdl3d_mtl_parse_map_path(const char *p, const char *end)
{
    /*
     * .mtl map_* directives may carry option flags like
     *   map_Kd -s 1 1 1 -o 0 0 0 brick.png
     * Skip flag tokens (they start with '-') and return the final
     * non-flag token as the path.
     */
    const char *last_begin = NULL;
    const char *last_end = NULL;
    while (p < end)
    {
        p = sdl3d_obj_skip_spaces(p, end);
        if (p >= end || *p == '\n')
        {
            break;
        }
        const char *tok_end = sdl3d_obj_find_space(p, end);
        if (*p == '-')
        {
            /* Flag; skip this token plus a format-dependent number of
             * operand tokens. We conservatively only skip the flag name
             * token; if the next token is numeric, skip it too. */
            p = tok_end;
            for (int i = 0; i < 3; ++i)
            {
                p = sdl3d_obj_skip_spaces(p, end);
                if (p >= end || *p == '\n')
                {
                    break;
                }
                const char *next = sdl3d_obj_find_space(p, end);
                /* If this token parses as a number, consume it. */
                char *tail = NULL;
                (void)SDL_strtod(p, &tail);
                if (tail == next)
                {
                    p = next;
                }
                else
                {
                    break;
                }
            }
            continue;
        }
        last_begin = p;
        last_end = tok_end;
        p = tok_end;
    }
    if (last_begin == NULL)
    {
        return NULL;
    }
    return sdl3d_obj_strdup_range(last_begin, last_end);
}

static bool sdl3d_mtl_load(sdl3d_obj_state *s, const char *mtl_path)
{
    size_t size = 0;
    char *text = sdl3d_obj_load_text(mtl_path, &size);
    if (text == NULL)
    {
        /* Warn but keep going — geometry still loads. */
        SDL_Log("sdl3d: could not open mtl file '%s' (%s); materials from this library will not be applied.", mtl_path,
                SDL_GetError());
        return true;
    }

    const char *p = text;
    const char *end = text + size;
    sdl3d_material *current = NULL;

    while (p < end)
    {
        p = sdl3d_obj_skip_spaces(p, end);
        if (p >= end)
        {
            break;
        }
        if (*p == '\n')
        {
            ++p;
            continue;
        }
        if (*p == '#')
        {
            while (p < end && *p != '\n')
            {
                ++p;
            }
            continue;
        }

        const char *kw_end = sdl3d_obj_find_space(p, end);
        const size_t kw_len = (size_t)(kw_end - p);
        const char *args = sdl3d_obj_skip_spaces(kw_end, end);
        const char *line_end = args;
        while (line_end < end && *line_end != '\n')
        {
            ++line_end;
        }

        if (kw_len == 6 && SDL_strncmp(p, "newmtl", 6) == 0)
        {
            char *name = sdl3d_obj_strdup_range(args, line_end);
            if (name == NULL)
            {
                SDL_free(text);
                return false;
            }
            /* Trim trailing spaces left by the range extract. */
            size_t nl = SDL_strlen(name);
            while (nl > 0 && sdl3d_obj_is_space(name[nl - 1]))
            {
                name[--nl] = '\0';
            }
            sdl3d_material mat;
            sdl3d_obj_material_init(&mat, name);
            SDL_free(name);
            if (!sdl3d_obj_buffer_push(&s->materials, &mat))
            {
                SDL_free(mat.name);
                SDL_free(text);
                return false;
            }
            current = &((sdl3d_material *)s->materials.data)[s->materials.count - 1];
        }
        else if (current == NULL)
        {
            /* Directive before any newmtl — skip gracefully. */
        }
        else if (kw_len == 2 && SDL_strncmp(p, "Kd", 2) == 0)
        {
            float rgb[3] = {1.0f, 1.0f, 1.0f};
            if (sdl3d_mtl_parse_floats(args, line_end, rgb, 3))
            {
                current->albedo[0] = rgb[0];
                current->albedo[1] = rgb[1];
                current->albedo[2] = rgb[2];
            }
        }
        else if ((kw_len == 1 && *p == 'd') || (kw_len == 2 && SDL_strncmp(p, "Tr", 2) == 0))
        {
            float a = 1.0f;
            if (sdl3d_mtl_parse_floats(args, line_end, &a, 1))
            {
                /* `Tr` is transparency, `d` is opacity. Convert to
                 * alpha-style opacity in [0,1]. */
                if (kw_len == 2)
                {
                    a = 1.0f - a;
                }
                current->albedo[3] = a;
            }
        }
        else if (kw_len == 2 && SDL_strncmp(p, "Ke", 2) == 0)
        {
            float rgb[3] = {0.0f, 0.0f, 0.0f};
            if (sdl3d_mtl_parse_floats(args, line_end, rgb, 3))
            {
                current->emissive[0] = rgb[0];
                current->emissive[1] = rgb[1];
                current->emissive[2] = rgb[2];
            }
        }
        else if (kw_len == 2 && SDL_strncmp(p, "Pr", 2) == 0)
        {
            float v = 1.0f;
            if (sdl3d_mtl_parse_floats(args, line_end, &v, 1))
            {
                current->roughness = v;
            }
        }
        else if (kw_len == 2 && SDL_strncmp(p, "Pm", 2) == 0)
        {
            float v = 0.0f;
            if (sdl3d_mtl_parse_floats(args, line_end, &v, 1))
            {
                current->metallic = v;
            }
        }
        else if (kw_len == 6 && SDL_strncmp(p, "map_Kd", 6) == 0)
        {
            SDL_free(current->albedo_map);
            current->albedo_map = sdl3d_mtl_parse_map_path(args, line_end);
        }
        else if (kw_len == 6 && SDL_strncmp(p, "map_Ke", 6) == 0)
        {
            SDL_free(current->emissive_map);
            current->emissive_map = sdl3d_mtl_parse_map_path(args, line_end);
        }
        else if (kw_len == 6 && SDL_strncmp(p, "map_Pr", 6) == 0)
        {
            /* Roughness alone; store under metallic_roughness_map so the
             * material has a single MR channel field like glTF. */
            SDL_free(current->metallic_roughness_map);
            current->metallic_roughness_map = sdl3d_mtl_parse_map_path(args, line_end);
        }
        else if (kw_len == 6 && SDL_strncmp(p, "map_Pm", 6) == 0)
        {
            if (current->metallic_roughness_map == NULL)
            {
                current->metallic_roughness_map = sdl3d_mtl_parse_map_path(args, line_end);
            }
        }
        else if ((kw_len == 8 && SDL_strncmp(p, "map_Bump", 8) == 0) ||
                 (kw_len == 8 && SDL_strncmp(p, "map_bump", 8) == 0) || (kw_len == 4 && SDL_strncmp(p, "norm", 4) == 0))
        {
            SDL_free(current->normal_map);
            current->normal_map = sdl3d_mtl_parse_map_path(args, line_end);
        }
        /* Unknown directive: silently skip. */

        p = (line_end < end) ? line_end + 1 : end;
    }

    SDL_free(text);
    return true;
}

/* --------------------------------------------------------------------
 * OBJ face-vertex parsing. Each token is `v[/vt][/vn]` with 1-based
 * indices; negative values are resolved against the current pool size.
 * -------------------------------------------------------------------- */

static bool sdl3d_obj_resolve_index(long raw, size_t pool_count, int *out)
{
    if (raw > 0)
    {
        if ((size_t)raw > pool_count)
        {
            return false;
        }
        *out = (int)raw;
        return true;
    }
    if (raw < 0)
    {
        const long resolved = (long)pool_count + raw + 1;
        if (resolved <= 0)
        {
            return false;
        }
        *out = (int)resolved;
        return true;
    }
    return false;
}

static bool sdl3d_obj_parse_face_vertex(const char *tok, const char *tok_end, sdl3d_obj_state *s,
                                        sdl3d_obj_face_vertex *out)
{
    out->v = 0;
    out->vt = 0;
    out->vn = 0;

    long parts[3] = {0, 0, 0};
    bool present[3] = {false, false, false};
    int field = 0;
    const char *cursor = tok;

    while (cursor < tok_end && field < 3)
    {
        if (*cursor == '/')
        {
            ++cursor;
            ++field;
            continue;
        }
        char *tail = NULL;
        const long v = SDL_strtol(cursor, &tail, 10);
        if (tail == cursor)
        {
            ++cursor;
            continue;
        }
        parts[field] = v;
        present[field] = true;
        cursor = tail;
    }

    if (!present[0])
    {
        return false;
    }
    if (!sdl3d_obj_resolve_index(parts[0], s->positions.count, &out->v))
    {
        return false;
    }
    if (present[1] && !sdl3d_obj_resolve_index(parts[1], s->uvs.count, &out->vt))
    {
        return false;
    }
    if (present[2] && !sdl3d_obj_resolve_index(parts[2], s->normals.count, &out->vn))
    {
        return false;
    }
    return true;
}

/* --------------------------------------------------------------------
 * Mesh assembly. Unindexed expansion (3 vertices per triangle) keeps
 * the parser simple; dedup is a meshoptimizer concern.
 * -------------------------------------------------------------------- */

static bool sdl3d_obj_emit_triangle(const sdl3d_obj_state *s, const sdl3d_obj_face_vertex *a,
                                    const sdl3d_obj_face_vertex *b, const sdl3d_obj_face_vertex *c,
                                    sdl3d_obj_buffer *positions_out, sdl3d_obj_buffer *normals_out,
                                    sdl3d_obj_buffer *uvs_out, bool *have_any_normal, bool *have_any_uv)
{
    const sdl3d_obj_face_vertex *verts[3] = {a, b, c};
    const float *positions = (const float *)s->positions.data;
    const float *normals = (const float *)s->normals.data;
    const float *uvs = (const float *)s->uvs.data;

    for (int i = 0; i < 3; ++i)
    {
        const sdl3d_obj_face_vertex *fv = verts[i];
        const size_t p_index = (size_t)(fv->v - 1);
        if (p_index >= s->positions.count)
        {
            return SDL_SetError("OBJ vertex index %d out of range.", fv->v);
        }
        const float p[3] = {positions[p_index * 3 + 0], positions[p_index * 3 + 1], positions[p_index * 3 + 2]};
        if (!sdl3d_obj_buffer_push(positions_out, p))
        {
            return false;
        }

        float n[3] = {0.0f, 0.0f, 0.0f};
        if (fv->vn > 0)
        {
            const size_t idx = (size_t)(fv->vn - 1);
            if (idx >= s->normals.count)
            {
                return SDL_SetError("OBJ normal index %d out of range.", fv->vn);
            }
            n[0] = normals[idx * 3 + 0];
            n[1] = normals[idx * 3 + 1];
            n[2] = normals[idx * 3 + 2];
            *have_any_normal = true;
        }
        if (!sdl3d_obj_buffer_push(normals_out, n))
        {
            return false;
        }

        float uv[2] = {0.0f, 0.0f};
        if (fv->vt > 0)
        {
            const size_t idx = (size_t)(fv->vt - 1);
            if (idx >= s->uvs.count)
            {
                return SDL_SetError("OBJ texcoord index %d out of range.", fv->vt);
            }
            uv[0] = uvs[idx * 2 + 0];
            uv[1] = uvs[idx * 2 + 1];
            *have_any_uv = true;
        }
        if (!sdl3d_obj_buffer_push(uvs_out, uv))
        {
            return false;
        }
    }
    return true;
}

static bool sdl3d_obj_build_mesh(const sdl3d_obj_state *s, int begin_face, int end_face, sdl3d_mesh *out_mesh)
{
    SDL_zerop(out_mesh);
    const sdl3d_obj_face *faces = (const sdl3d_obj_face *)s->faces.data;
    const sdl3d_obj_face_vertex *fvs = (const sdl3d_obj_face_vertex *)s->face_vertices.data;

    sdl3d_obj_buffer positions, normals, uvs;
    sdl3d_obj_buffer_init(&positions, sizeof(float) * 3);
    sdl3d_obj_buffer_init(&normals, sizeof(float) * 3);
    sdl3d_obj_buffer_init(&uvs, sizeof(float) * 2);

    bool have_any_normal = false;
    bool have_any_uv = false;
    bool ok = true;

    for (int f = begin_face; f < end_face && ok; ++f)
    {
        const sdl3d_obj_face *face = &faces[f];
        for (int i = 1; i + 1 < face->count && ok; ++i)
        {
            const sdl3d_obj_face_vertex *a = &fvs[face->first];
            const sdl3d_obj_face_vertex *b = &fvs[face->first + i];
            const sdl3d_obj_face_vertex *c = &fvs[face->first + i + 1];
            ok = sdl3d_obj_emit_triangle(s, a, b, c, &positions, &normals, &uvs, &have_any_normal, &have_any_uv);
        }
    }

    if (!ok)
    {
        sdl3d_obj_buffer_free(&positions);
        sdl3d_obj_buffer_free(&normals);
        sdl3d_obj_buffer_free(&uvs);
        return false;
    }

    const int vertex_count = (int)positions.count;
    out_mesh->vertex_count = vertex_count;
    out_mesh->positions = (float *)positions.data;
    positions.data = NULL;
    if (have_any_normal)
    {
        out_mesh->normals = (float *)normals.data;
        normals.data = NULL;
    }
    sdl3d_obj_buffer_free(&normals);
    if (have_any_uv)
    {
        out_mesh->uvs = (float *)uvs.data;
        uvs.data = NULL;
    }
    sdl3d_obj_buffer_free(&uvs);

    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)vertex_count * sizeof(unsigned int));
    if (indices == NULL)
    {
        SDL_free(out_mesh->positions);
        SDL_free(out_mesh->normals);
        SDL_free(out_mesh->uvs);
        SDL_zerop(out_mesh);
        return SDL_OutOfMemory();
    }
    for (int i = 0; i < vertex_count; ++i)
    {
        indices[i] = (unsigned int)i;
    }
    out_mesh->indices = indices;
    out_mesh->index_count = vertex_count;
    out_mesh->material_index = faces[begin_face].material;
    return true;
}

/* --------------------------------------------------------------------
 * Entry point.
 * -------------------------------------------------------------------- */

static void sdl3d_obj_state_cleanup(sdl3d_obj_state *s, bool free_materials)
{
    sdl3d_obj_buffer_free(&s->positions);
    sdl3d_obj_buffer_free(&s->normals);
    sdl3d_obj_buffer_free(&s->uvs);
    sdl3d_obj_buffer_free(&s->face_vertices);
    sdl3d_obj_buffer_free(&s->faces);
    if (free_materials)
    {
        sdl3d_material *mats = (sdl3d_material *)s->materials.data;
        for (size_t i = 0; i < s->materials.count; ++i)
        {
            SDL_free(mats[i].name);
            SDL_free(mats[i].albedo_map);
            SDL_free(mats[i].normal_map);
            SDL_free(mats[i].metallic_roughness_map);
            SDL_free(mats[i].emissive_map);
        }
    }
    sdl3d_obj_buffer_free(&s->materials);
    SDL_free(s->group_name);
    SDL_free(s->dirname);
}

bool sdl3d_load_model_obj(const char *path, sdl3d_model *out)
{
    SDL_zerop(out);

    size_t size = 0;
    char *text = sdl3d_obj_load_text(path, &size);
    if (text == NULL)
    {
        return false;
    }

    sdl3d_obj_state s;
    sdl3d_obj_buffer_init(&s.positions, sizeof(float) * 3);
    sdl3d_obj_buffer_init(&s.normals, sizeof(float) * 3);
    sdl3d_obj_buffer_init(&s.uvs, sizeof(float) * 2);
    sdl3d_obj_buffer_init(&s.face_vertices, sizeof(sdl3d_obj_face_vertex));
    sdl3d_obj_buffer_init(&s.faces, sizeof(sdl3d_obj_face));
    sdl3d_obj_buffer_init(&s.materials, sizeof(sdl3d_material));
    s.current_material = -1;
    s.current_group = 0;
    s.group_name = NULL;
    s.dirname = sdl3d_obj_dirname_dup(path);

    const char *p = text;
    const char *end = text + size;

    while (p < end)
    {
        p = sdl3d_obj_skip_spaces(p, end);
        if (p >= end)
        {
            break;
        }
        if (*p == '\n')
        {
            ++p;
            continue;
        }
        if (*p == '#')
        {
            while (p < end && *p != '\n')
            {
                ++p;
            }
            continue;
        }

        const char *kw_end = sdl3d_obj_find_space(p, end);
        const size_t kw_len = (size_t)(kw_end - p);
        const char *args = sdl3d_obj_skip_spaces(kw_end, end);
        const char *line_end = args;
        while (line_end < end && *line_end != '\n')
        {
            ++line_end;
        }

        bool handled = false;

        if (kw_len == 1 && *p == 'v')
        {
            float xyz[3] = {0.0f, 0.0f, 0.0f};
            if (!sdl3d_mtl_parse_floats(args, line_end, xyz, 3) || !sdl3d_obj_buffer_push(&s.positions, xyz))
            {
                sdl3d_obj_state_cleanup(&s, true);
                SDL_free(text);
                if (!SDL_GetError()[0])
                {
                    SDL_SetError("OBJ: failed to parse vertex in '%s'.", path);
                }
                return false;
            }
            handled = true;
        }
        else if (kw_len == 2 && p[0] == 'v' && p[1] == 'n')
        {
            float xyz[3] = {0.0f, 0.0f, 0.0f};
            if (!sdl3d_mtl_parse_floats(args, line_end, xyz, 3) || !sdl3d_obj_buffer_push(&s.normals, xyz))
            {
                sdl3d_obj_state_cleanup(&s, true);
                SDL_free(text);
                return false;
            }
            handled = true;
        }
        else if (kw_len == 2 && p[0] == 'v' && p[1] == 't')
        {
            float uv[2] = {0.0f, 0.0f};
            if (!sdl3d_mtl_parse_floats(args, line_end, uv, 2) || !sdl3d_obj_buffer_push(&s.uvs, uv))
            {
                sdl3d_obj_state_cleanup(&s, true);
                SDL_free(text);
                return false;
            }
            handled = true;
        }
        else if (kw_len == 1 && *p == 'f')
        {
            sdl3d_obj_face face;
            face.first = (int)s.face_vertices.count;
            face.count = 0;
            face.material = s.current_material;
            face.group_signature = (s.current_group << 16) | (s.current_material & 0xFFFF);

            const char *cursor = args;
            while (cursor < line_end)
            {
                cursor = sdl3d_obj_skip_spaces(cursor, line_end);
                if (cursor >= line_end)
                {
                    break;
                }
                const char *tok_end = sdl3d_obj_find_space(cursor, line_end);
                if (tok_end == cursor)
                {
                    break;
                }
                sdl3d_obj_face_vertex fv;
                if (!sdl3d_obj_parse_face_vertex(cursor, tok_end, &s, &fv))
                {
                    sdl3d_obj_state_cleanup(&s, true);
                    SDL_free(text);
                    return SDL_SetError("OBJ: malformed face vertex in '%s'.", path);
                }
                if (!sdl3d_obj_buffer_push(&s.face_vertices, &fv))
                {
                    sdl3d_obj_state_cleanup(&s, true);
                    SDL_free(text);
                    return false;
                }
                face.count += 1;
                cursor = tok_end;
            }
            if (face.count >= 3)
            {
                if (!sdl3d_obj_buffer_push(&s.faces, &face))
                {
                    sdl3d_obj_state_cleanup(&s, true);
                    SDL_free(text);
                    return false;
                }
            }
            handled = true;
        }
        else if ((kw_len == 1 && (*p == 'o' || *p == 'g')))
        {
            SDL_free(s.group_name);
            s.group_name = sdl3d_obj_strdup_range(args, line_end);
            s.current_group += 1;
            handled = true;
        }
        else if (kw_len == 6 && SDL_strncmp(p, "usemtl", 6) == 0)
        {
            char *name = sdl3d_obj_strdup_range(args, line_end);
            if (name == NULL)
            {
                sdl3d_obj_state_cleanup(&s, true);
                SDL_free(text);
                return false;
            }
            size_t nl = SDL_strlen(name);
            while (nl > 0 && sdl3d_obj_is_space(name[nl - 1]))
            {
                name[--nl] = '\0';
            }
            int idx = sdl3d_obj_find_material(&s, name);
            if (idx < 0)
            {
                /* Forward reference: create a stub so the face can bind
                 * immediately; the mtllib pass may fill it in later. */
                sdl3d_material stub;
                sdl3d_obj_material_init(&stub, name);
                if (!sdl3d_obj_buffer_push(&s.materials, &stub))
                {
                    SDL_free(stub.name);
                    SDL_free(name);
                    sdl3d_obj_state_cleanup(&s, true);
                    SDL_free(text);
                    return false;
                }
                idx = (int)s.materials.count - 1;
            }
            SDL_free(name);
            s.current_material = idx;
            handled = true;
        }
        else if (kw_len == 6 && SDL_strncmp(p, "mtllib", 6) == 0)
        {
            const char *cursor = args;
            while (cursor < line_end)
            {
                cursor = sdl3d_obj_skip_spaces(cursor, line_end);
                if (cursor >= line_end)
                {
                    break;
                }
                const char *tok_end = sdl3d_obj_find_space(cursor, line_end);
                char *rel = sdl3d_obj_strdup_range(cursor, tok_end);
                if (rel == NULL)
                {
                    sdl3d_obj_state_cleanup(&s, true);
                    SDL_free(text);
                    return false;
                }
                char *full = sdl3d_obj_join_path(s.dirname, rel);
                SDL_free(rel);
                if (full == NULL)
                {
                    sdl3d_obj_state_cleanup(&s, true);
                    SDL_free(text);
                    return false;
                }
                const bool mtl_ok = sdl3d_mtl_load(&s, full);
                SDL_free(full);
                if (!mtl_ok)
                {
                    sdl3d_obj_state_cleanup(&s, true);
                    SDL_free(text);
                    return false;
                }
                cursor = tok_end;
            }
            handled = true;
        }

        (void)handled;
        p = (line_end < end) ? line_end + 1 : end;
    }

    /*
     * Partition faces into meshes by group_signature. We walk the face
     * list once, emitting a mesh whenever the signature changes; this
     * preserves face order within a mesh so triangulation stays stable.
     */
    sdl3d_obj_buffer mesh_buffer;
    sdl3d_obj_buffer_init(&mesh_buffer, sizeof(sdl3d_mesh));

    const sdl3d_obj_face *faces_data = (const sdl3d_obj_face *)s.faces.data;
    int run_begin = 0;
    for (int i = 0; i <= (int)s.faces.count; ++i)
    {
        const bool end_of_run =
            (i == (int)s.faces.count) || (faces_data[i].group_signature != faces_data[run_begin].group_signature);
        if (end_of_run && i > run_begin)
        {
            sdl3d_mesh mesh;
            if (!sdl3d_obj_build_mesh(&s, run_begin, i, &mesh))
            {
                sdl3d_mesh *meshes = (sdl3d_mesh *)mesh_buffer.data;
                for (size_t m = 0; m < mesh_buffer.count; ++m)
                {
                    SDL_free(meshes[m].positions);
                    SDL_free(meshes[m].normals);
                    SDL_free(meshes[m].uvs);
                    SDL_free(meshes[m].indices);
                    SDL_free(meshes[m].name);
                }
                sdl3d_obj_buffer_free(&mesh_buffer);
                sdl3d_obj_state_cleanup(&s, true);
                SDL_free(text);
                return false;
            }
            if (!sdl3d_obj_buffer_push(&mesh_buffer, &mesh))
            {
                SDL_free(mesh.positions);
                SDL_free(mesh.normals);
                SDL_free(mesh.uvs);
                SDL_free(mesh.indices);
                sdl3d_obj_buffer_free(&mesh_buffer);
                sdl3d_obj_state_cleanup(&s, true);
                SDL_free(text);
                return false;
            }
            run_begin = i;
        }
    }

    out->meshes = (sdl3d_mesh *)mesh_buffer.data;
    out->mesh_count = (int)mesh_buffer.count;
    mesh_buffer.data = NULL;

    out->materials = (sdl3d_material *)s.materials.data;
    out->material_count = (int)s.materials.count;
    s.materials.data = NULL;
    s.materials.count = 0;

    out->source_path = sdl3d_obj_strdup(path);

    sdl3d_obj_state_cleanup(&s, false);
    SDL_free(text);

    if (out->mesh_count == 0)
    {
        sdl3d_free_model(out);
        return SDL_SetError("OBJ '%s' produced no meshes.", path);
    }

    return true;
}
