#include "CollisionMatrixPanel.h"

#include "Scene.h"

#include <imgui.h>

#include <cstring>

CollisionMatrixPanel::CollisionMatrixPanel(Scene *scene)
    : m_scene(scene)
{
    // Seed the inline name-edit buffers from the scene's matrix.
    for (int i = 0; i < CollisionMatrix::kLayerCount; ++i)
        std::strncpy(m_name_buffers[i], m_scene->collision_matrix.names[i].c_str(),
                     sizeof(m_name_buffers[i]) - 1);
}

void CollisionMatrixPanel::OnImGuiRender(float dt)
{
    (void)dt;
    if (!m_visible)
        return;

    ImGui::Begin("Collision Matrix", &m_visible, ImGuiWindowFlags_NoCollapse);

    ImGui::TextDisabled(
        "Bodies interact only when any of their layers collide (Inspector > "
        "Collider > Layers).");
    ImGui::Spacing();

    if (ImGui::Button("Reset All Pairs"))
        m_scene->collision_matrix.ResetAll();
    ImGui::SameLine();
    ImGui::TextDisabled("Re-enable every pair, including self-collision");

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::BeginTable("##collision_matrix", CollisionMatrix::kLayerCount + 1,
                          ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX |
                              ImGuiTableFlags_RowBg))
    {
        // Header row: one column per layer, labeled with its index (the row
        // labels carry the full names).
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        for (int c = 0; c < CollisionMatrix::kLayerCount; ++c)
            ImGui::TableSetupColumn(std::to_string(c).c_str(),
                                    ImGuiTableColumnFlags_WidthFixed, 22.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < CollisionMatrix::kLayerCount; ++i)
        {
            ImGui::TableNextRow();

            // Row label: the layer's editable name. Typing writes through to
            // the matrix when the widget is released; the buffer re-syncs from
            // the matrix whenever the field is idle (so a scene load or a
            // Reset All that renamed layers shows the fresh names).
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (ImGui::InputText("##name", m_name_buffers[i], sizeof(m_name_buffers[i]),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                m_scene->collision_matrix.SetLayerName(i, m_name_buffers[i]);
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_scene->collision_matrix.SetLayerName(i, m_name_buffers[i]);
            if (!ImGui::IsItemActive())
                std::strncpy(m_name_buffers[i], m_scene->collision_matrix.names[i].c_str(),
                             sizeof(m_name_buffers[i]) - 1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rename layer %d", i);

            // Pair cells. Toggling (i, c) flips the symmetric entry too, so
            // the grid always reads back the single shared pair state.
            for (int c = 0; c < CollisionMatrix::kLayerCount; ++c)
            {
                ImGui::TableSetColumnIndex(c + 1);
                ImGui::PushID(c);
                bool enabled = m_scene->collision_matrix.Collides(i, c);
                if (ImGui::Checkbox("##pair", &enabled))
                    m_scene->collision_matrix.SetPair(i, c, enabled);
                if (ImGui::IsItemHovered())
                {
                    if (i == c)
                        ImGui::SetTooltip("%s self-collision",
                                          m_scene->collision_matrix.LayerName(i));
                    else
                        ImGui::SetTooltip("%s <-> %s",
                                          m_scene->collision_matrix.LayerName(i),
                                          m_scene->collision_matrix.LayerName(c));
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
