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

// Which tool a brush stamp applies. Raise/Lower/Flatten/Smooth are the
// terrain-sculpting brushes (edit height); Paint is the splatting brush
// (edit per-vertex color) -- kept in the same enum since both are "brush
// stamps" driven by the same radius/strength/falloff math in LandscapeSculpt,
// but the editor UI presents them as two distinct modes, not one tool list.
enum class SculptTool
{
    Raise,   // lift the surface under the brush
    Lower,   // push the surface down under the brush
    Flatten, // pull heights toward the height under the brush center
    Smooth,  // blur heights toward the local neighbourhood average
    Paint,   // apply vertex color (material) to the terrain surface
};

// Shape of the brush's edge transition, applied within the fade band that
// `falloff` (below) sizes. Smooth eases in with a cubic curve, blending
// gently into untouched terrain or a neighboring stroke. Sharp falls off at
// a constant rate instead -- reads as a harder, more deliberate edge, useful
// for cliffs or a crisp material boundary between two paint layers.
enum class BrushFalloffProfile
{
    Smooth,
    Sharp,
};

// Shared brush state: edited by the Landscape panel, consumed by the viewport
// override (Application) each frame. Pure data so the headless render path and
// the UI agree on one source of truth.
struct LandscapeBrushSettings
{
    SculptTool tool = SculptTool::Raise;
    float radius = 3.0f;   // brush radius in world units
    // Per-second stamp amount. Tuned so a brief, deliberate hold (a few
    // hundred ms) reads as an immediate change instead of a slow fade-in --
    // still adjustable down to 0.01 for fine, incremental strokes.
    float strength = 1.5f;
    float falloff = 0.6f;  // 0 = hard edge, 1 = fully soft
    BrushFalloffProfile falloff_profile = BrushFalloffProfile::Smooth;
    int target_id = -1;    // entity id being sculpted (-1 = none)
    // Paint mode: RGB color applied to vertices under the brush.
    float paint_color[3] = { 0.30f, 0.55f, 0.20f }; // default: grass green
};

// One shared material swatch list for every paint entry point -- the
// Landscape panel's own Paint mode and the viewport toolbar's Paint Mode --
// so both present the exact same palette instead of two independent lists
// that happen to look similar. Colors are deliberately saturated and
// distinct from one another (and from the default unpainted white) so a
// stroke reads immediately, without needing a bound texture. `mat_file`
// names a .mat asset under assets/materials/, used only when painting onto
// an entity (LandscapeSculpt's Paint tool itself only ever touches
// per-vertex color, never material_path).
struct PaintMaterialPreset { const char *name; float color[3]; const char *mat_file; };
inline constexpr PaintMaterialPreset kLandscapePaintPalette[] = {
    { "Grass", { 0.30f, 0.55f, 0.20f }, "Grass.mat" },
    { "Stone", { 0.45f, 0.42f, 0.38f }, "Stone.mat" },
    { "Metal", { 0.35f, 0.45f, 0.62f }, "Metal.mat" },
    { "Dirt",  { 0.40f, 0.28f, 0.15f }, "Dirt.mat" },
};
inline constexpr int kLandscapePaintPaletteCount =
    (int)(sizeof(kLandscapePaintPalette) / sizeof(kLandscapePaintPalette[0]));

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
                     float falloff, const float paint_color[3] = nullptr,
                     BrushFalloffProfile profile = BrushFalloffProfile::Smooth);

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
