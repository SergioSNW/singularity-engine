#pragma once

#include <imgui.h>

// TextDisabled has no built-in wrapped variant; without wrapping, a long hint
// sentence pushes its window wider (or clips) instead of flowing onto extra
// lines when the panel is docked into a narrow rail (e.g. the ~20% width
// Hierarchy/Inspector/Landscape side panels).
inline void TextDisabledWrapped(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
