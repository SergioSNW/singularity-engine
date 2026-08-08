#pragma once

#include "EditorPanel.h"

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
// (drag payload "PREFAB"); material (.mat) and image assets drag onto the
// Inspector's Material section to assign them (payloads "MATERIAL"/"TEXTURE").
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
};
