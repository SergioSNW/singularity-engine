#pragma once

#include "EditorPanel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class TextEditor;
struct ImFont;

// In-editor Lua mini-IDE. A single dockable "Script Editor" window hosts a
// file-browser sidebar, a tab bar, and the syntax-highlighting code pane for
// the active tab. Multiple .lua scripts stay open at once: every tab owns its
// own TextEditor buffer, undo stack, and saved-on-disk baseline, so switching
// tabs never loses edits. Dirty tabs carry a '*' in the label and are
// confirmed before closing. The whole window docks by name into the workspace
// layouts ("Script Editor") and can be popped out as a floating window via the
// toolbar toggle.
//
// Real-time editing hooks carry over from Phase 32: Auto-save writes the active
// buffer the moment the IDE loses focus, and a disk watcher adopts external
// edits (hot-reloading the live play session through the ReloadCallback).
class ScriptEditorPanel : public EditorPanel
{
public:
    // Invoked after a file is saved. Should reload the live script session and
    // return true when a running session was actually swapped.
    using ReloadCallback = std::function<bool()>;

    // Invoked when the user pops the IDE back into the dock ("Dock" button);
    // the Application re-applies the current workspace, which routes the
    // window back into its canonical dock node.
    using RedockCallback = std::function<void()>;

    explicit ScriptEditorPanel(ImFont *mono_font, ReloadCallback reload,
                               RedockCallback redock);
    ~ScriptEditorPanel() override;

    void OnImGuiRender(float dt) override;

    bool IsVisible() const { return m_visible; }
    void ToggleVisible();

    // Hide/show the whole script-editing UI. Hiding preserves every open tab;
    // restoring reopens the window with the tabs intact. Used by the play-mode
    // panel isolation so the IDE is not rendered during gameplay.
    void SetVisible(bool visible);

    // One-shot: dock the IDE window into `node_id` on its next render.
    // Pass 0 to pop it out as a floating window.
    void RequestDockCodeWindow(unsigned int node_id);

    // The IDE window's ImGui title ("Script Editor"), or the title of the
    // active tab's file when a file is open.
    std::string GetCodeWindowTitle() const;

    // Open `path` in a tab, activating it if already open. Safe to call from
    // other panels (the Content Browser uses it for .lua assets).
    void RequestOpen(const std::string &path);

private:
    struct Tab
    {
        std::string path;
        TextEditor *editor;          // owned
        std::string saved_text;      // canonical saved-on-disk baseline
        std::int64_t last_write_time;
    };

    void RefreshFileList();
    int FindTab(const std::string &path) const;
    int OpenFile(const std::string &path, std::string *error);
    void CloseTab(int index);
    bool SaveTab(int index, std::string *error);
    bool IsTabDirty(int index) const;

    void ConfirmUnsavedModal();
    void DrawFileBrowser();
    void DrawTabBar();
    void DrawToolbar(bool dirty, bool docked);
    void DrawEditorPane(int tab_index);

    ReloadCallback m_reload;
    RedockCallback m_redock;
    ImFont *m_mono_font;

    std::vector<std::string> m_files;
    std::vector<Tab> m_tabs;
    int m_active_tab;             // index into m_tabs (-1 when none open)
    std::string m_status;         // last-action feedback line
    char m_new_name[256];         // "New Script" name input buffer
    int m_pending_close;          // tab waiting on the unsaved-changes dialog
    bool m_visible;               // IDE window visible
    bool m_modal_requested;       // open the unsaved-changes dialog this frame
    bool m_focus_window;          // focus the IDE window next frame
    bool m_dock_requested;        // apply m_dock_node on the next render
    unsigned int m_dock_node;     // dock node for the IDE (0 = floating)
    float m_sidebar_width;        // file-browser sidebar width (px)
    bool m_auto_save;             // save the active buffer when the IDE blurs
    bool m_was_focused;           // IDE window had focus last frame
};
