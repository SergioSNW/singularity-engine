#pragma once

#include "EditorPanel.h"

class CameraManager;

// Viewport layout panel (Phase 27): edits the CameraManager camera stack that
// drives multi-viewport rendering. Each entry couples a camera source (editor
// camera or a scene entity's CameraComponent) with a normalized rect and a
// z-order. Structural edits apply immediately (no undo step); Reset restores
// the shipped single-viewport layout.
class ViewportLayoutPanel : public EditorPanel
{
public:
    explicit ViewportLayoutPanel(CameraManager *cameras);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    CameraManager *m_cameras;
    bool m_visible = false;
};
