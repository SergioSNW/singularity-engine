#pragma once

#include "EditorPanel.h"
#include "editor/Theme.h"
#include "editor/GizmoController.h"
#include "core/EditorCamera.h"

#include <functional>

// Live Editor Settings window: the theme customizer (six key color tokens that
// re-skin the editor on change), the grid-snapping configuration (Phase 18)
// applied to gizmo translate/rotate/scale drags, and the viewport navigation
// tuning (Phase 25) used by Fly Mode. "Save Theme" persists the tokens to
// editor_theme.json (gitignored); the snap steps and navigation tuning are
// session-only.
class SettingsPanel : public EditorPanel
{
public:
    // `colors` is the live token set owned by Application; `snap` is the live
    // SnapSettings struct the Application reads when building GizmoFrames;
    // `camera_settings` is the live fly-speed / rotation-sensitivity struct the
    // Application reads while flying; `on_change` is invoked after any edit so
    // Application can re-apply ConfigureStyle.
    SettingsPanel(Theme::Colors *colors, SnapSettings *snap,
                  EditorCameraSettings *camera_settings,
                  std::function<void()> on_change);

    void OnImGuiRender(float dt) override;

    bool IsVisible() const { return m_visible; }
    void ToggleVisible();

private:
    Theme::Colors *m_colors;
    SnapSettings *m_snap;
    EditorCameraSettings *m_camera_settings;
    std::function<void()> m_on_change;
    bool m_visible;
};
