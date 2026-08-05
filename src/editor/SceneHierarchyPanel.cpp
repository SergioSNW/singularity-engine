#include "SceneHierarchyPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"
#include "SceneSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

SceneHierarchyPanel::SceneHierarchyPanel(SelectionState *selection, Scene *scene)
    : m_selection(selection)
    , m_scene(scene)
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

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Add Child"))
            m_scene->CreateEntity("Child", &entity);
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
                if (SceneSerializer::LoadPrefab(*m_scene, path, nullptr, &error))
                    m_status = "Spawned prefab: " + path;
                else
                    m_status = "Prefab spawn failed: " + error;
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
        m_scene->CreateEntity("New Entity");
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
            m_scene->CreateEntity("Child", selected);
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
        m_scene->DestroyEntity(id);
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
        int to_delete_id = -1;

        for (auto &entity_ptr : m_scene->GetEntities())
        {
            if (!entity_ptr->parent)
                DrawEntityNode(*entity_ptr, to_delete_id);
        }

        if (to_delete_id != -1)
        {
            ClearSelectionIfAffected(m_scene, m_selection, to_delete_id);
            m_scene->DestroyEntity(to_delete_id);
        }

        ImGui::TreePop();
    }

    // Drop target: prefabs dragged from the Content Browser spawn here.
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PREFAB"))
        {
            std::string path(static_cast<const char *>(payload->Data), payload->DataSize);
            while (!path.empty() && path.back() == '\0')
                path.pop_back();

            std::string error;
            if (SceneSerializer::LoadPrefab(*m_scene, path, nullptr, &error))
                m_status = "Spawned prefab: " + path;
            else
                m_status = "Prefab spawn failed: " + error;
        }
        ImGui::EndDragDropTarget();
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
