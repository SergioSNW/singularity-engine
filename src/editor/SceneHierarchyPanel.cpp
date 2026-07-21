#include "SceneHierarchyPanel.h"
#include "SelectionState.h"

#include <imgui.h>

static const char *g_entity_names[] = {
    "Scene Root",
    "Camera",
    "Directional Light",
    "Cube Object",
};

SceneHierarchyPanel::SceneHierarchyPanel(SelectionState *selection)
    : m_selection(selection)
{
}

void SceneHierarchyPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Hierarchy");

    {
        const int id = 0;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen
                                 | ImGuiTreeNodeFlags_SpanFullWidth;
        if (m_selection->entity_id == id)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (id == 0 && ImGui::TreeNodeEx(g_entity_names[id], flags))
        {
            for (int i = 1; i < 4; ++i)
            {
                ImGuiTreeNodeFlags child_flags = ImGuiTreeNodeFlags_Leaf
                                               | ImGuiTreeNodeFlags_SpanFullWidth
                                               | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (m_selection->entity_id == i)
                    child_flags |= ImGuiTreeNodeFlags_Selected;

                ImGui::TreeNodeEx(g_entity_names[i], child_flags);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                {
                    m_selection->entity_id = i;
                    m_selection->entity_name = g_entity_names[i];
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_selection->entity_id = id;
            m_selection->entity_name = g_entity_names[id];
        }
    }

    ImGui::End();
}
