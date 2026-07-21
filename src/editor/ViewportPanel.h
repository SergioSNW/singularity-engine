#pragma once

#include "EditorPanel.h"

class ViewportPanel : public EditorPanel
{
public:
    ViewportPanel();
    void OnImGuiRender(float dt) override;
};
