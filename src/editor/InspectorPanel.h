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
    char m_tag_buffer[256] = {};
    char m_mesh_buffer[256] = {};
    char m_script_buffer[256] = {};
    int m_last_selected_id = -1;
};
