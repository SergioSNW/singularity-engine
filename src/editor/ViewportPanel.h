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
    bool IsHovered() const { return m_hovered; }

    // Screen-space rect of the rendered 3D image inside the ImGui window.
    // Used to map the mouse cursor into viewport-pixel space for picking.
    ImVec2 GetImageMin()  const { return m_image_min; }
    ImVec2 GetImageSize() const { return m_image_size; }

    void       SetTexture(SDL_Texture *texture) { m_texture = texture; }
    SDL_Texture* GetTexture() const             { return m_texture; }

    // In "isolated" (play) mode the viewport detaches from the dock layout and
    // fills the whole window below the menu bar as a true game viewport.
    void SetIsolated(bool isolated) { m_isolated = isolated; }

private:
    int m_viewport_width;
    int m_viewport_height;
    bool m_hovered;
    bool m_isolated = false;
    ImVec2 m_image_min{0.0f, 0.0f};
    ImVec2 m_image_size{0.0f, 0.0f};
    SDL_Texture *m_texture;
};
