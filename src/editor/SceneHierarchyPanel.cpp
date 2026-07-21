#include "SceneHierarchyPanel.h"

#include <imgui.h>

SceneHierarchyPanel::SceneHierarchyPanel()
{
}

void SceneHierarchyPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Hierarchy");

    if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_Leaf))
            ImGui::TreePop();

        if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_Leaf))
            ImGui::TreePop();

        if (ImGui::TreeNodeEx("Cube Object", ImGuiTreeNodeFlags_Leaf))
            ImGui::TreePop();

        ImGui::TreePop();
    }

    ImGui::End();
}
