#pragma once

#include <string>

// Stage 6: a Lua script can call Game.LoadScene(path) to swap the active
// scene at runtime (e.g. walking through a level exit). The swap itself is
// wrapped in a brief fade-to-black-and-back rather than applied on the
// spot, for two reasons: it never reads as a single jarring frame-to-frame
// pop, and it lets the actual scene swap be deferred to the one safe point
// in the frame (Application::Run(), right after
// Scene::FlushPendingDestroyEntities -- every system that walks the entity
// list this frame has finished by then) instead of happening mid-script-call
// while OnTriggerEnter's own caller is still iterating the old scene.
static const float kSceneTransitionFadeDuration = 0.35f;  // seconds, each half

enum class SceneTransitionPhase
{
    None,       // no transition in flight
    FadingOut,  // screen darkening toward black; old scene still active underneath
    FadingIn,   // new scene just loaded; screen clearing back to normal
};

// In-flight scene transition. `target_scene` is captured once, when the
// request is accepted (FadingOut begins), and stays valid through both
// phases -- it's read once, at the FadingOut -> FadingIn boundary, to
// perform the actual load.
struct SceneTransitionState
{
    SceneTransitionPhase phase = SceneTransitionPhase::None;
    float t = 0.0f;
    std::string target_scene;
};

// Opacity of the full-screen fade overlay for the current phase/t, in
// [0, 1] -- 0 is fully clear, 1 is fully black. Pure so it's testable
// headless, the same way EditorCamera.h's CameraBlend is.
inline float SceneTransitionFadeAlpha(const SceneTransitionState &s)
{
    const float t = s.t < 0.0f ? 0.0f : (s.t > 1.0f ? 1.0f : s.t);
    if (s.phase == SceneTransitionPhase::FadingOut)
        return t;
    if (s.phase == SceneTransitionPhase::FadingIn)
        return 1.0f - t;
    return 0.0f;
}
