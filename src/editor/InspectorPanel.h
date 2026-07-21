#pragma once

#include "EditorPanel.h"

class InspectorPanel : public EditorPanel
{
public:
    InspectorPanel();
    void OnImGuiRender(float dt) override;

private:
    float m_position[3];
    float m_rotation[3];
    float m_scale[3];
    float m_color[4];
    bool m_active;
};
