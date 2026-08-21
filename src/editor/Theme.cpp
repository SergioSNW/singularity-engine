#include "Theme.h"

#include <imgui.h>

#include <SDL.h>
#include <algorithm>

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
        {0.12f, 0.12f, 0.12f, 1.0f},   // window_bg      matte charcoal base
        {0.16f, 0.16f, 0.16f, 1.0f},   // child_bg       panel recess
        {0.14f, 0.14f, 0.14f, 1.0f},   // popup_bg       menus / popovers
        {0.18f, 0.18f, 0.18f, 1.0f},   // frame_bg       controls / inputs
        {0.85f, 0.85f, 0.85f, 1.0f},   // text           off-white text
        {0.05f, 0.05f, 0.05f, 1.0f},   // border         subtle outlines
        {0.10f, 0.10f, 0.10f, 1.0f},   // secondary_bg   title / alternate fill
        {0.14f, 0.14f, 0.16f, 1.0f},   // folder_bg      browser cards / explorer surfaces
        {0.345f, 0.553f, 0.961f, 1.0f},// accent         indigo selection / tabs
    };
    return defaults;
}

// Small status colors (used by the play/stop transport). Not user tokens.
static const ImVec4 Green        = rgb(0x3E, 0xA0, 0x5E);
static const ImVec4 GreenHovered = rgb(0x4C, 0xB4, 0x6E);
static const ImVec4 Red          = rgb(0xB5, 0x4A, 0x4A);
static const ImVec4 RedHovered   = rgb(0xC9, 0x5C, 0x5C);

// Accent token applied by the most recent ConfigureStyle() call, captured so
// PushPrimaryButtonColor() can tint a control without re-deriving the palette.
static ImVec4 s_accent;

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

// The default glyph ranges omit box-drawing / geometric shapes (0x2500–0x25FF)
static const ImWchar *GlyphRangesWithSymbols()
{
    static ImWchar ranges[64];
    static bool built = false;
    if (!built)
    {
        const ImWchar *def = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
        int n = 0;
        while (n + 4 < 48 && def[n] != 0)
        {
            ranges[n] = def[n];
            ++n;
        }
        ranges[n++] = 0x2500; ranges[n++] = 0x25FF;  // box drawing + geometric shapes
        ranges[n++] = 0x2600; ranges[n++] = 0x26FF;  // misc symbols
        ranges[n] = 0;
        built = true;
    }
    return ranges;
}

static ImFont *LoadFont(const char *primary, const char *fallback,
                        const char *fallback2, float pixel_size)
{
    ImGuiIO &io = ImGui::GetIO();
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(
            primary, pixel_size, nullptr, GlyphRangesWithSymbols()))
        return font;
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(
            fallback, pixel_size, nullptr, GlyphRangesWithSymbols()))
        return font;
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(
            fallback2, pixel_size, nullptr, GlyphRangesWithSymbols()))
        return font;
    return io.Fonts->AddFontDefault();
}

void LoadFonts(Fonts &fonts, float dpi_scale, float base_ui, float base_mono)
{
    const float ui = base_ui * dpi_scale;
    const float mono = base_mono * dpi_scale;

    fonts.ui = LoadFont("assets/fonts/Roboto-Regular.ttf",
                        "C:/Windows/Fonts/segoeui.ttf",
                        "C:/Windows/Fonts/arial.ttf", ui);
    fonts.ui_bold = LoadFont("assets/fonts/Roboto-Medium.ttf",
                           "C:/Windows/Fonts/seguisb.ttf",
                           "C:/Windows/Fonts/arialbd.ttf", ui);
    fonts.mono = LoadFont("C:/Windows/Fonts/CascadiaMono.ttf",
                          "C:/Windows/Fonts/consola.ttf",
                          "C:/Windows/Fonts/cour.ttf", mono);
}

void ConfigureStyle(float ui_scale, const Colors &colors)
{
    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark();

    const ImVec4 window = V4(colors.window_bg);
    const ImVec4 child = V4(colors.child_bg);
    const ImVec4 popup = V4(colors.popup_bg);
    const ImVec4 frame = V4(colors.frame_bg);
    const ImVec4 border = V4(colors.border);
    const ImVec4 secondary = V4(colors.secondary_bg);
    const ImVec4 folder = V4(colors.folder_bg);
    const ImVec4 text = V4(colors.text);
    const ImVec4 accent = V4(colors.accent);

    const ImVec4 textMuted = Lerp(text, window, 0.45f);
    const ImVec4 accentSoft = Over(window, ImVec4(accent.x, accent.y, accent.z, 48.0f / 255.0f));
    const ImVec4 accentTint = Over(window, ImVec4(accent.x, accent.y, accent.z, 96.0f / 255.0f));
    const ImVec4 accentHovered = Lighten(accent, 0.06f);
    const ImVec4 accentActive = Darken(accent, 0.08f);
    const ImVec4 button = Lerp(frame, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.03f);
    const ImVec4 buttonHovered = Lerp(frame, accent, 0.20f);
    const ImVec4 buttonActive = Lerp(frame, accent, 0.32f);
    const ImVec4 separator = Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.06f);
    const ImVec4 scrollbarBg = Darken(window, 0.03f);
    const ImVec4 scrollbarGrab = Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.26f);
    const ImVec4 scrollbarActive = Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.34f);
    const ImVec4 tab = secondary;
    const ImVec4 tabHovered = Lerp(window, accent, 0.16f);
    const ImVec4 tabSelected = Lerp(window, accent, 0.12f);
    const ImVec4 tabDimmed = Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.01f);
    const ImVec4 tableHeader = Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.08f);

    s_accent = accent;

    style.Colors[ImGuiCol_WindowBg] = window;
    style.Colors[ImGuiCol_ChildBg] = folder;
    style.Colors[ImGuiCol_PopupBg] = popup;
    style.Colors[ImGuiCol_Border] = border;
    style.Colors[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
    style.Colors[ImGuiCol_MenuBarBg] = secondary;
    style.Colors[ImGuiCol_TitleBg] = secondary;
    style.Colors[ImGuiCol_TitleBgActive] = secondary;
    style.Colors[ImGuiCol_TitleBgCollapsed] = secondary;

    style.Colors[ImGuiCol_FrameBg] = frame;
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);

    style.Colors[ImGuiCol_Text] = text;
    style.Colors[ImGuiCol_TextDisabled] = textMuted;
    style.Colors[ImGuiCol_TextSelectedBg] = accentSoft;

    style.Colors[ImGuiCol_Button] = button;
    style.Colors[ImGuiCol_ButtonHovered] = buttonHovered;
    style.Colors[ImGuiCol_ButtonActive] = buttonActive;

    style.Colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

    style.Colors[ImGuiCol_Tab] = tab;
    style.Colors[ImGuiCol_TabHovered] = tabHovered;
    style.Colors[ImGuiCol_TabSelected] = tabSelected;
    style.Colors[ImGuiCol_TabSelectedOverline] = accent;
    style.Colors[ImGuiCol_TabDimmed] = tabDimmed;
    style.Colors[ImGuiCol_TabDimmedSelected] = tabSelected;
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = accentTint;

    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = accentHovered;

    style.Colors[ImGuiCol_Separator] = separator;
    style.Colors[ImGuiCol_SeparatorHovered] = accent;
    style.Colors[ImGuiCol_SeparatorActive] = accentHovered;

    style.Colors[ImGuiCol_ScrollbarBg] = scrollbarBg;
    style.Colors[ImGuiCol_ScrollbarGrab] = scrollbarGrab;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = scrollbarActive;
    style.Colors[ImGuiCol_ScrollbarGrabActive] = accent;

    style.Colors[ImGuiCol_TableHeaderBg] = tableHeader;
    style.Colors[ImGuiCol_TableBorderStrong] = border;
    style.Colors[ImGuiCol_TableBorderLight] = separator;
    style.Colors[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);
    style.Colors[ImGuiCol_TableRowBgAlt] = rgb(0xFF, 0xFF, 0xFF, 12);

    style.Colors[ImGuiCol_DragDropTarget] = accent;
    style.Colors[ImGuiCol_DockingPreview] = accentTint;
    style.Colors[ImGuiCol_DockingEmptyBg] = secondary;
    style.Colors[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 150);

    style.Colors[ImGuiCol_PlotHistogram] = accent;
    style.Colors[ImGuiCol_PlotHistogramHovered] = accentHovered;
    style.Colors[ImGuiCol_PlotLines] = textMuted;
    style.Colors[ImGuiCol_PlotLinesHovered] = accent;

    style.Colors[ImGuiCol_NavHighlight] = accent;
    style.Colors[ImGuiCol_NavWindowingHighlight] = accentTint;
    style.Colors[ImGuiCol_NavWindowingDimBg] = rgb(0, 0, 0, 150);

    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 22.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.TabBarOverlineSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Right;

    style.ScaleAllSizes(ui_scale);
}

void PushPrimaryButtonColor()
{
    ImGui::PushStyleColor(ImGuiCol_Button, s_accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lighten(s_accent, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Darken(s_accent, 0.10f));
}

void PopPrimaryButtonColor()
{
    ImGui::PopStyleColor(3);
}

} // namespace Theme