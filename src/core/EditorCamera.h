#pragma once

#include "EngineMath.h"

// Phase 25 — Free-Fly Editor Camera & Viewport Navigation.
//
// The editor owns a dedicated free-fly camera that is independent from the
// scene's gameplay camera entities: Fly Mode (right-click while hovering the
// viewport) drives this pose, and the editor renders the viewport through it.
// Toggling Play Mode smoothly blends the view between this editor camera and
// the active gameplay camera over kCameraTransitionDuration.

// Duration of the editor <-> gameplay camera blend on Play/Stop, in seconds.
static const float kCameraTransitionDuration = 0.6f;

// A full camera pose. Position is world space; pitch is the tilt about the
// camera's local right axis (degrees), yaw is the heading about world up
// (degrees), and fov is the vertical field of view (degrees). Mirrors the
// fields the gameplay CameraComponent carries on an entity, so a pose can be
// captured from either source and blended without conversion.
struct EditorCamera
{
    Vec3 position{ 0.0f, 2.0f, 8.0f };
    float pitch = -14.0f;
    float yaw = 0.0f;
    float fov = 60.0f;
};

// Editor navigation tuning (Phase 25). Exposed as sliders in the Editor
// Settings window and read by UpdateCameraControls while flying.
struct EditorCameraSettings
{
    float fly_speed = 8.0f;              // world-units per second (WASD/QE)
    float rotation_sensitivity = 0.2f;   // degrees of look per mouse pixel
};

// Which direction an editor <-> gameplay camera blend is traveling.
enum class CameraTransitionPhase
{
    None,       // no blend in flight; render from the active camera directly
    ToGameplay, // Editor -> gameplay camera (entering Play)
    ToEditor,   // gameplay -> editor camera (leaving Play)
};

// In-flight Play/Stop camera blend: from/to are full poses, t in [0, 1] is
// advanced by dt each frame and maps through CameraBlend's smoothstep.
struct CameraTransitionState
{
    CameraTransitionPhase phase = CameraTransitionPhase::None;
    float t = 0.0f;
    EditorCamera from;
    EditorCamera to;
};

// Blend two camera poses with smoothstep easing for `t` in [0, 1]. Yaw takes
// the shortest arc (a 350 deg -> 10 deg turn spins +20 deg, not -340), and the
// blended yaw is wrapped back into (-180, 180]. Extracted as a pure function so
// the transition math is testable headless (no SDL/ImGui in the build).
inline EditorCamera CameraBlend(const EditorCamera &from, const EditorCamera &to, float t)
{
    float s = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    s = s * s * (3.0f - 2.0f * s);  // Hermite smoothstep: 3t^2 - 2t^3

    float d_yaw = to.yaw - from.yaw;
    while (d_yaw > 180.0f)  d_yaw -= 360.0f;
    while (d_yaw < -180.0f) d_yaw += 360.0f;

    EditorCamera out;
    out.position.x = from.position.x + (to.position.x - from.position.x) * s;
    out.position.y = from.position.y + (to.position.y - from.position.y) * s;
    out.position.z = from.position.z + (to.position.z - from.position.z) * s;
    out.pitch = from.pitch + (to.pitch - from.pitch) * s;
    out.yaw = from.yaw + d_yaw * s;
    while (out.yaw > 180.0f)  out.yaw -= 360.0f;
    while (out.yaw < -180.0f) out.yaw += 360.0f;
    out.fov = from.fov + (to.fov - from.fov) * s;
    return out;
}
