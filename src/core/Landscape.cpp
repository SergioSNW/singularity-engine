#include "Landscape.h"

#include "Components.h"
#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{

float Smoothstep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 TransformPoint(const Mat4 &m, const Vec3 &p)
{
    float w;
    return Mat4MulVec3(m, p, w);
}

// Invert an affine 4x4 (last row 0 0 0 1) via the cofactor inverse of its
// linear part. Robust for rotation + non-uniform scale, unlike the
// transpose-of-rotation + column-length-division TRS inverse. Used to map the
// world-space brush back into the landscape's local grid space.
Mat4 AffineInverse(const Mat4 &m)
{
    const float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    const float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    const float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];
    const float det = a00 * (a11 * a22 - a12 * a21)
                    - a01 * (a10 * a22 - a12 * a20)
                    + a02 * (a10 * a21 - a11 * a20);
    Mat4 inv = Mat4Identity();
    if (std::fabs(det) < 1e-9f)
        return inv;
    const float id = 1.0f / det;
    inv.m[0]  = (a11 * a22 - a12 * a21) * id;
    inv.m[4]  = (a02 * a21 - a01 * a22) * id;
    inv.m[8]  = (a01 * a12 - a02 * a11) * id;
    inv.m[1]  = (a12 * a20 - a10 * a22) * id;
    inv.m[5]  = (a00 * a22 - a02 * a20) * id;
    inv.m[9]  = (a02 * a10 - a00 * a12) * id;
    inv.m[2]  = (a10 * a21 - a11 * a20) * id;
    inv.m[6]  = (a01 * a20 - a00 * a21) * id;
    inv.m[10] = (a00 * a11 - a01 * a10) * id;
    const float tx = m.m[12], ty = m.m[13], tz = m.m[14];
    inv.m[12] = -(inv.m[0] * tx + inv.m[4] * ty + inv.m[8] * tz);
    inv.m[13] = -(inv.m[1] * tx + inv.m[5] * ty + inv.m[9] * tz);
    inv.m[14] = -(inv.m[2] * tx + inv.m[6] * ty + inv.m[10] * tz);
    return inv;
}

} // namespace

void LandscapeInitialize(LandscapeComponent &landscape)
{
    const int res = std::max(1, landscape.resolution);
    landscape.heights.assign((size_t)(res + 1) * (res + 1), landscape.base_height);
    landscape.mesh_dirty = true;
}

void LandscapeRebuildMesh(LandscapeComponent &landscape)
{
    const int res = std::max(1, landscape.resolution);
    const int stride = res + 1;
    const size_t count = (size_t)stride * stride;
    if (landscape.heights.size() != count)
        landscape.heights.assign(count, landscape.base_height);

    auto mesh = std::make_shared<Mesh>();
    mesh->name = "Landscape";

    const float cell = landscape.size / (float)res;
    const float half = landscape.size * 0.5f;

    const size_t tri_count = (size_t)res * res * 2;
    mesh->positions.reserve(tri_count * 3);
    mesh->uvs.reserve(tri_count * 3);

    float min_y = landscape.heights[0];
    float max_y = landscape.heights[0];
    for (size_t i = 1; i < count; ++i)
    {
        min_y = std::min(min_y, landscape.heights[i]);
        max_y = std::max(max_y, landscape.heights[i]);
    }

    for (int r = 0; r < res; ++r)
    {
        for (int c = 0; c < res; ++c)
        {
            const float x0 = (float)c * cell - half;
            const float x1 = x0 + cell;
            const float z0 = (float)r * cell - half;
            const float z1 = z0 + cell;
            const Vec3 v00{ x0, landscape.heights[(size_t)r * stride + c], z0 };
            const Vec3 v10{ x1, landscape.heights[(size_t)r * stride + c + 1], z0 };
            const Vec3 v01{ x0, landscape.heights[(size_t)(r + 1) * stride + c], z1 };
            const Vec3 v11{ x1, landscape.heights[(size_t)(r + 1) * stride + c + 1], z1 };
            // Winding chosen so the two-cell diagonal runs v00 -> v11 and the
            // faces' normals point +Y (up).
            mesh->positions.push_back(v00);
            mesh->positions.push_back(v11);
            mesh->positions.push_back(v10);
            mesh->positions.push_back(v00);
            mesh->positions.push_back(v01);
            mesh->positions.push_back(v11);

            const float u0 = (float)c / (float)res;
            const float u1 = (float)(c + 1) / (float)res;
            const float v0 = (float)r / (float)res;
            const float v1 = (float)(r + 1) / (float)res;
            mesh->uvs.push_back({ u0, v0 });
            mesh->uvs.push_back({ u1, v1 });
            mesh->uvs.push_back({ u1, v0 });
            mesh->uvs.push_back({ u0, v0 });
            mesh->uvs.push_back({ u0, v1 });
            mesh->uvs.push_back({ u1, v1 });
        }
    }

    // Sparse wireframe: every k-th grid line keeps the overlay cheap at high
    // resolutions while still reading as a topology preview. Segments ride the
    // surface (per-vertex heights) so the preview hugs the terrain.
    const int wire_step = std::max(1, res / 8);
    mesh->edge_lines.reserve(
        (size_t)((res / wire_step + 1) * 2 * res) * 2);
    for (int r = 0; r <= res; r += wire_step)
    {
        const float z = (float)r * cell - half;
        for (int c = 0; c < res; ++c)
        {
            const float x0 = (float)c * cell - half;
            const float x1 = x0 + cell;
            const float y0 = landscape.heights[(size_t)r * stride + c];
            const float y1 = landscape.heights[(size_t)r * stride + c + 1];
            mesh->edge_lines.push_back({ x0, y0, z });
            mesh->edge_lines.push_back({ x1, y1, z });
        }
    }
    for (int c = 0; c <= res; c += wire_step)
    {
        const float x = (float)c * cell - half;
        for (int r = 0; r < res; ++r)
        {
            const float z0 = (float)r * cell - half;
            const float z1 = z0 + cell;
            const float y0 = landscape.heights[(size_t)r * stride + c];
            const float y1 = landscape.heights[(size_t)(r + 1) * stride + c];
            mesh->edge_lines.push_back({ x, y0, z0 });
            mesh->edge_lines.push_back({ x, y1, z1 });
        }
    }

    mesh->bounds_min = { -half, min_y, -half };
    mesh->bounds_max = {  half, max_y,  half };

    landscape.mesh = std::move(mesh);
    landscape.mesh_dirty = false;
}

void LandscapeSculpt(LandscapeComponent &landscape, SculptTool tool,
                     const Vec3 &center, float radius, float strength,
                     float falloff)
{
    if (landscape.heights.empty() || landscape.resolution < 1 || radius <= 0.0f)
        return;
    const int res = landscape.resolution;
    const int stride = res + 1;
    const float cell = landscape.size / (float)res;
    const float half = landscape.size * 0.5f;

    // Brush footprint in grid units.
    const float r_cells = std::max(radius / cell, 0.25f);
    const float gc = (center.x + half) / cell;
    const float gr = (center.z + half) / cell;
    const int min_c = std::max(0, (int)std::floor(gc - r_cells));
    const int max_c = std::min(res, (int)std::ceil(gc + r_cells));
    const int min_r = std::max(0, (int)std::floor(gr - r_cells));
    const int max_r = std::min(res, (int)std::ceil(gr + r_cells));
    const float fade_in = r_cells * std::clamp(falloff, 0.0f, 1.0f);

    // Flatten converges on the surface height under the brush center, sampled
    // once per stamp.
    const float target = LandscapeSampleHeightLocal(landscape, center.x, center.z);

    const float r2 = r_cells * r_cells;
    for (int r = min_r; r <= max_r; ++r)
    {
        for (int c = min_c; c <= max_c; ++c)
        {
            const float dr = (float)r - gr;
            const float dc = (float)c - gc;
            const float d2 = dr * dr + dc * dc;
            if (d2 > r2)
                continue;
            const float w = 1.0f - Smoothstep(r_cells - fade_in, r_cells,
                                              std::sqrt(d2));
            if (w <= 0.0f)
                continue;

            float &h = landscape.heights[(size_t)r * stride + c];
            switch (tool)
            {
                case SculptTool::Raise:
                    h += strength * w;
                    break;
                case SculptTool::Smooth:
                {
                    float sum = h;
                    int n = 1;
                    if (r > 0)   { sum += landscape.heights[(size_t)(r - 1) * stride + c]; ++n; }
                    if (r < res) { sum += landscape.heights[(size_t)(r + 1) * stride + c]; ++n; }
                    if (c > 0)   { sum += landscape.heights[(size_t)r * stride + (c - 1)]; ++n; }
                    if (c < res) { sum += landscape.heights[(size_t)r * stride + (c + 1)]; ++n; }
                    const float avg = sum / (float)n;
                    h += (avg - h) * strength * w;
                    break;
                }
                case SculptTool::Flatten:
                    h += (target - h) * strength * w;
                    break;
            }
        }
    }
    landscape.mesh_dirty = true;
}

float LandscapeSampleHeightLocal(const LandscapeComponent &landscape,
                                 float lx, float lz)
{
    if (landscape.heights.empty() || landscape.resolution < 1)
        return landscape.base_height;
    const int res = landscape.resolution;
    const int stride = res + 1;
    const float cell = landscape.size / (float)res;
    const float half = landscape.size * 0.5f;

    const float gx = std::clamp((lx + half) / cell, 0.0f, (float)res);
    const float gz = std::clamp((lz + half) / cell, 0.0f, (float)res);
    const int c0 = (int)gx;
    const int r0 = (int)gz;
    const int c1 = std::min(c0 + 1, res);
    const int r1 = std::min(r0 + 1, res);
    const float fx = gx - (float)c0;
    const float fz = gz - (float)r0;

    const float h00 = landscape.heights[(size_t)r0 * stride + c0];
    const float h10 = landscape.heights[(size_t)r0 * stride + c1];
    const float h01 = landscape.heights[(size_t)r1 * stride + c0];
    const float h11 = landscape.heights[(size_t)r1 * stride + c1];
    return h00 * (1.0f - fx) * (1.0f - fz) + h10 * fx * (1.0f - fz)
         + h01 * (1.0f - fx) * fz + h11 * fx * fz;
}

bool LandscapeRaycast(const LandscapeComponent &landscape, const Mat4 &world,
                      const Vec3 &ray_origin, const Vec3 &ray_dir,
                      float &out_t, Vec3 &out_hit)
{
    if (landscape.heights.empty() || landscape.resolution < 1)
        return false;

    // Local ray: transform two world points so the direction takes only the
    // linear part of the world matrix (no translation).
    const Mat4 inv = AffineInverse(world);
    const Vec3 lo = TransformPoint(inv, ray_origin);
    Vec3 ld = Vec3Sub(TransformPoint(inv, Vec3Add(ray_origin, ray_dir)), lo);
    const float len = Vec3Length(ld);
    if (len < 1e-9f)
        return false;
    ld = Vec3Scale(ld, 1.0f / len);

    const int res = landscape.resolution;
    const int stride = res + 1;
    const size_t count = (size_t)stride * stride;
    float min_y = landscape.heights[0];
    float max_y = landscape.heights[0];
    for (size_t i = 1; i < count; ++i)
    {
        min_y = std::min(min_y, landscape.heights[i]);
        max_y = std::max(max_y, landscape.heights[i]);
    }

    const float half = landscape.size * 0.5f;
    const Vec3 box_min{ -half, min_y - 0.5f, -half };
    const Vec3 box_max{  half, max_y + 0.5f,  half };

    // Ray / AABB intersection in local space (slab method).
    const Vec3 inv_d{
        (std::fabs(ld.x) > 1e-9f) ? 1.0f / ld.x : 1e9f,
        (std::fabs(ld.y) > 1e-9f) ? 1.0f / ld.y : 1e9f,
        (std::fabs(ld.z) > 1e-9f) ? 1.0f / ld.z : 1e9f,
    };
    float t_near = 0.0f;
    float t_far = 1e30f;
    const auto slab = [&](float lo_val, float hi_val, float o, float id) {
        if (id >= 1e8f)
            return (o >= lo_val && o <= hi_val);
        float t1 = (lo_val - o) * id;
        float t2 = (hi_val - o) * id;
        if (t1 > t2)
            std::swap(t1, t2);
        t_near = std::max(t_near, t1);
        t_far = std::min(t_far, t2);
        return true;
    };
    if (!slab(box_min.x, box_max.x, lo.x, inv_d.x) ||
        !slab(box_min.y, box_max.y, lo.y, inv_d.y) ||
        !slab(box_min.z, box_max.z, lo.z, inv_d.z))
        return false;
    if (t_far < 0.0f)
        return false;

    // March along the ray sampling the surface, then bisect the first crossing
    // for a sub-cell-accurate hit point.
    const float step = (landscape.size / (float)res) * 0.5f;
    float t = std::max(t_near, 0.0f);
    Vec3 p = Vec3Add(lo, Vec3Scale(ld, t));
    float h = LandscapeSampleHeightLocal(landscape, p.x, p.z) - p.y;
    if (h <= 0.0f)
    {
        out_t = t;
        out_hit = TransformPoint(world, p);
        return true;
    }
    int guard = 0;
    while (t <= t_far && guard < 65536)
    {
        ++guard;
        const float t_prev = t;
        const float h_prev = h;
        t += step;
        p = Vec3Add(lo, Vec3Scale(ld, t));
        h = LandscapeSampleHeightLocal(landscape, p.x, p.z) - p.y;
        if (h <= 0.0f)
        {
            float a = t_prev;
            float b = t;
            float fa = h_prev;
            for (int it = 0; it < 12; ++it)
            {
                const float m = (a + b) * 0.5f;
                const Vec3 pm = Vec3Add(lo, Vec3Scale(ld, m));
                const float fm = LandscapeSampleHeightLocal(landscape, pm.x, pm.z) - pm.y;
                if (fa * fm <= 0.0f)
                    b = m;
                else
                {
                    a = m;
                    fa = fm;
                }
            }
            out_t = (a + b) * 0.5f;
            out_hit = TransformPoint(world,
                                     Vec3Add(lo, Vec3Scale(ld, out_t)));
            return true;
        }
    }
    return false;
}

Vec3 LandscapeWorldToLocal(const Mat4 &world, const Vec3 &world_point)
{
    return TransformPoint(AffineInverse(world), world_point);
}

float LandscapeWorldScale(const Mat4 &world)
{
    const Vec3 c0{ world.m[0], world.m[1], world.m[2] };
    const Vec3 c1{ world.m[4], world.m[5], world.m[6] };
    const Vec3 c2{ world.m[8], world.m[9], world.m[10] };
    return (Vec3Length(c0) + Vec3Length(c1) + Vec3Length(c2)) / 3.0f;
}
