#pragma once

#include <string>
#include <vector>

// Phase 27 — Camera & Multi-Viewport Management.
//
// The CameraManager replaces the engine's single-camera assumption with a
// stack of camera entries. Each entry couples a camera *source* (the free-fly
// editor camera, or a CameraComponent living on a scene entity) to a *viewport
// layout definition*: a normalized rectangle on the render target plus a
// z-order. The renderer walks the stack once per frame (multi-pass), drawing
// each enabled entry into its own region so split-screen / multi-camera scenes
// render in a single pass pipeline.
//
// The manager is deliberately pure data + layout math: no SDL, no ImGui, no
// Scene access. Pose resolution is the Application's job (it needs the scene
// to read entity transforms), which keeps this class unit-testable headless.

// Where a camera entry gets its pose from.
enum class CameraSourceType
{
    Editor,      // the free-fly editor camera (or the blended gameplay camera
                 // during play) — the classic single-viewport source
    SceneEntity, // a CameraComponent on a scene entity, referenced by id
};

// One camera slot in the stack. `x/y/w/h` form a normalized (0..1) rectangle
// on the render target: x/y is the top-left corner, w/h the size. `z` decides
// draw order (higher renders on top; ties keep insertion order). `primary`
// marks the entry that owns editor input (gizmo picking, asset drops) and the
// only one that draws editor overlays. `label` is a display name for the UI.
struct CameraEntry
{
    CameraSourceType type = CameraSourceType::Editor;
    int entity_id = -1;      // SceneEntity: the camera entity's scene id
    float x = 0.0f;          // normalized left edge
    float y = 0.0f;          // normalized top edge
    float w = 1.0f;          // normalized width
    float h = 1.0f;          // normalized height
    int z = 0;               // draw order: higher z renders on top
    bool enabled = true;
    bool primary = false;    // exactly one entry may be primary
    std::string label;
};

class CameraManager
{
public:
    CameraManager();

    // Stack operations. Add() returns the new entry's index; Remove() keeps
    // the primary invariant (if the primary entry is removed, the next
    // topmost enabled entry becomes primary).
    int Add(const CameraEntry &entry);
    bool Remove(size_t index);
    void Clear();

    size_t Count() const { return m_entries.size(); }

    // Number of entries with enabled == true (the status bar's "active
    // viewports" metric). PrimaryIndex() can still fall back to an enabled
    // entry even when the primary flag is on a disabled one.
    size_t EnabledCount() const
    {
        size_t n = 0;
        for (const CameraEntry &e : m_entries)
            if (e.enabled)
                ++n;
        return n;
    }
    const CameraEntry *Get(size_t index) const
    {
        return index < m_entries.size() ? &m_entries[index] : nullptr;
    }
    CameraEntry *GetMutable(size_t index)
    {
        return index < m_entries.size() ? &m_entries[index] : nullptr;
    }

    // Demote every entry and mark `index` primary. No-op when out of range.
    void SetPrimary(size_t index);

    // Indices ordered for drawing: z ascending (bottom-up), stable so entries
    // with equal z keep their stack order. Each pass renders into its region
    // and higher z regions paint over lower ones.
    std::vector<size_t> DrawOrder() const;

    // Index of the primary entry; -1 when the stack is empty. Falls back to
    // the topmost (largest z, latest added) enabled entry if none is primary.
    int PrimaryIndex() const;

    // Normalized layout rect -> pixel rect within a target. Returns false for
    // degenerate/zero rects; the result is clamped into the target bounds.
    static bool RectToPixels(const CameraEntry &e, int target_w, int target_h,
                             int &px, int &py, int &pw, int &ph);

    // The shipped layout: one full-screen primary editor camera. Restores the
    // classic single-viewport behavior exactly.
    void ResetToSingleViewport();

private:
    std::vector<CameraEntry> m_entries;
};
