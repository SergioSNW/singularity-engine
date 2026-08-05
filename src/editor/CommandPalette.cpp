#include "CommandPalette.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace {

// Subsequence fuzzy score: how well `query` matches `text`. Returns -1 when no
// subsequence match exists, otherwise a score rewarding (in order of weight):
// exact-prefix hits, word-boundary and camel-hump hits, and consecutive runs.
static int FuzzyScore(const char *query, const char *text)
{
    if (!*query)
        return 0; // empty query matches everything with a neutral score

    int score = 0;
    const char *p = text;
    int prev_pos = -2;
    bool first = true;
    for (const char *q = query; *q; ++q)
    {
        const char *f = p;
        while (*f && std::tolower((unsigned char)*f) != std::tolower((unsigned char)*q))
            ++f;
        if (!*f)
            return -1;

        const int pos = (int)(f - text);
        if (pos == 0)
            score += 10; // prefix match
        else if (first)
            score += 6;  // first query char found anywhere
        else
            score += 1;

        if (pos > 0)
        {
            const char prev = text[pos - 1];
            if (prev == ' ' || prev == '-' || prev == '_' || prev == '/' ||
                prev == '.' || prev == '(')
                score += 4; // word boundary
            else if (std::isupper((unsigned char)*f) &&
                     std::islower((unsigned char)prev))
                score += 4; // camel hump
        }
        if (pos == prev_pos + 1)
            score += 8; // consecutive run

        prev_pos = pos;
        p = f + 1;
        first = false;
    }
    return score;
}

} // namespace

CommandPalette::CommandPalette()
{
}

void CommandPalette::Register(const Command &command)
{
    m_commands.push_back(command);
}

void CommandPalette::ToggleOpen()
{
    if (m_open)
    {
        Close();
        return;
    }
    m_open = true;
    m_popup_open = false;
    m_grab_focus = true;
    m_scroll_to_selected = false;
    m_filter[0] = '\0';
    m_selected = 0;
}

void CommandPalette::Close()
{
    if (m_open && m_popup_open)
        ImGui::CloseCurrentPopup();
    m_open = false;
    m_popup_open = false;
}

void CommandPalette::RefreshMatches()
{
    m_matches.clear();

    // Tokenize the query on whitespace; every token must match (multi-word
    // queries like "open editor" work, each token scored independently).
    std::vector<std::string> tokens;
    {
        std::istringstream iss(m_filter);
        std::string tok;
        while (iss >> tok)
            tokens.push_back(tok);
    }

    struct Entry { int index; int score; };
    std::vector<Entry> scored;
    scored.reserve(m_commands.size());

    for (size_t i = 0; i < m_commands.size(); ++i)
    {
        const Command &cmd = m_commands[i];
        const std::string haystack = cmd.category + " " + cmd.label + " " + cmd.shortcut;

        int total = 0;
        bool ok = true;
        for (const std::string &tok : tokens)
        {
            const int s = FuzzyScore(tok.c_str(), haystack.c_str());
            if (s < 0)
            {
                ok = false;
                break;
            }
            total += s;
        }
        if (!ok)
            continue;

        scored.push_back({ (int)i, total });
    }

    // Best matches first; stable sort keeps registration order for ties, so
    // commands stay grouped by category.
    std::stable_sort(scored.begin(), scored.end(),
        [](const Entry &a, const Entry &b) { return a.score > b.score; });

    for (const Entry &e : scored)
        m_matches.push_back(e.index);

    if (m_selected >= (int)m_matches.size())
        m_selected = m_matches.empty() ? 0 : (int)m_matches.size() - 1;
}

void CommandPalette::RunCommand(int index)
{
    if (index < 0 || index >= (int)m_matches.size())
        return;

    Command command = m_commands[m_matches[index]];
    Close();
    if (command.action)
        command.action();
}

void CommandPalette::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_open)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        Close();
        return;
    }

    RefreshMatches();

    if (!m_popup_open)
    {
        ImGui::OpenPopup("Command Palette");
        m_popup_open = true;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.25f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Command Palette", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings))
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));

    // Filter input: focus it the instant the palette opens so typing works
    // immediately (no mouse interaction required).
    if (m_grab_focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_grab_focus = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##PaletteFilter", "Type a command...",
                                 m_filter, sizeof(m_filter)))
    {
        m_selected = 0;
        m_scroll_to_selected = true;
    }

    // Results list. Height grows with the match count but is capped so the
    // palette never fills the screen.
    const int n = (int)m_matches.size();
    const float row_h = ImGui::GetFrameHeightWithSpacing();
    const float list_h = row_h * (float)std::clamp(n, 1, 9);
    ImGui::BeginChild("##PaletteResults", ImVec2(0.0f, list_h));

    if (n == 0)
    {
        ImGui::TextDisabled("No matching commands");
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const Command &cmd = m_commands[m_matches[i]];
            const bool selected = (i == m_selected);

            // Keep the keyboard selection scrolled into view.
            if (selected && m_scroll_to_selected)
                ImGui::SetScrollHereY(0.5f);

            ImGui::PushID(m_matches[i]);
            if (ImGui::Selectable(cmd.label.c_str(), selected))
                RunCommand(i);
            if (!cmd.shortcut.empty())
            {
                const float sw = ImGui::CalcTextSize(cmd.shortcut.c_str()).x;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - sw -
                                ImGui::GetStyle().FramePadding.x);
                ImGui::TextDisabled("%s", cmd.shortcut.c_str());
            }
            ImGui::PopID();
        }
    }
    m_scroll_to_selected = false;
    ImGui::EndChild();

    // Keyboard navigation (Enter / mouse already handled by the rows above).
    if (n > 0)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            m_selected = (m_selected - 1 + n) % n;
            m_scroll_to_selected = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            m_selected = (m_selected + 1) % n;
            m_scroll_to_selected = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            RunCommand(m_selected);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Up/Down navigate   Enter run   Esc close");

    ImGui::PopStyleVar(1);
    ImGui::EndPopup();
}
