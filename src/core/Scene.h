#pragma once

#include "Entity.h"

#include <string>
#include <vector>

class Scene
{
public:
    Scene();
    ~Scene();

    Entity& CreateEntity(const std::string &name);
    std::vector<Entity>& GetEntities();
    Entity* GetEntityById(int id);

private:
    std::vector<Entity> m_entities;
    int m_next_id;
};
