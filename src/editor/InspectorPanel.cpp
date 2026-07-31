#include "InspectorPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"

#include <imgui.h>
#include <cstring>

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

    if (m_selection->entity_id != m_last_selected_id)
    {
        m_last_selected_id = m_selection->entity_id;
        std::strncpy(m_tag_buffer, entity->tag.tag.c_str(), sizeof(m_tag_buffer) - 1);
        m_tag_buffer[sizeof(m_tag_buffer) - 1] = '\0';
    }

    if (ImGui::InputText("Tag", m_tag_buffer, sizeof(m_tag_buffer)))
        entity->tag.tag = m_tag_buffer;

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

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("FOV", &entity->camera.fov, 0.5f, 1.0f, 179.0f);
        ImGui::DragFloat("Pitch", &entity->camera.pitch, 0.1f, -89.0f, 89.0f);
        ImGui::DragFloat("Yaw",   &entity->camera.yaw,   0.1f);
        ImGui::DragFloat("Near", &entity->camera.near_plane, 0.01f, 0.001f, 10.0f);
        ImGui::DragFloat("Far",  &entity->camera.far_plane,  0.1f,  0.1f, 1000.0f);
        ImGui::Checkbox("Primary", &entity->camera.primary);
    }

    ImGui::End();
}
