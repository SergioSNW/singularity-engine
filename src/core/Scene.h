#pragma once

#include "CollisionMatrix.h"
#include "Entity.h"

#include <memory>
#include <string>
#include <vector>

struct Mat4;

// Human-readable map metadata stored in the scene file's "meta" block. Kept on
// the Scene itself so the editor (and SceneManager) can show it and so Save
// can stamp the current name/author/date into the file.
struct SceneMetadata
{
    std::string name;     // display name (SceneManager falls back to file stem)
    std::string author;   // optional attribution
    std::string created;  // ISO-8601 date of first save, e.g. "2026-08-05"
};

class Scene
{
public:
    Scene();
    ~Scene();

    Entity& CreateEntity(const std::string &name, Entity *parent = nullptr);
    void DestroyEntity(int entity_id);

    // Stage 3: safe entity destruction from inside a callback that is itself
    // iterating the entity list (PhysicsManager::Step()'s overlap pairs,
    // ScriptEngine::UpdateSession()'s per-entity OnUpdate loop) -- both call
    // into Lua while mid-loop over GetEntities(), so an immediate
    // DestroyEntity() from a Lua hook (e.g. entity:Destroy()) would mutate
    // the vector those loops are actively walking. QueueDestroyEntity just
    // records the id; FlushPendingDestroyEntities (called once, after every
    // per-frame system that iterates entities has finished) is where the
    // actual removal happens. Duplicate/stale ids are handled by
    // DestroyEntity's own existing not-found behavior, so no dedup is done
    // here.
    void QueueDestroyEntity(int entity_id);
    void FlushPendingDestroyEntities();

    void SetParent(int entity_id, int parent_id);
    void Clear();
    std::vector<std::unique_ptr<Entity>>& GetEntities();
    const std::vector<std::unique_ptr<Entity>>& GetEntities() const;
    Entity* GetEntityById(int id);
    bool IsDescendantOf(int entity_id, int ancestor_id) const;

    SceneMetadata &Meta() { return m_meta; }
    const SceneMetadata &Meta() const { return m_meta; }

    // World matrix = parent's world matrix * local transform matrix, or just
    // the local transform matrix for a root entity.
    Mat4 ComputeWorldMatrix(const Entity &entity) const;

    // Phase 36: the scene-wide collision layer matrix (which layers interact).
    // Scene state, serialized with the scene file and edited by the Collision
    // Matrix panel; the physics step reads it every frame.
    CollisionMatrix collision_matrix;

private:
    std::vector<std::unique_ptr<Entity>> m_entities;
    SceneMetadata m_meta;
    int m_next_id;
    std::vector<int> m_pending_destroy;
};

// Create a ready-to-use directional light entity: the light is active and its
// material is disabled so it never rasterizes a placeholder mesh.
Entity& CreateDirectionalLightEntity(Scene &scene, const std::string &tag);

// Ensure the scene has at least one active light; if every light is inactive
// (or there are none), add a default one and return it.
Entity* EnsureActiveLight(Scene &scene);
