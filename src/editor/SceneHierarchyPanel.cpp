#include "SceneHierarchyPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"

#include <imgui.h>

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

    ImGui::End();
}
