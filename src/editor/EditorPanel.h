#pragma once

class EditorPanel
{
public:
    virtual ~EditorPanel() = default;
    virtual void OnImGuiRender(float dt) = 0;
};
