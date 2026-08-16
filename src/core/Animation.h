#pragma once

#include <functional>
#include <vector>

// Phase 35 animation & timeline foundation.
//
// Data-only animation types (no SDL / ImGui dependencies) so the core stays
// lean:
//   * AnimationKeyframe  - one time-stamped value on a property track.
//   * AnimationTrack     - a time-sorted run of keyframes for ONE transform
//                          property (Position, Rotation, or Scale).
//   * AnimationComponent - the entity-side component: the three property tracks
//                          plus playback policy. Stored in Entity.h.
//   * TimelineState      - the Application-owned global clock + transport.
//   * TimelineBridge     - the editor contract (Timeline panel + Inspector)
//                          that reads the clock and fires Application actions.
//
// Sampling: AnimationTrack values interpolate with linear interpolation (LERP);
// the rotation track interpolates with spherical linear interpolation (SLERP)
// in quaternion space so orientations rotate through the shortest arc (see
// Animation.cpp for the convention). The Application drives the clock and
// Anim::Apply() writes the sampled pose back onto an entity's transform.

struct AnimationKeyframe
{
    float time = 0.0f;                     // seconds on the global timeline clock
    float value[3] = { 0.0f, 0.0f, 0.0f }; // pos / euler-rotation (deg) / scale
};

inline bool operator==(const AnimationKeyframe &a, const AnimationKeyframe &b)
{
    return a.time == b.time &&
           a.value[0] == b.value[0] && a.value[1] == b.value[1] &&
           a.value[2] == b.value[2];
}

// One property lane: keyframes kept sorted ascending by time. Insert/remove
// through Anim::SetKeyframe / Anim::RemoveKeyframe so ordering is preserved.
struct AnimationTrack
{
    std::vector<AnimationKeyframe> keys;
    bool IsEmpty() const { return keys.empty(); }
    size_t Size() const { return keys.size(); }
    float MaxTime() const { return keys.empty() ? 0.0f : keys.back().time; }
};

// The entity-side animation component: the three transform-property tracks plus
// playback policy. `loop` re-wraps the global clock; `duration` mirrors the
// longest key time (the component's timeline extent) and is refreshed on edit.
struct AnimationComponent
{
    AnimationTrack position;
    AnimationTrack rotation;
    AnimationTrack scale;
    bool loop = false;
    float duration = 0.0f;
};

// Which transform property a timeline lane / Inspector toggle addresses.
enum class AnimProperty
{
    Position,
    Rotation,
    Scale,
};

// Application-owned global timeline clock. `playing` advances `time` by the
// frame delta every editor frame; scrubbing writes `time` directly.
struct TimelineState
{
    float time = 0.0f;
    float duration = 10.0f;
    bool playing = false;
    bool loop = false;
};

// Editor contract shared by the Timeline panel and the Inspector's keyframe
// toggles: read `state`, fire transport / scrub / record actions into the
// Application (which owns the undo transactions and scene mutations). Every
// callback may be null; the UI simply degrades.
struct TimelineBridge
{
    TimelineState *state = nullptr;
    std::function<void()> on_play_pause;
    std::function<void()> on_stop;
    std::function<void()> on_scrub;
    std::function<void(AnimProperty)> on_set_keyframe;
    std::function<void(AnimProperty, float)> on_remove_keyframe;
};

namespace Anim
{

// Insert `value` at `time` (replacing any key already at that time), keeping
// the track sorted ascending by time.
void SetKeyframe(AnimationTrack &track, float time, const float value[3]);

// Remove the key exactly at `time` (within a small epsilon), if any.
// Returns true when a key was removed.
bool RemoveKeyframe(AnimationTrack &track, float time);

// The key exactly at `time`, or nullptr.
const AnimationKeyframe *KeyAt(const AnimationTrack &track, float time);

// Longest key time across all three tracks (the component's timeline extent).
float TrackDuration(const AnimationComponent &anim);

// Linear interpolation (LERP) along a track. Before the first key the first
// value is held, after the last key the last is held; with `loop` the time is
// wrapped into the track's span first.
void SampleValue(const AnimationTrack &track, float t, bool loop, float out[3]);

// Rotation track sampled with spherical linear interpolation (SLERP) in
// quaternion space, so orientations rotate through the shortest arc. Times
// that land exactly on a keyframe reproduce that keyframe's stored Euler
// angles verbatim, so recorded poses never drift.
void SampleRotation(const AnimationTrack &track, float t, bool loop, float out[3]);

// Write the sampled pose into `pos` / `rot` / `scale` (Euler degrees). Only
// properties that actually have keyframes are overwritten — an empty track
// leaves the entity's authored value untouched.
void Apply(const AnimationComponent &anim, float t,
           float pos[3], float rot[3], float scale[3]);

} // namespace Anim
