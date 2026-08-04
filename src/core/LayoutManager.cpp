#include "LayoutManager.h"

#include "Json.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <fstream>
#include <sstream>

// The node tree is built around a full-screen DockSpace. A transparent host
// window pins that DockSpace to the viewport's work area so every panel docks
// into one unified workspace instead of scattering as floating windows.
static const unsigned int kDockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;

LayoutManager::LayoutManager()
    : m_preset(Preset::Default)
    , m_dockspace_valid(false)
    , m_needs_rebuild(false)
    , m_use_loaded_layout(false)
    , m_save_requested(false)
    , m_dockspace_id(0)
    , m_code_window_node(0)
{
}

void LayoutManager::RebuildLayout()
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
    ImGui::DockBuilderSetNodeSize(m_dockspace_id, ImGui::GetMainViewport()->WorkSize);

    ImGuiID top, bottom;
    ImGuiID left, center, right;

    if (m_preset == Preset::Scripting)
    {
        // Scripting workspace: taller bottom strip dedicated to the IDE, with
        // the code window docked in its own slot beside the sidebar so it is
        // part of the unified dock rather than floating.
        ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up, 0.62f, &top, &bottom);
        ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.18f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, &right, &center);

        ImGuiID bottom_left, bottom_center, bottom_right;
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.22f, &bottom_left, &bottom_center);
        ImGui::DockBuilderSplitNode(bottom_center, ImGuiDir_Right, 0.22f, &bottom_center, &bottom_right);

        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Script Editor", bottom_left);
        ImGui::DockBuilderDockWindow("Singularity Engine Stats", bottom_right);

        // The code window title embeds the file name, so it cannot be docked by
        // name here; the Application routes it through SetNextWindowDockID with
        // this node.
        m_code_window_node = bottom_center;
    }
    else
    {
        // Default workspace: classic editor arrangement, code window floating.
        ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up, 0.78f, &top, &bottom);
        ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.22f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, &right, &center);

        ImGuiID bottom_left, bottom_right;
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.72f, &bottom_left, &bottom_right);

        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Script Editor", bottom_left);
        ImGui::DockBuilderDockWindow("Singularity Engine Stats", bottom_right);
    }

    ImGui::DockBuilderFinish(m_dockspace_id);
}

void LayoutManager::DrawDockspace()
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
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
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

void LayoutManager::RequestRebuild()
{
    m_use_loaded_layout = false;
    m_needs_rebuild = true;
}

unsigned int LayoutManager::ApplyPreset(Preset preset)
{
    m_preset = preset;
    m_use_loaded_layout = false;
    RebuildLayout();
    m_dockspace_valid = true;
    m_needs_rebuild = false;
    SaveToFile();
    return m_code_window_node;
}

void LayoutManager::ResetToDefault()
{
    m_saved_layout.clear();
    ApplyPreset(Preset::Default);
}

void LayoutManager::RequestSaveCurrent()
{
    m_save_requested = true;
}

void LayoutManager::FinalizeSave()
{
    if (!m_save_requested)
        return;
    m_save_requested = false;

    size_t size = 0;
    const char *data = ImGui::SaveIniSettingsToMemory(&size);
    m_saved_layout = (data && size > 0) ? std::string(data, size) : std::string();

    SaveToFile();
}

void LayoutManager::SaveToFile() const
{
    json::Value root = json::Value::MakeObject();
    root.object.emplace_back("version", json::Value::MakeNumber(1.0));
    root.object.emplace_back(
        "preset", json::Value::MakeString(
                      m_preset == Preset::Scripting ? "scripting" : "default"));
    if (!m_saved_layout.empty())
        root.object.emplace_back("saved_layout",
                                 json::Value::MakeString(m_saved_layout));

    std::ofstream out("editor_layout.json", std::ios::out | std::ios::trunc);
    if (!out)
        return;
    out << json::WritePretty(root) << "\n";
    out.close();
}

void LayoutManager::LoadFromFile()
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

    m_preset = (root.String("preset", "default") == "scripting")
                   ? Preset::Scripting
                   : Preset::Default;

    const std::string saved = root.String("saved_layout");
    if (!saved.empty())
    {
        // Restore the user's captured layout before the first NewFrame so the
        // .ini drives all docking; the canonical preset build is skipped.
        ImGui::LoadIniSettingsFromMemory(saved.c_str(), saved.size());
        m_saved_layout = saved;
        m_use_loaded_layout = true;
        m_dockspace_valid = true;
        m_needs_rebuild = false;
    }
}
