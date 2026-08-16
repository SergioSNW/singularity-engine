#pragma once

#include <map>
#include <string>

// A material asset (.mat) describing how a mesh entity is shaded.
//
// `.mat` files are plain JSON written through the engine's own json::Value
// serializer so they round-trip byte-for-byte and stay hand-editable:
//
//     {
//       "name": "Gold",
//       "color": [1.0, 0.85, 0.35, 1.0],
//       "texture": "brushed.bmp",
//       "albedo_multiplier": 1.0,
//       "normal_texture": "",
//       "normal_strength": 1.0,
//       "metallic": 1.0,
//       "metallic_texture": "",
//       "metallic_multiplier": 1.0,
//       "roughness": 0.25,
//       "roughness_texture": "",
//       "roughness_multiplier": 1.0,
//       "ao": 1.0,
//       "ao_texture": "",
//       "ao_multiplier": 1.0,
//       "shininess": 0.0
//     }
//
// Phase 38 expands the material into explicit PBR channels. `color` stays the
// RGBA albedo tint (final fragment = texture.rgb * color.rgb * albedo_multiplier
// when a texture is present, tint * albedo_multiplier otherwise). `texture` is
// the albedo map, resolved under assets/textures/ by the TextureLibrary.
// `metallic` (0..1), `roughness` (0..1) and `ao` (0..1) are scalar channel
// values; each has an optional texture-map slot plus a multiplier that scales
// the sampled channel (the CPU rasterizer shades from the scalars — the map
// slots validate and serialize so a per-pixel pipeline can consume them later).
// `normal_texture` / `normal_strength` are the normal-map slot (also
// slot-only in the software renderer). `shininess` is the legacy pre-v0.38
// specular knob, kept for file compatibility and superseded by `roughness`.
struct Material
{
    std::string name;            // display name, defaults to the asset stem
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string texture;         // albedo map (assets/textures/ filename or empty)
    float albedo_multiplier = 1.0f;
    std::string normal_texture;
    float normal_strength = 1.0f;
    float metallic = 0.0f;
    std::string metallic_texture;
    float metallic_multiplier = 1.0f;
    float roughness = 0.5f;
    std::string roughness_texture;
    float roughness_multiplier = 1.0f;
    float ao = 1.0f;
    std::string ao_texture;
    float ao_multiplier = 1.0f;
    float shininess = 0.0f;      // legacy knob; superseded by roughness
};

// The resolved scalars the software shader consumes, pulled out of a Material
// (and defaulting sensibly when an entity has no .mat asset at all).
struct MaterialShading
{
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    float albedo_multiplier = 1.0f;

    static MaterialShading FromMaterial(const Material &m)
    {
        MaterialShading s;
        s.metallic = m.metallic;
        s.roughness = m.roughness;
        s.ao = m.ao;
        s.albedo_multiplier = m.albedo_multiplier;
        return s;
    }
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

    // Update an existing material asset in place: rewrites
    // assets/materials/<filename> and refreshes every cached copy (both the
    // bare-filename and prefixed keys) so edits appear immediately on the next
    // render. Returns nullptr and sets `error` on failure.
    const Material* Save(const std::string &filename, const Material &material,
                         std::string *error = nullptr);

    // Apply an in-memory edit to every cached copy of <filename> WITHOUT
    // touching the file. Live authoring: the editor updates the cache on every
    // slider tick so the scene and preview render the new values immediately,
    // and Save() persists when the user commits. Returns the refreshed copy
    // (or nullptr if the material isn't in the cache yet).
    const Material* LiveUpdate(const std::string &filename, const Material &material);

private:
    std::map<std::string, Material> m_materials;  // keyed by caller path
};
