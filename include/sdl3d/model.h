#ifndef SDL3D_MODEL_H
#define SDL3D_MODEL_H

#include <stdbool.h>

#include "sdl3d/image.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Unified PBR-friendly material description. Every loader fills the
     * subset of fields its source format supports; unknown fields keep
     * their defaults so consumers never have to branch on the source
     * format.
     *
     * Texture fields are stored exactly as authored by the source asset
     * (typically paths relative to the model's source directory). Images
     * are NOT decoded at load time — callers
     * choose when to realize them into sdl3d_texture2d, so scenes can be
     * loaded without touching disk twice.
     */
    typedef struct sdl3d_material
    {
        char *name;

        float albedo[4];   /* RGBA factor; default {1,1,1,1} */
        float metallic;    /* [0,1]; default 0 */
        float roughness;   /* [0,1]; default 1 */
        float emissive[3]; /* linear RGB factor; default {0,0,0} */

        char *albedo_map;
        char *normal_map;
        char *metallic_roughness_map;
        char *emissive_map;
    } sdl3d_material;

    /*
     * A single renderable submesh. Positions are required; every other
     * attribute array is optional (NULL when absent).
     *
     * Vertex attributes are de-interleaved: each pointer is an array of
     * `vertex_count` entries with the stride implied by its semantic
     * (3 floats for positions/normals, 2 for uvs, 4 for colors).
     * Colors, when present, are RGBA floats in [0, 1].
     *
     * Indices are always 32-bit unsigned and index into this mesh's
     * vertex arrays. `material_index` is -1 when no material is bound.
     */
    typedef struct sdl3d_mesh
    {
        char *name;

        float *positions;
        float *normals;
        float *uvs;
        float *colors;
        int vertex_count;

        unsigned int *indices;
        int index_count;

        int material_index;

        /* Skinning attributes (NULL when no skeleton). Up to 4 joints
         * per vertex. joint_indices indexes into the model's skeleton
         * joint array. joint_weights sum to 1.0 per vertex. */
        unsigned short *joint_indices; /* vertex_count * 4 */
        float *joint_weights;          /* vertex_count * 4 */
    } sdl3d_mesh;

    /*
     * A loaded model is a flat list of meshes plus the material palette
     * they reference by index. `source_path` is kept so higher layers
     * can resolve texture paths relative to the original file.
     */
    struct sdl3d_skeleton;
    struct sdl3d_animation_clip;

    typedef struct sdl3d_model
    {
        sdl3d_mesh *meshes;
        int mesh_count;

        sdl3d_material *materials;
        int material_count;

        char *source_path;

        struct sdl3d_skeleton *skeleton;
        struct sdl3d_animation_clip *animations;
        int animation_count;

        /* Embedded textures decoded at load time (e.g. from GLB buffer views).
         * Materials reference these by index via albedo_map = "#0", "#1", etc. */
        sdl3d_image *embedded_textures;
        int embedded_texture_count;
    } sdl3d_model;

    /*
     * Dispatching entry point. Picks a loader by file extension:
     *   .obj            -> in-house OBJ + MTL parser
     *   .gltf / .glb    -> cgltf (reserved; returns error until M3 slice)
     *   .fbx            -> ufbx (reserved; returns error until M3 slice)
     *
     * Returns false with SDL_GetError populated on any failure.
     */
    bool sdl3d_load_model_from_file(const char *path, sdl3d_model *out);

    /*
     * Release everything owned by the model, including mesh vertex
     * buffers, material strings, and the source path. Safe on a zero-
     * initialized struct.
     */
    void sdl3d_free_model(sdl3d_model *model);

#ifdef __cplusplus
}
#endif

#endif
