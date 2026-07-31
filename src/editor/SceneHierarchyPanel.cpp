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
    if (ImGui::Button("Delete Selected") || (has_selection && ImGui::IsKeyPressed(ImGuiKey_Delete)))
    {
        int id = m_selection->entity_id;
        m_selection->entity_id = -1;
        m_selection->entity_name.clear();
        m_scene->DestroyEntity(id);
    }
    if (!has_selection)
    {
        ImGui::EndDisabled();
    }

    if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
    {
        int to_delete_id = -1;

        for (auto &entity : m_scene->GetEntities())
        {
            ImGui::PushID(entity.id);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                     | ImGuiTreeNodeFlags_SpanFullWidth
                                     | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (m_selection->entity_id == entity.id)
                flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx(entity.tag.tag.c_str(), flags);

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                m_selection->entity_id = entity.id;
                m_selection->entity_name = entity.tag.tag;
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Delete"))
                    to_delete_id = entity.id;
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                to_delete_id = entity.id;

            ImGui::PopID();
        }

        if (to_delete_id != -1)
        {
            if (m_selection->entity_id == to_delete_id)
            {
                m_selection->entity_id = -1;
                m_selection->entity_name.clear();
            }
            m_scene->DestroyEntity(to_delete_id);
        }

        ImGui::TreePop();
    }

    ImGui::End();
}
