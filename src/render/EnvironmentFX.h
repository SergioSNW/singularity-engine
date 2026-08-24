#pragma once

#include <cstdint>
#include <vector>

#include "core/Environment.h"

struct SDL_Renderer;
struct SDL_Texture;

// Phase 37 environment FX: procedural skybox + CPU post-processing chain.
//
// The class owns every texture/buffer it touches and lazily reallocates when
// the region size or the settings signature changes. It is deliberately
// passive — Application drives it, so the camera preview can reuse the sky
// pass and skip post while the primary viewport runs the full chain.
//
// Sky: a streaming RGBA8888 texture the size of the requested region, filled
// with the procedural gradient + stars + sun disk/glow. Regenerated only when
// the camera basis (position/fwd/fov), the region size, or the sky settings
// change; otherwise the cached texture is blitted as-is (cheap).
//
// Post: reads an RGBA8888 render target (or a sub-rect of it) at full source
// resolution, runs bloom on a separate, smaller buffer (threshold -> gaussian
// blur -> bilinear-sampled additive composite) sized by post_scale, then
// applies exposure / temperature / saturation / contrast / ACES / gamma-LUT
// grading to every full-resolution pixel, writes the result to a streaming
// work texture, and blits it back over the region at 1:1. post_scale only
// controls bloom's internal resolution (cheap to shrink -- bloom is meant to
// look soft) so it trades bloom cost/fidelity for FPS without softening the
// rest of the image; it used to scale the whole pipeline's resolution,
// which is why turning it down used to blur the entire scene. Only cost
// when enabled and when the pixels actually changed (we do not know that
// here, so it runs every frame — but never more than once per region per
// frame).
class EnvironmentFX
{
public:
    ~EnvironmentFX();

    void Destroy(); // free all SDL resources (safe to call twice)

    // Draw the procedural sky into the current render target at (x, y, w, h).
    // `cam_pos`/`fwd`/`right`/`up` form the camera basis, `fov_deg` the
    // vertical FOV. Leaves the current render target untouched (draws into it).
    void DrawSky(SDL_Renderer *renderer, const EnvironmentSettings &env,
                 const float cam_pos[3], const float fwd[3], const float right[3],
                 const float up[3], float fov_deg, int x, int y, int w, int h);

    // Post-process the region (x, y, w, h) of the current render target in
    // place. Returns false when post is disabled (no work done). The render
    // target is left bound to `source`.
    bool PostProcess(SDL_Renderer *renderer, SDL_Texture *source,
                     int x, int y, int w, int h, const EnvironmentSettings &env);

private:
    void EnsureSky(SDL_Renderer *renderer, const EnvironmentSettings &env,
                   const float cam_pos[3], const float fwd[3], const float right[3],
                   const float up[3], float fov_deg, int w, int h);
    void RebuildSky(SDL_Renderer *renderer, const EnvironmentSettings &env,
                    const float fwd[3], const float right[3], const float up[3],
                    float fov_deg, int w, int h);

    void EnsureWork(SDL_Renderer *renderer, const EnvironmentSettings &env,
                    int src_w, int src_h);
    void RebuildLUT(const EnvironmentSettings &env);
    void BuildBloom(float *bloom, int bloom_w, int bloom_h,
                    const float *lin, int lin_w, int lin_h,
                    const EnvironmentSettings &env);

    static uint64_t SignatureFromSettings(const EnvironmentSettings &env,
                                          const float cam_pos[3],
                                          const float fwd[3], const float right[3],
                                          const float up[3], float fov_deg,
                                          int w, int h);

    // --- sky ---
    SDL_Texture *m_sky = nullptr;
    int m_sky_w = 0;
    int m_sky_h = 0;
    uint64_t m_sky_sig = 0;
    std::vector<uint32_t> m_sky_pixels;

    // --- post ---
    SDL_Texture *m_work = nullptr;   // streaming full-res work texture
    int m_work_w = 0;
    int m_work_h = 0;
    uint64_t m_work_sig = 0;
    std::vector<float>  m_lin;       // full-res scene RGB floats
    std::vector<float>  m_bloom;     // post_scale-sized blurred bright pass
    std::vector<float>  m_tmp;       // blur scratch (same size as m_bloom)
    std::vector<uint32_t> m_out;     // full-res RGBA output
    int m_bloom_w = 0;               // bloom buffer dims -- resized independently
    int m_bloom_h = 0;               // of m_work since post_scale no longer affects it
    uint8_t m_lut[4096];             // tonemap + gamma lookup
    uint8_t m_lut_tonemap = 0xFF;    // cache: rebuilt when gamma/tonemap change
    float   m_lut_gamma = -1.0f;
};
