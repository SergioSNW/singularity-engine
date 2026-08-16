#include "EnvironmentFX.h"

#include "EnvironmentCore.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

constexpr float kPi = 3.14159265358979323846f;

inline uint64_t HashBits(uint64_t seed, const void *data, size_t n)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < n; ++i)
    {
        seed ^= p[i];
        seed *= 0x100000001b3ull;
    }
    return seed;
}

inline uint64_t HashFloat(uint64_t seed, float v)
{
    return HashBits(seed, &v, sizeof(v));
}

inline float SmoothStep(float edge0, float edge1, float x)
{
    const float d = edge1 - edge0;
    if (d <= 0.0f)
        return (x >= edge1) ? 1.0f : 0.0f;
    float t = (x - edge0) / d;
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

EnvironmentFX::~EnvironmentFX()
{
    Destroy();
}

void EnvironmentFX::Destroy()
{
    if (m_sky)
    {
        SDL_DestroyTexture(m_sky);
        m_sky = nullptr;
    }
    if (m_work)
    {
        SDL_DestroyTexture(m_work);
        m_work = nullptr;
    }
    m_sky_pixels.clear();
    m_lin.clear();
    m_bloom.clear();
    m_tmp.clear();
    m_out.clear();
    m_sky_w = m_sky_h = 0;
    m_work_w = m_work_h = 0;
    m_sky_sig = m_work_sig = 0;
}

// ---------------------------------------------------------------------------
// Signature / caching
// ---------------------------------------------------------------------------

uint64_t EnvironmentFX::SignatureFromSettings(const EnvironmentSettings &env,
                                              const float cam_pos[3],
                                              const float fwd[3],
                                              const float right[3],
                                              const float up[3], float fov_deg,
                                              int w, int h)
{
    uint64_t sig = 0xcbf29ce484222325ull;
    sig = HashBits(sig, cam_pos, sizeof(float) * 3);
    sig = HashBits(sig, fwd, sizeof(float) * 3);
    sig = HashBits(sig, right, sizeof(float) * 3);
    sig = HashBits(sig, up, sizeof(float) * 3);
    sig = HashFloat(sig, fov_deg);
    sig = HashFloat(sig, (float)w);
    sig = HashFloat(sig, (float)h);

    sig = HashFloat(sig, env.sky_enabled ? 1.0f : 0.0f);
    sig = HashBits(sig, env.sky_color_top, sizeof(env.sky_color_top));
    sig = HashBits(sig, env.sky_color_horizon, sizeof(env.sky_color_horizon));
    sig = HashBits(sig, env.sky_sun_color, sizeof(env.sky_sun_color));
    sig = HashFloat(sig, env.sky_sun_intensity);
    sig = HashFloat(sig, env.sky_sun_glow);
    sig = HashFloat(sig, env.sky_sun_disk);
    sig = HashFloat(sig, env.sky_sun_yaw);
    sig = HashFloat(sig, env.sky_sun_pitch);
    sig = HashFloat(sig, env.sky_star_intensity);
    return sig;
}

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------

void EnvironmentFX::DrawSky(SDL_Renderer *renderer, const EnvironmentSettings &env,
                            const float cam_pos[3], const float fwd[3],
                            const float right[3], const float up[3], float fov_deg,
                            int x, int y, int w, int h)
{
    if (!env.sky_enabled || !renderer || w <= 0 || h <= 0)
        return;

    EnsureSky(renderer, env, cam_pos, fwd, right, up, fov_deg, w, h);

    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(renderer, m_sky, nullptr, &dst);
}

void EnvironmentFX::EnsureSky(SDL_Renderer *renderer, const EnvironmentSettings &env,
                              const float cam_pos[3], const float fwd[3],
                              const float right[3], const float up[3], float fov_deg,
                              int w, int h)
{
    const uint64_t sig = SignatureFromSettings(env, cam_pos, fwd, right, up, fov_deg, w, h);
    if (m_sky && sig == m_sky_sig)
        return;

    if (m_sky)
        SDL_DestroyTexture(m_sky);
    m_sky = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                              SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!m_sky)
        return;

    m_sky_w = w;
    m_sky_h = h;
    m_sky_sig = sig;
    RebuildSky(renderer, env, fwd, right, up, fov_deg, w, h);
}

void EnvironmentFX::RebuildSky(SDL_Renderer *renderer, const EnvironmentSettings &env,
                               const float fwd[3], const float right[3],
                               const float up[3], float fov_deg, int w, int h)
{
    if (!m_sky)
        return;

    const size_t count = (size_t)w * (size_t)h;
    m_sky_pixels.assign(count, 0xFF000000u);

    env::SkyParams sp;
    sp.top[0] = env.sky_color_top[0];
    sp.top[1] = env.sky_color_top[1];
    sp.top[2] = env.sky_color_top[2];
    sp.horizon[0] = env.sky_color_horizon[0];
    sp.horizon[1] = env.sky_color_horizon[1];
    sp.horizon[2] = env.sky_color_horizon[2];
    sp.star_intensity = env.sky_star_intensity;

    float sun[3];
    env::SunDirection(env.sky_sun_yaw, env.sky_sun_pitch, sun);

    const float half_fov = std::tan(fov_deg * kPi / 360.0f);
    const float aspect = (float)w / (float)h;

    // Project the sun into screen space once.
    const float s_fwd = sun[0] * fwd[0] + sun[1] * fwd[1] + sun[2] * fwd[2];
    const float s_rgt = sun[0] * right[0] + sun[1] * right[1] + sun[2] * right[2];
    const float s_up  = sun[0] * up[0] + sun[1] * up[1] + sun[2] * up[2];
    float sun_px = -1000000.0f, sun_py = -1000000.0f;
    if (s_fwd > 0.0001f)
    {
        sun_px = ((s_rgt / s_fwd) / (half_fov * aspect) * 0.5f + 0.5f) * w;
        sun_py = (0.5f - (s_up / s_fwd) / half_fov * 0.5f) * h;
    }
    const float disk_px = env.sky_sun_disk * (float)std::min(w, h);
    const float glow_px = env.sky_sun_glow * (float)std::min(w, h);
    const float sun_r = env.sky_sun_color[0] * env.sky_sun_intensity;
    const float sun_g = env.sky_sun_color[1] * env.sky_sun_intensity;
    const float sun_b = env.sky_sun_color[2] * env.sky_sun_intensity;

    for (int j = 0; j < h; ++j)
    {
        const float py = ((j + 0.5f) / h) * 2.0f - 1.0f;
        for (int i = 0; i < w; ++i)
        {
            const float px = ((i + 0.5f) / w) * 2.0f - 1.0f;

            float ray[3] = {
                fwd[0] + right[0] * (px * half_fov * aspect) + up[0] * (-py * half_fov),
                fwd[1] + right[1] * (px * half_fov * aspect) + up[1] * (-py * half_fov),
                fwd[2] + right[2] * (px * half_fov * aspect) + up[2] * (-py * half_fov),
            };
            float len = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
            if (len > 0.0f)
            {
                ray[0] /= len;
                ray[1] /= len;
                ray[2] /= len;
            }

            float r, g, b;
            float grad[3];
            env::SkyGradient(sp, ray, grad);
            float star[3];
            env::SkyStars(sp, ray, star);
            r = grad[0] + star[0];
            g = grad[1] + star[1];
            b = grad[2] + star[2];

            // Sun disk + glow in screen space (pure arithmetic falloff).
            if (glow_px > 0.0f)
            {
                const float dx = (float)i - sun_px;
                const float dy = (float)j - sun_py;
                const float d = std::sqrt(dx * dx + dy * dy);
                float glow = 1.0f - SmoothStep(glow_px * 0.25f, glow_px, d);
                float disk = 1.0f - SmoothStep(disk_px, disk_px * 1.5f, d);
                r += sun_r * (disk + glow * 0.45f);
                g += sun_g * (disk + glow * 0.45f);
                b += sun_b * (disk + glow * 0.45f);
            }

            r = std::max(0.0f, std::min(1.0f, r));
            g = std::max(0.0f, std::min(1.0f, g));
            b = std::max(0.0f, std::min(1.0f, b));
            m_sky_pixels[(size_t)j * w + i] =
                ((uint32_t)(r * 255.0f) << 24) |
                ((uint32_t)(g * 255.0f) << 16) |
                ((uint32_t)(b * 255.0f) << 8) |
                0xFFu;
        }
    }

    void *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(m_sky, nullptr, &pixels, &pitch) == 0)
    {
        if (pitch == w * 4)
            std::memcpy(pixels, m_sky_pixels.data(), (size_t)count * 4);
        else
            for (int j = 0; j < h; ++j)
                std::memcpy((uint8_t *)pixels + (size_t)j * pitch,
                            &m_sky_pixels[(size_t)j * w], (size_t)w * 4);
        SDL_UnlockTexture(m_sky);
    }
}

// ---------------------------------------------------------------------------
// Post-processing
// ---------------------------------------------------------------------------

void EnvironmentFX::RebuildLUT(const EnvironmentSettings &env)
{
    const bool tm = env.post_tonemap_enabled;
    const float gamma = env.post_gamma;
    if (m_lut_tonemap != 0xFF && m_lut_tonemap == (tm ? 1 : 0) &&
        std::fabs(m_lut_gamma - gamma) < 1e-5f)
        return;

    const float inv_gamma = 1.0f / std::max(0.1f, gamma);
    for (int i = 0; i < 4096; ++i)
    {
        float v = (float)i / 4095.0f;
        if (tm)
            v = env::Aces(v);
        v = std::pow(std::max(0.0f, v), inv_gamma);
        v = std::max(0.0f, std::min(1.0f, v));
        m_lut[i] = (uint8_t)(v * 255.0f + 0.5f);
    }
    m_lut_tonemap = tm ? 1 : 0;
    m_lut_gamma = gamma;
}

void EnvironmentFX::EnsureWork(SDL_Renderer *renderer, const EnvironmentSettings &env,
                               int src_w, int src_h)
{
    int work_w = std::max(1, (int)std::lround(src_w * env.post_scale));
    int work_h = std::max(1, (int)std::lround(src_h * env.post_scale));

    if (m_work && m_work_w == work_w && m_work_h == work_h)
        return;

    if (m_work)
        SDL_DestroyTexture(m_work);
    m_work = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_STREAMING, work_w, work_h);
    m_work_w = work_w;
    m_work_h = work_h;
    m_work_sig = 0;
    m_lin.assign((size_t)work_w * work_h * 3, 0.0f);
    m_out.assign((size_t)work_w * work_h, 0xFF000000u);

    const int bw = std::max(1, (work_w + 1) / 2);
    const int bh = std::max(1, (work_h + 1) / 2);
    m_bloom.assign((size_t)bw * bh * 3, 0.0f);
    m_tmp.assign((size_t)bw * bh * 3, 0.0f);
}

void EnvironmentFX::BuildBloom(float *bloom, int bloom_w, int bloom_h,
                               const float *lin, int lin_w, int lin_h,
                               const EnvironmentSettings &env)
{
    // Box downsample the working-res linear scene, extracting the bright pass.
    const float th = env.post_bloom_threshold;
    for (int j = 0; j < bloom_h; ++j)
    {
        const int y0 = (int)((float)j / (float)bloom_h * lin_h);
        const int y1 = std::min(lin_h - 1, (int)((float)(j + 1) / (float)bloom_h * lin_h));
        for (int i = 0; i < bloom_w; ++i)
        {
            const int x0 = (int)((float)i / (float)bloom_w * lin_w);
            const int x1 = std::min(lin_w - 1, (int)((float)(i + 1) / (float)bloom_w * lin_w));

            float r = 0.0f, g = 0.0f, b = 0.0f;
            int n = 0;
            for (int yy = y0; yy <= y1; ++yy)
            {
                for (int xx = x0; xx <= x1; ++xx)
                {
                    const float *p = &lin[((size_t)yy * lin_w + xx) * 3];
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    ++n;
                }
            }
            if (n > 0)
            {
                r /= n;
                g /= n;
                b /= n;
            }
            const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            if (lum > th)
            {
                const float k = std::max(0.0f, (lum - th) / std::max(1e-6f, lum));
                r = std::max(0.0f, r * k);
                g = std::max(0.0f, g * k);
                b = std::max(0.0f, b * k);
            }
            else
            {
                r = g = b = 0.0f;
            }
            bloom[((size_t)j * bloom_w + i) * 3 + 0] = r;
            bloom[((size_t)j * bloom_w + i) * 3 + 1] = g;
            bloom[((size_t)j * bloom_w + i) * 3 + 2] = b;
        }
    }

    // Separable gaussian blur (horizontal then vertical, ping-pong into tmp).
    const float sigma = std::max(0.5f, env.post_bloom_radius * 0.5f);
    const int taps = std::min(9, 1 + 2 * (int)std::ceil(env.post_bloom_radius));
    std::vector<float> weights((size_t)taps);
    float wsum = 0.0f;
    for (int t = 0; t < taps; ++t)
    {
        const int off = t - taps / 2;
        weights[(size_t)t] = std::exp(-(float)(off * off) / (2.0f * sigma * sigma));
        wsum += weights[(size_t)t];
    }
    for (float &w : weights)
        w /= wsum;

    const int half = taps / 2;
    for (int j = 0; j < bloom_h; ++j)
    {
        for (int i = 0; i < bloom_w; ++i)
        {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int t = 0; t < taps; ++t)
            {
                const int xx = std::max(0, std::min(bloom_w - 1, i + t - half));
                const float *p = &bloom[((size_t)j * bloom_w + xx) * 3];
                r += p[0] * weights[(size_t)t];
                g += p[1] * weights[(size_t)t];
                b += p[2] * weights[(size_t)t];
            }
            m_tmp[((size_t)j * bloom_w + i) * 3 + 0] = r;
            m_tmp[((size_t)j * bloom_w + i) * 3 + 1] = g;
            m_tmp[((size_t)j * bloom_w + i) * 3 + 2] = b;
        }
    }
    for (int j = 0; j < bloom_h; ++j)
    {
        for (int i = 0; i < bloom_w; ++i)
        {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int t = 0; t < taps; ++t)
            {
                const int yy = std::max(0, std::min(bloom_h - 1, j + t - half));
                const float *p = &m_tmp[((size_t)yy * bloom_w + i) * 3];
                r += p[0] * weights[(size_t)t];
                g += p[1] * weights[(size_t)t];
                b += p[2] * weights[(size_t)t];
            }
            bloom[((size_t)j * bloom_w + i) * 3 + 0] = r;
            bloom[((size_t)j * bloom_w + i) * 3 + 1] = g;
            bloom[((size_t)j * bloom_w + i) * 3 + 2] = b;
        }
    }
}

bool EnvironmentFX::PostProcess(SDL_Renderer *renderer, SDL_Texture *source,
                                int x, int y, int w, int h,
                                const EnvironmentSettings &env)
{
    if (!env.post_enabled || !renderer || !source || w <= 0 || h <= 0)
        return false;

    EnsureWork(renderer, env, w, h);
    if (!m_work)
        return false;

    // Read the region pixels (RGBA8888, matching the target format).
    SDL_Rect region = { x, y, w, h };
    std::vector<uint32_t> raw((size_t)w * (size_t)h);
    if (SDL_RenderReadPixels(renderer, &region, SDL_PIXELFORMAT_RGBA8888,
                             raw.data(), (int)((size_t)w * 4)) != 0)
        return false;

    // Downsample to the working resolution into m_lin (linear RGB floats).
    RebuildLUT(env);
    const int ww = m_work_w;
    const int wh = m_work_h;
    for (int j = 0; j < wh; ++j)
    {
        const int y0 = (int)((float)j / (float)wh * h);
        const int y1 = std::min(h - 1, (int)((float)(j + 1) / (float)wh * h));
        for (int i = 0; i < ww; ++i)
        {
            const int x0 = (int)((float)i / (float)ww * w);
            const int x1 = std::min(w - 1, (int)((float)(i + 1) / (float)ww * w));

            float r = 0.0f, g = 0.0f, b = 0.0f;
            int n = 0;
            for (int yy = y0; yy <= y1; ++yy)
            {
                const uint32_t *row = &raw[(size_t)yy * w];
                for (int xx = x0; xx <= x1; ++xx)
                {
                    const uint32_t p = row[xx];
                    r += (p >> 24) & 0xFF;
                    g += (p >> 16) & 0xFF;
                    b += (p >> 8) & 0xFF;
                    ++n;
                }
            }
            r /= 255.0f * n;
            g /= 255.0f * n;
            b /= 255.0f * n;
            float *dst = &m_lin[((size_t)j * ww + i) * 3];
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
        }
    }

    // Bloom: half-res bright pass + gaussian blur (bilinear sampling on apply).
    const int bw = std::max(1, (ww + 1) / 2);
    const int bh = std::max(1, (wh + 1) / 2);
    if (env.post_bloom_enabled)
        BuildBloom(m_bloom.data(), bw, bh, m_lin.data(), ww, wh, env);

    // Per-pixel grade + LUT, writing the working-res output.
    env::PostParams pp;
    pp.exposure = env.post_exposure;
    pp.gamma = env.post_gamma;
    pp.bloom_strength = env.post_bloom_strength;
    pp.saturation = env.post_saturation;
    pp.contrast = env.post_contrast;
    pp.temperature = env.post_temperature;
    pp.tonemap_enabled = env.post_tonemap_enabled;

    for (int j = 0; j < wh; ++j)
    {
        // Bilinear sampling of the half-res bloom buffer: compute fractional
        // position and blend the 4 nearest texels for smooth bloom falloff.
        const float bfy = std::min((float)j * 0.5f, (float)(bh - 1));
        const int by0 = std::max(0, std::min(bh - 2, (int)bfy));
        const int by1 = by0 + 1;
        const float fy = bfy - (float)by0;

        for (int i = 0; i < ww; ++i)
        {
            const float *lin = &m_lin[((size_t)j * ww + i) * 3];
            float bloom[3] = { 0.0f, 0.0f, 0.0f };
            if (env.post_bloom_enabled)
            {
                const float bfx = std::min((float)i * 0.5f, (float)(bw - 1));
                const int bx0 = std::max(0, std::min(bw - 2, (int)bfx));
                const int bx1 = bx0 + 1;
                const float fx = bfx - (float)bx0;

                const float *p00 = &m_bloom[((size_t)by0 * bw + bx0) * 3];
                const float *p10 = &m_bloom[((size_t)by0 * bw + bx1) * 3];
                const float *p01 = &m_bloom[((size_t)by1 * bw + bx0) * 3];
                const float *p11 = &m_bloom[((size_t)by1 * bw + bx1) * 3];
                const float w00 = (1.0f - fx) * (1.0f - fy);
                const float w10 = fx * (1.0f - fy);
                const float w01 = (1.0f - fx) * fy;
                const float w11 = fx * fy;
                for (int c = 0; c < 3; ++c)
                    bloom[c] = p00[c] * w00 + p10[c] * w10 +
                               p01[c] * w01 + p11[c] * w11;
            }

            float out[3];
            env::PostProcess(lin, bloom, pp, out);
            const int ri = std::isfinite(out[0]) ? std::clamp((int)(out[0] * 4095.0f + 0.5f), 0, 4095) : 0;
            const int gi = std::isfinite(out[1]) ? std::clamp((int)(out[1] * 4095.0f + 0.5f), 0, 4095) : 0;
            const int bi = std::isfinite(out[2]) ? std::clamp((int)(out[2] * 4095.0f + 0.5f), 0, 4095) : 0;
            const uint32_t r = m_lut[ri];
            const uint32_t g = m_lut[gi];
            const uint32_t b = m_lut[bi];
            m_out[(size_t)j * ww + i] =
                ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFFu;
        }
    }

    void *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(m_work, nullptr, &pixels, &pitch) != 0)
        return false;
    if (pitch == ww * 4)
        std::memcpy(pixels, m_out.data(), (size_t)ww * wh * 4);
    else
        for (int j = 0; j < wh; ++j)
            std::memcpy((uint8_t *)pixels + (size_t)j * pitch,
                        &m_out[(size_t)j * ww], (size_t)ww * 4);
    SDL_UnlockTexture(m_work);

    // Blit the graded working-res image back over the region (linear scaling).
    SDL_SetRenderTarget(renderer, source);
    SDL_Rect src_rect = { 0, 0, ww, wh };
    SDL_Rect dst_rect = { x, y, w, h };
    SDL_RenderCopy(renderer, m_work, &src_rect, &dst_rect);
    return true;
}
