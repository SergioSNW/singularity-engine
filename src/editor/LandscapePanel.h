#pragma once

#include "EditorPanel.h"
#include <imgui.h>

#include <functional>

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

private:
    Scene *m_scene;
    SelectionState *m_selection;
    LandscapeBrushSettings *m_brush;
    std::function<void()> m_on_create_landscape;
};
