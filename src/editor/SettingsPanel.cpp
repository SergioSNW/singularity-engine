#include "SettingsPanel.h"

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
                             std::function<void()> on_change)
    : m_colors(colors)
    , m_snap(snap)
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

    ImGui::TextUnformatted("Live theme customizer — edits apply instantly.");
    ImGui::Separator();
    ImGui::Spacing();

    // The six key tokens; every other style color is derived from these, so
    // a single edit re-skins selection, tabs, borders and scrollbars too.
    ColorRow("Window Background", m_colors->window_bg, m_on_change);
    ColorRow("Panel / Child", m_colors->child_bg, m_on_change);
    ColorRow("Popup / Menu", m_colors->popup_bg, m_on_change);
    ColorRow("Controls / Frames", m_colors->frame_bg, m_on_change);
    ColorRow("Text", m_colors->text, m_on_change);
    ColorRow("Accent", m_colors->accent, m_on_change);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Reset to Default"))
    {
        *m_colors = Theme::DefaultColors();
        if (m_on_change)
            m_on_change();
        Theme::SaveThemeToFile(*m_colors);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Theme"))
        Theme::SaveThemeToFile(*m_colors);
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        m_visible = false;

    ImGui::TextDisabled("Theme persisted to editor_theme.json (gitignored).");

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
    ImGui::TextDisabled("Hold Ctrl during a gizmo drag to snap regardless of "
                        "the toggle. Steps are not persisted.");

    ImGui::End();
}
