#include "SettingsPanel.h"
#include "UiText.h"

#include <imgui.h>

namespace {

// A color token row: picker first, label after, live apply on change.
static bool ColorRow(const char *label, float color[4],
                     std::function<void()> on_change)
{
    bool changed = false;
    ImGui::PushID(label);
    changed = ImGui::ColorEdit4("##value", color,
                                ImGuiColorEditFlags_NoAlpha |
                                ImGuiColorEditFlags_NoTooltip |
                                ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();

    if (changed && on_change)
        on_change();
    return changed;
}

} // namespace

SettingsPanel::SettingsPanel(Theme::Colors *colors, SnapSettings *snap,
                             EditorCameraSettings *camera_settings,
                             std::function<void()> on_change)
    : m_colors(colors)
    , m_snap(snap)
    , m_camera_settings(camera_settings)
    , m_on_change(std::move(on_change))
    , m_visible(false)
{
}

void SettingsPanel::ToggleVisible()
{
    m_visible = !m_visible;
}

void SettingsPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    if (!ImGui::Begin("Editor Settings", &m_visible, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Matte slate-gray theme — edits below apply live and are saved to disk automatically.");
    ImGui::Separator();
    ImGui::Spacing();

    // The key tokens; every other style color is derived from these, so a single
    // edit re-skins selection, tabs, borders, secondary panels and scrollbars.
    ColorRow("Window Background", m_colors->window_bg, m_on_change);
    ColorRow("Panel / Child", m_colors->child_bg, m_on_change);
    ColorRow("Secondary Panel", m_colors->secondary_bg, m_on_change);
    ColorRow("Folder / Browser", m_colors->folder_bg, m_on_change);
    ColorRow("Popup / Menu", m_colors->popup_bg, m_on_change);
    ColorRow("Controls / Frames", m_colors->frame_bg, m_on_change);
    ColorRow("Text", m_colors->text, m_on_change);
    ColorRow("Border", m_colors->border, m_on_change);
    ColorRow("Accent", m_colors->accent, m_on_change);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Reset to Default"))
    {
        *m_colors = Theme::DefaultColors();
        if (m_on_change)
            m_on_change();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        m_visible = false;

    TextDisabledWrapped("Saved to config/theme.json and reloaded on the next launch. Reset to Default also clears the saved file.");

    // --- Grid & Snapping (Phase 18) ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("Grid & Snapping");
    ImGui::Checkbox("Snap to grid", &m_snap->enabled);
    ImGui::DragFloat("Translation step", &m_snap->translation,
                     0.05f, 0.01f, 100.0f, "%.2f");
    ImGui::DragFloat("Rotation step (deg)", &m_snap->rotation,
                     1.0f, 1.0f, 360.0f, "%.0f");
    ImGui::DragFloat("Scale step", &m_snap->scale,
                     0.05f, 0.01f, 10.0f, "%.2f");
    TextDisabledWrapped("Hold Ctrl during a gizmo drag to snap regardless of "
                        "the toggle. Steps are not persisted.");

    // --- Viewport navigation (Phase 25) ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("Viewport Navigation");
    ImGui::SliderFloat("Fly Speed", &m_camera_settings->fly_speed,
                       1.0f, 40.0f, "%.1f");
    ImGui::SliderFloat("Rotation Sensitivity", &m_camera_settings->rotation_sensitivity,
                       0.05f, 1.0f, "%.2f");
    TextDisabledWrapped("Right-click in the viewport to fly: WASD to move, "
                        "QE to rise/lower, mouse to look. Settings are "
                        "session-only.");

    ImGui::End();
}
