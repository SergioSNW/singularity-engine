#include "render/ThumbnailCache.h"

#include "EngineMath.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "core/AssetCatalog.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr int THUMB_SIZE = 96;

// Projection used by the mesh renderer: identical math to the viewport pass
// (perspective divide, depth == w_out) so thumbnails frame like a camera view.
struct Tri
{
    float depth;
    int x0, y0, x1, y1, x2, y2;
    Uint8 r, g, b;
};

bool ProjectThumb(const Mat4 &view_proj, float near_p, int w, int h,
                  const Vec3 &p, int &sx, int &sy, float &depth)
{
    float w_out;
    Vec3 clip = Mat4MulVec3(view_proj, p, w_out);
    if (w_out < near_p)
        return false;
    sx = (int)((clip.x / w_out + 1.0f) * 0.5f * w);
    sy = (int)((1.0f - clip.y / w_out) * 0.5f * h);
    depth = w_out;
    return true;
}

void DrawThumbLine(SDL_Renderer *renderer, const Mat4 &view_proj, float near_p,
                   int w, int h, Vec3 a, Vec3 b)
{
    float wa, wb;
    Mat4MulVec3(view_proj, a, wa);
    Mat4MulVec3(view_proj, b, wb);

    if (wa < near_p && wb < near_p)
        return;

    if (wa < near_p)
    {
        const float t = (near_p - wa) / (wb - wa);
        a.x += (b.x - a.x) * t;
        a.y += (b.y - a.y) * t;
        a.z += (b.z - a.z) * t;
    }
    else if (wb < near_p)
    {
        const float t = (near_p - wb) / (wa - wb);
        b.x += (a.x - b.x) * t;
        b.y += (a.y - b.y) * t;
        b.z += (a.z - b.z) * t;
    }

    Vec3 ca = Mat4MulVec3(view_proj, a, wa);
    Vec3 cb = Mat4MulVec3(view_proj, b, wb);

    const int ax = (int)((ca.x / wa + 1.0f) * 0.5f * w);
    const int ay = (int)((1.0f - ca.y / wa) * 0.5f * h);
    const int bx = (int)((cb.x / wb + 1.0f) * 0.5f * w);
    const int by = (int)((1.0f - cb.y / wb) * 0.5f * h);
    SDL_RenderDrawLine(renderer, ax, ay, bx, by);
}

} // namespace

ThumbnailCache::ThumbnailCache(SDL_Renderer *renderer, MeshLibrary *meshes,
                               MaterialLibrary *materials, TextureLibrary *textures)
    : m_renderer(renderer)
    , m_meshes(meshes)
    , m_materials(materials)
    , m_textures(textures)
{
}

ThumbnailCache::~ThumbnailCache()
{
    Shutdown();
}

SDL_Texture *ThumbnailCache::Get(const std::string &path)
{
    if (!m_renderer)
        return nullptr;

    // Hit the cache for anything we've already resolved this session.
    auto it = m_cache.find(path);
    if (it != m_cache.end())
        return it->second.texture;

    const AssetCatalog::AssetKind kind = AssetCatalog::ClassifyAsset(path);
    SDL_Texture *texture = nullptr;
    bool owned = false;

    switch (kind)
    {
        case AssetCatalog::AssetKind::Mesh:
            texture = GenerateMesh(path);
            owned = texture != nullptr;
            break;
        case AssetCatalog::AssetKind::Material:
            texture = GenerateMaterial(path);
            owned = texture != nullptr;
            break;
        case AssetCatalog::AssetKind::Texture:
            // Image thumbnails are the loaded GPU texture itself (the library
            // already decodes and caches it); borrow it, never own it.
            if (m_textures)
                texture = m_textures->GetTexture(path);
            owned = false;
            break;
        default:
            break;
    }

    if (!texture)
        return nullptr;

    m_cache[path] = { texture, owned };
    return texture;
}

SDL_Texture *ThumbnailCache::GenerateMesh(const std::string &path)
{
    if (!m_renderer || !m_meshes)
        return nullptr;

    std::string error;
    const Mesh *mesh = m_meshes->Load(path, &error);
    if (!mesh || mesh->positions.empty())
        return nullptr;

    SDL_Texture *texture = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        THUMB_SIZE, THUMB_SIZE);
    if (!texture)
        return nullptr;

    SDL_Texture *prev_target = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, texture);

    SDL_SetRenderDrawColor(m_renderer, 24, 24, 32, 255);
    SDL_RenderClear(m_renderer);

    // Frame the mesh: orbit the bounds center from a 3/4 angle so the shape
    // reads immediately, with a fixed-distance perspective like the viewport.
    const Vec3 center = {
        (mesh->bounds_min.x + mesh->bounds_max.x) * 0.5f,
        (mesh->bounds_min.y + mesh->bounds_max.y) * 0.5f,
        (mesh->bounds_min.z + mesh->bounds_max.z) * 0.5f,
    };
    const float radius = std::max({
        (mesh->bounds_max.x - mesh->bounds_min.x) * 0.5f,
        (mesh->bounds_max.y - mesh->bounds_min.y) * 0.5f,
        (mesh->bounds_max.z - mesh->bounds_min.z) * 0.5f,
    });
    const float safe_radius = std::max(radius, 1e-3f);

    const float fov = 45.0f;
    const float dist = safe_radius / std::tan(fov * 3.1415926535f / 360.0f) * 1.15f;
    const float yaw = 45.0f * 3.1415926535f / 180.0f;
    const float pitch = 22.0f * 3.1415926535f / 180.0f;
    const Vec3 eye = {
        center.x + dist * std::cos(pitch) * std::sin(yaw),
        center.y + dist * std::sin(pitch),
        center.z + dist * std::cos(pitch) * std::cos(yaw),
    };
    const Mat4 view = Mat4LookAt(eye, center, { 0.0f, 1.0f, 0.0f });
    const float near_p = std::max(dist - safe_radius * 2.0f, 0.05f);
    const float far_p = dist + safe_radius * 2.0f;
    const Mat4 proj = Mat4Perspective(fov, 1.0f, near_p, far_p);
    const Mat4 view_proj = Mat4Mul(proj, view);

    // Flat-shaded fills: top-down light, painter's-algorithm depth sort.
    const Vec3 light_dir = { 0.0f, -1.0f, 0.0f }; // engine default light
    const float ambient = 0.35f;
    const float base_r = 96.0f, base_g = 120.0f, base_b = 178.0f;

    const std::vector<Vec3> &pos = mesh->positions;
    std::vector<Tri> tris;
    tris.reserve(pos.size() / 3);
    for (size_t i = 0; i + 2 < pos.size(); i += 3)
    {
        Vec3 a = pos[i], b = pos[i + 1], c = pos[i + 2];

        int ax, ay, bx, by, cx, cy;
        float da, db, dc;
        if (!ProjectThumb(view_proj, near_p, THUMB_SIZE, THUMB_SIZE, a, ax, ay, da) ||
            !ProjectThumb(view_proj, near_p, THUMB_SIZE, THUMB_SIZE, b, bx, by, db) ||
            !ProjectThumb(view_proj, near_p, THUMB_SIZE, THUMB_SIZE, c, cx, cy, dc))
            continue;

        Vec3 e1 = Vec3Sub(b, a);
        Vec3 e2 = Vec3Sub(c, a);
        Vec3 n = Vec3Normalize(Vec3Cross(e1, e2));
        const float diffuse = std::max(0.0f, Vec3Dot(n, Vec3Scale(light_dir, -1.0f)));
        const float factor = ambient + (1.0f - ambient) * diffuse;

        Tri t;
        t.depth = (da + db + dc) / 3.0f;
        t.x0 = ax; t.y0 = ay;
        t.x1 = bx; t.y1 = by;
        t.x2 = cx; t.y2 = cy;
        t.r = (Uint8)std::min(255.0f, base_r * factor);
        t.g = (Uint8)std::min(255.0f, base_g * factor);
        t.b = (Uint8)std::min(255.0f, base_b * factor);
        tris.push_back(t);
    }

    std::stable_sort(tris.begin(), tris.end(),
        [](const Tri &a, const Tri &b) { return a.depth > b.depth; });

    std::vector<SDL_Vertex> verts;
    verts.reserve(tris.size() * 3);
    for (const Tri &t : tris)
    {
        SDL_Color col = { t.r, t.g, t.b, 255 };
        verts.push_back({ { (float)t.x0, (float)t.y0 }, col, { 0.0f, 0.0f } });
        verts.push_back({ { (float)t.x1, (float)t.y1 }, col, { 0.0f, 0.0f } });
        verts.push_back({ { (float)t.x2, (float)t.y2 }, col, { 0.0f, 0.0f } });
    }
    if (!verts.empty())
        SDL_RenderGeometry(m_renderer, nullptr, verts.data(), (int)verts.size(), nullptr, 0);

    // Wireframe pass: brighter edges over the fills, the engine's mesh look.
    SDL_SetRenderDrawColor(m_renderer, 170, 195, 235, 255);
    for (size_t i = 0; i + 1 < mesh->edge_lines.size(); i += 2)
        DrawThumbLine(m_renderer, view_proj, near_p, THUMB_SIZE, THUMB_SIZE,
                      mesh->edge_lines[i], mesh->edge_lines[i + 1]);

    SDL_SetRenderTarget(m_renderer, prev_target);
    return texture;
}

SDL_Texture *ThumbnailCache::GenerateMaterial(const std::string &path)
{
    if (!m_renderer || !m_materials)
        return nullptr;

    std::string error;
    const Material *material = m_materials->Load(path, &error);
    if (!material)
        return nullptr;

    SDL_Texture *texture = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        THUMB_SIZE, THUMB_SIZE);
    if (!texture)
        return nullptr;

    SDL_Texture *prev_target = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, texture);

    const Uint8 r = (Uint8)std::min(255.0f, material->color[0] * 255.0f);
    const Uint8 g = (Uint8)std::min(255.0f, material->color[1] * 255.0f);
    const Uint8 b = (Uint8)std::min(255.0f, material->color[2] * 255.0f);
    SDL_SetRenderDrawColor(m_renderer, r, g, b, 255);
    SDL_RenderClear(m_renderer);

    // Subtle inner border so the swatch reads as a card at grid sizes.
    SDL_SetRenderDrawColor(m_renderer, 20, 20, 28, 200);
    SDL_Rect border = { 0, 0, THUMB_SIZE, THUMB_SIZE };
    SDL_RenderDrawRect(m_renderer, &border);

    SDL_SetRenderTarget(m_renderer, prev_target);
    return texture;
}

void ThumbnailCache::Shutdown()
{
    if (m_renderer)
    {
        for (auto &entry : m_cache)
        {
            if (entry.second.owned && entry.second.texture)
                SDL_DestroyTexture(entry.second.texture);
            entry.second.texture = nullptr;
        }
    }
    m_cache.clear();
}
