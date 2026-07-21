#pragma once

#include "EditorPanel.h"
#include <imgui.h>

struct SDL_Texture;

class ViewportPanel : public EditorPanel
{
public:
    ViewportPanel();
    void OnImGuiRender(float dt) override;

    int GetWidth()  const { return m_viewport_width; }
    int GetHeight() const { return m_viewport_height; }

    void       SetTexture(SDL_Texture *texture) { m_texture = texture; }
    SDL_Texture* GetTexture() const             { return m_texture; }

private:
    int m_viewport_width;
    int m_viewport_height;
    SDL_Texture *m_texture;
};
