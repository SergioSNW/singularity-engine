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
    // Optional reference to a .mat asset. When non-empty, MaterialLibrary
    // resolves it (assets/materials/...) and its texture/shininess override
    // the per-entity fields; the entity keeps its own `color` so the flat
    // fallback still applies when the asset has no texture. The editor fills
    // `texture_path` on drop so the lookup stays cheap.
    std::string material_path;
    std::string texture_path;
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

// Axis-aligned box collider for the physics bridge. `enabled` gates the entity
// in the physics step; the volume is the local-space box `center +/- extents`
// transformed by the entity's world matrix (so rotation/scale/parenting apply).
// `type` decides behavior: Solid boxes block other Solid boxes (the physics
// step separates the pair along the minimum-penetration axis), while Trigger
// boxes are pass-through ghost volumes that only raise OnTriggerEnter/Exit
// events on the script side. Defaults to disabled so only explicitly-colliding
// entities participate.
struct ColliderComponent
{
    enum class Type { Solid, Trigger };
    Type type = Type::Solid;
    bool enabled = false;
    Vec3 center{ 0.0f, 0.0f, 0.0f };
    Vec3 extents{ 0.5f, 0.5f, 0.5f };
};

// Gameplay script reference. An empty `path` means no script; any other value
// names a Lua file (e.g. "assets/scripts/player.lua") loaded by the
// ScriptEngine when the scene enters play mode. The ScriptEngine binds the
// entity's transform to the script's `transform`/`entity` globals and drives
// its OnStart / OnUpdate(dt) lifecycle hooks during play.
struct ScriptComponent
{
    std::string path;
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
