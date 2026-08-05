#pragma once

#include "EditorPanel.h"

#include <functional>
#include <string>
#include <vector>

// Global command palette: a modal, keyboard-driven quick launcher
// (Ctrl+Shift+P). Typing fuzzy-filters the editor's core actions live;
// Up/Down move the selection, Enter (or a click) runs it, Esc dismisses it.
// The filter box grabs keyboard focus the instant the palette opens, so the
// palette is usable purely from the keyboard.
class CommandPalette : public EditorPanel
{
public:
    struct Command
    {
        std::string label;       // display text, e.g. "Open Script Editor"
        std::string category;    // group, e.g. "View" / "Layout"
        std::string shortcut;    // optional, e.g. "Ctrl+Shift+P"
        std::function<void()> action;
    };

    CommandPalette();

    void Register(const Command &command);
    void ToggleOpen();
    bool IsOpen() const { return m_open; }

    void OnImGuiRender(float dt) override;

private:
    void RefreshMatches();
    void RunCommand(int index);
    void Close();

    std::vector<Command> m_commands;
    std::vector<int> m_matches;     // indices into m_commands (filtered + sorted)
    char m_filter[128] = {};
    int m_selected = 0;
    bool m_open = false;
    bool m_popup_open = false;      // OpenPopup issued for the current session
    bool m_grab_focus = false;      // focus the filter on the next frame
    bool m_scroll_to_selected = false;
};
