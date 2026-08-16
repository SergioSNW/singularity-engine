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
//   bloom (thresholded bright pass, gaussian-blurred at half working res, added
//   back scaled by strength), then exposure + temperature + saturation +
//   contrast, an ACES filmic tone map, and output gamma. Everything runs at an
//   internal working resolution = target * post_scale (post_scale is the AA/
//   thermal knob: 0.5 = half of the supersampled target).
struct EnvironmentSettings
{
    // --- Sky ---
    bool sky_enabled = true;
    float sky_color_top[3] = { 0.09f, 0.20f, 0.45f };      // zenith stop
    float sky_color_horizon[3] = { 0.62f, 0.70f, 0.80f };  // horizon stop
    float sky_sun_color[3] = { 1.0f, 0.93f, 0.82f };
    float sky_sun_intensity = 0.9f;
    float sky_sun_glow = 0.38f;     // glow radius (fraction of the shorter axis)
    float sky_sun_disk = 0.012f;    // bright disk radius (same units)
    float sky_sun_yaw = 35.0f;      // sun direction heading (degrees)
    float sky_sun_pitch = 42.0f;    // sun direction elevation (degrees)
    float sky_star_intensity = 0.0f;// 0..1; 0 disables the star hash

    // --- Fog ---
    bool fog_enabled = true;
    float fog_color[3] = { 0.62f, 0.70f, 0.80f };
    float fog_density = 0.012f;     // 1/world-unit base extinction
    float fog_height_falloff = 0.08f;// exponential density falloff with height
    float fog_start = 5.0f;         // distance at which fog begins

    // --- Post-processing ---
    bool post_enabled = true;
    float post_exposure = 1.0f;
    float post_gamma = 2.2f;        // output gamma (display curve)
    bool post_bloom_enabled = true;
    float post_bloom_threshold = 0.92f;  // luminance above which pixels bloom
    float post_bloom_strength = 0.2f;    // additive gain of the blurred pass
    float post_bloom_radius = 2.0f;      // gaussian blur radius (1..4)
    bool post_tonemap_enabled = true;    // ACES filmic tone map
    float post_saturation = 1.0f;        // 0 = grayscale, 2 = vivid
    float post_contrast = 1.05f;         // pivot 0.5
    float post_temperature = 0.0f;       // -1 cool (blue) .. +1 warm (orange)
    float post_scale = 0.5f;             // working res fraction of the target
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
