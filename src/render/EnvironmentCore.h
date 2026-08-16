#pragma once

// Pure, header-only math for the Phase 37 environment stack (sky gradient,
// height fog, tone mapping, color grading). No SDL, no filesystem, no engine
// dependencies — only <cmath>/<cstdint> — so it can be exercised from a tiny
// scratch harness with the MSYS2 g++ toolchain (which cannot link anything
// touching <filesystem>).
//
// All color values are linear-ish scene space on input (the raw per-pixel RGB
// the rasterizer produced), and post-processing emits display-space 0..1 RGB.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace env
{

// ---------------------------------------------------------------------------
// Tone mapping & color grading
// ---------------------------------------------------------------------------

inline float Luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ACES filmic fit (Narkowicz). Maps [0, inf) -> [0, 1) with a long soft tail.
inline float Aces(float x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return std::max(0.0f, std::min(1.0f, (x * (a * x + b)) / (x * (c * x + d) + e)));
}

struct PostParams
{
    float exposure = 1.0f;
    float gamma = 2.2f;
    float bloom_strength = 0.7f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float temperature = 0.0f;      // -1 cool .. +1 warm
    bool  tonemap_enabled = true;
};

inline void Clamp01(float &v)
{
    if (!std::isfinite(v))
        v = 0.0f;
    else
        v = std::max(0.0f, std::min(1.0f, v));
}

// Full per-pixel post chain. `in` is the scene-linear pixel, `bloom` the
// pre-tone-mapped blurred bright-pass (0 if disabled). Bloom is added *before*
// exposure so highlights survive tone mapping.
inline void PostProcess(const float in[3], const float bloom[3],
                        const PostParams &p, float out[3])
{
    float r = in[0] + p.bloom_strength * bloom[0];
    float g = in[1] + p.bloom_strength * bloom[1];
    float b = in[2] + p.bloom_strength * bloom[2];

    r *= p.exposure;
    g *= p.exposure;
    b *= p.exposure;

    if (p.temperature != 0.0f)
    {
        const float cool[3] = { 0.45f, 0.60f, 1.05f };
        const float warm[3] = { 1.05f, 0.85f, 0.55f };
        const float t = p.temperature * 0.5f + 0.5f;
        r *= cool[0] + (warm[0] - cool[0]) * t;
        g *= cool[1] + (warm[1] - cool[1]) * t;
        b *= cool[2] + (warm[2] - cool[2]) * t;
    }

    if (p.saturation != 1.0f)
    {
        const float lum = Luminance(r, g, b);
        r = lum + (r - lum) * p.saturation;
        g = lum + (g - lum) * p.saturation;
        b = lum + (b - lum) * p.saturation;
    }

    if (p.contrast != 1.0f)
    {
        r = (r - 0.5f) * p.contrast + 0.5f;
        g = (g - 0.5f) * p.contrast + 0.5f;
        b = (b - 0.5f) * p.contrast + 0.5f;
    }

    if (p.tonemap_enabled)
    {
        r = Aces(r);
        g = Aces(g);
        b = Aces(b);
    }

    const float inv_gamma = 1.0f / std::max(0.1f, p.gamma);
    out[0] = std::pow(std::max(0.0f, r), inv_gamma);
    out[1] = std::pow(std::max(0.0f, g), inv_gamma);
    out[2] = std::pow(std::max(0.0f, b), inv_gamma);
    Clamp01(out[0]);
    Clamp01(out[1]);
    Clamp01(out[2]);
}

// ---------------------------------------------------------------------------
// Exponential height fog (per-triangle during rasterization)
// ---------------------------------------------------------------------------

// Fog fraction in [0,1] for a triangle at distance `dist` whose representative
// world Y is `world_y`, viewed from `cam_y`. Density grows below the camera
// (valleys haze up) and decays above it (summits stay clear): the density term
// is exp(-height_falloff * (world_y - cam_y)), so a triangle sunk in a valley
// sees extinction > 1 and fog saturates quickly.
inline float HeightFog(float density, float height_falloff, float fog_start,
                       float dist, float cam_y, float world_y)
{
    const float beyond = dist - fog_start;
    if (density <= 0.0f || beyond <= 0.0f)
        return 0.0f;
    const float extinction = std::exp(-height_falloff * (world_y - cam_y));
    float f = 1.0f - std::exp(-density * beyond * extinction);
    return std::max(0.0f, std::min(1.0f, f));
}

// ---------------------------------------------------------------------------
// Procedural sky
// ---------------------------------------------------------------------------

// World direction (unit) of the sun from yaw/pitch in degrees. +Y is up.
inline void SunDirection(float yaw_deg, float pitch_deg, float out[3])
{
    const float p = pitch_deg * 3.14159265358979323846f / 180.0f;
    const float y = yaw_deg * 3.14159265358979323846f / 180.0f;
    const float cy = std::cos(p), sy = std::sin(p);
    const float cz = std::cos(y), sz = std::sin(y);
    out[0] = cy * sz;
    out[1] = sy;
    out[2] = cy * cz;
}

struct SkyParams
{
    float top[3] = { 0.09f, 0.20f, 0.45f };
    float horizon[3] = { 0.62f, 0.70f, 0.80f };
    float star_intensity = 0.0f;   // 0 disables stars
};

// Zenith->horizon gradient for a unit view ray (world +Y up). Below the
// horizon the sky darkens toward the horizon color (earth shadow).
inline void SkyGradient(const SkyParams &p, const float ray[3], float out[3])
{
    if (ray[1] <= 0.0f)
    {
        out[0] = p.horizon[0] * 0.85f;
        out[1] = p.horizon[1] * 0.85f;
        out[2] = p.horizon[2] * 0.85f;
        return;
    }
    const float t = std::pow(std::min(1.0f, ray[1]), 0.5f);
    out[0] = p.horizon[0] + (p.top[0] - p.horizon[0]) * t;
    out[1] = p.horizon[1] + (p.top[1] - p.horizon[1]) * t;
    out[2] = p.horizon[2] + (p.top[2] - p.horizon[2]) * t;
}

// Deterministic, cheap star hash on a unit direction. Returns a value in
// [0,1); individual "stars" (cells whose hash clears a sparse threshold) are
// picked out in SkyStars.
inline float StarHash(float x, float y, float z)
{
    const uint32_t xi = (uint32_t)std::floor(x * 2048.0f);
    const uint32_t yi = (uint32_t)std::floor(y * 2048.0f);
    const uint32_t zi = (uint32_t)std::floor(z * 2048.0f);
    uint32_t h = xi * 73856093u ^ yi * 19349663u ^ zi * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xffffu) / 65535.0f;
}

// Additive star contribution scaled by intensity. Sparse, brightest near the
// zenith band where the hash cell is large enough.
inline void SkyStars(const SkyParams &p, const float ray[3], float out[3])
{
    const float threshold = 0.998f;
    const float v = StarHash(ray[0], ray[1], ray[2]);
    if (p.star_intensity <= 0.0f || v < threshold)
    {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float b = (v - threshold) / (1.0f - threshold) * p.star_intensity;
    out[0] = b;
    out[1] = b;
    out[2] = b * 1.1f;
}

} // namespace env
