#include "ScriptEditorPanel.h"

#include "ImGuiColorTextEdit/TextEditor.h"
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

// Mini-IDE theme: a deeper navy-black editor with accent-tinted current line,
// a contrasting line-number gutter, and syntax colors tuned to the engine's
// accent (0x5B7CFA). TextEditor stores palette entries as 0xAABBGGRR.
static TextEditor::Palette MakeEditorPalette()
{
    TextEditor::Palette p = TextEditor::GetDarkPalette();
    p[(size_t)TextEditor::PaletteIndex::Background] = 0xFF1B1412;        // #12141B
    p[(size_t)TextEditor::PaletteIndex::CurrentLineFill] = 0x22FA7C5B;   // accent @ 13%
    p[(size_t)TextEditor::PaletteIndex::CurrentLineFillInactive] = 0x06FFFFFF;
    p[(size_t)TextEditor::PaletteIndex::CurrentLineEdge] = 0x55FA7C5B;   // accent @ 33%
    p[(size_t)TextEditor::PaletteIndex::LineNumberFill] = 0xFF221916;    // #161922 gutter
    p[(size_t)TextEditor::PaletteIndex::LineNumber] = 0xFF62524A;        // #4A5262
    p[(size_t)TextEditor::PaletteIndex::Selection] = 0x809E4F3A;         // #3A4F9E
    p[(size_t)TextEditor::PaletteIndex::Keyword] = 0xFFF58D9C;           // #9C8DF5
    p[(size_t)TextEditor::PaletteIndex::Number] = 0xFF4BA0E5;            // #E5A04B
    p[(size_t)TextEditor::PaletteIndex::String] = 0xFFA7C85C;            // #5CC8A7
    p[(size_t)TextEditor::PaletteIndex::CharLiteral] = 0xFFC2D97F;       // #7FD9C2
    p[(size_t)TextEditor::PaletteIndex::Punctuation] = 0xFFA3938A;       // #8A93A3
    p[(size_t)TextEditor::PaletteIndex::Preprocessor] = 0xFFDB7BB5;      // #B57BDB
    p[(size_t)TextEditor::PaletteIndex::Identifier] = 0xFFD4C9C4;        // #C4C9D4
    p[(size_t)TextEditor::PaletteIndex::KnownIdentifier] = 0xFFFFAA82;   // #82AAFF
    p[(size_t)TextEditor::PaletteIndex::PreprocIdentifier] = 0xFFF0A6E0; // #E0A6F0
    p[(size_t)TextEditor::PaletteIndex::Comment] = 0xFF72645B;           // #5B6472
    p[(size_t)TextEditor::PaletteIndex::MultiLineComment] = 0xFF66564E;  // #4E5666
    p[(size_t)TextEditor::PaletteIndex::Cursor] = 0xFFFFFFFF;
    return p;
}

ScriptEditorPanel::ScriptEditorPanel(ImFont *mono_font, ReloadCallback reload,
                                     RedockCallback redock)
    : m_reload(std::move(reload))
    , m_redock(std::move(redock))
    , m_mono_font(mono_font)
    , m_active_tab(-1)
    , m_pending_close(-1)
    , m_visible(true)
    , m_modal_requested(false)
    , m_focus_window(false)
    , m_dock_requested(false)
    , m_dock_node(0)
    , m_sidebar_width(220.0f)
    , m_auto_save(true)
    , m_was_focused(false)
{
    m_new_name[0] = '\0';
}

ScriptEditorPanel::~ScriptEditorPanel()
{
    for (Tab &tab : m_tabs)
        delete tab.editor;
}

void ScriptEditorPanel::ToggleVisible()
{
    m_visible = !m_visible;
    if (m_visible)
        m_focus_window = true;
}

void ScriptEditorPanel::SetVisible(bool visible)
{
    if (visible == m_visible)
        return;
    ToggleVisible();
}

void ScriptEditorPanel::RequestDockCodeWindow(unsigned int node_id)
{
    m_dock_requested = true;
    m_dock_node = node_id;
}

std::string ScriptEditorPanel::GetCodeWindowTitle() const
{
    if (m_active_tab < 0)
        return std::string();
    return "Script Editor: " +
           std::filesystem::path(m_tabs[m_active_tab].path).filename().string();
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

int ScriptEditorPanel::FindTab(const std::string &path) const
{
    for (int i = 0; i < (int)m_tabs.size(); ++i)
    {
        if (m_tabs[i].path == path)
            return i;
    }
    return -1;
}

int ScriptEditorPanel::OpenFile(const std::string &path, std::string *error)
{
    const int existing = FindTab(path);
    if (existing >= 0)
    {
        m_active_tab = existing;
        return existing;
    }

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
    {
        if (error)
            *error = "cannot open '" + path + "'";
        return -1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    Tab tab;
    tab.path = path;
    tab.editor = new TextEditor();
    tab.editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    tab.editor->SetPalette(MakeEditorPalette());
    tab.saved_text = ss.str();
    tab.editor->SetText(tab.saved_text);
    // TextEditor canonicalizes line endings / trailing newlines, so re-read the
    // buffer's canonical text as the saved baseline; otherwise the buffer looks
    // dirty right after opening (and auto-save would rewrite the file).
    tab.saved_text = tab.editor->GetText();
    std::error_code ec;
    tab.last_write_time =
        std::filesystem::last_write_time(path, ec).time_since_epoch().count();

    m_tabs.push_back(std::move(tab));
    m_active_tab = (int)m_tabs.size() - 1;
    m_status = "Opened " + path;
    return m_active_tab;
}

void ScriptEditorPanel::CloseTab(int index)
{
    if (index < 0 || index >= (int)m_tabs.size())
        return;
    if (IsTabDirty(index))
    {
        m_pending_close = index;
        m_modal_requested = true;
        return;
    }
    delete m_tabs[index].editor;
    m_tabs.erase(m_tabs.begin() + index);
    if (m_active_tab > index)
        m_active_tab = m_active_tab - 1;
    else if (m_active_tab == index)
        m_active_tab = (int)m_tabs.size() - 1;  // now-last tab (-1 if empty)
}

bool ScriptEditorPanel::SaveTab(int index, std::string *error)
{
    if (index < 0 || index >= (int)m_tabs.size())
    {
        if (error)
            *error = "no script open";
        return false;
    }
    Tab &tab = m_tabs[index];
    std::ofstream out(tab.path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        if (error)
            *error = "cannot write '" + tab.path + "'";
        return false;
    }
    const std::string text = tab.editor->GetText();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    tab.saved_text = text;
    std::error_code ec;
    tab.last_write_time =
        std::filesystem::last_write_time(tab.path, ec).time_since_epoch().count();
    m_status = "Saved " + tab.path;
    return true;
}

bool ScriptEditorPanel::IsTabDirty(int index) const
{
    if (index < 0 || index >= (int)m_tabs.size())
        return false;
    return m_tabs[index].editor->GetText() != m_tabs[index].saved_text;
}

void ScriptEditorPanel::RequestOpen(const std::string &path)
{
    const int existing = FindTab(path);
    if (existing >= 0)
    {
        m_active_tab = existing;
        return;
    }
    std::string error;
    if (OpenFile(path, &error) < 0)
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

    if (m_pending_close >= 0 && m_pending_close < (int)m_tabs.size())
    {
        const std::string file =
            std::filesystem::path(m_tabs[m_pending_close].path).filename().string();
        ImGui::TextUnformatted(("Save changes to '" + file + "' before closing?").c_str());
    }
    ImGui::Separator();

    if (ImGui::Button("Save"))
    {
        if (m_pending_close >= 0)
        {
            std::string error;
            if (SaveTab(m_pending_close, &error))
                CloseTab(m_pending_close);
            else
                m_status = error;
        }
        m_pending_close = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
    {
        if (m_pending_close >= 0)
            CloseTab(m_pending_close);
        m_pending_close = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_pending_close = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ScriptEditorPanel::DrawFileBrowser()
{
    ImGui::TextUnformatted("Scripts (assets/scripts)");
    ImGui::Separator();

    ImGui::BeginChild("ScriptsList", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.5f));
    for (const std::string &path : m_files)
    {
        const bool selected = (FindTab(path) >= 0);
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
    ImGui::TextDisabled("%zu scripts, %d open", m_files.size(), (int)m_tabs.size());

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

void ScriptEditorPanel::DrawTabBar()
{
    ImGui::BeginChild("##ScriptTabs", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() + 2.0f));
    int close_after = -1;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    for (int i = 0; i < (int)m_tabs.size(); ++i)
    {
        const Tab &tab = m_tabs[i];
        const bool dirty = IsTabDirty(i);
        const bool selected = (i == m_active_tab);
        const std::string file = std::filesystem::path(tab.path).filename().string();
        // The '*' dirty marker is display text; the stable path id keeps the
        // tab's identity across dirty-state toggles.
        const std::string label = (dirty ? "*" : "") + file;

        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? ImVec4(0.36f, 0.49f, 0.98f, 0.22f)
                                       : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              selected ? ImVec4(0.36f, 0.49f, 0.98f, 0.32f)
                                       : ImVec4(0.30f, 0.34f, 0.42f, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              dirty ? ImVec4(1.0f, 0.80f, 0.25f, 1.0f)
                                    : ImVec4(0.82f, 0.85f, 0.90f, 1.0f));
        if (ImGui::Button(label.c_str()))
            m_active_tab = i;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.66f, 0.74f, 1.0f));
        if (ImGui::Button(("x##close" + tab.path).c_str()))
            close_after = i;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    if (close_after >= 0)
        CloseTab(close_after);
}

void ScriptEditorPanel::DrawToolbar(bool dirty, bool docked)
{
    ImGui::BeginDisabled(m_active_tab < 0 || !dirty);
    if (ImGui::Button("Save"))
    {
        std::string error;
        if (!SaveTab(m_active_tab, &error))
            m_status = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save & Reload"))
    {
        std::string error;
        if (SaveTab(m_active_tab, &error))
        {
            const bool reloaded = m_reload ? m_reload() : false;
            m_status = reloaded ? "Saved & live session reloaded"
                                : "Saved (loads on next Play)";
        }
        else
            m_status = error;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-save", &m_auto_save);
    ImGui::SameLine();
    if (ImGui::Button(docked ? "Float" : "Dock to Workspace"))
    {
        if (docked)
        {
            // Pop the mini-IDE out of the dock as a floating window.
            m_dock_requested = true;
            m_dock_node = 0;
        }
        else if (m_redock)
        {
            m_redock();
        }
    }
    ImGui::SameLine();
    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::Separator();
}

void ScriptEditorPanel::DrawEditorPane(int tab_index)
{
    if (tab_index < 0)
    {
        ImGui::TextDisabled("No script open — pick one in the sidebar or the "
                            "Content Browser.");
        return;
    }
    Tab &tab = m_tabs[tab_index];
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushFont(m_mono_font ? m_mono_font : ImGui::GetFont());
    tab.editor->Render("##ScriptBuffer", avail, false);
    ImGui::PopFont();
}

void ScriptEditorPanel::OnImGuiRender(float /*dt*/)
{
    RefreshFileList();

    if (!m_visible)
        return;

    if (m_dock_requested)
    {
        ImGui::SetNextWindowDockID(m_dock_node, ImGuiCond_Always);
        m_dock_requested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(920.0f, 560.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Script Editor", &m_visible, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }
    const bool docked = ImGui::IsWindowDocked();
    if (m_focus_window)
    {
        ImGui::SetWindowFocus();
        m_focus_window = false;
    }

    ConfirmUnsavedModal();

    // Auto-open the first script the first time the IDE appears.
    if (m_active_tab < 0 && !m_files.empty())
    {
        std::string error;
        if (OpenFile(m_files.front(), &error) < 0)
            m_status = error;
    }

    // Sidebar | splitter | (tab bar + toolbar + editor).
    ImGui::BeginChild("##IDE_sidebar", ImVec2(m_sidebar_width, 0.0f),
                      ImGuiChildFlags_Borders);
    DrawFileBrowser();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("##ide_splitter",
                           ImVec2(6.0f, ImGui::GetContentRegionAvail().y));
    ImGui::PopStyleVar();
    if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0.0f)
        m_sidebar_width = std::clamp(m_sidebar_width + ImGui::GetIO().MouseDelta.x,
                                     130.0f, 420.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImGui::SameLine();
    ImGui::BeginChild("##IDE_content", ImVec2(0.0f, 0.0f));
    {
        DrawTabBar();
        DrawToolbar(m_active_tab >= 0 && IsTabDirty(m_active_tab), docked);
        DrawEditorPane(m_active_tab);

        // Ctrl+S saves the active tab while the IDE has keyboard focus.
        if (ImGui::GetIO().KeyCtrl && ImGui::IsWindowFocused() &&
            ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            std::string error;
            if (!SaveTab(m_active_tab, &error))
                m_status = error;
        }

        // Real-time hooks (active tab only): auto-save on blur, and watch the
        // file on disk so an external edit hot-reloads the live session (the
        // reload callback no-ops outside play mode).
        const bool focused_now = ImGui::IsWindowFocused();
        if (focused_now)
        {
            m_was_focused = true;
        }
        else if (m_was_focused)
        {
            m_was_focused = false;
            if (m_auto_save && m_active_tab >= 0 && IsTabDirty(m_active_tab))
            {
                std::string error;
                if (SaveTab(m_active_tab, &error))
                    m_status = (m_reload && m_reload())
                                   ? "Auto-saved & live session reloaded"
                                   : "Auto-saved";
                else
                    m_status = error;
            }
        }

        if (m_active_tab >= 0)
        {
            Tab &tab = m_tabs[m_active_tab];
            std::error_code ec;
            const auto mtime = std::filesystem::last_write_time(tab.path, ec);
            if (!ec && mtime.time_since_epoch().count() > tab.last_write_time)
            {
                tab.last_write_time = mtime.time_since_epoch().count();
                std::ifstream in(tab.path, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    const std::string on_disk = ss.str();
                    if (on_disk != tab.saved_text)
                    {
                        // A dirty buffer is never clobbered by an external edit.
                        if (!IsTabDirty(m_active_tab))
                        {
                            tab.saved_text = on_disk;
                            tab.editor->SetText(on_disk);
                            tab.saved_text = tab.editor->GetText();  // canonical
                            m_status = (m_reload && m_reload())
                                           ? "Reloaded external change & live session"
                                           : "Reloaded external change";
                        }
                        else
                        {
                            m_status =
                                "File changed on disk; buffer dirty (not clobbered)";
                        }
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
}
