#include "StatsPanel.h"

#include <imgui.h>
#include <cstdio>

StatsPanel::StatsPanel(int window_width, int window_height)
    : m_window_width(window_width)
    , m_window_height(window_height)
{
}

void StatsPanel::OnImGuiRender(float dt)
{
    float fps = (dt > 0.0f) ? 1.0f / dt : 0.0f;
    char fps_buffer[64];
    snprintf(fps_buffer, sizeof(fps_buffer), "%.1f", fps);

    ImGui::Begin("Singularity Engine Stats");
    ImGui::Text("Delta Time: %.4f ms", dt * 1000.0f);
    ImGui::Text("FPS: %s", fps_buffer);
    ImGui::Separator();
    ImGui::Text("Resolution: %dx%d", m_window_width, m_window_height);
    ImGui::Text("V-Sync: ON");
    ImGui::End();

}
