#include "Scene.h"

#include "EngineMath.h"

#include <chrono>
#include <cstdio>
#include <random>

namespace {

// Version-4 style UUID: 16 random bytes with the version/variant bits set,
// rendered as 8-4-4-4-12 lowercase hex. Persistent entity identity across
// save/load cycles; runtime int ids are internal and get reassigned on load.
std::string GenerateUUID()
{
    static std::mt19937_64 rng(
        std::random_device{}() ^
        (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );

    uint64_t a = rng();
    uint64_t b = rng();
    unsigned char bytes[16];
    for (int i = 0; i < 8; ++i) bytes[i]     = (unsigned char)(a >> (i * 8));
    for (int i = 0; i < 8; ++i) bytes[i + 8] = (unsigned char)(b >> (i * 8));

    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40); // version 4
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80); // RFC 4122 variant

    char buf[37];
    snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buf);
}

} // namespace

Scene::Scene()
    : m_next_id(0)
{
}

Scene::~Scene() = default;

Entity& Scene::CreateEntity(const std::string &name, Entity *parent)
{
    auto entity = std::make_unique<Entity>();
    entity->id = m_next_id++;
    entity->uuid = GenerateUUID();
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

void Scene::Clear()
{
    m_entities.clear();
    m_next_id = 0;
}

std::vector<std::unique_ptr<Entity>>& Scene::GetEntities()
{
    return m_entities;
}

const std::vector<std::unique_ptr<Entity>>& Scene::GetEntities() const
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
