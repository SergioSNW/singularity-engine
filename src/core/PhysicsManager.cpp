#include "PhysicsManager.h"

#include "Scene.h"
#include "Entity.h"
#include "EngineMath.h"
#include "script/ScriptEngine.h"

#include <algorithm>

namespace {

struct Body
{
    Entity *entity;
    Vec3 min, max;
};

bool AABBsOverlap(const Body &a, const Body &b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// Push `b` (the second / higher-id body of a solid pair) out of `a` along the
// axis of minimum penetration, updating its world AABB so later pair tests in
// the same frame see the resolved position. Only root entities are moved
// (transform.position is the world translation); a parented body keeps its
// relative placement and only reports the collision.
void ResolveSolid(const Body &a, Body &b)
{
    const float ox = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
    const float oy = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
    const float oz = std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z);

    Vec3 axis{ 1.0f, 0.0f, 0.0f };
    float pen = ox;
    if (oy < pen) { pen = oy; axis = { 0.0f, 1.0f, 0.0f }; }
    if (oz < pen) { pen = oz; axis = { 0.0f, 0.0f, 1.0f }; }

    if (pen <= 1e-6f)
        return;
    if (b.entity->parent)
        return;

    // Sign from the two centers: push B further away from A on that axis.
    float sign;
    if (axis.x != 0.0f)
        sign = (a.min.x + a.max.x < b.min.x + b.max.x) ? 1.0f : -1.0f;
    else if (axis.y != 0.0f)
        sign = (a.min.y + a.max.y < b.min.y + b.max.y) ? 1.0f : -1.0f;
    else
        sign = (a.min.z + a.max.z < b.min.z + b.max.z) ? 1.0f : -1.0f;

    const Vec3 delta{ axis.x * sign * pen, axis.y * sign * pen, axis.z * sign * pen };
    b.entity->transform.position[0] += delta.x;
    b.entity->transform.position[1] += delta.y;
    b.entity->transform.position[2] += delta.z;
    b.min.x += delta.x; b.max.x += delta.x;
    b.min.y += delta.y; b.max.y += delta.y;
    b.min.z += delta.z; b.max.z += delta.z;
}

} // namespace

void PhysicsManager::Step(Scene &scene, ScriptEngine &scripts)
{
    // --- Build world-space AABBs for every enabled collider ---
    std::vector<Body> bodies;
    for (auto &entity_ptr : scene.GetEntities())
    {
        Entity &entity = *entity_ptr;
        if (!entity.collider.enabled)
            continue;

        const Vec3 lmin{
            entity.collider.center.x - entity.collider.extents.x,
            entity.collider.center.y - entity.collider.extents.y,
            entity.collider.center.z - entity.collider.extents.z,
        };
        const Vec3 lmax{
            entity.collider.center.x + entity.collider.extents.x,
            entity.collider.center.y + entity.collider.extents.y,
            entity.collider.center.z + entity.collider.extents.z,
        };
        Vec3 wmin, wmax;
        TransformAABB(lmin, lmax, scene.ComputeWorldMatrix(entity), wmin, wmax);
        bodies.push_back({ &entity, wmin, wmax });
    }

    // --- Broad-phase pair test + solid resolution ---
    std::map<OverlapKey, PairKind> current;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        for (size_t j = i + 1; j < bodies.size(); ++j)
        {
            Body &a = bodies[i];
            Body &b = bodies[j];
            if (!AABBsOverlap(a, b))
                continue;

            const bool a_trigger =
                (a.entity->collider.type == ColliderComponent::Type::Trigger);
            const bool b_trigger =
                (b.entity->collider.type == ColliderComponent::Type::Trigger);

            OverlapKey key;
            key.a = a.entity->id;
            key.b = b.entity->id;
            if (key.a > key.b)
                std::swap(key.a, key.b);
            current[key] = (a_trigger || b_trigger) ? PairKind::Trigger
                                                    : PairKind::Solid;

            if (!a_trigger && !b_trigger)
                ResolveSolid(a, b);   // b = higher id = the body that yields
        }
    }

    // --- Edge-tracked events: fire Enter once, Exit once per transition ---
    auto notify = [&scripts](Entity *a, Entity *b, bool a_trigger, bool b_trigger,
                             ScriptEngine::ScriptEvent solid_event,
                             ScriptEngine::ScriptEvent trigger_event) {
        if (a_trigger || b_trigger)
        {
            // Trigger semantics: only the trigger volume raises events; a solid
            // overlapping a trigger is pass-through and hears nothing.
            if (a_trigger)
                scripts.DispatchEvent(a->id, trigger_event, b);
            if (b_trigger)
                scripts.DispatchEvent(b->id, trigger_event, a);
        }
        else
        {
            scripts.DispatchEvent(a->id, solid_event, b);
            scripts.DispatchEvent(b->id, solid_event, a);
        }
    };

    for (const auto &pair : current)
    {
        if (m_overlaps.find(pair.first) != m_overlaps.end())
            continue;   // was already overlapping last frame: no edge

        Entity *a = scene.GetEntityById(pair.first.a);
        Entity *b = scene.GetEntityById(pair.first.b);
        if (!a || !b)
            continue;
        const bool a_trigger =
            (a->collider.type == ColliderComponent::Type::Trigger);
        const bool b_trigger =
            (b->collider.type == ColliderComponent::Type::Trigger);
        notify(a, b, a_trigger, b_trigger,
               ScriptEngine::ScriptEvent::CollisionEnter,
               ScriptEngine::ScriptEvent::TriggerEnter);
    }

    for (const auto &pair : m_overlaps)
    {
        if (current.find(pair.first) != current.end())
            continue;   // still overlapping: no edge

        Entity *a = scene.GetEntityById(pair.first.a);
        Entity *b = scene.GetEntityById(pair.first.b);
        if (!a || !b)
            continue;
        const bool a_trigger =
            (a->collider.type == ColliderComponent::Type::Trigger);
        const bool b_trigger =
            (b->collider.type == ColliderComponent::Type::Trigger);
        notify(a, b, a_trigger, b_trigger,
               ScriptEngine::ScriptEvent::CollisionExit,
               ScriptEngine::ScriptEvent::TriggerExit);
    }

    m_overlaps = std::move(current);
}

void PhysicsManager::Clear()
{
    m_overlaps.clear();
}
