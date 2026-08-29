#pragma once

#include "EditorPanel.h"
#include <imgui.h>

#include <functional>

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

    // Workspace-driven visibility (Phase 35): the Sequencing workspace hides the
    // viewport entirely and replaces it with the Timeline editor, so the whole
    // window stops being submitted while hidden.
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }

    // Drag-drop handler for asset payloads dropped onto the 3D view: fires
    // with the payload type ("PREFAB"/"MESH"/"MATERIAL"/"TEXTURE") and its
    // null-terminated path string. Wired by the Application, which computes the
    // drop point from GetImageMin()/GetImageSize() and the current mouse
    // position; left null for headless use.
    std::function<void(const char *type, const char *payload)> on_drop;

    // Phase 29 viewport chrome callbacks, wired by the Application (which owns
    // the toolbar/HUD state). Fired only in docked editor mode (never in the
    // isolated play view):
    //   on_toolbar - drawn right after ImGui::Begin, above the 3D image. It
    //                shrinks the content region, so the image rect captured
    //                after it stays correct for picking/dropping.
    //   on_overlay - drawn right before ImGui::End, on top of the 3D image.
    std::function<void()> on_toolbar;
    std::function<void()> on_overlay;

    // Stage 3 gameplay HUD: the mirror image of on_overlay -- drawn at the
    // same point (right before ImGui::End, on top of the 3D image) but only
    // in isolated (Play) mode, since on_overlay is an editor-only stats
    // readout that intentionally never shows during Play.
    std::function<void()> on_gameplay_hud;

private:
    int m_viewport_width;
    int m_viewport_height;
    bool m_hovered;
    bool m_isolated = false;
    bool m_visible = true;
    ImVec2 m_image_min{0.0f, 0.0f};
    ImVec2 m_image_size{0.0f, 0.0f};
    SDL_Texture *m_texture;
};
