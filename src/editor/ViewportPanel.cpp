#include "ViewportPanel.h"
#include <SDL.h>

ViewportPanel::ViewportPanel()
    : m_viewport_width(0)
    , m_viewport_height(0)
    , m_texture(nullptr)
{
}

void ViewportPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Viewport", nullptr,
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    ImVec2 size = ImGui::GetContentRegionAvail();
    m_viewport_width  = (int)size.x;
    m_viewport_height = (int)size.y;

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
