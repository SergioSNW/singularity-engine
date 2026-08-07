#pragma once

#include <map>
#include <string>

// A material asset (.mat) describing how a mesh entity is shaded.
//
// `.mat` files are plain JSON written through the engine's own json::Value
// serializer so they round-trip byte-for-byte and stay hand-editable:
//
//     {
//       "name": "Checker",
//       "color": [0.6, 0.8, 1.0, 1.0],
//       "texture": "checkerboard.bmp",
//       "shininess": 0.25
//     }
//
// `color` is the RGBA albedo tint. When a tint is combined with a texture the
// final fragment color is texture.rgb * tint.rgb (alpha from the tint unless
// the texture provides its own). `texture` is the asset filename resolved
// under assets/textures/ by the TextureLibrary; empty means flat shading.
// `shininess` is a generic shader knob (kept in the file so the editor can
// expose it later) and is currently honored as a subtle emissive boost.
struct Material
{
    std::string name;            // display name, defaults to the asset stem
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string texture;         // assets/textures/ filename or empty
    float shininess = 0.0f;      // 0..1 generic specular/emissive knob
};

// Serialize to / from the .mat JSON layout described above.
std::string MaterialToJson(const Material &material);
bool        MaterialFromJson(const std::string &text, Material &out,
                             std::string *error = nullptr);

// Cache of Material assets, mirroring MeshLibrary: entries are keyed by the
// path the caller used (raw, or with the "assets/materials/" prefix resolved),
// files are parsed on first touch, and a default material is always present.
class MaterialLibrary
{
public:
    MaterialLibrary();

    // Load (and cache) a .mat file. On failure returns nullptr and fills
    // `error` with a human-readable message.
    const Material* Load(const std::string &path, std::string *error = nullptr);

    // Look up an already-loaded material by key; returns nullptr when absent.
    const Material* Get(const std::string &key) const;

    const Material* GetDefault() const;

    // Create a brand-new material asset: writes `material` to
    // assets/materials/<filename> (creating the directory on demand) and
    // inserts it into the cache. Returns nullptr and sets `error` on failure.
    const Material* Create(const std::string &filename, const Material &material,
                           std::string *error = nullptr);

private:
    std::map<std::string, Material> m_materials;  // keyed by caller path
};
