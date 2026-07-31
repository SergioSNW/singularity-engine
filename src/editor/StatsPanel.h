#pragma once

#include "EditorPanel.h"

class Window;

class StatsPanel : public EditorPanel
{
public:
    StatsPanel(const Window *window);
    void OnImGuiRender(float dt) override;

private:
    const Window *m_window;
};
