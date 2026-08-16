#pragma once

#include <map>
#include <string>

// A physics material asset (.pmat) describing how a collider behaves when it
// touches another body.
//
// `.pmat` files are plain JSON written through the engine's own json::Value
// serializer so they round-trip byte-for-byte and stay hand-editable:
//
//     {
//       "name": "Bouncy Rubber",
//       "friction": 0.35,
//       "restitution": 0.9
//     }
//
// `friction` (0..1) is the tangential grip: 0 = ice-slick (no resistance to
// sliding), 1 = maximum grip. `restitution` (0..1) is bounciness: 0 =
// perfectly inelastic, 1 = perfectly elastic. Both are authored *per asset*
// and assigned to a ColliderComponent through the Inspector; when two bodies
// collide the pair is combined with the standard rules
// (`CombinePhysicsMaterials`): restitution = max of the two (the bouncier
// body wins) and friction = geometric mean sqrt(fa * fb). A collider with no
// assigned asset uses the library's Default material.
struct PhysicsMaterial
{
    std::string name;            // display name, defaults to the asset stem
    float friction = 0.5f;       // 0..1 tangential grip
    float restitution = 0.1f;    // 0..1 bounciness
};

// Combine two materials into the effective pair values used for a contact.
// Restitution takes the max (bounciness is dominated by the more elastic
// body); friction is the geometric mean so a slippery surface wins against a
// grippy one without ever exceeding either input.
void CombinePhysicsMaterials(const PhysicsMaterial &a, const PhysicsMaterial &b,
                             float &out_friction, float &out_restitution);

// Serialize to / from the .pmat JSON layout described above.
std::string PhysicsMaterialToJson(const PhysicsMaterial &material);
bool        PhysicsMaterialFromJson(const std::string &text, PhysicsMaterial &out,
                                    std::string *error = nullptr);

// Cache of PhysicsMaterial assets, mirroring MaterialLibrary: entries are
// keyed by the path the caller used (raw, or with the "assets/physics/"
// prefix resolved), files are parsed on first touch, and a default material is
// always present.
class PhysicsMaterialLibrary
{
public:
    PhysicsMaterialLibrary();

    // Load (and cache) a .pmat file. On failure returns nullptr and fills
    // `error` with a human-readable message.
    const PhysicsMaterial* Load(const std::string &path, std::string *error = nullptr);

    // Look up an already-loaded material by key; returns nullptr when absent.
    const PhysicsMaterial* Get(const std::string &key) const;

    const PhysicsMaterial* GetDefault() const;

    // Create a brand-new material asset: writes `material` to
    // assets/physics/<filename> (creating the directory on demand) and inserts
    // it into the cache. Returns nullptr and sets `error` on failure.
    const PhysicsMaterial* Create(const std::string &filename,
                                  const PhysicsMaterial &material,
                                  std::string *error = nullptr);

    // Update an existing material asset in place: rewrites
    // assets/physics/<filename> and refreshes every cached copy so edits
    // appear immediately. Returns nullptr and sets `error` on failure.
    const PhysicsMaterial* Save(const std::string &filename,
                                const PhysicsMaterial &material,
                                std::string *error = nullptr);

private:
    std::map<std::string, PhysicsMaterial> m_materials;  // keyed by caller path
};
