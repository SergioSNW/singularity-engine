#include "MaterialPreviewPanel.h"

#include "editor/Theme.h"

#include <imgui.h>
#include <cmath>
#include <cstring>

namespace {

const char *const kMeshes[] = { "UV Sphere", "Cylinder" };

} // namespace

MaterialPreviewPanel::MaterialPreviewPanel(
    const std::function<void *(int, int)> &preview_provider)
    : m_preview_provider(preview_provider)
{
}

void MaterialPreviewPanel::OnImGuiRender(float dt)
{
    m_frame_active = false;
    if (!m_visible)
        return;

    const bool drawn = ImGui::Begin("Material Preview", &m_visible,
                                    ImGuiWindowFlags_NoCollapse);
    if (!drawn)
    {
        ImGui::End();
        return;
    }
    m_frame_active = true;

    // --- Toolbar -----------------------------------------------------------
    ImGui::Checkbox("Auto-rotate", &m_autorotate);
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        m_yaw = 35.0f;
        m_pitch = 20.0f;
        m_dist = 3.2f;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Test Mesh", &m_mesh_index, kMeshes, 2);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Viewport ----------------------------------------------------------
    // Fit a 4:3 target into the window (width-driven like the camera preview),
    // capped so the software rasterizer stays cheap even on huge docks.
    const float avail = ImGui::GetContentRegionAvail().x;
    int pw = (int)(avail > 16.0f ? avail : 16.0f);
    if (pw > 512)
        pw = 512;
    int ph = pw * 3 / 4;

    void *texture = m_preview_provider ? m_preview_provider(pw, ph) : nullptr;
    if (!texture)
    {
        ImGui::TextDisabled("Preview unavailable");
        ImGui::End();
        return;
    }

    ImGui::Image(texture, ImVec2((float)pw, (float)ph));

    // Transparent drag/zoom layer on top of the image.
    ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
    ImGui::InvisibleButton("##mat_preview_drag", ImVec2((float)pw, (float)ph));
    if (ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            m_dist *= std::exp(-wheel * 0.12f);
            if (m_dist < 1.0f)
                m_dist = 1.0f;
            if (m_dist > 8.0f)
                m_dist = 8.0f;
        }
    }
    if (ImGui::IsItemActive())
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_yaw += delta.x * 0.6f;
        m_pitch -= delta.y * 0.6f;
        if (m_pitch > 89.0f)
            m_pitch = 89.0f;
        if (m_pitch < -89.0f)
            m_pitch = -89.0f;
    }

    ImGui::TextDisabled("Drag to orbit, scroll to zoom");

    if (m_autorotate)
        m_yaw += dt * 25.0f;

    ImGui::End();
}
