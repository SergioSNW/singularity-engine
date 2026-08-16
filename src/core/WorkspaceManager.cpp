#include "WorkspaceManager.h"

#include "Json.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <fstream>
#include <sstream>

// The node tree is built around a full-screen DockSpace. A transparent host
// window pins that DockSpace to the viewport's work area so every panel docks
// into one unified workspace instead of scattering as floating windows.
static const unsigned int kDockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;

// Panels grouped into the same dock node render as a single tabbed window.
// DockBuilder focuses the LAST window docked into a node, so order below
// controls which tab is active when a workspace is applied.
static const char *kHierarchyWindow = "Hierarchy";
static const char *kViewportWindow = "Viewport";
static const char *kInspectorWindow = "Inspector";
static const char *kSettingsWindow = "Editor Settings";
static const char *kContentBrowserWindow = "Content Browser";
static const char *kConsoleWindow = "Console";
static const char *kStatsWindow = "Singularity Engine Stats";
static const char *kScriptEditorWindow = "Script Editor";
static const char *kMaterialEditorWindow = "Material Editor";
static const char *kHistoryWindow = "History";
static const char *kViewportLayoutWindow = "Viewport Layout";
static const char *kLandscapeWindow = "Landscape";
static const char *kTimelineWindow = "Timeline";
static const char *kCollisionMatrixWindow = "Collision Matrix";
static const char *kEnvironmentWindow = "Environment & Shading";
static const char *kMaterialPreviewWindow = "Material Preview";

const char *WorkspaceManager::WorkspaceName(Workspace ws)
{
    switch (ws)
    {
        case Workspace::LevelDesign:     return "Level Design";
        case Workspace::Scripting:       return "Scripting";
        case Workspace::ShadingAndAssets:return "Shading & Assets";
        case Workspace::Landscape:       return "Landscape Mode";
        case Workspace::Timeline:        return "Sequencing";
    }
    return "Level Design";
}

WorkspaceManager::WorkspaceManager()
    : m_workspace(Workspace::LevelDesign)
    , m_dockspace_valid(false)
    , m_needs_rebuild(false)
    , m_use_loaded_layout(false)
    , m_save_requested(false)
    , m_dockspace_id(0)
    , m_code_window_node(0)
    , m_bottom_bar_height(0.0f)
{
}

void WorkspaceManager::RebuildLayout()
{
    // A saved custom layout drives docking entirely through the .ini; never
    // overwrite it with a canonical rebuild.
    if (m_use_loaded_layout)
        return;

    if (m_dockspace_id == 0)
        m_dockspace_id = ImGui::GetID("MainDockspace");
    m_code_window_node = 0;

    ImGui::DockBuilderRemoveNode(m_dockspace_id);
    ImGui::DockBuilderAddNode(m_dockspace_id, kDockspaceFlags);
    const ImVec2 dock_size = ImGui::GetMainViewport()->WorkSize;
    ImGui::DockBuilderSetNodeSize(
        m_dockspace_id,
        ImVec2(dock_size.x, dock_size.y - m_bottom_bar_height));

    ImGuiID top, bottom;
    ImGuiID left, center, right;

    // Helper that docks the standard right-hand rail: Editor Settings tabbed
    // under the Inspector (Settings first, so Inspector is the active tab).
    auto dock_right_rail = [](ImGuiID node) {
        ImGui::DockBuilderDockWindow(kSettingsWindow, node);
        ImGui::DockBuilderDockWindow(kInspectorWindow, node);
    };

    // Helper for the bottom "Development Zone" tab group: Material Editor,
    // Console and Content Browser share one tabbed region (Material Editor
    // docked first so the Content Browser stays the active tab). The Collision
    // Matrix (Phase 36) and the Material Preview (Phase 38) are docked first of
    // all so they sit behind every tab.
    auto dock_dev_zone = [](ImGuiID node) {
        ImGui::DockBuilderDockWindow(kMaterialPreviewWindow, node);
        ImGui::DockBuilderDockWindow(kEnvironmentWindow, node);
        ImGui::DockBuilderDockWindow(kCollisionMatrixWindow, node);
        ImGui::DockBuilderDockWindow(kMaterialEditorWindow, node);
        ImGui::DockBuilderDockWindow(kConsoleWindow, node);
        ImGui::DockBuilderDockWindow(kHistoryWindow, node);
        ImGui::DockBuilderDockWindow(kViewportLayoutWindow, node);
        ImGui::DockBuilderDockWindow(kContentBrowserWindow, node);
    };

    switch (m_workspace)
    {
        case Workspace::Scripting:
        {
            // Scripting workspace: a taller bottom strip dedicated to the IDE,
            // with the unified "Script Editor" mini-IDE (browser sidebar, tab
            // bar and code pane all inside the one window) docked beside the
            // "Development Zone" tabs, so the whole IDE is part of the unified
            // dock rather than floating.
            ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up, 0.62f, &top, &bottom);
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.18f, &left, &center);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, &right, &center);

            ImGuiID left_top, left_bottom;
            ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.55f, &left_top, &left_bottom);

            ImGuiID ide_slot, dev_right;
            ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.66f, &ide_slot, &dev_right);

            ImGui::DockBuilderDockWindow(kHierarchyWindow, left_top);
            ImGui::DockBuilderDockWindow(kStatsWindow, left_bottom);
            ImGui::DockBuilderDockWindow(kViewportWindow, center);
            dock_right_rail(right);
            ImGui::DockBuilderDockWindow(kScriptEditorWindow, ide_slot);
            dock_dev_zone(dev_right);

            // The mini-IDE is a single fixed-title window ("Script Editor"),
            // so it can be docked by name; the node is still routed to the
            // Application for the Float/Dock toolbar toggle.
            m_code_window_node = ide_slot;
            break;
        }
        case Workspace::ShadingAndAssets:
        {
            // Shading & Assets workspace: the Material Editor owns the right
            // rail as the primary material-authoring zone, with the Inspector
            // + Editor Settings + Content Browser tabbed beneath it, and a
            // bottom Console + Stats tab group. The center column splits into
            // the main viewport (top) and the dedicated Material Preview
            // viewport (bottom strip) so shaded geometry previews alongside
            // the scene under the same environment lighting.
            ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up, 0.68f, &top, &bottom);
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.16f, &left, &center);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.32f, &right, &center);

            ImGuiID vp_top, vp_bottom;
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.62f, &vp_top, &vp_bottom);

            ImGuiID mat_top, mat_bottom;
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.58f, &mat_top, &mat_bottom);

            ImGui::DockBuilderDockWindow(kHierarchyWindow, left);
            ImGui::DockBuilderDockWindow(kViewportWindow, vp_top);
            ImGui::DockBuilderDockWindow(kMaterialPreviewWindow, vp_bottom);
            // Phase 37: the Environment & Shading panel docks *behind* the
            // Material Editor in the primary zone (last-docked wins focus).
            ImGui::DockBuilderDockWindow(kEnvironmentWindow, mat_top);
            ImGui::DockBuilderDockWindow(kMaterialEditorWindow, mat_top);

            // Asset-focused right-rail group: Content Browser is the active tab.
            ImGui::DockBuilderDockWindow(kSettingsWindow, mat_bottom);
            ImGui::DockBuilderDockWindow(kInspectorWindow, mat_bottom);
            ImGui::DockBuilderDockWindow(kContentBrowserWindow, mat_bottom);
            ImGui::DockBuilderDockWindow(kCollisionMatrixWindow, mat_bottom);
            ImGui::DockBuilderDockWindow(kEnvironmentWindow, mat_bottom);

            // Bottom zone: Console is the active tab over the stats.
            ImGui::DockBuilderDockWindow(kStatsWindow, bottom);
            ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);
            break;
        }
        default: // Workspace::LevelDesign
        {
            // Level Design workspace: the full-height left rail is the
            // Hierarchy (max room to manage entities and light sources), the
            // right rail tabs Inspector over Editor Settings, and the bottom
            // "Development Zone" groups the Material Editor, Console, History,
            // Stats and Content Browser in one tabbed strip beside the docked
            // Script Editor mini-IDE.
            ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up, 0.76f, &top, &bottom);
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.20f, &left, &center);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.20f, &right, &center);

            ImGuiID bottom_left, bottom_right;
            ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.50f, &bottom_left, &bottom_right);

            ImGui::DockBuilderDockWindow(kHierarchyWindow, left);
            ImGui::DockBuilderDockWindow(kViewportWindow, center);
            dock_right_rail(right);
            ImGui::DockBuilderDockWindow(kStatsWindow, bottom_left);
            dock_dev_zone(bottom_left);
            ImGui::DockBuilderDockWindow(kScriptEditorWindow, bottom_right);
            m_code_window_node = bottom_right;
            break;
        }
        case Workspace::Landscape:
        {
            // Landscape Mode: a terrain-authoring workspace. The Landscape
            // panel owns the right rail (brush + tool palette) with the
            // Inspector + Editor Settings tabbed beneath it; the viewport stays
            // center-stage for sculpting. The bottom zone hosts the Development
            // Zone tabs beside the stats; the Script Editor stays free-floating
            // while sculpting.
            ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up, 0.74f, &top, &bottom);
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.18f, &left, &center);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, &right, &center);

            ImGuiID brush_top, brush_bottom;
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.60f, &brush_top, &brush_bottom);

            ImGuiID bottom_left, bottom_right;
            ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.50f, &bottom_left, &bottom_right);

            ImGui::DockBuilderDockWindow(kHierarchyWindow, left);
            ImGui::DockBuilderDockWindow(kViewportWindow, center);
            ImGui::DockBuilderDockWindow(kLandscapeWindow, brush_top);
            dock_right_rail(brush_bottom);
            dock_dev_zone(bottom_left);
            ImGui::DockBuilderDockWindow(kStatsWindow, bottom_right);
            m_code_window_node = 0;
            break;
        }
        case Workspace::Timeline:
        {
            // Sequencing workspace (Phase 35): the track-based Timeline editor
            // replaces the viewport center-stage, so the animation timeline is
            // the primary authoring surface. The Inspector stays on the right
            // rail (its keyframe toggles pair with the lanes), the Hierarchy on
            // the left, and the Development Zone + Stats along the bottom. The
            // viewport is hidden by the Application while this workspace is
            // active; the Script Editor stays free-floating.
            ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up, 0.80f, &top, &bottom);
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.18f, &left, &center);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, &right, &center);

            ImGuiID bottom_left, bottom_right;
            ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.50f, &bottom_left, &bottom_right);

            ImGui::DockBuilderDockWindow(kHierarchyWindow, left);
            ImGui::DockBuilderDockWindow(kTimelineWindow, center);
            dock_right_rail(right);
            dock_dev_zone(bottom_left);
            ImGui::DockBuilderDockWindow(kStatsWindow, bottom_right);
            m_code_window_node = 0;
            break;
        }
    }

    ImGui::DockBuilderFinish(m_dockspace_id);
}

void WorkspaceManager::DrawDockspace()
{
    // Stable identity for the dock node, shared by the host window, the
    // DockBuilder tree, and the .ini persistence (must be computed the same way
    // on every run so saved layouts match).
    if (m_dockspace_id == 0)
        m_dockspace_id = ImGui::GetID("MainDockspace");

    if (m_use_loaded_layout)
    {
        // A saved custom layout was restored at startup; the .ini drives all
        // docking, so only the host window is needed.
        m_use_loaded_layout = false;
        m_dockspace_valid = true;
    }
    else if (!m_dockspace_valid || m_needs_rebuild)
    {
        RebuildLayout();
        m_dockspace_valid = true;
        m_needs_rebuild = false;
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    ImVec2 work_size = viewport->WorkSize;
    if (m_bottom_bar_height > 0.0f)
        work_size.y = std::max(1.0f, work_size.y - m_bottom_bar_height);
    ImGui::SetNextWindowPos(work_pos);
    ImGui::SetNextWindowSize(work_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##EditorDockHost", nullptr, host_flags);
    ImGui::DockSpace(m_dockspace_id, ImVec2(0.0f, 0.0f), kDockspaceFlags);
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void WorkspaceManager::RequestRebuild()
{
    m_use_loaded_layout = false;
    m_needs_rebuild = true;
}

unsigned int WorkspaceManager::ApplyWorkspace(Workspace ws)
{
    m_workspace = ws;
    m_use_loaded_layout = false;
    RebuildLayout();
    m_dockspace_valid = true;
    m_needs_rebuild = false;
    SaveToFile();
    return m_code_window_node;
}

unsigned int WorkspaceManager::ResetToWorkspaceDefault()
{
    m_saved_layout.clear();
    return ApplyWorkspace(m_workspace);
}

void WorkspaceManager::RequestSaveCurrent()
{
    m_save_requested = true;
}

void WorkspaceManager::FinalizeSave()
{
    if (!m_save_requested)
        return;
    m_save_requested = false;

    size_t size = 0;
    const char *data = ImGui::SaveIniSettingsToMemory(&size);
    m_saved_layout = (data && size > 0) ? std::string(data, size) : std::string();

    SaveToFile();
}

void WorkspaceManager::SaveToFile() const
{
    const char *ws_name = (m_workspace == Workspace::Scripting)
        ? "scripting"
        : (m_workspace == Workspace::ShadingAndAssets) ? "shading_assets"
        : (m_workspace == Workspace::Landscape) ? "landscape"
        : (m_workspace == Workspace::Timeline) ? "timeline"
        : "level_design";

    json::Value root = json::Value::MakeObject();
    root.object.emplace_back("version", json::Value::MakeNumber(1.0));
    root.object.emplace_back("workspace", json::Value::MakeString(ws_name));
    if (!m_saved_layout.empty())
        root.object.emplace_back("saved_layout",
                                 json::Value::MakeString(m_saved_layout));

    std::ofstream out("editor_layout.json", std::ios::out | std::ios::trunc);
    if (!out)
        return;
    out << json::WritePretty(root) << "\n";
    out.close();
}

void WorkspaceManager::LoadFromFile()
{
    std::ifstream in("editor_layout.json", std::ios::in | std::ios::binary);
    if (!in)
        return;

    std::stringstream buffer;
    buffer << in.rdbuf();

    std::string error;
    json::Value root = json::Parse(buffer.str(), &error);
    if (!root.IsObject())
        return;

    // "workspace" is the current key; older files (Phase 18/19) stored the
    // layout mode under "preset" ("default" | "scripting"), so fall back to it
    // for a smooth upgrade.
    std::string ws = root.String("workspace");
    if (ws.empty())
        ws = root.String("preset", "default");
    m_workspace = (ws == "scripting")
        ? Workspace::Scripting
        : (ws == "shading_assets") ? Workspace::ShadingAndAssets
        : (ws == "landscape") ? Workspace::Landscape
        : (ws == "timeline") ? Workspace::Timeline
        : Workspace::LevelDesign;

    const std::string saved = root.String("saved_layout");
    if (!saved.empty())
    {
        // Restore the user's captured layout before the first NewFrame so the
        // .ini drives all docking; the canonical workspace build is skipped.
        ImGui::LoadIniSettingsFromMemory(saved.c_str(), saved.size());
        m_saved_layout = saved;
        m_use_loaded_layout = true;
        m_dockspace_valid = true;
        m_needs_rebuild = false;
    }
}
