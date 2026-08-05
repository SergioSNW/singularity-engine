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

SettingsPanel::SettingsPanel(Theme::Colors *colors, std::function<void()> on_change)
    : m_colors(colors)
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

    if (!ImGui::Begin("Theme Settings", &m_visible, ImGuiWindowFlags_NoCollapse))
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

    ImGui::TextDisabled("Persisted to editor_theme.json (gitignored).");

    ImGui::End();
}
