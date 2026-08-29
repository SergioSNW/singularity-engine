#pragma once

#include <string>

// Global environment & post-processing settings (Phase 37).
//
// One `.env` asset (assets/environment/default.env, JSON through the engine's
// own json::Value serializer) owns the three blocks of the environment stack:
// the procedural sky, the exponential height fog, and the post-processing
// chain. It is *global* scene-independent state (like the theme or the editor
// layout): the Application loads it at startup, the Environment & Shading
// panel edits it live, and "Save" writes it back to disk. Edits apply
// immediately and are not part of the undo history.
//
// Sky — procedural skybox drawn behind the geometry:
//   sky_color_top/horizon are the zenith and horizon gradient stops, blended
//   by the per-pixel view ray's up component; sky_sun_* place a radial sun
//   glow+disk whose world direction comes from sky_sun_yaw/pitch; stars are an
//   optional deterministic point hash near the zenith.
// Fog — exponential height fog, applied per-triangle during rasterization:
//   fogFactor = 1 - exp(-density * (dist - fog_start) * exp(-height_falloff *
//   max(camY - worldY, 0))). Density grows below the camera height and decays
//   above it, so valleys fill with haze while hilltops stay clear.
// Post — a CPU post-processing chain run on the final viewport pixels:
//   bloom (thresholded bright pass, gaussian-blurred on a buffer sized by
//   post_scale, bilinear-sampled back and added scaled by strength), then
//   exposure + temperature + saturation + contrast, an ACES filmic tone map,
//   and output gamma applied to every full-resolution pixel. post_scale only
//   sizes the bloom buffer (cheap to shrink -- bloom is meant to look soft)
//   so it trades bloom cost/fidelity for frame time without softening the
//   rest of the image; it used to scale the whole post pipeline's working
//   resolution, which blurred the entire viewport at low settings.
struct EnvironmentSettings
{
    // --- Sky ---
    bool sky_enabled = true;
    float sky_color_top[3] = { 0.06f, 0.12f, 0.28f };      // zenith stop (dark moody blue)
    float sky_color_horizon[3] = { 0.22f, 0.25f, 0.32f };  // horizon stop (slate-gray)
    float sky_sun_color[3] = { 1.0f, 0.93f, 0.82f };
    float sky_sun_intensity = 0.65f;
    float sky_sun_glow = 0.22f;     // glow radius (fraction of the shorter axis)
    float sky_sun_disk = 0.012f;    // bright disk radius (same units)
    float sky_sun_yaw = 35.0f;      // sun direction heading (degrees)
    float sky_sun_pitch = 42.0f;    // sun direction elevation (degrees)
    float sky_star_intensity = 0.0f;// 0..1; 0 disables the star hash

    // --- Fog ---
    bool fog_enabled = true;
    float fog_color[3] = { 0.22f, 0.25f, 0.32f };
    float fog_density = 0.012f;     // 1/world-unit base extinction
    float fog_height_falloff = 0.08f;// exponential density falloff with height
    float fog_start = 5.0f;         // distance at which fog begins

    // --- Post-processing ---
    // Off by default: this is a CPU software-rasterizer readback pass
    // (SDL_RenderReadPixels + a full-resolution per-pixel grade), not a free
    // GPU post effect. It costs ~200ms/frame even with tuned "reasonable"
    // values, which tanks the editor to 3-5 FPS in every workspace since
    // Lit mode is the default render mode everywhere. Bloom/grading are an
    // opt-in polish layer now, not an always-on tax -- flip "Enable Post" in
    // the Environment & Shading panel when you actually want them and are
    // willing to pay for them.
    bool post_enabled = false;
    float post_exposure = 1.0f;
    float post_gamma = 2.2f;        // output gamma (display curve)
    bool post_bloom_enabled = true;
    float post_bloom_threshold = 0.92f;  // luminance above which pixels bloom
    float post_bloom_strength = 0.12f;   // additive gain of the blurred pass
    float post_bloom_radius = 2.0f;      // gaussian blur radius (1..4)
    bool post_tonemap_enabled = true;    // ACES filmic tone map
    float post_saturation = 1.0f;        // 0 = grayscale, 2 = vivid
    float post_contrast = 1.10f;         // pivot 0.5
    float post_temperature = 0.0f;       // -1 cool (blue) .. +1 warm (orange)
    float post_scale = 1.0f;             // bloom buffer resolution fraction (output stays full-res)

    // --- Editor Working Light ---
    // A flat, unshadowed ambient fill applied only while editing (Editor
    // state, Lit mode -- never in Play mode, so gameplay lighting is judged
    // on its own). Existing per-light `ambient` (Components.h) already sets a
    // floor for *that* light's own contribution, but a shadowed or
    // grazing-angle face still reads as ambient*intensity, which can go
    // near-black. This adds a second, independent floor on top: not part of
    // any light, so it is never attenuated by DirectionalShadowFactor and
    // never disappears just because a level has no lights configured yet --
    // it exists purely so terrain valleys, primitive undersides, and
    // unlit-looking faces stay readable from any camera angle while you work.
    bool editor_fill_light_enabled = true;
    float editor_fill_light_intensity = 0.35f; // 0 = off, 1 = fully flat/shadowless

    // --- Audio (Stage 4) ---
    // A single global gain over every sound the AudioManager plays --
    // footsteps, jump/land, ambient beds, Lua's Audio.Play(), the Inspector's
    // Preview buttons, all of it. Lives here rather than on AudioManager
    // itself because this whole struct is exactly "scene-independent state
    // the Environment & Shading panel edits live and persists to disk" --
    // the same reason sky/fog/post/the editor light live here instead of on
    // whatever subsystem actually consumes them.
    float master_volume = 1.0f;
};

// Serialize to / from the .env JSON layout described above.
std::string EnvironmentSettingsToJson(const EnvironmentSettings &env);
bool        EnvironmentSettingsFromJson(const std::string &text,
                                        EnvironmentSettings &out,
                                        std::string *error = nullptr);

// Asset helpers over assets/environment/. Load reads the file (leaving `out`
// untouched on failure); Save writes it, creating the directory on demand.
bool LoadEnvironmentAsset(const std::string &path, EnvironmentSettings &out,
                          std::string *error = nullptr);
bool SaveEnvironmentAsset(const std::string &path, const EnvironmentSettings &env,
                          std::string *error = nullptr);
