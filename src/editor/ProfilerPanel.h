#pragma once

#include "EditorPanel.h"

class Profiler;

// Profiler panel (Phase 30): a dockable live view of the engine's performance
// telemetry. Plots the rolling per-stage frame times (Update / Render / UI /
// Physics) and the frame total, plus resource trends (entity count, 3D draw
// calls, resident memory). A Pause/Resume button freezes the buffers so a
// specific frame can be inspected; Clear drops the history. It reads the
// Application's Profiler instance (owned by Application) and writes nothing.
class ProfilerPanel : public EditorPanel
{
public:
    explicit ProfilerPanel(Profiler *profiler);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    Profiler *m_profiler;
    bool m_visible = false;
};
