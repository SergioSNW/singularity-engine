#include "StatsPanel.h"

#include <imgui.h>
#include <cstdio>

#include "Window.h"

StatsPanel::StatsPanel(const Window *window)
    : m_window(window)
{
}

void StatsPanel::OnImGuiRender(float dt)
{
    float fps = (dt > 0.0f) ? 1.0f / dt : 0.0f;
    char fps_buffer[64];
    snprintf(fps_buffer, sizeof(fps_buffer), "%.1f", fps);

    ImGui::Begin("Singularity Engine Stats", nullptr,
        ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Delta Time: %.4f ms", dt * 1000.0f);
    ImGui::Text("FPS: %s", fps_buffer);
    ImGui::Separator();
    ImGui::Text("Resolution: %dx%d",
        m_window ? m_window->GetWidth() : 0,
        m_window ? m_window->GetHeight() : 0);
    ImGui::Text("V-Sync: ON");
    ImGui::End();

}
