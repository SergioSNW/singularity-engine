#include "Texture.h"

#include <cstdio>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb/stb_image.h"

// Loads an image through stb_image (BMP/PNG/JPG/GIF/TGA/PSD/PIC/PNM/HDR) and
// uploads it to the renderer as an SDL texture. The raw decode always requests
// RGBA8 so SDL_CreateRGBSurfaceWithFormatFrom can map it straight onto
// SDL_PIXELFORMAT_ABGR8888 (the renderer's native byte order) without a copy.
static bool UploadTexture(SDL_Renderer *renderer, const std::string &path,
                          TextureInfo &out, std::string *error)
{
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        if (error) *error = "cannot decode image '" + path + "' (" +
                            std::string(stbi_failure_reason() ? stbi_failure_reason() : "unknown") + ")";
        return false;
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
    if (!surface)
    {
        if (error) *error = "cannot create surface for '" + path + "' (" +
                            std::string(SDL_GetError()) + ")";
        stbi_image_free(pixels);
        return false;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture)
    {
        // Linear filtering keeps scaled-down thumbnails and distant geometry
        // from aliasing; per-texture alpha is blended against the scene.
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(surface);
    stbi_image_free(pixels);

    if (!texture)
    {
        if (error) *error = "cannot upload texture '" + path + "' (" +
                            std::string(SDL_GetError()) + ")";
        return false;
    }

    out.texture = texture;
    out.width = width;
    out.height = height;
    if (error) error->clear();
    return true;
}

const TextureInfo* TextureLibrary::Load(const std::string &path, std::string *error)
{
    auto it = m_textures.find(path);
    if (it != m_textures.end())
        return &it->second;

    if (!m_renderer)
    {
        if (error) *error = "no active renderer for texture '" + path + "'";
        return nullptr;
    }

    std::string resolved = path;
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec))
    {
        const std::string alt = "assets/textures/" + path;
        if (std::filesystem::exists(alt, ec))
            resolved = alt;
        else
        {
            if (error) *error = "texture file not found: '" + path + "'";
            return nullptr;
        }
    }

    TextureInfo info;
    if (!UploadTexture(m_renderer, resolved, info, error))
        return nullptr;

    auto res = m_textures.emplace(path, std::move(info));
    return &res.first->second;
}

const TextureInfo* TextureLibrary::Get(const std::string &key) const
{
    auto it = m_textures.find(key);
    return (it == m_textures.end()) ? nullptr : &it->second;
}

SDL_Texture* TextureLibrary::GetTexture(const std::string &path, std::string *error)
{
    if (const TextureInfo *info = Load(path, error))
        return info->texture;
    return nullptr;
}

void TextureLibrary::DestroyAll()
{
    for (auto &entry : m_textures)
        if (entry.second.texture)
            SDL_DestroyTexture(entry.second.texture);
    m_textures.clear();
}

size_t TextureLibrary::ResidentBytes() const
{
    size_t bytes = 0;
    for (const auto &entry : m_textures)
        bytes += (size_t)entry.second.width * (size_t)entry.second.height * 4u;
    return bytes;
}
