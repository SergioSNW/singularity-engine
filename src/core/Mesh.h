#pragma once

#include "EngineMath.h"

#include <map>
#include <string>
#include <vector>

// CPU-side triangle mesh. `positions` is a triangle soup (every three entries
// form one triangle) so it can be handed straight to SDL_RenderGeometry after
// projection; `edge_lines` is a deduplicated list of wireframe segments (pairs
// of vertices). `uvs` parallels `positions` (three entries per triangle) and
// holds OBJ "vt" texture coordinates, flipped so v = 0 is the texture top —
// matching SDL's top-left UV origin; it is empty when the source has no UVs
// (the renderer then falls back to flat shading). `bounds_min/max` is the
// axis-aligned bounding box in local space, used for viewport picking.
struct Mesh
{
    std::string name;
    std::vector<Vec3> positions;   // groups of 3 = one triangle (soup)
    std::vector<Vec3> edge_lines;  // groups of 2 = one wireframe segment
    std::vector<Vec2> uvs;         // parallel to positions; empty = no UVs
    Vec3 bounds_min{0.0f, 0.0f, 0.0f};
    Vec3 bounds_max{0.0f, 0.0f, 0.0f};
};

// Asset-side mesh cache. Loads Wavefront .obj files once and hands out stable
// pointers keyed by asset path. The path is resolved against the current
// working directory and, as a fallback, against `assets/meshes/`.
class MeshLibrary
{
public:
    MeshLibrary();

    // Load (and cache) a mesh. Returns nullptr on failure and fills `error`.
    const Mesh* Load(const std::string &path, std::string *error = nullptr);

    const Mesh* Get(const std::string &path) const;
    const Mesh* GetOrLoad(const std::string &path, std::string *error = nullptr);

    // Unit cube used as the default procedural primitive. Entities without a
    // mesh asset render this cube.
    const Mesh* GetBuiltinCube() const;

private:
    std::map<std::string, Mesh> m_meshes;
};
