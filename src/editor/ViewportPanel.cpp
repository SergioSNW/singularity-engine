#include "ViewportPanel.h"

#include <imgui.h>

ViewportPanel::ViewportPanel()
{
}

void ViewportPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Viewport", nullptr,
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::Dummy(size);

    ImGui::End();
}
