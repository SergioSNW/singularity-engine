#pragma once

#include "EditorPanel.h"

struct SelectionState;
class Scene;

class InspectorPanel : public EditorPanel
{
public:
    InspectorPanel(SelectionState *selection, Scene *scene);
    void OnImGuiRender(float dt) override;

private:
    SelectionState *m_selection;
    Scene *m_scene;
};
