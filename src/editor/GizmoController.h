#pragma once

#include "EngineMath.h"

#include <functional>

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

// Editor grid-snapping configuration (Phase 18). Applied to translate/rotate/
// scale gizmo drags whenever GizmoFrame::snap_active is set — which happens
// when the editor's snap toggle is enabled OR the Ctrl modifier is held during
// the drag (hold-to-snap). The values are the grid steps for each operation;
// a non-positive step disables snapping for that operation.
struct SnapSettings
{
    bool enabled = false;    // persistent snap toggle (toolbar / Settings)
    float translation = 0.5f; // world units per grid step
    float rotation = 15.0f;   // degrees per step
    float scale = 0.1f;       // scale multiplier per step
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

    // Grid snapping (Phase 18): when snap_active is true, translate/rotate/
    // scale drags snap to the configured increments. Filled by the editor from
    // its SnapSettings (toggle or Ctrl-held).
    float snap_translation = 0.5f;
    float snap_rotation = 15.0f;
    float snap_scale = 0.1f;
    bool snap_active = false;

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

    // Id of the entity under the cursor (nearest ray/AABB hit), or -1. Updated
    // by Update() every editor frame, used by the editor to draw the hover
    // bounds box. Distinct from selection: hovering never changes selection.
    int GetHoverEntity() const { return m_hover_entity; }

    // True while an axis drag is active (between mouse-down on a handle and
    // mouse release). The editor suppresses undo shortcuts during a drag so
    // Ctrl (hold-to-snap) can't accidentally pop history.
    bool IsDragging() const { return m_dragging; }

    // Undo hooks (Phase 22): fired when a gizmo drag begins (with the dragged
    // entity's id) and when it ends, so the editor can wrap the drag in an
    // undo transaction. Wired by the Application; left null for headless use.
    std::function<void(int entity_id)> on_drag_start;
    std::function<void()> on_drag_end;

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
    int m_hover_entity = -1;     // entity under the cursor (ray/AABB hit), -1 if none

    void StartDrag(const GizmoFrame &frame, Entity &entity);
    void ApplyDrag(const GizmoFrame &frame, Entity &entity);
    void Pick(const GizmoFrame &frame);
    int RaycastEntity(const GizmoFrame &frame);  // nearest entity whose world AABB the cursor ray hits
};
