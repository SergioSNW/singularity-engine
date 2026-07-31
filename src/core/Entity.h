#pragma once

#include "Components.h"

#include <vector>

struct Entity
{
    int id;
    TagComponent tag;
    TransformComponent transform;
    MaterialComponent material;
    CameraComponent camera;

    // Scene graph links. Entities are stored in the Scene by stable address
    // (std::unique_ptr), so these raw pointers stay valid until the entity is
    // destroyed. Children are owned by the scene, never by the parent.
    Entity *parent = nullptr;
    std::vector<Entity*> children;
};
