#pragma once

#include "EditorPanel.h"

struct SelectionState;

class InspectorPanel : public EditorPanel
{
public:
    InspectorPanel(SelectionState *selection);
    void OnImGuiRender(float dt) override;

private:
    SelectionState *m_selection;

    float m_position[3];
    float m_rotation[3];
    float m_scale[3];
    float m_color[4];
    bool m_active;
};
