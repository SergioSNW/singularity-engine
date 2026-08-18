#include "ContentBrowserPanel.h"

#include "SceneManager.h"
#include "SceneSerializer.h"
#include "editor/ScriptEditorPanel.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "render/ThumbnailCache.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
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

// Human-readable byte count for the list view's size column.
std::string HumanSize(uint64_t bytes)
{
    if (bytes >= 1024ull * 1024ull)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
        return buf;
    }
    if (bytes >= 1024ull)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
        return buf;
    }
    return std::to_string(bytes) + " B";
}

// Per-type preview badge color (small square), shared by grid cells, list rows,
// and the fallback drawn when no live thumbnail is available.
ImU32 BadgeColor(AssetCatalog::AssetKind kind)
{
    switch (kind)
    {
        case AssetCatalog::AssetKind::Folder:   return IM_COL32(140, 120, 65, 255);
        case AssetCatalog::AssetKind::Scene:    return IM_COL32(55, 110, 150, 255);
        case AssetCatalog::AssetKind::Prefab:   return IM_COL32(140, 80, 140, 255);
        case AssetCatalog::AssetKind::Script:   return IM_COL32(65, 130, 65, 255);
        case AssetCatalog::AssetKind::Mesh:     return IM_COL32(140, 95, 55, 255);
        case AssetCatalog::AssetKind::Material: return IM_COL32(70, 115, 160, 255);
        case AssetCatalog::AssetKind::Texture:  return IM_COL32(155, 135, 75, 255);
        case AssetCatalog::AssetKind::Audio:    return IM_COL32(70, 135, 135, 255);
        default:                                return IM_COL32(100, 100, 100, 255);
    }
}

} // namespace

ContentBrowserPanel::ContentBrowserPanel(SceneManager *scene_manager,
                                         ScriptEditorPanel *script_editor,
                                         MaterialLibrary *material_library,
                                         TextureLibrary *texture_library,
                                         SDL_Renderer *renderer,
                                         MeshLibrary *mesh_library)
    : m_scene_manager(scene_manager)
    , m_script_editor(script_editor)
    , m_material_library(material_library)
    , m_texture_library(texture_library)
    , m_thumbnails(std::make_unique<ThumbnailCache>(
          renderer, mesh_library, material_library, texture_library))
    , m_root("assets")
{
    m_current = m_root;
    RefreshTree();
    RefreshFiles();
}

ContentBrowserPanel::~ContentBrowserPanel() = default;

bool ContentBrowserPanel::UnderRoot(const std::string &path) const
{
    if (path == m_root)
        return true;
    return path.rfind(m_root + "/", 0) == 0;
}

ContentBrowserPanel::FileKind ContentBrowserPanel::Classify(const std::string &path)
{
    // Prefabs vs scenes are decided by file content (a prefab flag in the JSON
    // root), which the extension-based AssetCatalog can't see; everything else
    // delegates to the shared taxonomy so the browser and OS importer agree.
    if (EndsWith(path, ".json"))
        return SceneSerializer::IsPrefabFile(path) ? FileKind::Prefab : FileKind::Scene;
    return AssetCatalog::ClassifyAsset(path);
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
    ImGui::BeginChild("##content_tree", ImVec2(200.0f, 0.0f), true);

    if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
    {
        for (const std::string &dir : m_dirs)
        {
            if (dir == m_root)
                continue;
            const bool selected = (dir == m_current);
            const int depth = Depth(dir) - 1;
            ImGui::PushID(dir.c_str());
            ImGui::Indent(16.0f * depth);
            if (ImGui::Selectable(Leaf(dir).c_str(), selected))
                Navigate(dir);
            ImGui::Unindent(16.0f * depth);
            ImGui::PopID();
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

    // Breadcrumbs: each path segment jumps straight to that folder.
    const std::vector<std::string> crumbs = AssetCatalog::BreadcrumbSegments(m_current);
    for (size_t i = 0; i < crumbs.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::TextDisabled("/");
            ImGui::SameLine();
        }
        ImGui::PushID(("crumb" + std::to_string(i)).c_str());
        if (ImGui::SmallButton(Leaf(crumbs[i]).c_str()))
            Navigate(crumbs[i]);
        ImGui::PopID();
        ImGui::SameLine();
    }

    // Filtered item count (what the grid/list will actually show).
    int visible = 0;
    for (const std::string &path : m_files)
    {
        std::error_code ec;
        const bool is_dir = fs::is_directory(path, ec);
        if (PassesFilter(path, is_dir ? FileKind::Folder : Classify(path)))
            ++visible;
    }
    ImGui::TextDisabled("%d item(s)", visible);
    ImGui::SameLine();

    if (ImGui::Button("Refresh"))
    {
        RefreshTree();
        RefreshFiles();
    }
    ImGui::SameLine();

    if (ImGui::Button("New Folder"))
        m_show_new_folder = !m_show_new_folder;

    ImGui::Separator();

    // View mode toggle + condensed toolbar: thumb scale, search, and filter
    // chips on one line to save vertical space.
    if (ImGui::Button(m_list_view ? "Grid" : "List"))
        m_list_view = !m_list_view;
    ImGui::SameLine();

    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("##thumb", &m_thumb_scale, 48.0f, 192.0f, "%.0f");
    ImGui::SameLine();

    const float search_w = std::max(100.0f, ImGui::GetContentRegionAvail().x - 260.0f);
    ImGui::SetNextItemWidth(search_w);
    ImGui::InputTextWithHint("##content_search", "Search...",
                             m_search, sizeof(m_search));
    ImGui::SameLine();

    ImGui::TextDisabled("Filter:");
    ImGui::SameLine();
    const AssetCatalog::AssetFilter filters[] = {
        AssetCatalog::AssetFilter::All,
        AssetCatalog::AssetFilter::Meshes,
        AssetCatalog::AssetFilter::Materials,
        AssetCatalog::AssetFilter::Textures,
        AssetCatalog::AssetFilter::Audio,
        AssetCatalog::AssetFilter::Prefabs,
    };
    for (const AssetCatalog::AssetFilter f : filters)
    {
        const bool active = (m_filter == f);
        if (ImGui::Selectable(AssetCatalog::AssetFilterLabel(f), active,
                              ImGuiSelectableFlags_DontClosePopups))
            m_filter = f;
        ImGui::SameLine();
    }
    ImGui::NewLine();
}

bool ContentBrowserPanel::PassesFilter(const std::string &path, FileKind kind) const
{
    // Search matches file/folder names (case-insensitive substring); category
    // chips hide files that aren't of the requested kind. Folders always pass
    // the chip so navigation stays possible under an active filter.
    if (m_search[0] != '\0' && !AssetCatalog::NameMatches(Leaf(path), m_search))
        return false;
    return AssetCatalog::AssetPassesFilter(kind, m_filter);
}

void ContentBrowserPanel::DrawCreateFolderRow()
{
    if (!m_show_new_folder)
        return;

    ImGui::PushID("new_folder");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 160.0f);
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

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 160.0f);
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
                                   int col, int cols, float cell_w, float cell_h)
{
    ImGui::PushID(path.c_str());
    if (col > 0)
        ImGui::SameLine();

    const bool selected = (m_selected == path);
    if (ImGui::Selectable("##cell", selected, ImGuiSelectableFlags_DontClosePopups,
                          ImVec2(cell_w, cell_h)))
        m_selected = path;

    if (kind == FileKind::Prefab && ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("PREFAB", path.c_str(), path.size() + 1, ImGuiCond_Once);
        ImGui::TextUnformatted(Leaf(path).c_str());
        ImGui::EndDragDropSource();
    }
    if (kind == FileKind::Mesh && ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("MESH", path.c_str(), path.size() + 1, ImGuiCond_Once);
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

    // Dark-slate card background behind the icon + text.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 pmin = ImGui::GetItemRectMin();
    const ImVec2 pmax = ImGui::GetItemRectMax();
    const ImU32 card_bg = selected
        ? IM_COL32(50, 55, 70, 255)
        : IM_COL32(32, 33, 38, 255);
    dl->AddRectFilled(pmin, pmax, card_bg, 4.0f);
    if (selected)
        dl->AddRect(pmin, pmax, IM_COL32(88, 141, 245, 200), 4.0f, 0, 1.5f);

    // Thumbnail preview centered inside the card.
    const float box = std::min(m_thumb_scale, cell_w - 16.0f);
    const float pad_top = 8.0f;
    const ImVec2 box_min(pmin.x + (cell_w - box) * 0.5f, pmin.y + pad_top);
    const ImVec2 box_max(box_min.x + box, box_min.y + box);

    bool preview_drawn = false;

    if ((kind == FileKind::Mesh || kind == FileKind::Material) && m_thumbnails)
    {
        if (SDL_Texture *thumb = m_thumbnails->Get(path))
        {
            dl->AddImage((ImTextureID)thumb, box_min, box_max);
            preview_drawn = true;
        }
    }
    else if (kind == FileKind::Texture && m_texture_library)
    {
        if (const TextureInfo *info = m_texture_library->Load(path))
        {
            const float iw = (float)std::max(1, info->width);
            const float ih = (float)std::max(1, info->height);
            float w = box, h = box;
            if (iw > ih) h = box * (ih / iw);
            else         w = box * (iw / ih);
            const ImVec2 tl(box_min.x + (box - w) * 0.5f, box_min.y + (box - h) * 0.5f);
            dl->AddImage((ImTextureID)info->texture, tl, ImVec2(tl.x + w, tl.y + h));
            preview_drawn = true;
        }
    }

    if (!preview_drawn)
    {
        const float icon = std::min(box * 0.55f, 64.0f);
        const ImVec2 c(box_min.x + box * 0.5f, box_min.y + box * 0.5f);
        const ImU32 col = BadgeColor(kind);
        if (kind == FileKind::Folder)
        {
            // Folder: wider body rectangle + smaller tab rectangle on top-left.
            const float body_w = icon * 1.1f, body_h = icon * 0.72f;
            const float tab_w = icon * 0.40f, tab_h = icon * 0.24f;
            const ImVec2 body_min(c.x - body_w * 0.5f, c.y - body_h * 0.5f + tab_h);
            const ImVec2 body_max(c.x + body_w * 0.5f, c.y + body_h * 0.5f + tab_h);
            dl->AddRectFilled(body_min, body_max, col, 3.0f);
            dl->AddRectFilled(ImVec2(body_min.x, body_min.y - tab_h),
                              ImVec2(body_min.x + tab_w, body_min.y), col, 3.0f);
            dl->AddRect(body_min, body_max,
                        IM_COL32(180, 180, 190, 60), 3.0f, 0, 1.0f);
        }
        else
        {
            const float fw = icon * 0.7f, fh = icon * 0.85f;
            const float fold = icon * 0.22f;
            const ImVec2 tl(c.x - fw * 0.5f, c.y - fh * 0.5f);
            const ImVec2 br(c.x + fw * 0.5f, c.y + fh * 0.5f);
            const ImVec2 points[] = { tl, ImVec2(br.x - fold, tl.y),
                ImVec2(br.x, tl.y + fold), br, tl };
            dl->AddConvexPolyFilled(points, 5, col);
            dl->AddLine(ImVec2(br.x - fold, tl.y), ImVec2(br.x - fold, tl.y + fold),
                        IM_COL32(180, 180, 190, 80), 1.5f);
            dl->AddLine(ImVec2(br.x - fold, tl.y + fold), ImVec2(br.x, tl.y + fold),
                        IM_COL32(180, 180, 190, 80), 1.5f);
        }
    }

    // Text label clipped below the thumbnail with proper padding.
    const float text_y = box_max.y + 6.0f;
    const float text_area_w = cell_w - 12.0f;
    dl->PushClipRect(ImVec2(pmin.x + 6.0f, text_y),
                     ImVec2(pmax.x - 6.0f, pmax.y - 2.0f), true);
    dl->AddText(ImVec2(pmin.x + 6.0f, text_y),
                ImGui::GetColorU32(ImGuiCol_Text),
                ClipToWidth(Leaf(path), text_area_w).c_str());
    dl->PopClipRect();

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
    const float min_cell = m_thumb_scale + 26.0f;
    const int cols = std::max(1, (int)((avail + spacing) / (min_cell + spacing)));
    const float cell_w = (avail - spacing * (cols - 1)) / (float)cols;
    const float cell_h = m_thumb_scale + 34.0f;

    int col = 0;
    int visible = 0;
    for (const std::string &path : m_files)
    {
        std::error_code ec;
        const bool is_dir = fs::is_directory(path, ec);
        const FileKind kind = is_dir ? FileKind::Folder : Classify(path);
        if (!PassesFilter(path, kind))
            continue;
        DrawItem(path, kind, col, cols, cell_w, cell_h);
        col = (col + 1) % cols;
        ++visible;
    }

    if (visible == 0)
    {
        ImGui::Dummy(ImVec2(0.0f, 40.0f));
        ImGui::TextDisabled(m_files.empty()
            ? "Empty folder. Use 'New Folder' or drop assets here."
            : "No items match the current filter or search.");
    }

    ImGui::EndChild();
}

void ContentBrowserPanel::DrawListRow(const std::string &path, FileKind kind)
{
    ImGui::PushID(path.c_str());

    const bool selected = (m_selected == path);
    if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_DontClosePopups,
                          ImVec2(-FLT_MIN, 28.0f)))
        m_selected = path;

    if (kind == FileKind::Prefab && ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("PREFAB", path.c_str(), path.size() + 1, ImGuiCond_Once);
        ImGui::TextUnformatted(Leaf(path).c_str());
        ImGui::EndDragDropSource();
    }
    if (kind == FileKind::Mesh && ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("MESH", path.c_str(), path.size() + 1, ImGuiCond_Once);
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

    // Small preview on the left, name + kind + size across the row.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 pmin = ImGui::GetItemRectMin();
    const float row_h = ImGui::GetItemRectMax().y - pmin.y;
    const float box = 22.0f;
    const ImVec2 box_min(pmin.x + 6.0f, pmin.y + (row_h - box) * 0.5f);
    const ImVec2 box_max(box_min.x + box, box_min.y + box);

    bool preview_drawn = false;
    if ((kind == FileKind::Mesh || kind == FileKind::Material) && m_thumbnails)
    {
        if (SDL_Texture *thumb = m_thumbnails->Get(path))
        {
            dl->AddImage((ImTextureID)thumb, box_min, box_max);
            preview_drawn = true;
        }
    }
    else if (kind == FileKind::Texture && m_texture_library)
    {
        if (const TextureInfo *info = m_texture_library->Load(path))
        {
            const float iw = (float)std::max(1, info->width);
            const float ih = (float)std::max(1, info->height);
            float w = box, h = box;
            if (iw > ih) h = box * (ih / iw);
            else         w = box * (iw / ih);
            const ImVec2 tl(box_min.x + (box - w) * 0.5f, box_min.y + (box - h) * 0.5f);
            dl->AddImage((ImTextureID)info->texture, tl, ImVec2(tl.x + w, tl.y + h));
            preview_drawn = true;
        }
    }
    if (!preview_drawn)
    {
        const float icon = box * 0.75f;
        const ImVec2 c(box_min.x + box * 0.5f, box_min.y + box * 0.5f);
        const ImU32 col = BadgeColor(kind);
        if (kind == FileKind::Folder)
        {
            const float body_w = icon, body_h = icon * 0.72f;
            const float tab_w = icon * 0.35f, tab_h = icon * 0.22f;
            const ImVec2 body_min(c.x - body_w * 0.5f, c.y - body_h * 0.5f + tab_h);
            const ImVec2 body_max(c.x + body_w * 0.5f, c.y + body_h * 0.5f + tab_h);
            dl->AddRectFilled(body_min, body_max, col, 3.0f);
            dl->AddRectFilled(ImVec2(body_min.x, body_min.y - tab_h),
                              ImVec2(body_min.x + tab_w, body_min.y), col, 2.0f);
        }
        else
        {
            const float fw = icon * 0.7f, fh = icon * 0.85f;
            const float fold = icon * 0.22f;
            const ImVec2 tl(c.x - fw * 0.5f, c.y - fh * 0.5f);
            const ImVec2 br(c.x + fw * 0.5f, c.y + fh * 0.5f);
            const ImVec2 points[] = { tl, ImVec2(br.x - fold, tl.y),
                ImVec2(br.x, tl.y + fold), br, tl };
            dl->AddConvexPolyFilled(points, 5, col);
            dl->AddLine(ImVec2(br.x - fold, tl.y), ImVec2(br.x - fold, tl.y + fold),
                        IM_COL32(180, 180, 190, 80), 1.0f);
            dl->AddLine(ImVec2(br.x - fold, tl.y + fold), ImVec2(br.x, tl.y + fold),
                        IM_COL32(180, 180, 190, 80), 1.0f);
        }
    }

    // Name (clipped to leave room for the right-aligned meta) + kind/size.
    const ImVec2 text_pos(pmin.x + 34.0f, pmin.y + (row_h - ImGui::GetFontSize()) * 0.5f);
    const float meta_w = ImGui::CalcTextSize("Material  999.9 KB").x;
    const float name_w = ImGui::GetWindowWidth() - 36.0f - meta_w;
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos,
                ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_Text),
                ClipToWidth(Leaf(path), name_w).c_str());

    std::string size_str = "-";
    std::error_code ec;
    if (const uintmax_t bytes = fs::file_size(path, ec); !ec)
        size_str = HumanSize(bytes);
    const std::string meta = std::string(AssetCatalog::AssetKindLabel(kind)) + "  " + size_str;
    const ImVec2 meta_size = ImGui::CalcTextSize(meta.c_str());
    const ImVec2 meta_pos(pmin.x + ImGui::GetWindowWidth() - meta_size.x - 10.0f,
                          text_pos.y);
    dl->AddText(meta_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), meta.c_str());

    ImGui::PopID();
}

void ContentBrowserPanel::DrawFileList()
{
    ImGui::BeginChild("##content_list", ImVec2(0.0f, 0.0f));

    DrawToolbar();
    ImGui::Separator();
    DrawCreateFolderRow();
    DrawRenameRow();

    if (!m_status.empty())
    {
        ImGui::TextDisabled("%s", m_status.c_str());
        ImGui::Separator();
    }

    int visible = 0;
    for (const std::string &path : m_files)
    {
        std::error_code ec;
        const bool is_dir = fs::is_directory(path, ec);
        const FileKind kind = is_dir ? FileKind::Folder : Classify(path);
        if (!PassesFilter(path, kind))
            continue;
        DrawListRow(path, kind);
        ++visible;
    }

    if (visible == 0)
    {
        ImGui::Dummy(ImVec2(0.0f, 40.0f));
        ImGui::TextDisabled(m_files.empty()
            ? "Empty folder. Use 'New Folder' or drop assets here."
            : "No items match the current filter or search.");
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
    if (ImGui::MenuItem("Duplicate"))
        DuplicateAsset(path);
    if (ImGui::MenuItem("Delete"))
    {
        m_pending_delete = path;
        m_confirm_delete = true;
    }
}

void ContentBrowserPanel::DuplicateAsset(const std::string &path)
{
    if (path == m_root || !UnderRoot(path))
    {
        m_status = "Duplicate failed: outside assets/";
        return;
    }

    // "<stem>_copy[.ext]", then "_copy_2" on collisions.
    const size_t slash = path.find_last_of('/');
    const std::string leaf = (slash == std::string::npos)
        ? path : path.substr(slash + 1);
    const size_t dot = leaf.rfind('.');
    const std::string stem = (dot == std::string::npos) ? leaf : leaf.substr(0, dot);
    const std::string ext = (dot == std::string::npos) ? "" : leaf.substr(dot);
    const std::string parent = (slash == std::string::npos) ? "" : path.substr(0, slash + 1);

    std::string candidate = parent + stem + "_copy" + ext;
    int suffix = 2;
    while (fs::exists(candidate))
        candidate = parent + stem + "_copy_" + std::to_string(suffix++) + ext;

    std::error_code ec;
    if (fs::is_directory(path))
        fs::copy(path, candidate, fs::copy_options::recursive, ec);
    else
        fs::copy_file(path, candidate, fs::copy_options::overwrite_existing, ec);

    if (!ec)
    {
        m_status = "Duplicated to " + candidate;
        m_selected = candidate;
    }
    else
    {
        m_status = "Duplicate failed: " + ec.message();
    }
    RefreshTree();
    RefreshFiles();
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
        case FileKind::Audio:
            m_status = "Audio asset: " + path;
            break;
        default:
            m_status = "No action for: " + path;
            break;
    }
}

void ContentBrowserPanel::OnImGuiRender(float dt)
{
    if (m_import_flash_timer > 0.0f)
        m_import_flash_timer -= dt;

    if (!m_visible)
        return;

    ImGui::Begin("Content Browser", nullptr, ImGuiWindowFlags_NoCollapse);

    // Record the window rect for OS file-drop routing (the editor checks
    // whether a dropped file landed over this panel before choosing a folder).
    m_window_min = ImGui::GetWindowPos();
    m_window_max = ImVec2(m_window_min.x + ImGui::GetWindowWidth(),
                          m_window_min.y + ImGui::GetWindowHeight());

    DrawFolderTree();
    ImGui::SameLine();
    if (m_list_view)
        DrawFileList();
    else
        DrawFileGrid();

    // Transient overlay after an OS file drop imported assets (Phase 23).
    if (m_import_flash_timer > 0.0f)
    {
        const char *label = m_import_flash_count == 1
            ? "Imported 1 file"
            : ("Imported " + std::to_string(m_import_flash_count) + " files").c_str();
        const ImVec2 size = ImGui::CalcTextSize(label);
        const ImVec2 center = ImVec2(m_window_min.x + (m_window_max.x - m_window_min.x) * 0.5f,
                                     m_window_min.y + 40.0f);
        ImGui::SetCursorScreenPos(ImVec2(center.x - size.x * 0.5f, center.y));
        ImGui::Text("%s", label);
    }

    ImGui::End();

    DrawConfirmDeleteModal();
}

void ContentBrowserPanel::FlashImportResult(int imported)
{
    m_import_flash_count = imported;
    m_import_flash_timer = 2.0f;
}

bool ContentBrowserPanel::IsPointInside(const ImVec2 &screen_pos) const
{
    return screen_pos.x >= m_window_min.x && screen_pos.x <= m_window_max.x &&
           screen_pos.y >= m_window_min.y && screen_pos.y <= m_window_max.y;
}
