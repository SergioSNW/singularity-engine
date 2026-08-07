#pragma once

#include <SDL.h>

#include <map>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;

// A decoded GPU texture plus its size. Owned by TextureLibrary; the library
// keeps the SDL_Texture* alive until DestroyAll() (called at Application
// shutdown while the renderer still exists).
struct TextureInfo
{
    SDL_Texture *texture = nullptr;
    int width = 0;
    int height = 0;
};

// Cache of image assets decoded through stb_image and uploaded with
// SDL_CreateTextureFromSurface. Keys are the asset filenames as the caller
// passed them (raw, or resolved under "assets/textures/"), so a single path
// always maps to a single GPU texture. Textures are loaded lazily on first
// touch and share the engine's accelerated renderer.
class TextureLibrary
{
public:
    TextureLibrary() = default;
    ~TextureLibrary() = default;

    // Bind the accelerated renderer used to upload textures. Must be called
    // after window/renderer creation and before any Load(); Safe to keep
    // calling since it only stores the pointer.
    void SetRenderer(SDL_Renderer *renderer) { m_renderer = renderer; }

    // Load (and cache) an image file. Returns nullptr and sets `error` on
    // failure; the texture's SDL handle is written to `out` only on success.
    const TextureInfo* Load(const std::string &path, std::string *error = nullptr);

    // Look up an already-loaded texture by key; nullptr when absent.
    const TextureInfo* Get(const std::string &key) const;

    // Convenience: Load(), then return just the SDL handle (or nullptr).
    SDL_Texture* GetTexture(const std::string &path, std::string *error = nullptr);

    // Release every GPU texture. Call before the renderer/window are torn down.
    void DestroyAll();

private:
    std::map<std::string, TextureInfo> m_textures;
    SDL_Renderer *m_renderer = nullptr;
};
