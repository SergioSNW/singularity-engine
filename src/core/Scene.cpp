#include "Scene.h"

Scene::Scene()
    : m_next_id(0)
{
}

Scene::~Scene() = default;

Entity& Scene::CreateEntity(const std::string &name)
{
    Entity e;
    e.id = m_next_id++;
    e.tag.tag = name;
    if (name == "Camera")
        e.camera.primary = true;
    m_entities.push_back(e);
    return m_entities.back();
}

void Scene::DestroyEntity(int entity_id)
{
    for (auto it = m_entities.begin(); it != m_entities.end(); ++it)
    {
        if (it->id == entity_id)
        {
            m_entities.erase(it);
            return;
        }
    }
}

std::vector<Entity>& Scene::GetEntities()
{
    return m_entities;
}

Entity* Scene::GetEntityById(int id)
{
    for (auto &e : m_entities)
        if (e.id == id)
            return &e;
    return nullptr;
}
