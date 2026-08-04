#include "ScriptEditorPanel.h"

#include "ImGuiColorTextEdit/TextEditor.h"
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

// Palette tuned to the engine's dark blue-grey theme. TextEditor ships a dark
// palette; only the surface tones are overridden so the buffer matches the
// editor UI while keeping the keyword/number/string colors intact.
static TextEditor::Palette MakeEditorPalette()
{
    TextEditor::Palette palette = TextEditor::GetDarkPalette();
    palette[static_cast<size_t>(TextEditor::PaletteIndex::Background)] = 0x1A1A22FF;
    palette[static_cast<size_t>(TextEditor::PaletteIndex::CurrentLineFill)] = 0x24242EFF;
    palette[static_cast<size_t>(TextEditor::PaletteIndex::CurrentLineFillInactive)] = 0x1F1F27FF;
    palette[static_cast<size_t>(TextEditor::PaletteIndex::CurrentLineEdge)] = 0x00000000;
    palette[static_cast<size_t>(TextEditor::PaletteIndex::LineNumber)] = 0x56566BFF;
    palette[static_cast<size_t>(TextEditor::PaletteIndex::Selection)] = 0x33508FFF;
    return palette;
}

ScriptEditorPanel::ScriptEditorPanel(ReloadCallback reload)
    : m_reload(std::move(reload))
    , m_editor(new TextEditor())
    , m_mono_font(nullptr)
    , m_visible(true)
    , m_editor_open(false)
    , m_modal_requested(false)
    , m_focus_code_window(false)
    , m_editor_pos_valid(false)
{
    m_new_name[0] = '\0';

    // Monospace font for the buffer, loaded once. Construction happens during
    // Init (before the first frame), so the font atlas is still unbuilt and the
    // glyphs merge into the default atlas.
    ImGuiIO &io = ImGui::GetIO();
    m_mono_font = io.Fonts->AddFontFromFileTTF(
        "C:/Windows/Fonts/consola.ttf", 15.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    if (!m_mono_font)
        m_mono_font = io.Fonts->AddFontFromFileTTF(
            "C:/Windows/Fonts/cour.ttf", 15.0f, nullptr, io.Fonts->GetGlyphRangesDefault());

    m_editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    m_editor->SetPalette(MakeEditorPalette());
}

ScriptEditorPanel::~ScriptEditorPanel()
{
    delete m_editor;
}

void ScriptEditorPanel::ToggleVisible()
{
    m_visible = !m_visible;
    // The View-menu toggle and F4 control the whole script-editing UI: hiding
    // the sidebar also hides the floating code window, and re-showing it
    // brings the code window back when a file is already loaded.
    if (!m_visible)
        m_editor_open = false;
    else if (!m_current.empty())
        m_editor_open = true;
}

void ScriptEditorPanel::RefreshFileList()
{
    m_files.clear();
    std::error_code ec;
    for (std::filesystem::directory_iterator it("assets/scripts", ec), end;
         !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const std::string path = it->path().generic_string();
        if (path.size() < 4 || path.substr(path.size() - 4) != ".lua")
            continue;
        m_files.push_back(path);
    }
    std::sort(m_files.begin(), m_files.end());
}

bool ScriptEditorPanel::OpenFile(const std::string &path, std::string *error)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
    {
        if (error)
            *error = "cannot open '" + path + "'";
        return false;
    }

    // The code-window title embeds the file name, so switching files changes
    // the ImGui window identity. If a code window is already showing a
    // different file, remember its geometry so the retitled window reappears
    // in the same spot instead of bouncing around.
    if (m_editor_open && !m_current.empty() && m_current != path)
        m_editor_pos_valid = true;

    std::ostringstream ss;
    ss << in.rdbuf();
    m_current = path;
    m_saved_text = ss.str();
    m_editor->SetText(m_saved_text);
    m_editor_open = true;
    m_focus_code_window = true;
    m_status = "Opened " + path;
    return true;
}

bool ScriptEditorPanel::SaveCurrent(std::string *error)
{
    if (m_current.empty())
    {
        if (error)
            *error = "no script open";
        return false;
    }
    std::ofstream out(m_current, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        if (error)
            *error = "cannot write '" + m_current + "'";
        return false;
    }
    const std::string text = m_editor->GetText();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    m_saved_text = text;
    m_status = "Saved " + m_current;
    return true;
}

void ScriptEditorPanel::RequestOpen(const std::string &path)
{
    // A dirty buffer is never silently discarded: ask first.
    if (m_editor->GetText() != m_saved_text)
    {
        m_pending_open = path;
        m_modal_requested = true;
        return;
    }
    std::string error;
    if (!OpenFile(path, &error))
        m_status = error;
}

void ScriptEditorPanel::ConfirmUnsavedModal()
{
    if (m_modal_requested)
    {
        ImGui::OpenPopup("Unsaved changes");
        m_modal_requested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Save changes to the current script before switching?");
    if (!m_pending_open.empty())
        ImGui::TextDisabled("%s", m_pending_open.c_str());
    ImGui::Separator();

    if (ImGui::Button("Save"))
    {
        std::string error;
        if (SaveCurrent(&error))
        {
            if (!OpenFile(m_pending_open, &error))
                m_status = error;
            m_pending_open.clear();
        }
        else
            m_status = error;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
    {
        if (!m_pending_open.empty())
        {
            std::string error;
            if (!OpenFile(m_pending_open, &error))
                m_status = error;
            m_pending_open.clear();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_pending_open.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ScriptEditorPanel::DrawFileBrowser()
{
    ImGui::TextUnformatted("Scripts (assets/scripts)");
    ImGui::Separator();

    ImGui::BeginChild("ScriptsList", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()));
    for (const std::string &path : m_files)
    {
        const bool selected = (path == m_current);
        if (ImGui::Selectable(path.c_str(), selected))
            RequestOpen(path);
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextDisabled("New script");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##new_script_name", "script_name.lua", m_new_name,
                             sizeof(m_new_name));
    const bool enter_pressed =
        ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    const bool create_pressed = ImGui::Button("Create");
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        RefreshFileList();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu scripts", m_files.size());

    if (create_pressed || enter_pressed)
    {
        std::string name = m_new_name;
        // Trim surrounding whitespace.
        const size_t first = name.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            name.clear();
        else
        {
            const size_t last = name.find_last_not_of(" \t\r\n");
            name = name.substr(first, last - first + 1);
        }
        if (!name.empty())
        {
            if (name.size() < 4 || name.substr(name.size() - 4) != ".lua")
                name += ".lua";
            const std::string path = "assets/scripts/" + name;
            std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (out.is_open())
            {
                out.close();
                RefreshFileList();
                m_new_name[0] = '\0';
                RequestOpen(path);
            }
            else
            {
                m_status = "cannot create '" + path + "'";
            }
        }
    }
}

void ScriptEditorPanel::DrawCodeWindow()
{
    if (!m_editor_open || m_current.empty())
        return;

    // The title embeds the file name (window identity changes on switch), so a
    // remembered geometry is re-applied before Begin to keep the window put.
    if (m_editor_pos_valid)
    {
        ImGui::SetNextWindowPos(ImVec2(m_editor_pos[0], m_editor_pos[1]));
        ImGui::SetNextWindowSize(ImVec2(m_editor_size[0], m_editor_size[1]));
        m_editor_pos_valid = false;
    }
    ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);

    const std::string name = std::filesystem::path(m_current).filename().string();
    const std::string title = "Script Editor: " + name;
    if (!ImGui::Begin(title.c_str(), &m_editor_open))
    {
        ImGui::End();
        return;
    }
    if (m_focus_code_window)
    {
        ImGui::SetWindowFocus();
        m_focus_code_window = false;
    }
    m_editor_pos[0] = ImGui::GetWindowPos().x;
    m_editor_pos[1] = ImGui::GetWindowPos().y;
    m_editor_size[0] = ImGui::GetWindowSize().x;
    m_editor_size[1] = ImGui::GetWindowSize().y;

    // Toolbar: dirty marker, save actions, last-action status.
    const bool dirty = (m_editor->GetText() != m_saved_text);
    if (dirty)
        ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.25f, 1.00f), "* %s", name.c_str());
    else
        ImGui::TextUnformatted(name.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save"))
    {
        std::string error;
        if (!SaveCurrent(&error))
            m_status = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save & Reload"))
    {
        std::string error;
        if (SaveCurrent(&error))
        {
            const bool reloaded = m_reload ? m_reload() : false;
            m_status = reloaded ? "Saved & live session reloaded" : "Saved (loads on next Play)";
        }
        else
            m_status = error;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::Separator();

    // Ctrl+S saves while this window has keyboard focus.
    if (ImGui::GetIO().KeyCtrl && ImGui::IsWindowFocused() &&
        ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        std::string error;
        if (!SaveCurrent(&error))
            m_status = error;
    }

    // The syntax-highlighting buffer fills the remaining content region.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushFont(m_mono_font ? m_mono_font : ImGui::GetFont());
    m_editor->Render("##ScriptBuffer", avail, false);
    ImGui::PopFont();

    ImGui::End();
}

void ScriptEditorPanel::OnImGuiRender(float dt)
{
    (void)dt;
    RefreshFileList();

    if (m_visible)
    {
        if (ImGui::Begin("Script Editor", &m_visible, ImGuiWindowFlags_NoCollapse))
        {
            ConfirmUnsavedModal();

            // Open the first script the first time the editor appears.
            if (m_current.empty() && !m_files.empty())
            {
                std::string error;
                if (!OpenFile(m_files.front(), &error))
                    m_status = error;
            }

            DrawFileBrowser();
        }
        ImGui::End();
    }

    // The dedicated code window floats independently of the sidebar.
    DrawCodeWindow();
}
