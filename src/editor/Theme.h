#pragma once

#include <string>

struct ImFont;

// UI theme and font pipeline for the editor. Centralizes the two things that
// make the workspace look professional and stay razor-sharp:
//
//   * A single custom dark style (modern contrast, refined rounding, an indigo
//     accent instead of stock grey) applied from one place so every panel
//     shares identical colors and metrics.
//   * DPI-aware font loading. ImGui lays out in *logical* pixels while the SDL2
//     renderer backend scales draw data by io.DisplayFramebufferScale into
//     *physical* pixels. Rasterizing glyphs at that same factor and folding it
//     out of FontGlobalScale (1/dpi) keeps text density exactly 1:1 with the
//     framebuffer, so text and borders stay crisp on high-DPI displays instead
//     of being scaled up (blurred) at present time.
namespace Theme {

// Fonts the editor needs, loaded once during Init (before the first frame, so
// glyphs merge into the still-unbuilt atlas). Each is baked at its logical
// size multiplied by the DPI scale.
struct Fonts
{
    ImFont *ui = nullptr;        // Segoe UI (UI text, labels, inputs)
    ImFont *ui_bold = nullptr;   // Segoe UI Semibold (titles, headers)
    ImFont *mono = nullptr;      // Cascadia Mono (script editor buffer)
};

// Framebuffer scale (physical / logical), clamped to >= 1.0. Computed from the
// same inputs the ImGui SDL2 backend uses (renderer output size vs window
// size) so font density matches io.DisplayFramebufferScale exactly.
float ComputeDpiScale(void *window, void *renderer);

// Rasterize the editor fonts at `base_* * dpi_scale`. Falls back along a
// curated chain (Segoe UI -> Arial -> default; Cascadia Mono -> Consolas ->
// Courier New) so the editor still builds on stripped-down Windows installs.
void LoadFonts(Fonts &fonts, float dpi_scale, float base_ui = 18.0f,
               float base_mono = 15.0f);

// The key color tokens the user can live-edit in the Theme Settings panel.
// Everything else in the style (borders, hovers, tints, tabs, scrollbars) is
// derived from these at ConfigureStyle time, so tweaking one token re-skins the
// whole editor coherently. Stored as 0-1 RGBA floats (no ImGui dependency in
// this header). Live edits are saved to disk (see SaveColors) and reloaded on
// the next launch, so customization survives a restart.
struct Colors
{
    float window_bg[4];      // main window / surface background
    float child_bg[4];       // child windows, panel recesses
    float popup_bg[4];       // popups, menus, dropdowns
    float frame_bg[4];      // input frames, buttons, combo boxes
    float text[4];          // primary text
    float border[4];        // borders, dividers, outlines
    float secondary_bg[4];  // alternate surface / title / secondary panel fills
    float folder_bg[4];     // content browser / folder cards and browser surfaces
    float accent[4];        // indigo accent (selection, tabs, focus rings)
};

// The engine's default palette (the warm charcoal + indigo scheme).
const Colors &DefaultColors();

// Default location for the persisted palette, relative to the working
// directory the editor is launched from (matches the "assets/..." convention
// used by LoadFonts).
extern const char *const kColorsPath;

// Write `colors` to `path` as JSON, creating parent directories as needed.
// Returns false on I/O failure (path unwritable, etc.); the caller can
// ignore the result since a failed save just means the palette won't survive
// a restart, not a broken session.
bool SaveColors(const Colors &colors, const std::string &path = kColorsPath);

// Read a previously saved palette from `path` into `out`. Returns false (and
// leaves `out` untouched) if the file doesn't exist or fails to parse, so
// callers should seed `out` from DefaultColors() first and only overwrite it
// on success.
bool LoadColors(Colors &out, const std::string &path = kColorsPath);

// Rebuild the ImGui style from scratch using `colors` and apply `ui_scale` to
// every style metric. Rebuilding from `ImGuiStyle()` first keeps the palette
// from drifting across repeated calls (UI-scale changes, live color edits).
void ConfigureStyle(float ui_scale, const Colors &colors);

// Accent-styled colors for a window's primary action button (Save, Create).
// Push before the button and Pop immediately after; derived from the accent
// token most recently applied by ConfigureStyle().
void PushPrimaryButtonColor();
void PopPrimaryButtonColor();

} // namespace Theme
