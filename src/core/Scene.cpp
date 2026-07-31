#include "Scene.h"

#include "EngineMath.h"

Scene::Scene()
    : m_next_id(0)
{
}

Scene::~Scene() = default;

Entity& Scene::CreateEntity(const std::string &name, Entity *parent)
{
    auto entity = std::make_unique<Entity>();
    entity->id = m_next_id++;
    entity->tag.tag = name;
    entity->parent = parent;
    if (name == "Camera")
        entity->camera.primary = true;

    if (parent)
        parent->children.push_back(entity.get());

    Entity *raw = entity.get();
    m_entities.push_back(std::move(entity));
    return *raw;
}

void Scene::DestroyEntity(int entity_id)
{
    // Destroy descendants first so no dangling children pointers remain.
    // Iterate over a copy because DestroyEntity mutates the children lists.
    for (auto &entity : m_entities)
    {
        if (entity->id == entity_id)
        {
            std::vector<Entity*> children = entity->children;
            for (Entity *child : children)
                DestroyEntity(child->id);
            break;
        }
    }

    for (auto it = m_entities.begin(); it != m_entities.end(); ++it)
    {
        if ((*it)->id == entity_id)
        {
            Entity *removed = it->get();
            if (removed->parent)
            {
                std::vector<Entity*> &siblings = removed->parent->children;
                for (auto sit = siblings.begin(); sit != siblings.end(); ++sit)
                {
                    if (*sit == removed)
                    {
                        siblings.erase(sit);
                        break;
                    }
                }
            }
            m_entities.erase(it);
            return;
        }
    }
}

void Scene::SetParent(int entity_id, int parent_id)
{
    Entity *child = GetEntityById(entity_id);
    if (!child)
        return;

    if (child->parent)
    {
        std::vector<Entity*> &siblings = child->parent->children;
        for (auto it = siblings.begin(); it != siblings.end(); ++it)
        {
            if (*it == child)
            {
                siblings.erase(it);
                break;
            }
        }
    }

    // Reject cycles: an entity can never be its own ancestor.
    if (parent_id == -1 || parent_id == entity_id || IsDescendantOf(parent_id, entity_id))
    {
        child->parent = nullptr;
        return;
    }

    Entity *new_parent = GetEntityById(parent_id);
    if (!new_parent)
    {
        child->parent = nullptr;
        return;
    }

    child->parent = new_parent;
    new_parent->children.push_back(child);
}

std::vector<std::unique_ptr<Entity>>& Scene::GetEntities()
{
    return m_entities;
}

Entity* Scene::GetEntityById(int id)
{
    for (auto &e : m_entities)
        if (e->id == id)
            return e.get();
    return nullptr;
}

bool Scene::IsDescendantOf(int entity_id, int ancestor_id) const
{
    for (const auto &e : m_entities)
    {
        if (e->id != entity_id)
            continue;
        for (const Entity *cur = e->parent; cur; cur = cur->parent)
            if (cur->id == ancestor_id)
                return true;
        return false;
    }
    return false;
}

Mat4 Scene::ComputeWorldMatrix(const Entity &entity) const
{
    Vec3 pos = {
        entity.transform.position[0],
        entity.transform.position[1],
        entity.transform.position[2]
    };
    Vec3 rot = {
        entity.transform.rotation[0],
        entity.transform.rotation[1],
        entity.transform.rotation[2]
    };
    Vec3 scale = {
        entity.transform.scale[0],
        entity.transform.scale[1],
        entity.transform.scale[2]
    };
    Mat4 local = Mat4TRS(pos, rot, scale);

    if (entity.parent)
        return Mat4Mul(ComputeWorldMatrix(*entity.parent), local);

    return local;
}
