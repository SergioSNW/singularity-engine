#pragma once

#include "EngineMath.h"

#include <string>

struct LandscapeComponent;
struct Mat4;

// Grid resolution LandscapeLoadHeightmap will build a heightmap up to,
// independent of the source image's native pixel size. This bounds more than
// the one-time import cost: LandscapeRebuildMesh reruns every single frame
// while the user is actively sculpting (LandscapeSculpt sets mesh_dirty on
// every stroke tick, by design, for live feedback), so the resolution here
// caps the worst-case *per-frame* CPU cost, not just a one-shot load. 128 is
// 4x the historical default (64) in cell count -- enough headroom to look
// meaningfully more detailed without turning every sculpt stroke into a
// multi-frame stall. Raise with real profiling data, not by feel.
constexpr int kMaxHeightmapResolution = 128;
constexpr int kMinHeightmapResolution = 4;

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
    Paint,   // apply vertex color (material) to the terrain surface
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
    // Paint mode: RGB color applied to vertices under the brush.
    float paint_color[3] = { 0.30f, 0.55f, 0.20f }; // default: grass green
};

// Fill `heights` with `base_height`, sized for `resolution`.
void LandscapeInitialize(LandscapeComponent &landscape);

// Load a grayscale-intensity heightmap image (any format stb_image supports)
// from `path` and rebuild `landscape` from it: the image is bilinearly
// resampled onto a (target_resolution+1)^2 grid -- independent of the source
// image's native size, and clamped to [kMinHeightmapResolution,
// kMaxHeightmapResolution] -- so a huge source texture still yields a grid
// the rasterizer can handle. Each vertex's height is
// `base_height + luminance * heightmap_scale`. Also rebuilds `landscape.mesh`
// (with smooth normals) so it is ready to render immediately. On success,
// sets `landscape.heightmap_path` to `path` and `landscape.resolution` to the
// clamped target. Returns false and fills `error` on failure (file not
// found, decode failure), leaving `landscape` unchanged.
bool LandscapeLoadHeightmap(LandscapeComponent &landscape, const std::string &path,
                            int target_resolution, std::string *error = nullptr);

// Rebuild `landscape.mesh` from `heights`: a triangle-soup grid (winding faces
// +Y), a sparse wireframe and the local AABB. Sets mesh_dirty = false.
void LandscapeRebuildMesh(LandscapeComponent &landscape);

// Apply one brush stamp in LOCAL grid space. `center` is the local-space point
// on the grid plane, `radius` the brush radius in local units. `strength` is
// applied as-is (the caller scales it by dt for time-based strokes).
void LandscapeSculpt(LandscapeComponent &landscape, SculptTool tool,
                     const Vec3 &center, float radius, float strength,
                     float falloff, const float paint_color[3] = nullptr);

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
