#include "SceneHierarchyPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"
#include "SceneSerializer.h"
#include "core/CommandHistory.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

SceneHierarchyPanel::SceneHierarchyPanel(SelectionState *selection, Scene *scene,
                                         CommandHistory *history)
    : m_selection(selection)
    , m_scene(scene)
    , m_history(history)
{
}

static void ClearSelectionIfAffected(Scene *scene, SelectionState *selection, int deleted_id)
{
    if (selection->entity_id == -1)
        return;
    if (selection->entity_id == deleted_id ||
        scene->IsDescendantOf(selection->entity_id, deleted_id))
    {
        selection->entity_id = -1;
        selection->entity_name.clear();
    }
}

static int CountDescendants(const Entity &entity)
{
    int count = 0;
    for (const Entity *child : entity.children)
        count += 1 + CountDescendants(*child);
    return count;
}

void SceneHierarchyPanel::DeleteEntity(Entity &entity)
{
    if (m_history)
        m_history->ExecuteDelete(entity, ("Delete '" + entity.tag.tag + "'").c_str());
    else
        m_scene->DestroyEntity(entity.id);
}

void SceneHierarchyPanel::DuplicateNode(Entity &entity)
{
    // Clone the node and its whole subtree as a sibling under the same parent
    // (fresh ids/uuid), then select the clone so a follow-up Ctrl+D keeps
    // duplicating the newest copy. The spawn is undoable.
    Entity *clone = SceneSerializer::DuplicateEntity(*m_scene, entity, entity.parent);
    if (clone)
    {
        if (m_history)
            m_history->PushSpawn(*clone, ("Duplicate '" + entity.tag.tag + "'").c_str());
        m_selection->entity_id = clone->id;
        m_selection->entity_name = clone->tag.tag;
        m_status = "Duplicated '" + clone->tag.tag + "'";
    }
}

void SceneHierarchyPanel::InstantiateMeshAsset(const std::string &mesh_path, Entity *parent)
{
    // Display name = file stem, e.g. "assets/meshes/gear.obj" -> "gear".
    const size_t slash = mesh_path.find_last_of('/');
    const std::string leaf = (slash == std::string::npos)
        ? mesh_path : mesh_path.substr(slash + 1);
    std::string name = leaf;
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);

    Entity &created = m_scene->CreateEntity(name.empty() ? "Mesh" : name, parent);
    created.mesh.path = mesh_path;
    if (m_history)
        m_history->PushSpawn(created, ("Create '" + created.tag.tag + "'").c_str());
    m_selection->entity_id = created.id;
    m_selection->entity_name = created.tag.tag;
    m_status = "Spawned '" + created.tag.tag + "' from " + mesh_path;
}

void SceneHierarchyPanel::DrawEntityNode(Entity &entity, int &to_delete_id)
{
    ImGui::PushID(entity.id);

    bool has_children = !entity.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (!has_children)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_selection->entity_id == entity.id)
        flags |= ImGuiTreeNodeFlags_Selected;

    bool open = ImGui::TreeNodeEx(entity.tag.tag.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        m_selection->entity_id = entity.id;
        m_selection->entity_name = entity.tag.tag;
    }

    // Drag the row to re-parent this entity: drop it on another node to
    // parent it there, or on Scene Root / empty space to detach it.
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("ENTITY", &entity.id, sizeof(entity.id));
        ImGui::Text("Re-parent '%s'", entity.tag.tag.c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target: accept an entity being re-parented to this node. Dropping
    // a node onto itself or onto one of its descendants would form a cycle, so
    // those targets are not offered (Scene::SetParent also guards against it).
    const ImGuiPayload *active_drag = ImGui::GetDragDropPayload();
    int active_drag_id = -1;
    if (active_drag && active_drag->IsDataType("ENTITY") &&
        active_drag->DataSize == (int)sizeof(int))
        active_drag_id = *(const int *)active_drag->Data;

    bool cycle_target = active_drag_id != -1 &&
                        (active_drag_id == entity.id ||
                         m_scene->IsDescendantOf(entity.id, active_drag_id));
    if (!cycle_target)
    {
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY"))
            {
                int dragged_id = *(const int *)payload->Data;
                if (Entity *dragged = m_scene->GetEntityById(dragged_id))
                {
                    if (m_history)
                        m_history->BeginEntityEdit(dragged_id, "Re-parent");
                    m_scene->SetParent(dragged_id, entity.id);
                    if (m_history)
                        m_history->EndEntityEdit();
                    m_status = "Parented '" + dragged->tag.tag + "' to '" +
                               entity.tag.tag + "'";
                }
            }

            // Mesh asset: spawn a child carrying the dropped .obj.
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MESH"))
            {
                std::string path(static_cast<const char *>(payload->Data), payload->DataSize);
                while (!path.empty() && path.back() == '\0')
                    path.pop_back();
                InstantiateMeshAsset(path, &entity);
            }

            // Material / texture asset: assign to this entity.
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MATERIAL"))
            {
                const char *data = (const char *)payload->Data;
                if (data && *data)
                {
                    if (m_history)
                        m_history->BeginEntityEdit(entity.id, "Assign Material");
                    entity.material.material_path = data;
                    if (m_history)
                        m_history->EndEntityEdit();
                    m_status = "Assigned material '" + std::string(data) + "' to '" +
                               entity.tag.tag + "'";
                }
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("TEXTURE"))
            {
                const char *data = (const char *)payload->Data;
                if (data && *data)
                {
                    if (m_history)
                        m_history->BeginEntityEdit(entity.id, "Assign Texture");
                    entity.material.texture_path = data;
                    if (m_history)
                        m_history->EndEntityEdit();
                    m_status = "Assigned texture '" + std::string(data) + "' to '" +
                               entity.tag.tag + "'";
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Add Child"))
        {
            Entity &created = m_scene->CreateEntity("Child", &entity);
            if (m_history)
                m_history->PushSpawn(created, "Create 'Child'");
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            DuplicateNode(entity);
        if (ImGui::MenuItem("Save as Prefab..."))
        {
            m_prefab_entity_id = entity.id;
            std::strncpy(m_prefab_name, entity.tag.tag.c_str(), sizeof(m_prefab_name) - 1);
            m_prefab_name[sizeof(m_prefab_name) - 1] = '\0';
            m_prefab_modal_open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))
            to_delete_id = entity.id;
        ImGui::EndPopup();
    }

    if (open && has_children)
    {
        for (Entity *child : entity.children)
            DrawEntityNode(*child, to_delete_id);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void SceneHierarchyPanel::DrawPrefabSaveModal()
{
    if (m_prefab_modal_open)
    {
        ImGui::OpenPopup("Save as Prefab");
        m_prefab_modal_open = false;
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Save as Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    Entity *entity = (m_prefab_entity_id >= 0)
        ? m_scene->GetEntityById(m_prefab_entity_id) : nullptr;
    if (!entity)
    {
        ImGui::CloseCurrentPopup();
        return;
    }

    ImGui::TextUnformatted("Save this entity (and its children) as a reusable prefab.");
    ImGui::TextDisabled("Root: '%s' (%d entity/ies in the subtree)",
                        entity->tag.tag.c_str(), 1 + (int)CountDescendants(*entity));
    ImGui::Separator();

    ImGui::InputText("Name", m_prefab_name, sizeof(m_prefab_name));
    ImGui::TextDisabled("Writes assets/prefabs/<name>.prefab.json");

    ImGui::Separator();
    if (ImGui::Button("Save"))
    {
        if (m_prefab_name[0] != '\0')
        {
            std::string path = std::string("assets/prefabs/") + m_prefab_name + ".prefab.json";
            std::string error;
            if (SceneSerializer::SavePrefab(*entity, path, &error))
                m_status = "Prefab saved to " + path;
            else
                m_status = "Prefab save failed: " + error;
        }
        else
        {
            m_status = "Prefab save failed: name is empty";
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void SceneHierarchyPanel::DrawSpawnPrefabModal()
{
    if (m_spawn_modal_open)
    {
        // Re-scan prefab files every time the dialog opens so new saves show up.
        m_prefab_files.clear();
        std::error_code ec;
        for (const auto &entry : fs::recursive_directory_iterator("assets", ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            std::string path = entry.path().generic_string();
            if (path.size() > 5 && path.substr(path.size() - 5) == ".json" &&
                SceneSerializer::IsPrefabFile(path))
            {
                m_prefab_files.push_back(path);
            }
        }
        std::sort(m_prefab_files.begin(), m_prefab_files.end());

        ImGui::OpenPopup("Spawn Prefab");
        m_spawn_modal_open = false;
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Spawn Prefab", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (m_prefab_files.empty())
    {
        ImGui::TextDisabled("No prefabs found under assets/. Save one from the "
                            "Hierarchy context menu.");
    }
    else if (ImGui::BeginListBox("##prefabs", ImVec2(430.0f, 240.0f)))
    {
        for (const std::string &path : m_prefab_files)
        {
            if (ImGui::Selectable(path.c_str()))
            {
                std::string error;
                if (Entity *spawned = SceneSerializer::LoadPrefab(*m_scene, path, nullptr, &error))
                {
                    if (m_history)
                        m_history->PushSpawn(*spawned, ("Spawn prefab '" + path + "'").c_str());
                    m_status = "Spawned prefab: " + path;
                }
                else
                {
                    m_status = "Prefab spawn failed: " + error;
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndListBox();
    }

    ImGui::Separator();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void SceneHierarchyPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Hierarchy", nullptr,
        ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Add Entity"))
    {
        Entity &created = m_scene->CreateEntity("New Entity");
        if (m_history)
            m_history->PushSpawn(created, "Create 'New Entity'");
    }

    ImGui::SameLine();

    bool has_selection = m_selection->entity_id != -1;
    if (!has_selection)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Add Child"))
    {
        Entity *selected = m_scene->GetEntityById(m_selection->entity_id);
        if (selected)
        {
            Entity &created = m_scene->CreateEntity("Child", selected);
            if (m_history)
                m_history->PushSpawn(created, "Create 'Child'");
        }
    }
    if (!has_selection)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (!has_selection)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Duplicate"))
    {
        Entity *selected = m_scene->GetEntityById(m_selection->entity_id);
        if (selected)
            DuplicateNode(*selected);
    }
    if (!has_selection)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (!has_selection)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Selected") || (has_selection && ImGui::IsKeyPressed(ImGuiKey_Delete)))
    {
        int id = m_selection->entity_id;
        ClearSelectionIfAffected(m_scene, m_selection, id);
        if (Entity *to_delete = m_scene->GetEntityById(id))
            DeleteEntity(*to_delete);
    }
    if (!has_selection)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Spawn Prefab..."))
        m_spawn_modal_open = true;

    if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
    {
        // Drop target on the root header: prefabs dragged from the Content
        // Browser spawn at the scene root, and entities dragged here are
        // detached to the root (unparented).
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PREFAB"))
            {
                std::string path(static_cast<const char *>(payload->Data), payload->DataSize);
                while (!path.empty() && path.back() == '\0')
                    path.pop_back();

                std::string error;
                if (Entity *spawned = SceneSerializer::LoadPrefab(*m_scene, path, nullptr, &error))
                {
                    if (m_history)
                        m_history->PushSpawn(*spawned, ("Spawn prefab '" + path + "'").c_str());
                    m_status = "Spawned prefab: " + path;
                }
                else
                {
                    m_status = "Prefab spawn failed: " + error;
                }
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MESH"))
            {
                std::string path(static_cast<const char *>(payload->Data), payload->DataSize);
                while (!path.empty() && path.back() == '\0')
                    path.pop_back();
                InstantiateMeshAsset(path, nullptr);
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY"))
            {
                int dragged_id = *(const int *)payload->Data;
                if (Entity *dragged = m_scene->GetEntityById(dragged_id))
                {
                    if (dragged->parent)
                    {
                        if (m_history)
                            m_history->BeginEntityEdit(dragged_id, "Detach to root");
                        m_scene->SetParent(dragged_id, -1);
                        if (m_history)
                            m_history->EndEntityEdit();
                        m_status = "Detached '" + dragged->tag.tag + "' to scene root";
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        int to_delete_id = -1;

        for (auto &entity_ptr : m_scene->GetEntities())
        {
            if (!entity_ptr->parent)
                DrawEntityNode(*entity_ptr, to_delete_id);
        }

        if (to_delete_id != -1)
        {
            ClearSelectionIfAffected(m_scene, m_selection, to_delete_id);
            if (Entity *to_delete = m_scene->GetEntityById(to_delete_id))
                DeleteEntity(*to_delete);
        }

        // The remaining tree area doubles as a detach drop zone: dragging an
        // entity onto empty space unparents it to the scene root.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 0.0f)
        {
            ImGui::InvisibleButton("##hierarchy_detach_drop", ImVec2(avail.x, avail.y));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY"))
                {
                    int dragged_id = *(const int *)payload->Data;
                    if (Entity *dragged = m_scene->GetEntityById(dragged_id))
                    {
                        if (dragged->parent)
                        {
                            if (m_history)
                                m_history->BeginEntityEdit(dragged_id, "Detach to root");
                            m_scene->SetParent(dragged_id, -1);
                            if (m_history)
                                m_history->EndEntityEdit();
                            m_status = "Detached '" + dragged->tag.tag + "' to scene root";
                        }
                    }
                }
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("MESH"))
                {
                    std::string path(static_cast<const char *>(payload->Data), payload->DataSize);
                    while (!path.empty() && path.back() == '\0')
                        path.pop_back();
                    InstantiateMeshAsset(path, nullptr);
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::TreePop();
    }

    if (!m_status.empty())
    {
        ImGui::Separator();
        ImGui::TextDisabled("%s", m_status.c_str());
    }

    ImGui::End();

    DrawPrefabSaveModal();
    DrawSpawnPrefabModal();
}
