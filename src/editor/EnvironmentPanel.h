#pragma once

#include <functional>
#include <string>

#include "EditorPanel.h"
#include "core/Environment.h"

// Global environment & post-processing settings editor (Phase 37).
//
// Three collapsible sections (Sky, Fog, Post-Processing) edit the shared
// EnvironmentSettings in place — every slider/checkbox takes effect on the
// next frame, exactly like the theme or the viewport overlay settings, and is
// intentionally NOT part of the undo history. "Reload" discards live edits and
// re-reads the .env asset; "Save" persists the current state to it (the asset
// directory is created on demand). A shortcut button opens the Material Editor
// docked in the same "Shading & Assets" workspace.
class EnvironmentPanel : public EditorPanel
{
public:
    EnvironmentPanel(EnvironmentSettings *settings);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    // Refresh from disk / persist to disk (paths resolved by the Application).
    void Reload();
    void Save();
    void SetAssetPath(const std::string &path) { m_asset_path = path; }

    // Invoked when the in-panel "Material Editor" shortcut is pressed, so the
    // Application can dock/focus that panel in the same workspace.
    using OpenMaterialEditorCallback = std::function<void()>;
    void SetOpenMaterialEditorCallback(OpenMaterialEditorCallback cb)
    {
        m_open_material_editor_cb = std::move(cb);
    }

    const std::string &LastError() const { return m_last_error; }

private:
    void DrawSectionHeader(const char *label, bool *enabled);
    void DrawColor3(const char *label, float color[3]);
    void DrawSlider(const char *label, float *value, float min, float max,
                    const char *fmt = "%.3f");

    EnvironmentSettings *m_settings;
    std::string m_asset_path;
    std::string m_last_error;
    bool m_visible = true;
    bool m_open_material_editor = false;
    OpenMaterialEditorCallback m_open_material_editor_cb;
};
