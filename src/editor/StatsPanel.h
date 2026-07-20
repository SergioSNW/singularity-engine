#pragma once

#include "EditorPanel.h"

class StatsPanel : public EditorPanel
{
public:
    StatsPanel(int window_width, int window_height);
    void OnImGuiRender(float dt) override;

private:
    int m_window_width;
    int m_window_height;
};
