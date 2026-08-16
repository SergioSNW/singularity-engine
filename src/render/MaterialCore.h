#pragma once

#include <algorithm>
#include <cmath>

// Pure-metallic/roughness shading math for the software rasterizer. Kept
// header-only and dependency-free (no <filesystem>, no SDL) so it can be
// unit-tested under any toolchain and inlined into the per-triangle shade loop.
namespace pbr
{
    // Roughness (0 = mirror, 1 = fully matte) mapped to a Blinn-Phong
    // specular power: smooth surfaces get a tight highlight (high power),
    // rough ones spread it wide.
    inline float SpecularPower(float roughness)
    {
        const float r = std::clamp(roughness, 0.0f, 1.0f);
        return 1.0f + 256.0f * (1.0f - r) * (1.0f - r);
    }

    // Fresnel F0 at normal incidence: dielectrics reflect a fixed ~4% while
    // metals reflect their base albedo. `metallic` interpolates between the
    // two, so a gold material (metallic=1, albedo ~0.85) mirrors its tint.
    inline float DielectricF0(float metallic, float albedo)
    {
        const float m = std::clamp(metallic, 0.0f, 1.0f);
        const float a = std::clamp(albedo, 0.0f, 1.0f);
        return 0.04f + m * (a - 0.04f);
    }

    // Ambient floor after occlusion: AO dims the view-independent term that
    // would otherwise keep shadowed faces fully lit.
    inline float AmbientFloor(float ambient, float ao)
    {
        return std::clamp(ambient, 0.0f, 1.0f) * std::clamp(ao, 0.0f, 1.0f);
    }

    // Blinn-Phong NdotH specular weight (power >= 1).
    inline float BlinnPhong(float ndh, float power)
    {
        return std::pow(std::max(ndh, 0.0f), std::max(1.0f, power));
    }

    // Specular intensity falloff: glossy materials reflect noticeably more
    // than matte ones at equal power; roughness 1 drives the term to zero.
    inline float SpecularWeight(float roughness)
    {
        return 1.0f - std::clamp(roughness, 0.0f, 1.0f);
    }
}
