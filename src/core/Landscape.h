#pragma once

#include "EngineMath.h"

struct LandscapeComponent;
struct Mat4;

// Phase 34 landscape & topology design suite: procedural heightfield terrain
// with live sculpting. The LandscapeComponent owns the height grid + the
// generated mesh; this module owns geometry building, the sculpt kernels and
// picking. Everything is pure data + math, so it is testable headlessly.

// Which tool a brush stamp applies.
enum class SculptTool
{
    Raise,   // lift / lower the surface under the brush
    Smooth,  // blur heights toward the local neighbourhood average
    Flatten, // pull heights toward the height under the brush center
};

// Shared brush state: edited by the Landscape panel, consumed by the viewport
// override (Application) each frame. Pure data so the headless render path and
// the UI agree on one source of truth.
struct LandscapeBrushSettings
{
    SculptTool tool = SculptTool::Raise;
    float radius = 3.0f;   // brush radius in world units
    float strength = 0.5f; // per-second stamp amount
    float falloff = 0.6f;  // 0 = hard edge, 1 = fully soft
    int target_id = -1;    // entity id being sculpted (-1 = none)
};

// Fill `heights` with `base_height`, sized for `resolution`.
void LandscapeInitialize(LandscapeComponent &landscape);

// Rebuild `landscape.mesh` from `heights`: a triangle-soup grid (winding faces
// +Y), a sparse wireframe and the local AABB. Sets mesh_dirty = false.
void LandscapeRebuildMesh(LandscapeComponent &landscape);

// Apply one brush stamp in LOCAL grid space. `center` is the local-space point
// on the grid plane, `radius` the brush radius in local units. `strength` is
// applied as-is (the caller scales it by dt for time-based strokes).
void LandscapeSculpt(LandscapeComponent &landscape, SculptTool tool,
                     const Vec3 &center, float radius, float strength,
                     float falloff);

// Bilinear height sample at local (lx, lz). Returns `base_height` when the
// grid is uninitialized.
float LandscapeSampleHeightLocal(const LandscapeComponent &landscape,
                                 float lx, float lz);

// Ray vs the heightfield in world space. `world` is the landscape entity's
// world matrix (handles rotation + non-uniform scale via an affine inverse).
// Returns true and the world-space hit point on success.
bool LandscapeRaycast(const LandscapeComponent &landscape, const Mat4 &world,
                      const Vec3 &ray_origin, const Vec3 &ray_dir,
                      float &out_t, Vec3 &out_hit);

// Map a world-space point into the landscape's local grid space, and the
// average linear scale of `world` (for converting world-unit brush sizes).
Vec3 LandscapeWorldToLocal(const Mat4 &world, const Vec3 &world_point);
float LandscapeWorldScale(const Mat4 &world);
