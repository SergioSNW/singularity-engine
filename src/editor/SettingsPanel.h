#pragma once

#include "EditorPanel.h"
#include "editor/Theme.h"

#include <functional>

// Live Theme Customizer. Exposes the six key color tokens from Theme::Colors
// as color pickers that re-skin the whole editor the moment they change
// (derived colors follow automatically). "Save Theme" persists the tokens to
// editor_theme.json (gitignored) so a custom scheme survives restarts;
// "Reset to Default" restores the warm charcoal + indigo palette.
class SettingsPanel : public EditorPanel
{
public:
    // `colors` is the live token set owned by Application; `on_change` is
    // invoked after any edit so Application can re-apply ConfigureStyle.
    SettingsPanel(Theme::Colors *colors, std::function<void()> on_change);

    void OnImGuiRender(float dt) override;

    bool IsVisible() const { return m_visible; }
    void ToggleVisible();

private:
    Theme::Colors *m_colors;
    std::function<void()> m_on_change;
    bool m_visible;
};
