#pragma once

#include "EditorPanel.h"

#include <functional>
#include <string>
#include <vector>

class TextEditor;
struct ImFont;

// In-editor Lua script editor. The "Script Editor" window is a file-browser
// sidebar over assets/scripts/; selecting a script spawns a dedicated floating
// "Script Editor: <file>" code window (resizable, minimizable, dockable) that
// hosts the syntax-highlighting buffer, a Save / Save & Reload toolbar, and a
// dirty marker. "Save & Reload" writes the file to disk and, when a play
// session is live, hot-swaps the running session via
// ScriptEngine::ReloadSession so OnStart re-runs against the new text.
class ScriptEditorPanel : public EditorPanel
{
public:
    // Invoked after a file is saved. Should reload the live script session and
    // return true when a running session was actually swapped.
    using ReloadCallback = std::function<bool()>;

    explicit ScriptEditorPanel(ImFont *mono_font, ReloadCallback reload);
    ~ScriptEditorPanel() override;

    void OnImGuiRender(float dt) override;

    bool IsVisible() const { return m_visible; }
    void ToggleVisible();

    // Hide/show the whole script-editing UI (sidebar + code window). Hiding
    // also closes the floating code window; restoring reopens it when a file
    // is already loaded, exactly like ToggleVisible. Used by the play-mode
    // panel isolation so the IDE is not rendered during gameplay.
    void SetVisible(bool visible);

    // One-shot: dock the dedicated code window into `node_id` on its next
    // render. Pass 0 to undock it back to a floating window. Used by the
    // layout-preset system so the code editor participates in the workspace.
    void RequestDockCodeWindow(unsigned int node_id);

    // The code window's ImGui title ("Script Editor: <file>"), or "" when no
    // file is open.
    std::string GetCodeWindowTitle() const;

    // Open `path` in the code window. If the current buffer is dirty, the
    // unsaved-changes dialog runs first. Safe to call from other panels (the
    // Content Browser uses it for .lua assets).
    void RequestOpen(const std::string &path);

private:
    void RefreshFileList();
    bool OpenFile(const std::string &path, std::string *error);
    bool SaveCurrent(std::string *error);
    void ConfirmUnsavedModal();
    void DrawFileBrowser();
    void DrawCodeWindow();

    ReloadCallback m_reload;
    TextEditor *m_editor;
    ImFont *m_mono_font;

    std::vector<std::string> m_files;
    std::string m_current;       // path of the open file ("" when none open)
    std::string m_saved_text;    // last text written to disk
    std::string m_status;        // last-action feedback line
    char m_new_name[256];        // "New Script" name input buffer
    std::string m_pending_open;  // switch target waiting on the unsaved-changes dialog
    bool m_visible;              // sidebar (file browser) window visible
    bool m_editor_open;          // dedicated code window visible
    bool m_modal_requested;      // open the unsaved-changes dialog this frame
    bool m_focus_code_window;    // focus the code window next frame
    bool m_dock_requested;       // apply m_dock_node on the next code-window render
    unsigned int m_dock_node;    // dock node for the code window (0 = floating)
    float m_editor_pos[2];       // remembered code-window position
    float m_editor_size[2];      // remembered code-window size
    bool m_editor_pos_valid;     // remembered code-window geometry exists
};
