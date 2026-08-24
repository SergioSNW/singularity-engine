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
    std::vector<Vec3> normals;     // parallel to positions; averaged vertex normals for Gouraud shading
    std::vector<Vec3> edge_lines;  // groups of 2 = one wireframe segment
    std::vector<Vec2> uvs;         // parallel to positions; empty = no UVs
    std::vector<float> colors;     // parallel to positions * 3 (RGB per vertex); empty = use material color
    Vec3 bounds_min{0.0f, 0.0f, 0.0f};
    Vec3 bounds_max{0.0f, 0.0f, 0.0f};
};

// Path keys for the always-available structural primitives (block-out level
// design: Cube, Wall, Floor, Ramp). These never resolve to a file --
// MeshLibrary registers them in its cache at construction, and the normal
// GetOrLoad(entity.mesh.path) resolution path already serves a cache hit
// before ever touching the filesystem, so setting an entity's mesh.path to
// one of these "just works" through every existing render/spawn/placement
// path with no special-casing, the same way the historical builtin cube
// (used as the fallback for an *empty* mesh.path) already does.
extern const char *const kBuiltinCubePath;
extern const char *const kBuiltinWallPath;
extern const char *const kBuiltinFloorPath;
extern const char *const kBuiltinRampPath;

// Short, human-readable name for a builtin primitive path (e.g. a spawned
// entity's default tag, or a UI label) -- nullptr if `path` isn't one of the
// kBuiltin*Path constants above.
const char *BuiltinPrimitiveDisplayName(const std::string &path);

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

    // Estimated resident memory (bytes) across every cached mesh: the map
    // node plus the vector storage for positions / edge lines / UVs. A cheap
    // order-of-magnitude figure for the Profiler readout, not a malloc trace.
    size_t ResidentBytes() const;

private:
    std::map<std::string, Mesh> m_meshes;
};
