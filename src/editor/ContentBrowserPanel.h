#pragma once

#include "EditorPanel.h"
#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

class SceneManager;
class ScriptEditorPanel;
class MaterialLibrary;
class TextureLibrary;

// Content Browser: a dockable asset-management panel over the project's
// assets/ tree.
//
// Layout is a two-pane split: a recursive folder tree on the left, and a
// responsive file grid on the right (folders first, then files), each item
// carrying a colored per-type badge. Double-clicking acts on the asset:
//
//   * folder          -> navigate into it
//   * .json scene     -> load as the active map (SceneManager::LoadScene)
//   * .json prefab    -> instantiate into the active scene (fresh UUIDs)
//   * .lua            -> open in the Script Editor
//   * .obj / .mat     -> informational status line
//   * image textures  -> informational status line
//
// Standard file operations are provided via a toolbar and per-item context
// menus: create folder, rename (inline), and delete (with a confirm dialog).
// Prefab files can be dragged onto the Hierarchy window to spawn an instance
// (drag payload "PREFAB"); mesh (.obj) assets drag onto the Hierarchy or the
// viewport to spawn an entity (payload "MESH"); material (.mat) and image
// assets drag onto the Inspector's Material section to assign them (payloads
// "MATERIAL"/"TEXTURE").
class ContentBrowserPanel : public EditorPanel
{
public:
    ContentBrowserPanel(SceneManager *scene_manager, ScriptEditorPanel *script_editor,
                        MaterialLibrary *material_library, TextureLibrary *texture_library);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    // Invoked when a scene asset is double-clicked. The Application wires this
    // to LoadSceneFile so m_scene_path / status / play-mode guard stay in sync.
    std::function<void(const std::string &)> on_load_scene;

    // Re-scan the assets tree and the current folder. Called after OS file
    // drops ingest new assets (Phase 23) so they appear without a manual
    // Refresh click.
    void Refresh() { RefreshTree(); RefreshFiles(); }

    const std::string &CurrentDir() const { return m_current; }

    // Show a transient "Imported N file(s)" overlay after an OS drop ingested
    // files into the browsed folder (Phase 23). The Application calls this
    // right after Refresh().
    void FlashImportResult(int imported);

    // True when `screen_pos` lies inside this panel's window rect. The rect is
    // recorded every frame in OnImGuiRender, so the result is only meaningful
    // when called during or just after the panel drew itself (the editor
    // routes dropped files to the browsed folder this way).
    bool IsPointInside(const ImVec2 &screen_pos) const;

private:
    enum class FileKind { Folder, Scene, Prefab, Script, Mesh, Material, Texture, Other };

    void RefreshTree();
    void RefreshFiles();
    void Navigate(const std::string &path);
    void DrawFolderTree();
    void DrawToolbar();
    void DrawFileGrid();
    void DrawCreateFolderRow();
    void DrawRenameRow();
    void DrawConfirmDeleteModal();
    void DrawItem(const std::string &path, FileKind kind, int col, int cols, float cell_w);
    void OpenItem(const std::string &path, FileKind kind);
    void ContextMenu(const std::string &path, FileKind kind);
    // Copy `path` (file or folder) beside it as "<stem>_copy[.ext]" /
    // "<stem>_copy_2", etc., skipping any name that already exists.
    void DuplicateAsset(const std::string &path);
    static FileKind Classify(const std::string &path);
    bool UnderRoot(const std::string &path) const;

    SceneManager *m_scene_manager;
    ScriptEditorPanel *m_script_editor;
    MaterialLibrary *m_material_library;
    TextureLibrary *m_texture_library;

    std::string m_root;              // e.g. "assets" (relative to working dir)
    std::string m_current;           // currently browsed directory
    std::vector<std::string> m_dirs; // recursive folder tree (sorted, incl. root)
    std::vector<std::string> m_files;// files in m_current (sorted, folders first)
    std::string m_selected;          // last single-clicked item in the grid

    char m_new_folder[64] = {};
    bool m_show_new_folder = false;

    std::string m_rename_path;       // item awaiting an inline rename
    char m_rename_buffer[256] = {};

    std::string m_pending_delete;    // item waiting for delete confirmation
    bool m_confirm_delete = false;

    std::string m_status;            // last-action feedback line
    bool m_visible = true;

    // Transient import feedback: countdown (seconds) and count for the overlay
    // flashed after an OS file drop ingested new assets (Phase 23).
    float m_import_flash_timer = 0.0f;
    int m_import_flash_count = 0;

    // On-screen rect of the Content Browser window, refreshed each frame.
    ImVec2 m_window_min{0.0f, 0.0f};
    ImVec2 m_window_max{0.0f, 0.0f};
};
