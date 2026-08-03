#pragma once

#include "EngineMath.h"

class Scene;
class SelectionState;
class MeshLibrary;
struct Entity;
struct SDL_Renderer;

enum class GizmoMode
{
    Translate,
    Rotate,
    Scale,
};

// Per-frame input/camera context handed to the gizmo controller by the editor.
// Mouse coordinates are in viewport-pixel space (0..vp_width, 0..vp_height).
struct GizmoFrame
{
    Scene *scene = nullptr;
    SelectionState *selection = nullptr;
    MeshLibrary *meshes = nullptr;
    int active_camera_id = -1;

    float vp_width = 0.0f;
    float vp_height = 0.0f;
    bool hovered = false;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;

    Vec3 cam_pos{0.0f, 0.0f, 0.0f};
    float cam_pitch = 0.0f;   // degrees
    float cam_yaw = 0.0f;     // degrees
    float cam_fov = 60.0f;    // degrees
    float near_p = 0.1f;
    Mat4 view_proj;
    float dt = 0.0f;

    // Pixels of the viewport render target per logical point (UI point) on
    // screen. This is the display's framebuffer scale (e.g. 2.0 on a 200%
    // high-DPI display) times the viewport supersample factor: the target is
    // created at that multiple of the logical viewport size, so all gizmo pixel
    // metrics (arm length, hit tolerances, ring radii) expressed in logical
    // points are multiplied by this factor to keep a constant on-screen size.
    float dpi_scale = 1.0f;
};

// Viewport transform gizmo (translate / rotate / scale) plus screen-space
// picking. Editor-only: the controller is never updated while playing or flying.
class GizmoController
{
public:
    GizmoMode mode = GizmoMode::Translate;

    // Switch the active operation. Any in-progress drag is cancelled so a
    // mid-drag mode change can never leave a stale axis/drag_axis referring to
    // the previous operation's handles (e.g. a rotate ring handle being
    // dragged while the mode is switched to Scale).
    void SetMode(GizmoMode new_mode)
    {
        if (mode == new_mode)
            return;
        mode = new_mode;
        m_dragging = false;
        m_drag_axis = -1;
    }

    // Process input: hover highlighting, click-to-pick, and axis dragging.
    // Must be called once per editor frame after the viewport has rendered.
    void Update(const GizmoFrame &frame);

    // Overlay the active gizmo into the viewport render target (after the 3D
    // scene pass, before the render target is unbound).
    void Draw(SDL_Renderer *renderer, const GizmoFrame &frame);

private:
    int m_drag_axis = -1;    // 0/1/2 = X/Y/Z axis, 3 = planar center, 4 = rotate ring
    bool m_dragging = false;
    float m_start_pos[3];
    float m_start_rot[3];
    float m_start_scale[3];
    float m_t_start = 0.0f;
    float m_angle_start = 0.0f;
    Vec3 m_plane_start{0.0f, 0.0f, 0.0f};
    Vec3 m_center{0.0f, 0.0f, 0.0f};
    float m_center_sx = 0.0f;    // projected gizmo center, viewport pixels
    float m_center_sy = 0.0f;
    float m_center_depth = 0.0f;
    float m_radius_world = 1.0f; // gizmo arm length in world units
    Vec3 m_axis_world[3];        // local axes of the selection, in world space
    int m_hover_axis = -1;
    bool m_hover_center = false;
    int m_hover_ring_axis = -1;  // rotate mode: -1 none, 0/1/2 axis ring, 3 outer trackball

    void StartDrag(const GizmoFrame &frame, Entity &entity);
    void ApplyDrag(const GizmoFrame &frame, Entity &entity);
    void Pick(const GizmoFrame &frame);
};
