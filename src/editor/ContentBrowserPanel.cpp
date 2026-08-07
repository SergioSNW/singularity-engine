#include "ContentBrowserPanel.h"

#include "SceneManager.h"
#include "SceneSerializer.h"
#include "editor/ScriptEditorPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Normalize a path to forward slashes and strip any trailing slash.
std::string Normalize(fs::path p)
{
    std::string s = p.lexically_normal().generic_string();
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    return s;
}

int Depth(const std::string &path)
{
    return (int)std::count(path.begin(), path.end(), '/');
}

// Last path segment (the display name).
std::string Leaf(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

bool EndsWith(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Clip a label to fit the available cell width using the current font's
// advance, appending an ellipsis when truncation happens.
std::string ClipToWidth(const std::string &label, float max_width)
{
    ImFont *font = ImGui::GetFont();
    const float ellipsis = ImGui::CalcTextSize("...").x;
    const char *remaining = nullptr;
    font->CalcTextSizeA(ImGui::GetFontSize(), max_width - ellipsis, 0.0f,
                        label.c_str(), nullptr, &remaining);
    const size_t len = (size_t)(remaining - label.c_str());
    if (len >= label.size())
        return label;
    return label.substr(0, len) + "...";
}

} // namespace

ContentBrowserPanel::ContentBrowserPanel(SceneManager *scene_manager,
                                         ScriptEditorPanel *script_editor)
    : m_scene_manager(scene_manager)
    , m_script_editor(script_editor)
    , m_root("assets")
{
    m_current = m_root;
    RefreshTree();
    RefreshFiles();
}

bool ContentBrowserPanel::UnderRoot(const std::string &path) const
{
    if (path == m_root)
        return true;
    return path.rfind(m_root + "/", 0) == 0;
}

ContentBrowserPanel::FileKind ContentBrowserPanel::Classify(const std::string &path)
{
    if (EndsWith(path, ".json"))
        return SceneSerializer::IsPrefabFile(path) ? FileKind::Prefab : FileKind::Scene;
    if (EndsWith(path, ".lua"))
        return FileKind::Script;
    if (EndsWith(path, ".obj"))
        return FileKind::Mesh;
    if (EndsWith(path, ".mat"))
        return FileKind::Material;
    if (EndsWith(path, ".bmp") || EndsWith(path, ".png") || EndsWith(path, ".jpg") ||
        EndsWith(path, ".jpeg") || EndsWith(path, ".tga") || EndsWith(path, ".gif"))
        return FileKind::Texture;
    return FileKind::Other;
}

void ContentBrowserPanel::RefreshTree()
{
    m_dirs.clear();
    m_dirs.push_back(m_root);

    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(m_root, ec))
    {
        if (!entry.is_directory(ec))
            continue;
        m_dirs.push_back(Normalize(entry.path()));
    }
    std::sort(m_dirs.begin() + 1, m_dirs.end());
}

void ContentBrowserPanel::RefreshFiles()
{
    m_files.clear();

    std::vector<std::string> dirs;
    std::vector<std::string> files;

    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(m_current, ec))
    {
        std::string path = Normalize(entry.path());
        if (entry.is_directory(ec))
            dirs.push_back(path);
        else if (entry.is_regular_file(ec))
            files.push_back(path);
    }

    auto by_leaf = [](const std::string &a, const std::string &b) {
        return Leaf(a) < Leaf(b);
    };
    std::sort(dirs.begin(), dirs.end(), by_leaf);
    std::sort(files.begin(), files.end(), by_leaf);

    // Folders first, then files, each group alphabetical.
    m_files.reserve(dirs.size() + files.size());
    m_files.insert(m_files.end(), dirs.begin(), dirs.end());
    m_files.insert(m_files.end(), files.begin(), files.end());
}

void ContentBrowserPanel::Navigate(const std::string &path)
{
    m_current = path;
    m_selected.clear();
    m_status.clear();
    RefreshFiles();
}

void ContentBrowserPanel::DrawFolderTree()
{
    ImGui::BeginChild("##content_tree", ImVec2(190.0f, 0.0f), true);

    if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
    {
        for (const std::string &dir : m_dirs)
        {
            if (dir == m_root)
                continue;
            const bool selected = (dir == m_current);
            const int indent = Depth(dir) - 1;
            for (int i = 0; i < indent; ++i)
                ImGui::Indent();

            if (ImGui::Selectable(Leaf(dir).c_str(), selected))
                Navigate(dir);
            ImGui::SameLine();

            for (int i = 0; i < indent; ++i)
                ImGui::Unindent();
        }
        ImGui::TreePop();
    }

    ImGui::EndChild();
}

void ContentBrowserPanel::DrawToolbar()
{
    // Up button: one level toward the assets root.
    ImGui::BeginDisabled(m_current == m_root);
    if (ImGui::Button("Up"))
        Navigate(m_current.substr(0, m_current.find_last_of('/')));
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::TextUnformatted(m_current.c_str());
    ImGui::SameLine();

    if (ImGui::Button("Refresh"))
    {
        RefreshTree();
        RefreshFiles();
    }
    ImGui::SameLine();

    if (ImGui::Button("New Folder"))
        m_show_new_folder = !m_show_new_folder;
    ImGui::SameLine();

    ImGui::TextDisabled("%d item(s)", (int)m_files.size());
}

void ContentBrowserPanel::DrawCreateFolderRow()
{
    if (!m_show_new_folder)
        return;

    ImGui::PushID("new_folder");
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("##name", m_new_folder, sizeof(m_new_folder),
                     ImGuiInputTextFlags_EnterReturnsTrue);

    bool commit = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::Button("Create"))
        commit = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_show_new_folder = false;
        m_new_folder[0] = '\0';
    }

    if (commit && m_new_folder[0] != '\0')
    {
        const std::string dir = m_current + "/" + m_new_folder;
        std::error_code ec;
        if (fs::create_directories(dir, ec))
        {
            m_status = "Created folder " + dir;
            m_show_new_folder = false;
            m_new_folder[0] = '\0';
            RefreshTree();
            RefreshFiles();
        }
        else
        {
            m_status = "Could not create folder: " + ec.message();
        }
    }
    ImGui::PopID();
}

void ContentBrowserPanel::DrawRenameRow()
{
    if (m_rename_path.empty())
        return;

    ImGui::PushID("rename");
    ImGui::TextUnformatted(("Rename: " + Leaf(m_rename_path)).c_str());
    ImGui::SameLine();

    ImGui::SetNextItemWidth(300.0f);
    const bool committed = ImGui::InputText(
        "##name", m_rename_buffer, sizeof(m_rename_buffer),
        ImGuiInputTextFlags_EnterReturnsTrue);
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

    if (committed && m_rename_buffer[0] != '\0')
    {
        std::string new_path = m_rename_path.substr(0, m_rename_path.find_last_of('/') + 1);
        new_path += m_rename_buffer;
        std::error_code ec;
        bool renamed = false;
        if (UnderRoot(new_path))
        {
            fs::rename(m_rename_path, new_path, ec);
            renamed = !ec;
        }
        if (renamed)
        {
            m_status = "Renamed to " + new_path;
            if (m_selected == m_rename_path)
                m_selected = new_path;
        }
        else
        {
            m_status = "Rename failed: " + ec.message();
        }
        m_rename_path.clear();
        RefreshTree();
        RefreshFiles();
    }
    else if (committed || cancelled)
    {
        m_rename_path.clear();
    }
    ImGui::PopID();
}

void ContentBrowserPanel::DrawItem(const std::string &path, FileKind kind,
                                   int col, int cols, float cell_w)
{
    ImGui::PushID(path.c_str());
    if (col > 0)
        ImGui::SameLine();

    const bool selected = (m_selected == path);
    if (ImGui::Selectable("##cell", selected, ImGuiSelectableFlags_DontClosePopups,
                          ImVec2(cell_w, 42.0f)))
        m_selected = path;

    // Drag source: prefabs spawn into the Hierarchy, .mat / image assets
    // assign onto the Inspector's Material section. Payload carries the path.
    if (kind == FileKind::Prefab && ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("PREFAB", path.c_str(), path.size() + 1, ImGuiCond_Once);
        ImGui::TextUnformatted(Leaf(path).c_str());
        ImGui::EndDragDropSource();
    }
    if ((kind == FileKind::Material || kind == FileKind::Texture) &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload(kind == FileKind::Material ? "MATERIAL" : "TEXTURE",
                                  path.c_str(), path.size() + 1, ImGuiCond_Once);
        ImGui::TextUnformatted(Leaf(path).c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem())
    {
        ContextMenu(path, kind);
        ImGui::EndPopup();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        OpenItem(path, kind);

    // Overlay the icon badge + clipped label inside the cell.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 pmin = ImGui::GetItemRectMin();

    ImU32 color;
    switch (kind)
    {
        case FileKind::Folder:   color = IM_COL32(210, 175, 90, 255); break;
        case FileKind::Scene:    color = IM_COL32(90, 175, 220, 255); break;
        case FileKind::Prefab:   color = IM_COL32(220, 120, 220, 255); break;
        case FileKind::Script:   color = IM_COL32(110, 200, 110, 255); break;
        case FileKind::Mesh:     color = IM_COL32(220, 140, 90, 255); break;
        case FileKind::Material: color = IM_COL32(120, 180, 235, 255); break;
        case FileKind::Texture:  color = IM_COL32(240, 210, 130, 255); break;
        default:                 color = IM_COL32(150, 150, 150, 255); break;
    }
    dl->AddRectFilled(ImVec2(pmin.x + 6.0f, pmin.y + 6.0f),
                      ImVec2(pmin.x + 18.0f, pmin.y + 18.0f), color, 3.0f);

    dl->AddText(ImVec2(pmin.x + 6.0f, pmin.y + 24.0f),
                ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_Text),
                ClipToWidth(Leaf(path), cell_w - 14.0f).c_str());

    ImGui::PopID();
}

void ContentBrowserPanel::DrawFileGrid()
{
    ImGui::BeginChild("##content_grid", ImVec2(0.0f, 0.0f));

    DrawToolbar();
    ImGui::Separator();
    DrawCreateFolderRow();
    DrawRenameRow();

    if (!m_status.empty())
    {
        ImGui::TextDisabled("%s", m_status.c_str());
        ImGui::Separator();
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float min_cell = 140.0f;
    const int cols = std::max(1, (int)((avail + spacing) / (min_cell + spacing)));
    const float cell_w = (avail - spacing * (cols - 1)) / (float)cols;

    int col = 0;
    for (const std::string &path : m_files)
    {
        std::error_code ec;
        const bool is_dir = fs::is_directory(path, ec);
        DrawItem(path, is_dir ? FileKind::Folder : Classify(path), col, cols, cell_w);
        col = (col + 1) % cols;
    }

    if (m_files.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, 40.0f));
        ImGui::TextDisabled("Empty folder. Use 'New Folder' or drop assets here.");
    }

    ImGui::EndChild();
}

void ContentBrowserPanel::ContextMenu(const std::string &path, FileKind kind)
{
    if (kind == FileKind::Folder)
    {
        if (ImGui::MenuItem("Open"))
            Navigate(path);
        ImGui::Separator();
    }
    else
    {
        const char *action = nullptr;
        if (kind == FileKind::Scene)
            action = "Load Scene";
        else if (kind == FileKind::Prefab)
            action = "Spawn Prefab";
        else if (kind == FileKind::Script)
            action = "Open in Script Editor";
        if (action && ImGui::MenuItem(action))
            OpenItem(path, kind);
        ImGui::Separator();
    }

    if (ImGui::MenuItem("Rename"))
    {
        m_rename_path = path;
        std::strncpy(m_rename_buffer, Leaf(path).c_str(), sizeof(m_rename_buffer) - 1);
        m_rename_buffer[sizeof(m_rename_buffer) - 1] = '\0';
    }
    if (ImGui::MenuItem("Delete"))
    {
        m_pending_delete = path;
        m_confirm_delete = true;
    }
}

void ContentBrowserPanel::DrawConfirmDeleteModal()
{
    if (m_confirm_delete)
    {
        ImGui::OpenPopup("Delete Asset");
        m_confirm_delete = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Delete the selected asset?");
    ImGui::TextDisabled("%s", m_pending_delete.c_str());
    if (m_pending_delete != m_root && !UnderRoot(m_pending_delete))
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f),
                           "Outside assets/ - deletion blocked.");
    ImGui::Separator();

    if (ImGui::Button("Delete"))
    {
        std::error_code ec;
        if (m_pending_delete != m_root && UnderRoot(m_pending_delete) &&
            fs::remove_all(m_pending_delete, ec) > 0)
            m_status = "Deleted " + m_pending_delete;
        else
            m_status = "Delete failed: " + ec.message();
        m_pending_delete.clear();
        if (m_selected == m_pending_delete)
            m_selected.clear();
        RefreshTree();
        RefreshFiles();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_pending_delete.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ContentBrowserPanel::OpenItem(const std::string &path, FileKind kind)
{
    switch (kind)
    {
        case FileKind::Folder:
            Navigate(path);
            break;
        case FileKind::Scene:
        {
            // Route through the host application so the active path, status
            // line, and play-mode guard stay consistent with the editor.
            if (on_load_scene)
                on_load_scene(path);
            else
                m_status = "No scene load handler wired";
            break;
        }
        case FileKind::Prefab:
        {
            if (!m_scene_manager)
                break;
            std::string error;
            if (SceneSerializer::LoadPrefab(*m_scene_manager->GetScene(), path, nullptr, &error))
            {
                m_status = "Spawned prefab: " + path;
            }
            else
            {
                m_status = "Prefab spawn failed: " + error;
            }
            break;
        }
        case FileKind::Script:
            if (m_script_editor)
                m_script_editor->RequestOpen(path);
            m_status = "Opened script: " + path;
            break;
        case FileKind::Mesh:
            m_status = "Mesh asset: " + path;
            break;
        case FileKind::Material:
            m_status = "Material asset: " + path +
                       " (drag onto an entity's Material section to assign)";
            break;
        case FileKind::Texture:
            m_status = "Texture asset: " + path +
                       " (drag onto an entity's Material section to assign)";
            break;
        default:
            m_status = "No action for: " + path;
            break;
    }
}

void ContentBrowserPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    ImGui::Begin("Content Browser", nullptr, ImGuiWindowFlags_NoCollapse);

    DrawFolderTree();
    ImGui::SameLine();
    DrawFileGrid();

    ImGui::End();

    DrawConfirmDeleteModal();
}
