#pragma once

#include "Entity.h"

#include <memory>
#include <string>
#include <vector>

struct Mat4;

class Scene
{
public:
    Scene();
    ~Scene();

    Entity& CreateEntity(const std::string &name, Entity *parent = nullptr);
    void DestroyEntity(int entity_id);
    void SetParent(int entity_id, int parent_id);
    void Clear();
    std::vector<std::unique_ptr<Entity>>& GetEntities();
    const std::vector<std::unique_ptr<Entity>>& GetEntities() const;
    Entity* GetEntityById(int id);
    bool IsDescendantOf(int entity_id, int ancestor_id) const;

    // World matrix = parent's world matrix * local transform matrix, or just
    // the local transform matrix for a root entity.
    Mat4 ComputeWorldMatrix(const Entity &entity) const;

private:
    std::vector<std::unique_ptr<Entity>> m_entities;
    int m_next_id;
};
