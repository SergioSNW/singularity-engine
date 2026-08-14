#pragma once

#include "EditorPanel.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// Dockable console window showing all engine + Lua output.
//
// Every row is drained from the shared Console sink and color-coded by
// severity: Info white, Warning yellow, Error red (Lua runtime exceptions and
// script bind failures land here as Error). A Clear button wipes the buffer
// and an Auto-scroll checkbox keeps the newest rows in view; both live in the
// window's toolbar so the scrolling region stays uninterrupted.
//
// A Lua REPL command line sits under the log: typing a snippet and pressing
// Enter fires on_execute (wired by the Application to ScriptEngine::Execute,
// so snippets run against the active scene). Up/Down walk the input history,
// Escape clears the current line.
class ConsolePanel : public EditorPanel
{
public:
    ConsolePanel();
    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    // Fired when a REPL line is submitted. The Application binds this to
    // ScriptEngine::Execute so the snippet runs against the active scene.
    std::function<void(const std::string &)> on_execute;

private:
    void DrawReplInput();
    void CycleHistory(int direction);

    bool m_visible = true;
    bool m_auto_scroll = true;
    std::size_t m_read = 0;  // entries already displayed (drives auto-scroll)
    char m_input[512];       // REPL command-line buffer
    std::vector<std::string> m_history;
    int m_history_index;     // walk position in [0..size]; size = editing fresh
};
