#pragma once

#include <cstdint>
#include <string>

// Bitmask-based collision layer matrix (Phase 36).
//
// The engine defines a fixed set of collision layers (16 — enough for the
// authoring use-cases this editor targets while keeping the matrix readable).
// Every ColliderComponent carries a *membership bitmask* (bits = the layers it
// belongs to; an entity can be on several layers at once). Two bodies interact
// during the physics step if and only if the matrix says that *any* layer of
// body A collides with *any* layer of body B:
//
//     LayersInteract(maskA, maskB)  ->  exists i in maskA, j in maskB,
//                                       Collides(i, j)
//
// The matrix is symmetric: toggling cell (i, j) flips both entries, and the
// diagonal is user-controllable too (set it off when, say, projectiles should
// pass through each other). Layer 0 ("Default") is on by default for every
// collider and the whole matrix starts with every pair enabled, so a scene
// that never touches layers behaves exactly as before Phase 36.
//
// The matrix is scene state, not an entity property: it lives on the Scene,
// ships inside the scene file ("collision_matrix" block), and is edited live
// by the Collision Matrix panel (there is no per-entity undo transaction for
// it, exactly like other global scene settings).
struct CollisionMatrix
{
    static constexpr int kLayerCount = 16;
    static constexpr unsigned int kAllLayers = 0xFFFFu;

    // rows[layer] = bitmask of the layers `layer` collides with (symmetric).
    std::uint16_t rows[kLayerCount];
    std::string names[kLayerCount];

    CollisionMatrix()
    {
        ResetAll();
        names[0]  = "Default";
        names[1]  = "Player";
        names[2]  = "Environment";
        names[3]  = "Projectile";
        names[4]  = "Enemy";
        names[5]  = "Pickup";
        names[6]  = "Interactable";
        names[7]  = "Debris";
        names[8]  = "Character";
        names[9]  = "Water";
        names[10] = "Vehicle";
        names[11] = "Destructible";
        names[12] = "Sensor";
        names[13] = "CameraBlock";
        names[14] = "Trigger";
        names[15] = "Custom";
    }

    // Turn every pair on (including the diagonal).
    void ResetAll()
    {
        for (int i = 0; i < kLayerCount; ++i)
            rows[i] = kAllLayers;
    }

    void SetPair(int a, int b, bool enabled)
    {
        if (a < 0 || b < 0 || a >= kLayerCount || b >= kLayerCount)
            return;
        const std::uint16_t bit = static_cast<std::uint16_t>(1u << b);
        if (enabled)
            rows[a] = static_cast<std::uint16_t>(rows[a] | bit);
        else
            rows[a] = static_cast<std::uint16_t>(rows[a] & ~bit);
        const std::uint16_t rbit = static_cast<std::uint16_t>(1u << a);
        if (enabled)
            rows[b] = static_cast<std::uint16_t>(rows[b] | rbit);
        else
            rows[b] = static_cast<std::uint16_t>(rows[b] & ~rbit);
    }

    bool Collides(int a, int b) const
    {
        if (a < 0 || b < 0 || a >= kLayerCount || b >= kLayerCount)
            return false;
        return (rows[a] & (1u << b)) != 0;
    }

    // True when any layer in `layers_a` is allowed to collide with any layer
    // in `layers_b`. This is the query the physics step makes per body pair.
    bool LayersInteract(unsigned int layers_a, unsigned int layers_b) const
    {
        for (int i = 0; i < kLayerCount; ++i)
        {
            if (((layers_a >> i) & 1u) == 0)
                continue;
            for (int j = 0; j < kLayerCount; ++j)
            {
                if (((layers_b >> j) & 1u) == 0)
                    continue;
                if ((rows[i] & (1u << j)) != 0)
                    return true;
            }
        }
        return false;
    }

    const char *LayerName(int layer) const
    {
        return (layer >= 0 && layer < kLayerCount) ? names[layer].c_str() : "";
    }

    void SetLayerName(int layer, const std::string &name)
    {
        if (layer >= 0 && layer < kLayerCount)
            names[layer] = name;
    }
};
