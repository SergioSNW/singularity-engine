#include "editor/ConsolePanel.h"

#include "core/Console.h"

#include <imgui.h>

ConsolePanel::ConsolePanel()
{
}

void ConsolePanel::OnImGuiRender(float /*dt*/)
{
    Console &console = Console::Instance();
    console.DrainPipes();

    const std::size_t total = console.Size();
    const bool got_new = (total > m_read);

    if (!m_visible)
        return;                 // keep capturing; auto-scroll on next open

    if (!ImGui::Begin("Console", &m_visible))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
    {
        console.Clear();
        m_read = 0;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_auto_scroll);
    ImGui::SameLine();
    ImGui::TextDisabled("%d entries", (int)console.Size());
    ImGui::Separator();

    if (got_new)
        m_read = total;

    ImGui::BeginChild("##ConsoleScroll", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders);
    const std::size_t count = console.Size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const LogEntry &entry = console.Entry(i);
        ImVec4 color;
        switch (entry.level)
        {
            case LogLevel::Warning: color = ImVec4(0.95f, 0.80f, 0.25f, 1.00f); break;
            case LogLevel::Error:   color = ImVec4(1.00f, 0.35f, 0.35f, 1.00f); break;
            case LogLevel::Info:
            default:
                color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.text.c_str());
        ImGui::PopStyleColor();
    }

    if (m_auto_scroll && got_new)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}
