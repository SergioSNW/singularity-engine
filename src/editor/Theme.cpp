#include "Theme.h"

#include <imgui.h>

#include <SDL.h>
#include <algorithm>

namespace Theme {

// --- Color helpers ----------------------------------------------------------
// Convert 0-255 components into the 0-1 range ImGui expects. Keep the palette
// as a readable token table so the whole UI reads as one coherent design.
static ImVec4 rgb(int r, int g, int b, int a = 255)
{
    const float inv = 1.0f / 255.0f;
    return ImVec4(r * inv, g * inv, b * inv, a * inv);
}

namespace palette {
// Core neutrals (warm charcoal scale).
static const ImVec4 BgWindow      = rgb(0x1B, 0x1D, 0x23);
static const ImVec4 BgChild       = rgb(0x1F, 0x21, 0x28);
static const ImVec4 BgPopup       = rgb(0x22, 0x25, 0x2C);
static const ImVec4 BgMenuBar     = rgb(0x16, 0x17, 0x1C);
static const ImVec4 BgScrollbar   = rgb(0x14, 0x16, 0x1B);
static const ImVec4 BgTableHeader = rgb(0x21, 0x24, 0x2B);

static const ImVec4 Frame         = rgb(0x24, 0x27, 0x2E);
static const ImVec4 FrameHovered  = rgb(0x2C, 0x30, 0x3A);
static const ImVec4 FrameActive   = rgb(0x35, 0x39, 0x46);

static const ImVec4 TitleBg       = rgb(0x1D, 0x1F, 0x25);
static const ImVec4 TitleBgActive = rgb(0x22, 0x24, 0x2C);

static const ImVec4 Button        = rgb(0x2C, 0x30, 0x3A);
static const ImVec4 ButtonHovered = rgb(0x38, 0x3D, 0x4B);
static const ImVec4 ButtonActive  = rgb(0x47, 0x4E, 0x62);

static const ImVec4 Border        = rgb(0x2A, 0x2E, 0x38);
static const ImVec4 BorderBright  = rgb(0x35, 0x3B, 0x49);
static const ImVec4 Separator     = rgb(0x2C, 0x30, 0x3A);

// Text scale.
static const ImVec4 Text          = rgb(0xC9, 0xCD, 0xD6);
static const ImVec4 TextMuted     = rgb(0x78, 0x7D, 0x89);
static const ImVec4 TextBright    = rgb(0xEF, 0xF1, 0xF5);

// Indigo accent: professional blue used for selection, tabs, drag grabs,
// check marks, and focus rings.
static const ImVec4 Accent        = rgb(0x5B, 0x7C, 0xFA);
static const ImVec4 AccentHovered = rgb(0x6B, 0x8A, 0xFF);
static const ImVec4 AccentActive  = rgb(0x4A, 0x6A, 0xE8);

// Accent tints for hover / active states (alpha composited over BgWindow).
static const ImVec4 AccentSoft    = rgb(0x5B, 0x7C, 0xFA,  40);
static const ImVec4 AccentTint    = rgb(0x5B, 0x7C, 0xFA,  80);
static const ImVec4 AccentStrong  = rgb(0x5B, 0x7C, 0xFA, 130);

static const ImVec4 ScrollbarGrab     = rgb(0x3A, 0x40, 0x4E);
static const ImVec4 ScrollbarHovered  = rgb(0x4A, 0x52, 0x64);
static const ImVec4 ScrollbarActive   = Accent;

static const ImVec4 Tab           = rgb(0x1D, 0x1F, 0x25);
static const ImVec4 TabHovered    = rgb(0x2A, 0x2F, 0x3C);
static const ImVec4 TabSelected   = rgb(0x26, 0x2B, 0x37);
static const ImVec4 TabDimmed     = rgb(0x20, 0x23, 0x2B);

// Small status colors (used by the play/stop transport).
static const ImVec4 Green         = rgb(0x3E, 0xA0, 0x5E);
static const ImVec4 GreenHovered  = rgb(0x4C, 0xB4, 0x6E);
static const ImVec4 Red           = rgb(0xB5, 0x4A, 0x4A);
static const ImVec4 RedHovered    = rgb(0xC9, 0x5C, 0x5C);
static const ImVec4 Amber         = rgb(0xF2, 0xC8, 0x4D);
} // namespace palette

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

void ConfigureStyle(float ui_scale)
{
    ImGuiStyle &style = ImGui::GetStyle();

    // Reset to stock defaults first so repeated calls (UI-scale changes) can
    // never drift the palette or metrics.
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

    // --- Colors: the engine's dark theme ---
    ImVec4 *colors = style.Colors;
    using namespace palette;

    // Windows / surfaces.
    colors[ImGuiCol_WindowBg]             = BgWindow;
    colors[ImGuiCol_ChildBg]              = BgChild;
    colors[ImGuiCol_PopupBg]              = BgPopup;
    colors[ImGuiCol_Border]               = Border;
    colors[ImGuiCol_BorderShadow]         = rgb(0, 0, 0, 0);
    colors[ImGuiCol_MenuBarBg]            = BgMenuBar;

    // Title bars.
    colors[ImGuiCol_TitleBg]              = TitleBg;
    colors[ImGuiCol_TitleBgActive]        = TitleBgActive;
    colors[ImGuiCol_TitleBgCollapsed]     = BgMenuBar;

    // Frames / inputs.
    colors[ImGuiCol_FrameBg]              = Frame;
    colors[ImGuiCol_FrameBgHovered]       = FrameHovered;
    colors[ImGuiCol_FrameBgActive]        = FrameActive;

    // Text.
    colors[ImGuiCol_Text]                 = Text;
    colors[ImGuiCol_TextDisabled]         = TextMuted;
    colors[ImGuiCol_TextSelectedBg]       = AccentSoft;

    // Buttons.
    colors[ImGuiCol_Button]               = Button;
    colors[ImGuiCol_ButtonHovered]        = ButtonHovered;
    colors[ImGuiCol_ButtonActive]         = ButtonActive;

    // Headers (selectable list items, collapsing headers).
    colors[ImGuiCol_Header]               = AccentTint;
    colors[ImGuiCol_HeaderHovered]        = AccentStrong;
    colors[ImGuiCol_HeaderActive]         = AccentStrong;

    // Checkbox / slider grabs and focus.
    colors[ImGuiCol_CheckMark]            = Accent;
    colors[ImGuiCol_SliderGrab]           = Accent;
    colors[ImGuiCol_SliderGrabActive]     = AccentHovered;

    // Tabs.
    colors[ImGuiCol_Tab]                  = Tab;
    colors[ImGuiCol_TabHovered]           = TabHovered;
    colors[ImGuiCol_TabSelected]          = TabSelected;
    colors[ImGuiCol_TabSelectedOverline]  = Accent;
    colors[ImGuiCol_TabDimmed]            = TabDimmed;
    colors[ImGuiCol_TabDimmedSelected]    = TabSelected;
    colors[ImGuiCol_TabDimmedSelectedOverline] = AccentTint;

    // Separators.
    colors[ImGuiCol_Separator]            = Separator;
    colors[ImGuiCol_SeparatorHovered]     = Accent;
    colors[ImGuiCol_SeparatorActive]      = AccentHovered;

    // Scrollbars.
    colors[ImGuiCol_ScrollbarBg]          = BgScrollbar;
    colors[ImGuiCol_ScrollbarGrab]        = ScrollbarGrab;
    colors[ImGuiCol_ScrollbarGrabHovered] = ScrollbarHovered;
    colors[ImGuiCol_ScrollbarGrabActive]  = ScrollbarActive;

    // Tables.
    colors[ImGuiCol_TableHeaderBg]        = BgTableHeader;
    colors[ImGuiCol_TableBorderStrong]    = Border;
    colors[ImGuiCol_TableBorderLight]     = Separator;
    colors[ImGuiCol_TableRowBg]           = rgb(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]        = rgb(0xFF, 0xFF, 0xFF, 12);

    // Drag-drop / docking previews.
    colors[ImGuiCol_DragDropTarget]       = Accent;
    colors[ImGuiCol_DockingPreview]       = AccentStrong;
    colors[ImGuiCol_DockingEmptyBg]       = rgb(0x14, 0x16, 0x1B);
    colors[ImGuiCol_ModalWindowDimBg]     = rgb(0, 0, 0, 150);

    // Plot widget.
    colors[ImGuiCol_PlotHistogram]        = Accent;
    colors[ImGuiCol_PlotHistogramHovered] = AccentHovered;
    colors[ImGuiCol_PlotLines]            = TextMuted;
    colors[ImGuiCol_PlotLinesHovered]     = Accent;

    // Nav highlight (keyboard focus ring).
    colors[ImGuiCol_NavHighlight]         = Accent;
    colors[ImGuiCol_NavWindowingHighlight] = AccentTint;
    colors[ImGuiCol_NavWindowingDimBg]    = rgb(0, 0, 0, 150);

    // Apply the user's global UI zoom to every metric.
    style.ScaleAllSizes(ui_scale);
}

} // namespace Theme
