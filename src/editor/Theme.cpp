#include "Theme.h"

#include "Json.h"

#include <imgui.h>

#include <SDL.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace Theme {

// --- Color helpers ----------------------------------------------------------
// Convert 0-255 components into the 0-1 range ImGui expects.
static ImVec4 rgb(int r, int g, int b, int a = 255)
{
    const float inv = 1.0f / 255.0f;
    return ImVec4(r * inv, g * inv, b * inv, a * inv);
}

static ImVec4 V4(const float c[4])
{
    return ImVec4(c[0], c[1], c[2], c[3]);
}

// --- Palette derivation -----------------------------------------------------
// The six user-editable tokens above are the only "design inputs". Every other
// style color is derived from them with these tiny helpers, so a live edit to
// e.g. the accent token re-skins selection, tabs, scrollbars and focus rings
// together instead of leaving orphaned stock colors behind.
static ImVec4 Lerp(const ImVec4 &a, const ImVec4 &b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

static ImVec4 Lighten(const ImVec4 &c, float t) { return Lerp(c, ImVec4(1, 1, 1, 1), t); }
static ImVec4 Darken(const ImVec4 &c, float t) { return Lerp(c, ImVec4(0, 0, 0, 1), t); }

// Alpha-composite `fg` over `bg` (fg.a in 0-1) — used for the accent tints.
static ImVec4 Over(const ImVec4 &bg, const ImVec4 &fg)
{
    const float inv = 1.0f - fg.w;
    return ImVec4(fg.x * fg.w + bg.x * inv,
                  fg.y * fg.w + bg.y * inv,
                  fg.z * fg.w + bg.z * inv, 1.0f);
}

const Colors &DefaultColors()
{
    static const Colors defaults = {
        {0.106f, 0.114f, 0.137f, 1.0f},   // window_bg 0x1B1D23
        {0.122f, 0.129f, 0.157f, 1.0f},   // child_bg  0x1F2128
        {0.133f, 0.145f, 0.173f, 1.0f},   // popup_bg  0x22252C
        {0.141f, 0.153f, 0.180f, 1.0f},   // frame_bg  0x24272E
        {0.788f, 0.804f, 0.839f, 1.0f},   // text      0xC9CDD6
        {0.357f, 0.486f, 0.980f, 1.0f},   // accent    0x5B7CFA
    };
    return defaults;
}

// Small status colors (used by the play/stop transport). Not user tokens.
static const ImVec4 Green        = rgb(0x3E, 0xA0, 0x5E);
static const ImVec4 GreenHovered = rgb(0x4C, 0xB4, 0x6E);
static const ImVec4 Red          = rgb(0xB5, 0x4A, 0x4A);
static const ImVec4 RedHovered   = rgb(0xC9, 0x5C, 0x5C);

float ComputeDpiScale(void *window, void *renderer)
{
    if (!window || !renderer)
        return 1.0f;

    int logical_w = 0, logical_h = 0;
    int physical_w = 0, physical_h = 0;
    SDL_GetWindowSize(static_cast<SDL_Window *>(window), &logical_w, &logical_h);
    SDL_GetRendererOutputSize(static_cast<SDL_Renderer *>(renderer),
                              &physical_w, &physical_h);

    float sx = (logical_w > 0) ? (float)physical_w / (float)logical_w : 1.0f;
    float sy = (logical_h > 0) ? (float)physical_h / (float)logical_h : 1.0f;
    return std::max(1.0f, std::max(sx, sy));
}

static ImFont *LoadFont(const char *primary, const char *fallback,
                        const char *fallback2, float pixel_size)
{
    ImGuiIO &io = ImGui::GetIO();
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(
            primary, pixel_size, nullptr, io.Fonts->GetGlyphRangesDefault()))
        return font;
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(
            fallback, pixel_size, nullptr, io.Fonts->GetGlyphRangesDefault()))
        return font;
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(
            fallback2, pixel_size, nullptr, io.Fonts->GetGlyphRangesDefault()))
        return font;
    // Last resort: keep the built-in font so the editor still opens.
    return io.Fonts->AddFontDefault();
}

void LoadFonts(Fonts &fonts, float dpi_scale, float base_ui, float base_mono)
{
    const float ui = base_ui * dpi_scale;
    const float mono = base_mono * dpi_scale;

    fonts.ui = LoadFont("C:/Windows/Fonts/segoeui.ttf",
                        "C:/Windows/Fonts/arial.ttf",
                        "C:/Windows/Fonts/verdana.ttf", ui);
    fonts.ui_bold = LoadFont("C:/Windows/Fonts/seguisb.ttf",
                             "C:/Windows/Fonts/arialbd.ttf",
                             "C:/Windows/Fonts/verdana.ttf", ui);
    fonts.mono = LoadFont("C:/Windows/Fonts/CascadiaMono.ttf",
                          "C:/Windows/Fonts/consola.ttf",
                          "C:/Windows/Fonts/cour.ttf", mono);
}

void ConfigureStyle(float ui_scale, const Colors &colors)
{
    ImGuiStyle &style = ImGui::GetStyle();

    // Reset to stock defaults first so repeated calls (UI-scale changes, live
    // theme edits) can never drift the palette or metrics.
    style = ImGuiStyle();

    // --- Metrics: refined rounding, generous but tight spacing ---
    style.WindowPadding    = ImVec2(10.0f, 10.0f);
    style.FramePadding     = ImVec2(7.0f, 5.0f);
    style.CellPadding      = ImVec2(6.0f, 5.0f);
    style.ItemSpacing      = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing    = 18.0f;
    style.ScrollbarSize    = 13.0f;
    style.GrabMinSize      = 9.0f;

    style.WindowBorderSize   = 1.0f;
    style.ChildBorderSize    = 1.0f;
    style.PopupBorderSize    = 1.0f;
    style.FrameBorderSize    = 0.0f;
    style.TabBorderSize      = 1.0f;

    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 5.0f;
    style.TabRounding       = 4.0f;
    style.TabBarOverlineSize = 2.0f; // selected-tab highlight

    style.WindowTitleAlign    = ImVec2(0.5f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Right;

    // --- Colors: derive the full palette from the user tokens ---
    const ImVec4 window  = V4(colors.window_bg);
    const ImVec4 child   = V4(colors.child_bg);
    const ImVec4 popup   = V4(colors.popup_bg);
    const ImVec4 frame   = V4(colors.frame_bg);
    const ImVec4 text    = V4(colors.text);
    const ImVec4 accent  = V4(colors.accent);

    const ImVec4 textMuted   = Lerp(text, window, 0.45f);
    const ImVec4 textBright  = Lerp(text, ImVec4(1, 1, 1, 1), 0.20f);

    const ImVec4 menuBg      = Darken(window, 0.05f);
    const ImVec4 titleBg     = Darken(window, 0.02f);
    const ImVec4 titleActive = Lerp(window, accent, 0.05f);

    const ImVec4 frameHovered = Lerp(frame, ImVec4(1, 1, 1, 1), 0.06f);
    const ImVec4 frameActive  = Lerp(frame, ImVec4(1, 1, 1, 1), 0.12f);

    const ImVec4 button        = Lerp(frame, ImVec4(1, 1, 1, 1), 0.03f);
    const ImVec4 buttonHovered = Lerp(frame, ImVec4(1, 1, 1, 1), 0.12f);
    const ImVec4 buttonActive  = Lerp(frame, ImVec4(1, 1, 1, 1), 0.22f);

    const ImVec4 border        = Lerp(window, ImVec4(1, 1, 1, 1), 0.09f);
    const ImVec4 borderBright  = Lerp(window, ImVec4(1, 1, 1, 1), 0.16f);
    const ImVec4 separator     = Lerp(window, ImVec4(1, 1, 1, 1), 0.07f);

    const ImVec4 scrollbarBg     = Darken(window, 0.03f);
    const ImVec4 scrollbarGrab   = Lerp(window, ImVec4(1, 1, 1, 1), 0.24f);
    const ImVec4 scrollbarActive = Lerp(window, ImVec4(1, 1, 1, 1), 0.32f);

    const ImVec4 accentHovered = Lighten(accent, 0.06f);
    const ImVec4 accentActive  = Darken(accent, 0.08f);

    // Accent tints (alpha composited over the window background).
    const ImVec4 accentSoft   = Over(window, ImVec4(accent.x, accent.y, accent.z, 40.0f / 255.0f));
    const ImVec4 accentTint   = Over(window, ImVec4(accent.x, accent.y, accent.z, 80.0f / 255.0f));
    const ImVec4 accentStrong = Over(window, ImVec4(accent.x, accent.y, accent.z, 130.0f / 255.0f));

    const ImVec4 tab           = Darken(window, 0.02f);
    const ImVec4 tabHovered    = Lerp(window, accent, 0.12f);
    const ImVec4 tabSelected   = Lerp(window, accent, 0.08f);
    const ImVec4 tabDimmed     = Lerp(window, ImVec4(1, 1, 1, 1), 0.01f);

    const ImVec4 tableHeader   = Lerp(window, ImVec4(1, 1, 1, 1), 0.06f);

    ImVec4 *colors_out = style.Colors;

    // Windows / surfaces.
    colors_out[ImGuiCol_WindowBg]             = window;
    colors_out[ImGuiCol_ChildBg]              = child;
    colors_out[ImGuiCol_PopupBg]              = popup;
    colors_out[ImGuiCol_Border]               = border;
    colors_out[ImGuiCol_BorderShadow]         = rgb(0, 0, 0, 0);
    colors_out[ImGuiCol_MenuBarBg]            = menuBg;

    // Title bars.
    colors_out[ImGuiCol_TitleBg]              = titleBg;
    colors_out[ImGuiCol_TitleBgActive]        = titleActive;
    colors_out[ImGuiCol_TitleBgCollapsed]     = menuBg;

    // Frames / inputs.
    colors_out[ImGuiCol_FrameBg]              = frame;
    colors_out[ImGuiCol_FrameBgHovered]       = frameHovered;
    colors_out[ImGuiCol_FrameBgActive]        = frameActive;

    // Text.
    colors_out[ImGuiCol_Text]                 = text;
    colors_out[ImGuiCol_TextDisabled]         = textMuted;
    colors_out[ImGuiCol_TextSelectedBg]       = accentSoft;

    // Buttons.
    colors_out[ImGuiCol_Button]               = button;
    colors_out[ImGuiCol_ButtonHovered]        = buttonHovered;
    colors_out[ImGuiCol_ButtonActive]         = buttonActive;

    // Headers (selectable list items, collapsing headers).
    colors_out[ImGuiCol_Header]               = accentTint;
    colors_out[ImGuiCol_HeaderHovered]        = accentStrong;
    colors_out[ImGuiCol_HeaderActive]         = accentStrong;

    // Checkbox / slider grabs and focus.
    colors_out[ImGuiCol_CheckMark]            = accent;
    colors_out[ImGuiCol_SliderGrab]           = accent;
    colors_out[ImGuiCol_SliderGrabActive]     = accentHovered;

    // Tabs.
    colors_out[ImGuiCol_Tab]                  = tab;
    colors_out[ImGuiCol_TabHovered]           = tabHovered;
    colors_out[ImGuiCol_TabSelected]          = tabSelected;
    colors_out[ImGuiCol_TabSelectedOverline]  = accent;
    colors_out[ImGuiCol_TabDimmed]            = tabDimmed;
    colors_out[ImGuiCol_TabDimmedSelected]    = tabSelected;
    colors_out[ImGuiCol_TabDimmedSelectedOverline] = accentTint;

    // Separators.
    colors_out[ImGuiCol_Separator]            = separator;
    colors_out[ImGuiCol_SeparatorHovered]     = accent;
    colors_out[ImGuiCol_SeparatorActive]      = accentHovered;

    // Scrollbars.
    colors_out[ImGuiCol_ScrollbarBg]          = scrollbarBg;
    colors_out[ImGuiCol_ScrollbarGrab]        = scrollbarGrab;
    colors_out[ImGuiCol_ScrollbarGrabHovered] = scrollbarActive;
    colors_out[ImGuiCol_ScrollbarGrabActive]  = accent;

    // Tables.
    colors_out[ImGuiCol_TableHeaderBg]        = tableHeader;
    colors_out[ImGuiCol_TableBorderStrong]    = border;
    colors_out[ImGuiCol_TableBorderLight]     = separator;
    colors_out[ImGuiCol_TableRowBg]           = rgb(0, 0, 0, 0);
    colors_out[ImGuiCol_TableRowBgAlt]        = rgb(0xFF, 0xFF, 0xFF, 12);

    // Drag-drop / docking previews.
    colors_out[ImGuiCol_DragDropTarget]       = accent;
    colors_out[ImGuiCol_DockingPreview]       = accentStrong;
    colors_out[ImGuiCol_DockingEmptyBg]       = Darken(window, 0.08f);
    colors_out[ImGuiCol_ModalWindowDimBg]     = rgb(0, 0, 0, 150);

    // Plot widget.
    colors_out[ImGuiCol_PlotHistogram]        = accent;
    colors_out[ImGuiCol_PlotHistogramHovered] = accentHovered;
    colors_out[ImGuiCol_PlotLines]            = textMuted;
    colors_out[ImGuiCol_PlotLinesHovered]     = accent;

    // Nav highlight (keyboard focus ring).
    colors_out[ImGuiCol_NavHighlight]         = accent;
    colors_out[ImGuiCol_NavWindowingHighlight] = accentTint;
    colors_out[ImGuiCol_NavWindowingDimBg]    = rgb(0, 0, 0, 150);

    // Apply the user's global UI zoom to every metric.
    style.ScaleAllSizes(ui_scale);
}

bool SaveThemeToFile(const Colors &colors, const char *path)
{
    auto WriteToken = [](json::Value &obj, const char *key, const float c[4])
    {
        json::Value arr = json::Value::MakeArray();
        for (int i = 0; i < 4; ++i)
            arr.array.push_back(json::Value::MakeNumber(c[i]));
        obj.object.emplace_back(key, std::move(arr));
    };

    json::Value root = json::Value::MakeObject();
    root.object.emplace_back("version", json::Value::MakeNumber(1.0));
    WriteToken(root, "window_bg", colors.window_bg);
    WriteToken(root, "child_bg", colors.child_bg);
    WriteToken(root, "popup_bg", colors.popup_bg);
    WriteToken(root, "frame_bg", colors.frame_bg);
    WriteToken(root, "text", colors.text);
    WriteToken(root, "accent", colors.accent);

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out)
        return false;
    out << json::WritePretty(root) << "\n";
    out.close();
    return true;
}

bool LoadThemeFromFile(Colors &colors, const char *path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
        return false;

    std::stringstream buffer;
    buffer << in.rdbuf();

    std::string error;
    json::Value root = json::Parse(buffer.str(), &error);
    if (!root.IsObject())
        return false;

    auto ReadToken = [&root](const char *key, float out[4]) -> bool
    {
        const json::Value *arr = root.Find(key);
        if (!arr || !arr->IsArray() || arr->Size() < 4)
            return false;
        for (int i = 0; i < 4; ++i)
            out[i] = (float)arr->At((size_t)i).num;
        return true;
    };

    Colors loaded = colors;
    bool any = false;
    any |= ReadToken("window_bg", loaded.window_bg);
    any |= ReadToken("child_bg", loaded.child_bg);
    any |= ReadToken("popup_bg", loaded.popup_bg);
    any |= ReadToken("frame_bg", loaded.frame_bg);
    any |= ReadToken("text", loaded.text);
    any |= ReadToken("accent", loaded.accent);
    if (!any)
        return false;

    colors = loaded;
    return true;
}

} // namespace Theme
