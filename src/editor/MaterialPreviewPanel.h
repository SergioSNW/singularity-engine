#pragma once

#include "EditorPanel.h"

#include <functional>
#include <string>

// Dedicated interactive material preview viewport (Phase 38).
//
// Renders a procedural test mesh (UV sphere or cylinder) lit by the active
// Environment & Shading settings and the selected material's PBR channels.
// The mesh is drawn into an off-screen target by the Application each frame
// (the preview_provider hands out the SDL texture like the Inspector's camera
// preview), so albedo/metallic/roughness/AO edits from the Material Editor are
// visible instantly. The camera orbits the test mesh: drag to rotate, mouse
// wheel to dolly, Auto-rotate spins it slowly, and Reset returns to the
// default framing. The default workspace docks this viewport under the main
// camera in the Shading & Assets workspace.
class MaterialPreviewPanel : public EditorPanel
{
public:
    explicit MaterialPreviewPanel(const std::function<void *(int, int)> &preview_provider);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    // Current orbit framing consumed by the Application's preview render.
    void GetOrbit(float &yaw, float &pitch, float &dist) const
    {
        yaw = m_yaw;
        pitch = m_pitch;
        dist = m_dist;
    }

    // Selected test mesh: 0 = UV sphere, 1 = cylinder.
    int MeshIndex() const { return m_mesh_index; }

    // True while the panel window was actually drawn last frame (docked-inactive
    // tabs report false), so the Application skips the software preview render
    // when nobody is looking at it.
    bool FrameActive() const { return m_frame_active; }

private:
    std::function<void *(int, int)> m_preview_provider;

    float m_yaw = 35.0f;
    float m_pitch = 20.0f;
    float m_dist = 3.2f;
    bool m_autorotate = true;
    int m_mesh_index = 0;
    bool m_visible = true;
    bool m_frame_active = false;
};
