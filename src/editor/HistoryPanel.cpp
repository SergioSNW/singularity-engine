#include "HistoryPanel.h"

#include "CommandHistory.h"
#include "editor/Theme.h"

#include <imgui.h>
#include <string>

HistoryPanel::HistoryPanel(CommandHistory *history)
    : m_history(history)
{
}

void HistoryPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    if (!ImGui::Begin("History", &m_visible, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Ctrl+Z undo / Ctrl+Y redo — up to 100 steps.");
    ImGui::Separator();

    ImGui::BeginDisabled(!(m_history && m_history->CanUndo()));
    Theme::PushPrimaryButtonColor();
    if (ImGui::Button("Undo") && m_history)
        m_history->Undo();
    Theme::PopPrimaryButtonColor();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!(m_history && m_history->CanRedo()));
    if (ImGui::Button("Redo") && m_history)
        m_history->Redo();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!(m_history && m_history->UndoSize() > 0));
    if (ImGui::Button("Clear") && m_history)
        m_history->Clear();
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!m_history)
    {
        ImGui::TextDisabled("No command history.");
        ImGui::End();
        return;
    }

    const int undo_count = (int)m_history->UndoSize();
    const int redo_count = (int)m_history->RedoSize();

    ImGui::TextUnformatted("Undo stack");
    ImGui::Separator();
    if (undo_count == 0)
    {
        ImGui::TextDisabled("(empty)");
    }
    else
    {
        for (int i = undo_count - 1; i >= 0; --i)
        {
            const Command *cmd = m_history->UndoAt((size_t)i);
            if (!cmd)
                continue;
            if (i == undo_count - 1)
                ImGui::BulletText("%s", cmd->Description().c_str());
            else
                ImGui::TextUnformatted(("  - " + cmd->Description()).c_str());
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Redo stack");
    ImGui::Separator();
    if (redo_count == 0)
    {
        ImGui::TextDisabled("(empty)");
    }
    else
    {
        for (int i = redo_count - 1; i >= 0; --i)
        {
            const Command *cmd = m_history->RedoAt((size_t)i);
            if (!cmd)
                continue;
            if (i == redo_count - 1)
                ImGui::BulletText("%s", cmd->Description().c_str());
            else
                ImGui::TextUnformatted(("  - " + cmd->Description()).c_str());
        }
    }

    ImGui::End();
}
