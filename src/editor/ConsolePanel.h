#pragma once

#include "EditorPanel.h"

#include <cstddef>

// Dockable console window showing all engine + Lua output.
//
// Every row is drained from the shared Console sink and color-coded by
// severity: Info white, Warning yellow, Error red (Lua runtime exceptions and
// script bind failures land here as Error). A Clear button wipes the buffer
// and an Auto-scroll checkbox keeps the newest rows in view; both live in the
// window's toolbar so the scrolling region stays uninterrupted.
class ConsolePanel : public EditorPanel
{
public:
    ConsolePanel();
    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    bool m_visible = true;
    bool m_auto_scroll = true;
    std::size_t m_read = 0;  // entries already displayed (drives auto-scroll)
};
