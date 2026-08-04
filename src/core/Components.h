#pragma once

#include "EngineMath.h"

#include <string>

struct TransformComponent
{
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3]    = {1.0f, 1.0f, 1.0f};
};

struct TagComponent
{
    std::string tag;
};

struct MaterialComponent
{
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool active = true;
};

// Geometry reference. An empty `path` selects the built-in cube primitive;
// any other value names an asset resolved by MeshLibrary (assets/meshes/...).
struct MeshComponent
{
    std::string path;
};

// Local-space axis-aligned bounding box of the entity's mesh geometry. The
// mesh library computes each mesh's box once at load (Mesh::bounds_min/max);
// this component mirrors it onto the entity and is refreshed automatically
// every render frame, so it always reflects the resolved mesh even after
// editor mesh changes or scene loads. `TransformAABB` derives the world-space
// box for picking / collision / visualization.
struct BoundsComponent
{
    Vec3 local_min{0.0f, 0.0f, 0.0f};
    Vec3 local_max{0.0f, 0.0f, 0.0f};
};

struct CameraComponent
{
    float fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float pitch = 0.0f;   // degrees, about the camera's local right axis
    float yaw = 0.0f;     // degrees, about the world Y axis
    bool primary = false;
};
