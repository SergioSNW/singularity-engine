#include "InspectorPanel.h"
#include "SelectionState.h"

#include <imgui.h>

InspectorPanel::InspectorPanel(SelectionState *selection)
    : m_selection(selection)
    , m_position{0.0f, 0.0f, 0.0f}
    , m_rotation{0.0f, 0.0f, 0.0f}
    , m_scale{1.0f, 1.0f, 1.0f}
    , m_color{1.0f, 1.0f, 1.0f, 1.0f}
    , m_active(true)
{
}

void InspectorPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Inspector", nullptr,
        ImGuiWindowFlags_NoCollapse);

    if (m_selection->entity_id < 0)
    {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity: %s", m_selection->entity_name.c_str());
    ImGui::Separator();

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
