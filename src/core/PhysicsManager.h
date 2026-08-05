#pragma once

#include <map>
#include <utility>
#include <vector>

class Scene;
class ScriptEngine;
struct Entity;

// Axis-aligned box physics step for play mode. Every frame it computes the
// world-space AABB of each enabled ColliderComponent and tests all pairs
// (broad-phase O(n^2), fine for the small scenes this engine targets).
//
// Two behaviors come out of the overlap test:
//   - Solid vs Solid: penetration is prevented by separating the second body
//     of the pair (the higher entity id) along the minimum-penetration axis,
//     and both bodies fire OnCollisionEnter/Exit.
//   - Trigger involved: the pair is pass-through (no separation) and each
//     trigger body fires OnTriggerEnter/Exit with the other entity.
//
// Enter/Exit events are edge-tracked across frames (a per-pair overlap map),
// so a hook fires exactly once per transition, never every frame. `Step` is
// driven by Application's play loop after the scripts' OnUpdate(dt) so events
// reflect the positions the scripts just produced.
class PhysicsManager
{
public:
    // Run one physics tick and dispatch collision/trigger events to `scripts`.
    void Step(Scene &scene, ScriptEngine &scripts);

    // Forget all tracked overlaps (called when entering play mode so no stale
    // Enter/Exit edges survive from a previous session).
    void Clear();

private:
    enum class PairKind { Solid, Trigger };

    struct OverlapKey
    {
        int a, b;   // sorted: a < b
        bool operator<(const OverlapKey &other) const
        {
            return a != other.a ? a < other.a : b < other.b;
        }
    };

    std::map<OverlapKey, PairKind> m_overlaps;
};
