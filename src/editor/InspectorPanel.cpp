#include "InspectorPanel.h"

#include <imgui.h>

InspectorPanel::InspectorPanel()
    : m_position{0.0f, 0.0f, 0.0f}
    , m_rotation{0.0f, 0.0f, 0.0f}
    , m_scale{1.0f, 1.0f, 1.0f}
    , m_color{1.0f, 1.0f, 1.0f, 1.0f}
    , m_active(true)
{
}

void InspectorPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Inspector");

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Position", m_position, 0.1f);
        ImGui::DragFloat3("Rotation", m_rotation, 0.1f);
        ImGui::DragFloat3("Scale", m_scale, 0.1f);
    }

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::ColorEdit4("Albedo", m_color);
    }

    ImGui::Checkbox("Active", &m_active);

    ImGui::End();
}
