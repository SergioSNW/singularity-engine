#pragma once

#include "EditorPanel.h"

#include <functional>
#include <string>
#include <vector>

class TextEditor;
struct ImFont;

// In-editor Lua script editor. A sidebar browses assets/scripts/ while a
// syntax-highlighting buffer holds the open file. Unsaved edits mark the
// filename with an asterisk; "Save & Reload" writes the file to disk and, when
// a play session is live, hot-swaps the running session via
// ScriptEngine::ReloadSession so OnStart re-runs against the new text.
class ScriptEditorPanel : public EditorPanel
{
public:
    // Invoked after a file is saved. Should reload the live script session and
    // return true when a running session was actually swapped.
    using ReloadCallback = std::function<bool()>;

    explicit ScriptEditorPanel(ReloadCallback reload);
    ~ScriptEditorPanel() override;

    void OnImGuiRender(float dt) override;

    bool &VisibleRef() { return m_visible; }

private:
    void RefreshFileList();
    bool OpenFile(const std::string &path, std::string *error);
    bool SaveCurrent(std::string *error);
    void RequestOpen(const std::string &path);
    void ConfirmUnsavedModal();
    void DrawFileBrowser();
    void DrawEditor();

    ReloadCallback m_reload;
    TextEditor *m_editor;
    ImFont *m_mono_font;

    std::vector<std::string> m_files;
    std::string m_current;       // path of the open file ("" when none open)
    std::string m_saved_text;    // last text written to disk
    std::string m_status;        // last-action feedback line
    char m_new_name[256];        // "New Script" name input buffer
    std::string m_pending_open;  // switch target waiting on the unsaved-changes dialog
    bool m_visible;
    bool m_modal_requested;      // open the unsaved-changes dialog this frame
};
