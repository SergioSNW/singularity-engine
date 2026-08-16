#pragma once

#include <string>

// Master docking / workspace manager.
//
// Owns the full-screen editor dockspace (a transparent host window covering
// the main viewport's work area) and the workspace preset system. A workspace
// is a canonical arrangement of dock nodes built with DockBuilder, so the
// editor is always deterministic ("no floating chaos").
//
// Three built-in workspaces target the main authoring tasks:
//
//   * LevelDesign      — the classic editor: Hierarchy over Stats on the left,
//                        Viewport center-stage, Inspector + Editor Settings
//                        tabbed in the right rail, and a bottom "Development
//                        Zone" grouping Content Browser + Console as tabs next
//                        to the docked Script Editor mini-IDE.
//   * Scripting        — a script-authoring workspace: a taller bottom strip
//                        hosts the unified Script Editor mini-IDE (browser
//                        sidebar, tab bar and code pane in a single window)
//                        beside the Content Browser + Console tabs, so the
//                        whole IDE is part of the unified dock.
//   * ShadingAndAssets — an asset/material workspace: the Material Editor is
//                        the primary right-rail authoring zone, with Inspector
//                        + Editor Settings + Content Browser tabbed beneath it
//                        and a bottom Console + Stats tab group, maximizing the
//                        viewport for inspecting shaded geometry. The Script
//                        Editor stays free-floating here.
//   * Landscape        — a terrain-authoring workspace: the Landscape panel
//                        owns the right rail (brush + tool palette) with the
//                        Inspector + Editor Settings tabbed beneath it, and the
//                        viewport is center-stage for sculpting. The viewport
//                        override replaces the transform gizmo with a projected
//                        brush cursor that sculpts the heightfield.
//
// Tab groups conserve screen real estate: several windows docked into one node
// render as a single tabbed window, with the last-docked window focused.
//
// A user-captured custom layout is persisted to editor_layout.json (with the
// active workspace name) and restored on the next launch.
class WorkspaceManager
{
public:
    enum class Workspace
    {
        LevelDesign,
        Scripting,
        ShadingAndAssets,
        Landscape,
    };

    // Human-readable workspace names, used by the menu-bar selector.
    static const char *WorkspaceName(Workspace ws);

    WorkspaceManager();

    // Render the host window + DockSpace. Call once per editor frame before
    // any panel is submitted. Rebuilds the node tree when a rebuild is pending
    // or on the very first frame (unless a saved custom layout was loaded).
    void DrawDockspace();

    // Force the node tree to rebuild on the next DrawDockspace(). Used after
    // play mode, which force-undocks the viewport and leaves docking
    // associations stale.
    void RequestRebuild();

    // Switch to a built-in workspace: rebuilds the tree immediately and
    // returns the dock node the Script Editor window should be placed in
    // (0 = the workspace leaves it free-floating).
    unsigned int ApplyWorkspace(Workspace ws);

    // Restore the active workspace's canonical layout and forget any saved
    // custom layout. Returns the dock node for the Script Editor window.
    unsigned int ResetToWorkspaceDefault();

    // Capture the current dock layout as the startup default. The serialized
    // .ini is stored at the end of the frame by FinalizeSave().
    void RequestSaveCurrent();

    // Serialize and persist a pending capture. Call after ImGui::Render().
    void FinalizeSave();

    // Startup restore: reads editor_layout.json. A captured custom layout is
    // applied before the first NewFrame; otherwise the stored workspace is
    // built on the first DrawDockspace().
    void LoadFromFile();
    void SaveToFile() const;

    Workspace GetWorkspace() const { return m_workspace; }
    bool HasSavedLayout() const { return !m_saved_layout.empty(); }

    // Reserve vertical space at the bottom of the dock host for the status
    // bar. Set once at startup (0 = status bar disabled / headless).
    void SetBottomBarHeight(float height) { m_bottom_bar_height = height; }
    float BottomBarHeight() const { return m_bottom_bar_height; }

private:
    void RebuildLayout();

    Workspace m_workspace;
    bool m_dockspace_valid;     // host window + tree created at least once
    bool m_needs_rebuild;       // rebuild the tree on the next DrawDockspace()
    bool m_use_loaded_layout;   // a saved custom layout drives docking (skip rebuild)
    bool m_save_requested;      // capture the current layout at FinalizeSave()
    unsigned int m_dockspace_id;
    unsigned int m_code_window_node;  // dock node for the script code window
    std::string m_saved_layout;       // captured custom .ini ("")
    std::string m_pending_save;       // serialized at FinalizeSave()
    float m_bottom_bar_height;        // px reserved for the status bar
};
