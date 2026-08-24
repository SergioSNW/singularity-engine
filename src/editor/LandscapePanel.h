#pragma once

#include "EditorPanel.h"
#include <imgui.h>

#include <functional>
#include <string>

class Scene;
struct SelectionState;
struct LandscapeBrushSettings;

// Phase 34 landscape & topology design suite: brush + tool controls for the
// procedural heightfield terrain. Owns no scene state — it edits the shared
// LandscapeBrushSettings that the Application's viewport override consumes,
// lists the scene's landscape entities as sculpt targets, and hands entity
// creation to the Application through the `on_create_landscape` callback
// (spawn + undo + selection + toast all live in one place).
class LandscapePanel : public EditorPanel
{
public:
    LandscapePanel(Scene *scene, SelectionState *selection,
                   LandscapeBrushSettings *brush,
                   std::function<void()> on_create_landscape);
    void OnImGuiRender(float dt) override;

    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }
    void ToggleVisible() { m_visible = !m_visible; }

private:
    Scene *m_scene;
    SelectionState *m_selection;
    LandscapeBrushSettings *m_brush;
    std::function<void()> m_on_create_landscape;
    bool m_visible = true;

    // Phase A heightmap import staging: the chosen source image and target
    // grid resolution are edited here before the user commits with "Load
    // Heightmap" (which is what actually touches the target entity), plus a
    // one-line status/error readout from the last attempt.
    std::string m_heightmap_pending_path;
    int m_heightmap_target_resolution = 64;  // matches the historical sculpt-terrain default
    std::string m_heightmap_status;
};
