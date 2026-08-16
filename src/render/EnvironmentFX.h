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
// Post: reads an RGBA8888 render target (or a sub-rect of it) at the working
// resolution = region * post_scale, runs bloom (threshold -> half-res gaussian
// blur -> additive, sampled nearest) then exposure / temperature / saturation /
// contrast / ACES / gamma-LUT, writes the result to a streaming work texture,
// and blits it back over the region. Only cost when enabled and when the
// pixels actually changed (we do not know that here, so it runs every frame —
// but never more than once per region per frame).
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
    SDL_Texture *m_work = nullptr;   // streaming working-res texture
    int m_work_w = 0;
    int m_work_h = 0;
    uint64_t m_work_sig = 0;
    std::vector<float>  m_lin;       // working-res scene RGB floats
    std::vector<float>  m_bloom;     // half-res blurred bright pass
    std::vector<float>  m_tmp;       // blur scratch (half res)
    std::vector<uint32_t> m_out;     // working-res RGBA output
    uint8_t m_lut[4096];             // tonemap + gamma lookup
    uint8_t m_lut_tonemap = 0xFF;    // cache: rebuilt when gamma/tonemap change
    float   m_lut_gamma = -1.0f;
};
