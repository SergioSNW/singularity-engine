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
    m_entities.push_back(e);
    return m_entities.back();
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
