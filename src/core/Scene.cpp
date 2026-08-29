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

void Scene::QueueDestroyEntity(int entity_id)
{
    m_pending_destroy.push_back(entity_id);
}

void Scene::FlushPendingDestroyEntities()
{
    // Swap out first: DestroyEntity's own descendant cascade could in theory
    // grow this list from something else queuing more work, and we only want
    // to process exactly the ids queued before this flush started.
    std::vector<int> ids;
    ids.swap(m_pending_destroy);
    for (int id : ids)
        DestroyEntity(id);
}

void Scene::SetParent(int entity_id, int parent_id)
{
    Entity *child = GetEntityById(entity_id);
    if (!child)
        return;

    // Reject invalid targets BEFORE unlinking so a rejected reparent leaves
    // the hierarchy untouched. This matters for the editor's drag-and-drop:
    // dropping a node onto itself or onto one of its own descendants must not
    // silently detach it to the root.
    if (parent_id != -1)
    {
        if (parent_id == entity_id)
            return;  // cannot parent an entity to itself
        if (IsDescendantOf(parent_id, entity_id))
            return;  // cycle: the target is inside this entity's own subtree
        if (!GetEntityById(parent_id))
            return;  // unknown target
    }

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

    child->parent = nullptr;
    if (parent_id == -1)
        return;  // explicit detach to scene root

    Entity *new_parent = GetEntityById(parent_id);
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

Entity& CreateDirectionalLightEntity(Scene &scene, const std::string &tag)
{
    Entity &light = scene.CreateEntity(tag);
    light.light.active = true;
    // Lights are pure volume-less light sources: keep them out of the render
    // passes so no placeholder cube shows up in the viewport.
    light.material.active = false;
    return light;
}

Entity* EnsureActiveLight(Scene &scene)
{
    for (auto &entity : scene.GetEntities())
        if (entity->light.active)
            return entity.get();
    return &CreateDirectionalLightEntity(scene, "Directional Light");
}
