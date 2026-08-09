#pragma once

#include "Components.h"

#include <string>
#include <vector>

struct Entity
{
    int id;
    std::string uuid;   // stable persistent identity (scene serialization)
    TagComponent tag;
    TransformComponent transform;
    MaterialComponent material;
    MeshComponent mesh;
    CameraComponent camera;
    BoundsComponent bounds;  // local-space AABB of the mesh geometry, auto-refreshed
    ColliderComponent collider;  // physics volume (Solid blocks, Trigger passes through)
    ScriptComponent script;  // Lua gameplay script bound on play start
    DirectionalLightComponent light;  // directional light feeding the shading pipeline

    // Scene graph links. Entities are stored in the Scene by stable address
    // (std::unique_ptr), so these raw pointers stay valid until the entity is
    // destroyed. Children are owned by the scene, never by the parent.
    Entity *parent = nullptr;
    std::vector<Entity*> children;
};
