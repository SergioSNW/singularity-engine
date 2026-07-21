#pragma once

#include "EditorPanel.h"

class SceneHierarchyPanel : public EditorPanel
{
public:
    SceneHierarchyPanel();
    void OnImGuiRender(float dt) override;
};
