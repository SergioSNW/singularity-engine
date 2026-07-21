#pragma once

#include "EditorPanel.h"

struct SelectionState;

class SceneHierarchyPanel : public EditorPanel
{
public:
    SceneHierarchyPanel(SelectionState *selection);
    void OnImGuiRender(float dt) override;

private:
    SelectionState *m_selection;
};
