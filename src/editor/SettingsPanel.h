#pragma once

#include "EditorPanel.h"
#include "editor/Theme.h"
#include "editor/GizmoController.h"

#include <functional>

// Live Editor Settings window: the theme customizer (six key color tokens that
// re-skin the editor on change) plus the grid-snapping configuration (Phase 18)
// applied to gizmo translate/rotate/scale drags. "Save Theme" persists the
// tokens to editor_theme.json (gitignored); the snap steps are session-only.
class SettingsPanel : public EditorPanel
{
public:
    // `colors` is the live token set owned by Application; `snap` is the live
    // SnapSettings struct the Application reads when building GizmoFrames;
    // `on_change` is invoked after any edit so Application can re-apply
    // ConfigureStyle.
    SettingsPanel(Theme::Colors *colors, SnapSettings *snap,
                  std::function<void()> on_change);

    void OnImGuiRender(float dt) override;

    bool IsVisible() const { return m_visible; }
    void ToggleVisible();

private:
    Theme::Colors *m_colors;
    SnapSettings *m_snap;
    std::function<void()> m_on_change;
    bool m_visible;
};
