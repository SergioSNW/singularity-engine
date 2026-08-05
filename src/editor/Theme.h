#pragma once

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
void LoadFonts(Fonts &fonts, float dpi_scale, float base_ui = 17.0f,
               float base_mono = 15.0f);

// The six key color tokens the user can live-edit in the Theme Settings
// panel. Everything else in the style (borders, hovers, tints, tabs, scrollbars)
// is derived from these at ConfigureStyle time, so tweaking one token re-skins
// the whole editor coherently. Stored as 0-1 RGBA floats (no ImGui dependency
// in this header) and persisted to editor_theme.json (gitignored).
struct Colors
{
    float window_bg[4];   // main window / surface background (warm charcoal)
    float child_bg[4];    // child windows, panel recesses
    float popup_bg[4];    // popups, menus, dropdowns
    float frame_bg[4];    // input frames, buttons, combo boxes
    float text[4];        // primary text
    float accent[4];      // indigo accent (selection, tabs, focus rings)
};

// The engine's default palette (the warm charcoal + indigo scheme).
const Colors &DefaultColors();

// Rebuild the ImGui style from scratch using `colors` and apply `ui_scale` to
// every style metric. Rebuilding from `ImGuiStyle()` first keeps the palette
// from drifting across repeated calls (UI-scale changes, live color edits).
void ConfigureStyle(float ui_scale, const Colors &colors);

// Persist the current token set to `path` (editor_theme.json) so a custom
// color scheme survives restarts. Returns true on success.
bool SaveThemeToFile(const Colors &colors, const char *path = "editor_theme.json");

// Load a saved token set from `path`. On success `colors` is overwritten and
// true is returned; on any failure (missing file, bad format) false is
// returned and `colors` is left untouched.
bool LoadThemeFromFile(Colors &colors, const char *path = "editor_theme.json");

} // namespace Theme
