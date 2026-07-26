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

    if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
    {
        for (auto &entity : m_scene->GetEntities())
        {
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
        }
        ImGui::TreePop();
    }

    ImGui::End();
}
