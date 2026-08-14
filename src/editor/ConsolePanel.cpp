#include "editor/ConsolePanel.h"

#include "core/Console.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

ConsolePanel::ConsolePanel()
    : m_input{0}
    , m_history_index(0)
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

    DrawReplInput();

    ImGui::End();
}

void ConsolePanel::CycleHistory(int direction)
{
    const int size = (int)m_history.size();
    if (size == 0)
        return;
    if (direction < 0 && m_history_index > 0)
        --m_history_index;
    else if (direction > 0 && m_history_index < size)
        ++m_history_index;

    if (m_history_index == size)
    {
        m_input[0] = '\0';
        return;
    }
    const std::string &entry = m_history[m_history_index];
    const std::size_t len = std::min<std::size_t>(entry.size(), sizeof(m_input) - 1);
    std::memcpy(m_input, entry.data(), len);
    m_input[len] = '\0';
}

void ConsolePanel::DrawReplInput()
{
    ImGui::Separator();
    ImGui::PushID("repl");

    ImGui::TextDisabled("Lua REPL");
    ImGui::SameLine();
    ImGui::TextDisabled("enter=run  up/down=history  esc=clear");

    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool submit = ImGui::InputText("##input", m_input, sizeof(m_input),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
            CycleHistory(-1);
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
            CycleHistory(1);
    }

    if (submit)
    {
        std::string line = m_input;
        const std::size_t first = line.find_first_not_of(" \t\r\n");
        if (first != std::string::npos)
        {
            const std::size_t last = line.find_last_not_of(" \t\r\n");
            line = line.substr(first, last - first + 1);
            if (!line.empty())
            {
                m_history.push_back(line);
                m_history_index = (int)m_history.size();
                if (on_execute)
                    on_execute(line);
            }
        }
        m_input[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);  // keep typing on the same field
    }
    else if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_input[0] = '\0';
    }

    ImGui::PopID();
}
