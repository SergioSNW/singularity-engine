#pragma once

#include "EditorPanel.h"

class Window;

class StatsPanel : public EditorPanel
{
public:
    StatsPanel(const Window *window);
    void OnImGuiRender(float dt) override;

    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }
    void ToggleVisible() { m_visible = !m_visible; }

private:
    const Window *m_window;
    bool m_visible = true;
};
