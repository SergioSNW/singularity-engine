#include "ViewportLayoutPanel.h"

#include "core/CameraManager.h"

#include <imgui.h>
#include <cstring>
#include <string>

ViewportLayoutPanel::ViewportLayoutPanel(CameraManager *cameras)
    : m_cameras(cameras)
{
}

void ViewportLayoutPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    if (!ImGui::Begin("Viewport Layout", &m_visible, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Camera stack: each entry renders the scene into its "
                        "own viewport region (higher z on top).");
    ImGui::TextDisabled("Exactly one entry is primary: it owns editor picking, "
                        "gizmos and asset drops.");
    ImGui::Separator();

    if (!m_cameras)
    {
        ImGui::TextDisabled("No camera manager.");
        ImGui::End();
        return;
    }

    if (m_cameras->Count() == 0)
        ImGui::TextDisabled("(empty)");

    const size_t count = m_cameras->Count();
    std::vector<size_t> to_remove;
    for (size_t i = 0; i < count; ++i)
    {
        CameraEntry *entry = m_cameras->GetMutable(i);
        if (!entry)
            continue;

        const std::string label = entry->label.empty()
            ? "Camera Entry " + std::to_string(i)
            : entry->label;
        if (ImGui::CollapsingHeader((label + "##" + std::to_string(i)).c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            char label_buf[64] = {};
            std::strncpy(label_buf, entry->label.c_str(), sizeof(label_buf) - 1);
            if (ImGui::InputText("Label", label_buf, sizeof(label_buf)))
                entry->label = label_buf;

            const char *types[] = { "Editor Camera", "Scene Entity" };
            int type_idx = (entry->type == CameraSourceType::SceneEntity) ? 1 : 0;
            if (ImGui::Combo("Source", &type_idx, types, 2))
            {
                entry->type = (type_idx == 1) ? CameraSourceType::SceneEntity
                                              : CameraSourceType::Editor;
                if (entry->type == CameraSourceType::Editor)
                    entry->entity_id = -1;
            }
            if (entry->type == CameraSourceType::SceneEntity)
                ImGui::DragInt("Entity ID", &entry->entity_id, 0.1f);

            ImGui::DragFloat("X", &entry->x, 0.01f, 0.0f, 1.0f, "%.3f");
            ImGui::DragFloat("Y", &entry->y, 0.01f, 0.0f, 1.0f, "%.3f");
            ImGui::DragFloat("Width", &entry->w, 0.01f, 0.0f, 1.0f, "%.3f");
            ImGui::DragFloat("Height", &entry->h, 0.01f, 0.0f, 1.0f, "%.3f");
            ImGui::DragInt("Z Order", &entry->z, 0.1f);
            ImGui::Checkbox("Enabled", &entry->enabled);

            if (ImGui::RadioButton("Primary", entry->primary))
                m_cameras->SetPrimary(i);
            ImGui::SameLine();
            if (ImGui::Button(("Remove##" + std::to_string(i)).c_str()))
                to_remove.push_back(i);
        }
    }

    // Apply removals after the loop (indices shift while iterating).
    for (size_t idx : to_remove)
        m_cameras->Remove(idx);

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Add Entry"))
    {
        CameraEntry entry;
        entry.label = "Camera Entry " + std::to_string(m_cameras->Count());
        // Default the new entry to a right-half split so layouts start sane.
        entry.x = 0.5f;
        entry.w = 0.5f;
        m_cameras->Add(entry);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Single Viewport"))
        m_cameras->ResetToSingleViewport();

    ImGui::End();
}
