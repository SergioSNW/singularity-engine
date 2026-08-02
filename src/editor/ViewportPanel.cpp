#include "ViewportPanel.h"
#include <SDL.h>

ViewportPanel::ViewportPanel()
    : m_viewport_width(0)
    , m_viewport_height(0)
    , m_hovered(false)
    , m_texture(nullptr)
{
}

void ViewportPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;

    if (m_isolated)
    {
        // Detach from the dockspace and pin the window to the full area below
        // the main menu bar, undecorated and immovable: a true game viewport.
        const float menu_h = ImGui::GetFrameHeight();
        ImGui::SetNextWindowPos(ImVec2(0.0f, menu_h));
        ImGui::SetNextWindowSize(
            ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - menu_h)
        );
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoSavedSettings;
    }

    ImGui::Begin("Viewport", nullptr, flags);

    if (m_isolated)
        ImGui::PopStyleVar(2);

    m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    ImVec2 size = ImGui::GetContentRegionAvail();
    m_viewport_width  = (int)size.x;
    m_viewport_height = (int)size.y;

    // Record the on-screen rect the 3D image occupies so the gizmo controller
    // can map the mouse cursor into viewport-pixel space for picking/dragging.
    m_image_min  = ImGui::GetCursorScreenPos();
    m_image_size = size;

    if (m_texture)
    {
        ImGui::Image((ImTextureID)m_texture, size);
    }
    else
    {
        ImU32 color = ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.09f, 1.0f));
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + size.x,
                   ImGui::GetCursorScreenPos().y + size.y),
            color
        );
    }

    ImGui::End();
}
