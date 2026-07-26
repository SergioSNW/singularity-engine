#pragma once

#include "EditorPanel.h"

struct SelectionState;
class Scene;

class SceneHierarchyPanel : public EditorPanel
{
public:
    SceneHierarchyPanel(SelectionState *selection, Scene *scene);
    void OnImGuiRender(float dt) override;

private:
    SelectionState *m_selection;
    Scene *m_scene;
};
