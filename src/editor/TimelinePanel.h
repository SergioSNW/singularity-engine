#pragma once

#include "Animation.h"
#include "EditorPanel.h"
#include <imgui.h>

class Scene;
struct SelectionState;

// Phase 35 animation & timeline foundation: the track-based timeline editor.
// Owns no scene state — it reads the Application-owned TimelineState through
// the shared TimelineBridge (clock, transport, recording) and fires actions
// back through the bridge's callbacks, which the Application turns into undo
// transactions and scene edits.
class TimelinePanel : public EditorPanel
{
public:
    TimelinePanel(Scene *scene, SelectionState *selection, TimelineBridge *bridge);
    void OnImGuiRender(float dt) override;

private:
    void DrawTransport();
    void DrawLane(const char *label, const AnimationTrack &track, AnimProperty prop);
    void ScrubTo(float time);

    Scene *m_scene;
    SelectionState *m_selection;
    TimelineBridge *m_bridge;
};
