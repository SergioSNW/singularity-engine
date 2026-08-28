#include "EnvironmentPanel.h"
#include "UiText.h"

#include "core/Environment.h"

#include <imgui.h>

#include <string>

EnvironmentPanel::EnvironmentPanel(EnvironmentSettings *settings)
    : m_settings(settings)
{
}

void EnvironmentPanel::DrawSectionHeader(const char *label, bool *enabled)
{
    ImGui::Checkbox(label, enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("on/off");
}

void EnvironmentPanel::DrawColor3(const char *label, float color[3])
{
    ImGui::ColorEdit3(label, color);
}

void EnvironmentPanel::DrawSlider(const char *label, float *value, float min,
                                  float max, const char *fmt)
{
    ImGui::SliderFloat(label, value, min, max, fmt);
}

void EnvironmentPanel::OnImGuiRender(float dt)
{
    (void)dt;
    if (!m_visible)
        return;

    ImGui::Begin("Environment & Shading", &m_visible, ImGuiWindowFlags_NoCollapse);

    TextDisabledWrapped("Global sky, fog and post-processing (not part of undo).");
    ImGui::Spacing();

    if (ImGui::Button("Reload"))
        Reload();
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        Save();
    ImGui::SameLine();
    if (ImGui::Button("Material Editor"))
        m_open_material_editor = true;
    ImGui::TextDisabled("Asset: %s", m_asset_path.c_str());
    if (!m_last_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_last_error.c_str());
    ImGui::Spacing();
    ImGui::Separator();

    // --- Sky ---
    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawSectionHeader("Enable Sky", &m_settings->sky_enabled);
        if (m_settings->sky_enabled)
        {
            DrawColor3("Top Color", m_settings->sky_color_top);
            DrawColor3("Horizon Color", m_settings->sky_color_horizon);
            DrawColor3("Sun Color", m_settings->sky_sun_color);
            DrawSlider("Sun Intensity", &m_settings->sky_sun_intensity, 0.0f, 4.0f);
            DrawSlider("Sun Glow", &m_settings->sky_sun_glow, 0.0f, 1.0f);
            DrawSlider("Sun Disk", &m_settings->sky_sun_disk, 0.0f, 0.1f, "%.4f");
            DrawSlider("Sun Yaw", &m_settings->sky_sun_yaw, -180.0f, 180.0f, "%.1f");
            DrawSlider("Sun Pitch", &m_settings->sky_sun_pitch, -90.0f, 90.0f, "%.1f");
            DrawSlider("Star Intensity", &m_settings->sky_star_intensity, 0.0f, 1.0f);
        }
        ImGui::Spacing();
        ImGui::Separator();
    }

    // --- Fog ---
    if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawSectionHeader("Enable Fog", &m_settings->fog_enabled);
        if (m_settings->fog_enabled)
        {
            DrawColor3("Color", m_settings->fog_color);
            DrawSlider("Density", &m_settings->fog_density, 0.0f, 0.1f, "%.4f");
            DrawSlider("Height Falloff", &m_settings->fog_height_falloff, 0.0f, 0.5f, "%.3f");
            DrawSlider("Start Distance", &m_settings->fog_start, 0.0f, 100.0f, "%.1f");
        }
        ImGui::Spacing();
        ImGui::Separator();
    }

    // --- Post-processing ---
    if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawSectionHeader("Enable Post", &m_settings->post_enabled);
        if (!m_settings->post_enabled)
            TextDisabledWrapped("Off by default: this is a CPU readback + full-res "
                                "grade, not a free GPU effect -- expect a real frame "
                                "cost when you turn it on.");
        if (m_settings->post_enabled)
        {
            DrawSectionHeader("Bloom", &m_settings->post_bloom_enabled);
            if (m_settings->post_bloom_enabled)
            {
                DrawSlider("Threshold", &m_settings->post_bloom_threshold, 0.0f, 2.0f);
                DrawSlider("Strength", &m_settings->post_bloom_strength, 0.0f, 2.0f);
                DrawSlider("Radius", &m_settings->post_bloom_radius, 1.0f, 4.0f);
                DrawSlider("Bloom Scale", &m_settings->post_scale, 0.1f, 1.0f, "%.2f");
                TextDisabledWrapped("Lower = cheaper, softer bloom. The rest of "
                                    "the image always renders at full resolution.");
            }
            ImGui::Separator();
            DrawSectionHeader("Tone Mapping", &m_settings->post_tonemap_enabled);
            DrawSlider("Exposure", &m_settings->post_exposure, 0.0f, 4.0f);
            DrawSlider("Gamma", &m_settings->post_gamma, 0.5f, 4.0f);
            DrawSlider("Saturation", &m_settings->post_saturation, 0.0f, 2.0f);
            DrawSlider("Contrast", &m_settings->post_contrast, 0.0f, 2.0f);
            DrawSlider("Temperature", &m_settings->post_temperature, -1.0f, 1.0f);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // --- Editor Working Light ---
    if (ImGui::CollapsingHeader("Editor Working Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawSectionHeader("Enable Fill Light", &m_settings->editor_fill_light_enabled);
        if (m_settings->editor_fill_light_enabled)
            DrawSlider("Fill Intensity", &m_settings->editor_fill_light_intensity, 0.0f, 1.0f);
        TextDisabledWrapped("A flat, unshadowed ambient fill so shadowed faces and "
                            "terrain valleys stay readable while sculpting, placing, "
                            "and painting -- from any camera angle. Editor-only: "
                            "never applied in Play mode, so gameplay lighting is "
                            "judged on its own.");
    }

    ImGui::End();

    if (m_open_material_editor)
    {
        m_open_material_editor = false;
        if (m_open_material_editor_cb)
            m_open_material_editor_cb();
    }
}

void EnvironmentPanel::Reload()
{
    EnvironmentSettings fresh;
    std::string error;
    if (LoadEnvironmentAsset(m_asset_path, fresh, &error))
        *m_settings = fresh;
    else
        m_last_error = "Reload failed: " + error;
}

void EnvironmentPanel::Save()
{
    std::string error;
    if (!SaveEnvironmentAsset(m_asset_path, *m_settings, &error))
        m_last_error = "Save failed: " + error;
    else
        m_last_error.clear();
}
