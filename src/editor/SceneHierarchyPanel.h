#pragma once

#include "EditorPanel.h"

struct SelectionState;
class Scene;
struct Entity;

class SceneHierarchyPanel : public EditorPanel
{
public:
    SceneHierarchyPanel(SelectionState *selection, Scene *scene);
    void OnImGuiRender(float dt) override;

private:
    void DrawEntityNode(Entity &entity, int &to_delete_id);

    SelectionState *m_selection;
    Scene *m_scene;
};
