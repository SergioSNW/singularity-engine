#pragma once

#include "Animation.h"
#include "EngineMath.h"
#include "Mesh.h"

#include <memory>
#include <string>
#include <vector>

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
//
// Phase 36 physics authoring: `layers` is a bitmask of which collision layers
// the collider belongs to (bit i = the i-th row of the scene's CollisionMatrix;
// bit 0 = "Default" is set by default). Two bodies interact during the step
// only when the matrix allows any layer of one to collide with any layer of
// the other — toggle the pairs in the Collision Matrix panel. `physics_material`
// names a .pmat asset (assets/physics/) resolved by the PhysicsMaterialLibrary;
// empty means the library's Default material (friction 0.5, restitution 0.1).
struct ColliderComponent
{
    enum class Type { Solid, Trigger };
    Type type = Type::Solid;
    bool enabled = false;
    Vec3 center{ 0.0f, 0.0f, 0.0f };
    Vec3 extents{ 0.5f, 0.5f, 0.5f };
    unsigned int layers = 1u;        // membership bitmask (bit 0 = Default layer)
    std::string physics_material;    // .pmat asset key, or empty = Default
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

// Sound effect reference for the audio bridge. An empty `path` means no audio;
// any other value names an audio asset (e.g. "assets/audio/beep.wav" or
// ".ogg") loaded on demand by the AudioManager when the entity plays. `volume`
// (0..1) scales the playback, `loop` repeats the clip until stopped, and
// `auto_play` fires it once when the scene enters play mode so ambient/looping
// sounds can start without a script. Scripts drive it at runtime through the
// Audio.Play(path, volume, loop) / Audio.Stop(path) Lua bindings.
struct AudioComponent
{
    std::string path;
    bool loop = false;
    float volume = 1.0f;
    bool auto_play = false;
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

// Directional light for the forward-shading pipeline. Light travels along
// `direction` (world space; normalized by the renderer), so a surface is lit
// when its normal faces the light source. `color` tints the diffuse term,
// `intensity` scales it, and `ambient` is the view-independent floor added to
// every surface. The shadow group tunes the cheap ray-cast directional shadow:
// `shadow_strength` is how dark occluded surfaces get (0 = none, 1 = black),
// `shadow_bias` offsets the shadow ray along the surface normal to avoid
// self-shadow acne, and `shadow_distance` is the max blocker distance before
// the shadow fades out. `active` gates the light in the renderer.
struct DirectionalLightComponent
{
    bool    active = false;
    float   color[3] = { 1.0f, 1.0f, 1.0f };
    float   intensity = 0.75f;
    float   direction[3] = { 0.4f, -0.8f, -0.45f };
    float   ambient = 0.10f;
    float   shadow_strength = 0.6f;
    float   shadow_bias = 0.05f;
    float   shadow_distance = 30.0f;
};

// Procedural heightfield terrain (Phase 34). When `enabled`, the entity owns a
// scalable grid of vertices: `heights` stores one height per vertex over a
// (resolution+1) x (resolution+1) grid spanning `size` x `size` world units
// centered on the entity origin (row-major, [row * stride + col]). The editor
// sculpts `heights` live (Raise / Smooth / Flatten) through the Landscape
// module, which mirrors them into `mesh` — a runtime-generated triangle soup +
// sparse wireframe + bounds, rebuilt on demand via `mesh_dirty`. The mesh is
// never serialized: on load the heights are restored and the mesh is
// regenerated on the first render frame.
struct LandscapeComponent
{
    bool enabled = false;
    int resolution = 64;           // grid cells per side (vertices = +1)
    float size = 40.0f;            // world units per side (x / z extent)
    float base_height = 0.0f;      // starting height for every vertex
    std::vector<float> heights;    // (resolution+1)^2 row-major; empty until initialized
    std::vector<float> colors;     // (resolution+1)^2 * 3 row-major RGB; painted vertex colors
    std::shared_ptr<Mesh> mesh;    // generated geometry (null until built)
    bool mesh_dirty = true;        // rebuild `mesh` from `heights`

    // Phase A heightmap import: `heights` above can come from an image
    // instead of sculpting. Kept as plain state (not a one-shot action) so
    // the Landscape panel can show what's currently loaded and re-import at
    // a different scale/resolution. Empty path = heights are hand-sculpted
    // or procedural, not image-derived.
    std::string heightmap_path;
    float heightmap_scale = 5.0f;  // world-unit height multiplier for the loaded heightmap
};
