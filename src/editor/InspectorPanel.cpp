#include "InspectorPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"

#include <imgui.h>

InspectorPanel::InspectorPanel(SelectionState *selection, Scene *scene)
    : m_selection(selection)
    , m_scene(scene)
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

    Entity *entity = m_scene->GetEntityById(m_selection->entity_id);
    if (!entity)
    {
        ImGui::TextDisabled("Entity not found");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity: %s", entity->tag.tag.c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Position", entity->transform.position, 0.1f);
        ImGui::DragFloat3("Rotation", entity->transform.rotation, 0.1f);
        ImGui::DragFloat3("Scale",    entity->transform.scale,    0.1f);
    }

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::ColorEdit4("Albedo", entity->material.color);
        ImGui::Checkbox("Active", &entity->material.active);
    }

    ImGui::End();
}
