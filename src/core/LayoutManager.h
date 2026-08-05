#pragma once

#include <string>

// Master docking layout manager.
//
// Owns the full-screen editor dockspace (a transparent host window covering
// the main viewport's work area) and the workspace preset system:
//
//   * Default  — the classic editor arrangement: Hierarchy (over the Content
//                Browser) | Viewport | Inspector over a bottom strip holding
//                the script sidebar and the stats panel. The code editor stays
//                free-floating.
//   * Scripting — a script-authoring workspace: a shorter bottom strip split
//                into the script sidebar (left) and a *docked* code window
//                (right), so the whole IDE is part of the unified dock.
//
// Presets rebuild the dock node tree with DockBuilder, so the workspace is
// always deterministic ("no floating chaos"). A user-captured custom layout is
// persisted to editor_layout.json and restored on the next launch.
class LayoutManager
{
public:
    enum class Preset
    {
        Default,
        Scripting,
    };

    LayoutManager();

    // Render the host window + DockSpace. Call once per editor frame before
    // any panel is submitted. Rebuilds the node tree when a rebuild is pending
    // or on the very first frame (unless a saved custom layout was loaded).
    void DrawDockspace();

    // Force the node tree to rebuild on the next DrawDockspace(). Used after
    // play mode, which force-undocks the viewport and leaves docking
    // associations stale.
    void RequestRebuild();

    // Switch to a built-in workspace: rebuilds the tree immediately and
    // returns the dock node the script-editor code window should be placed in
    // (0 = the preset leaves it free-floating).
    unsigned int ApplyPreset(Preset preset);

    // Restore the pristine Default workspace and forget any saved custom
    // layout.
    void ResetToDefault();

    // Capture the current dock layout as the startup default. The serialized
    // .ini is stored at the end of the frame by FinalizeSave().
    void RequestSaveCurrent();

    // Serialize and persist a pending capture. Call after ImGui::Render().
    void FinalizeSave();

    // Startup restore: reads editor_layout.json. A captured custom layout is
    // applied before the first NewFrame; otherwise the stored preset is built
    // on the first DrawDockspace().
    void LoadFromFile();
    void SaveToFile() const;

    Preset GetPreset() const { return m_preset; }
    bool HasSavedLayout() const { return !m_saved_layout.empty(); }

private:
    void RebuildLayout();

    Preset m_preset;
    bool m_dockspace_valid;     // host window + tree created at least once
    bool m_needs_rebuild;       // rebuild the tree on the next DrawDockspace()
    bool m_use_loaded_layout;   // a saved custom layout drives docking (skip rebuild)
    bool m_save_requested;      // capture the current layout at FinalizeSave()
    unsigned int m_dockspace_id;
    unsigned int m_code_window_node;  // dock node for the script code window
    std::string m_saved_layout;       // captured custom .ini ("")
    std::string m_pending_save;       // serialized at FinalizeSave()
};
