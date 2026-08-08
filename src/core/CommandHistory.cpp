#include "CommandHistory.h"

#include "Entity.h"
#include "Json.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include <cstring>
#include <utility>

// --- EntitySnapshot ---------------------------------------------------------
// A plain-data copy of every mutable component of an Entity, keyed by id, so a
// property-edit transaction can restore the exact pre-edit state. The Scene
// graph links (parent/children pointers) are captured as the parent id so
// Apply can reparent back even after the hierarchy changed.

struct EntitySnapshot
{
    int entity_id = -1;
    int parent_id = -1;

    std::string tag;
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float rotation[3] = { 0.0f, 0.0f, 0.0f };
    float scale[3]    = { 1.0f, 1.0f, 1.0f };

    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool  material_active = true;
    std::string material_path;
    std::string texture_path;

    std::string mesh_path;

    float camera_fov = 60.0f;
    float camera_near = 0.1f;
    float camera_far = 100.0f;
    float camera_pitch = 0.0f;
    float camera_yaw = 0.0f;
    bool  camera_primary = false;

    bool  collider_enabled = false;
    bool  collider_trigger = false;
    float collider_center[3] = { 0.0f, 0.0f, 0.0f };
    float collider_extents[3] = { 0.5f, 0.5f, 0.5f };

    std::string script_path;
};

static void CaptureSnapshot(const Entity &e, EntitySnapshot &out)
{
    out.entity_id = e.id;
    out.parent_id = e.parent ? e.parent->id : -1;
    out.tag = e.tag.tag;
    std::memcpy(out.position, e.transform.position, sizeof(out.position));
    std::memcpy(out.rotation, e.transform.rotation, sizeof(out.rotation));
    std::memcpy(out.scale, e.transform.scale, sizeof(out.scale));
    std::memcpy(out.color, e.material.color, sizeof(out.color));
    out.material_active = e.material.active;
    out.material_path = e.material.material_path;
    out.texture_path = e.material.texture_path;
    out.mesh_path = e.mesh.path;
    out.camera_fov = e.camera.fov;
    out.camera_near = e.camera.near_plane;
    out.camera_far = e.camera.far_plane;
    out.camera_pitch = e.camera.pitch;
    out.camera_yaw = e.camera.yaw;
    out.camera_primary = e.camera.primary;
    out.collider_enabled = e.collider.enabled;
    out.collider_trigger = (e.collider.type == ColliderComponent::Type::Trigger);
    std::memcpy(out.collider_center, &e.collider.center.x, sizeof(out.collider_center));
    std::memcpy(out.collider_extents, &e.collider.extents.x, sizeof(out.collider_extents));
    out.script_path = e.script.path;
}

static void ApplySnapshot(Scene *scene, const EntitySnapshot &snap)
{
    Entity *e = scene->GetEntityById(snap.entity_id);
    if (!e)
        return;
    e->tag.tag = snap.tag;
    std::memcpy(e->transform.position, snap.position, sizeof(snap.position));
    std::memcpy(e->transform.rotation, snap.rotation, sizeof(snap.rotation));
    std::memcpy(e->transform.scale, snap.scale, sizeof(snap.scale));
    std::memcpy(e->material.color, snap.color, sizeof(snap.color));
    e->material.active = snap.material_active;
    e->material.material_path = snap.material_path;
    e->material.texture_path = snap.texture_path;
    e->mesh.path = snap.mesh_path;
    e->camera.fov = snap.camera_fov;
    e->camera.near_plane = snap.camera_near;
    e->camera.far_plane = snap.camera_far;
    e->camera.pitch = snap.camera_pitch;
    e->camera.yaw = snap.camera_yaw;
    e->camera.primary = snap.camera_primary;
    e->collider.enabled = snap.collider_enabled;
    e->collider.type = snap.collider_trigger
        ? ColliderComponent::Type::Trigger : ColliderComponent::Type::Solid;
    std::memcpy(&e->collider.center.x, snap.collider_center, sizeof(snap.collider_center));
    std::memcpy(&e->collider.extents.x, snap.collider_extents, sizeof(snap.collider_extents));
    e->script.path = snap.script_path;

    if ((e->parent ? e->parent->id : -1) != snap.parent_id)
        scene->SetParent(e->id, snap.parent_id);
}

// --- Concrete commands ------------------------------------------------------

// Undoable property edit: restores the entity to its before/after snapshots.
class EntityStateCommand : public Command
{
public:
    EntityStateCommand(Scene *scene, EntitySnapshot before, EntitySnapshot after,
                       const char *description)
        : m_scene(scene)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
        m_description = description;
    }

    void Execute() override { ApplySnapshot(m_scene, m_after); }
    void Undo() override { ApplySnapshot(m_scene, m_before); }

private:
    Scene *m_scene;
    EntitySnapshot m_before;
    EntitySnapshot m_after;
};

// Undoable entity deletion. The entity subtree is captured as JSON (including
// uuids) so Undo re-spawns it byte-for-byte under its original parent; the live
// id is tracked through the cycle so repeated Undo/Redo stays correct.
class DeleteEntityCommand : public Command
{
public:
    DeleteEntityCommand(Scene *scene, int entity_id, json::Value tree,
                        int parent_id, const char *description)
        : m_scene(scene)
        , m_entity_id(entity_id)
        , m_tree(std::move(tree))
        , m_parent_id(parent_id)
    {
        m_description = description;
    }

    void Execute() override
    {
        if (m_scene->GetEntityById(m_entity_id))
            m_scene->DestroyEntity(m_entity_id);
    }

    void Undo() override
    {
        if (m_scene->GetEntityById(m_entity_id))
            return;  // already restored
        Entity *parent = (m_parent_id >= 0) ? m_scene->GetEntityById(m_parent_id) : nullptr;
        if (Entity *root = SceneSerializer::SpawnEntityTree(*m_scene, m_tree, parent))
            m_entity_id = root->id;
    }

private:
    Scene *m_scene;
    int m_entity_id;
    json::Value m_tree;
    int m_parent_id;
};

// Undoable entity spawn (duplicate / asset instancing). The live id is tracked
// so Undo removes exactly what Redo re-created, even after re-spawns.
class SpawnEntityCommand : public Command
{
public:
    SpawnEntityCommand(Scene *scene, json::Value tree, int parent_id,
                       int existing_id, const char *description)
        : m_scene(scene)
        , m_tree(std::move(tree))
        , m_parent_id(parent_id)
        , m_entity_id(existing_id)
    {
        m_description = description;
    }

    void Execute() override
    {
        if (m_scene->GetEntityById(m_entity_id))
            return;  // already applied
        Entity *parent = (m_parent_id >= 0) ? m_scene->GetEntityById(m_parent_id) : nullptr;
        if (Entity *root = SceneSerializer::SpawnEntityTree(*m_scene, m_tree, parent))
            m_entity_id = root->id;
    }

    void Undo() override
    {
        if (m_scene->GetEntityById(m_entity_id))
        {
            m_scene->DestroyEntity(m_entity_id);
            m_entity_id = -1;
        }
    }

private:
    Scene *m_scene;
    json::Value m_tree;
    int m_parent_id;
    int m_entity_id;
};

// --- CommandHistory ---------------------------------------------------------

CommandHistory::CommandHistory(Scene *scene)
    : m_scene(scene)
    , m_limit(100)
    , m_edit_entity(-1)
    , m_edit_before(nullptr)
{
}

void CommandHistory::Execute(std::unique_ptr<Command> cmd)
{
    if (!cmd)
        return;
    cmd->Execute();
    m_undo.push_back(std::move(cmd));
    if (m_undo.size() > m_limit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void CommandHistory::Push(std::unique_ptr<Command> cmd)
{
    if (!cmd)
        return;
    m_undo.push_back(std::move(cmd));
    if (m_undo.size() > m_limit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void CommandHistory::PushSpawn(Entity &entity, const char *description)
{
    json::Value tree = SceneSerializer::SerializeEntityTree(entity);
    const int parent_id = entity.parent ? entity.parent->id : -1;
    Push(std::make_unique<SpawnEntityCommand>(
        m_scene, std::move(tree), parent_id, entity.id,
        description ? description : "Create Entity"));
}

void CommandHistory::ExecuteDelete(Entity &entity, const char *description)
{
    json::Value tree = SceneSerializer::SerializeEntityTree(entity);
    const int parent_id = entity.parent ? entity.parent->id : -1;
    Execute(std::make_unique<DeleteEntityCommand>(
        m_scene, entity.id, std::move(tree), parent_id,
        description ? description : "Delete Entity"));
}

void CommandHistory::Undo()
{
    if (m_undo.empty())
        return;
    std::unique_ptr<Command> cmd = std::move(m_undo.back());
    m_undo.pop_back();
    cmd->Undo();
    m_redo.push_back(std::move(cmd));
}

void CommandHistory::Redo()
{
    if (m_redo.empty())
        return;
    std::unique_ptr<Command> cmd = std::move(m_redo.back());
    m_redo.pop_back();
    cmd->Execute();
    m_undo.push_back(std::move(cmd));
}

void CommandHistory::Clear()
{
    m_undo.clear();
    m_redo.clear();
    m_edit_entity = -1;
    delete m_edit_before;
    m_edit_before = nullptr;
}

void CommandHistory::BeginEntityEdit(int entity_id, const char *description)
{
    EndEntityEdit();  // a new transaction supersedes any dangling one

    Entity *e = m_scene->GetEntityById(entity_id);
    if (!e)
        return;
    m_edit_entity = entity_id;
    m_edit_desc = description ? description : "Edit";
    m_edit_before = new EntitySnapshot();
    CaptureSnapshot(*e, *m_edit_before);
}

void CommandHistory::EndEntityEdit()
{
    if (m_edit_entity < 0 || !m_edit_before)
        return;

    Entity *e = m_scene->GetEntityById(m_edit_entity);
    if (e)
    {
        EntitySnapshot after;
        CaptureSnapshot(*e, after);
        {
            // std::string members forbid memcmp; compare field-by-field.
            bool same = (m_edit_before->tag == after.tag) &&
                        std::memcmp(m_edit_before->position, after.position, sizeof(after.position)) == 0 &&
                        std::memcmp(m_edit_before->rotation, after.rotation, sizeof(after.rotation)) == 0 &&
                        std::memcmp(m_edit_before->scale, after.scale, sizeof(after.scale)) == 0 &&
                        std::memcmp(m_edit_before->color, after.color, sizeof(after.color)) == 0 &&
                        m_edit_before->material_active == after.material_active &&
                        m_edit_before->material_path == after.material_path &&
                        m_edit_before->texture_path == after.texture_path &&
                        m_edit_before->mesh_path == after.mesh_path &&
                        m_edit_before->camera_fov == after.camera_fov &&
                        m_edit_before->camera_near == after.camera_near &&
                        m_edit_before->camera_far == after.camera_far &&
                        m_edit_before->camera_pitch == after.camera_pitch &&
                        m_edit_before->camera_yaw == after.camera_yaw &&
                        m_edit_before->camera_primary == after.camera_primary &&
                        m_edit_before->collider_enabled == after.collider_enabled &&
                        m_edit_before->collider_trigger == after.collider_trigger &&
                        std::memcmp(m_edit_before->collider_center, after.collider_center,
                                    sizeof(after.collider_center)) == 0 &&
                        std::memcmp(m_edit_before->collider_extents, after.collider_extents,
                                    sizeof(after.collider_extents)) == 0 &&
                        m_edit_before->script_path == after.script_path;
            if (!same)
            {
                Push(std::make_unique<EntityStateCommand>(
                    m_scene, std::move(*m_edit_before), std::move(after),
                    m_edit_desc.c_str()));
            }
        }
    }

    delete m_edit_before;
    m_edit_before = nullptr;
    m_edit_entity = -1;
    m_edit_desc.clear();
}
