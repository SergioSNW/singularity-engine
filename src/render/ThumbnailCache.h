#pragma once

#include <map>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;

class MeshLibrary;
class MaterialLibrary;
class TextureLibrary;

// Generates and caches small preview textures for Content Browser assets.
//
//   * .obj meshes  -> rendered into an off-screen target with a framing orbit
//                     camera, flat-shaded fills + wireframe edges (the engine's
//                     mesh look at thumbnail scale)
//   * .mat files   -> a swatch texture filled with the material's diffuse color
//   * image files  -> the TextureLibrary's already-loaded GPU texture (borrowed,
//                     never owned here)
//
// The cache is keyed by asset path and generated lazily on first request, so a
// large folder costs nothing until its items are actually drawn. Textures are
// owned by this cache (mesh/material) and must be released via Shutdown() while
// the renderer is still alive.
class ThumbnailCache
{
public:
    ThumbnailCache(SDL_Renderer *renderer, MeshLibrary *meshes,
                   MaterialLibrary *materials, TextureLibrary *textures);
    ~ThumbnailCache();

    ThumbnailCache(const ThumbnailCache &) = delete;
    ThumbnailCache &operator=(const ThumbnailCache &) = delete;

    // Thumbnail for `path` (any asset kind), generating + caching on first use.
    // Returns nullptr when the kind has no preview or the asset can't load.
    SDL_Texture *Get(const std::string &path);

    // Release every owned thumbnail texture. Call before the renderer dies.
    void Shutdown();

private:
    struct Entry
    {
        SDL_Texture *texture = nullptr;
        bool owned = false;   // this cache created it and must destroy it
    };

    SDL_Texture *GenerateMesh(const std::string &path);
    SDL_Texture *GenerateMaterial(const std::string &path);

    SDL_Renderer *m_renderer;
    MeshLibrary *m_meshes;
    MaterialLibrary *m_materials;
    TextureLibrary *m_textures;
    std::map<std::string, Entry> m_cache;
};
