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

// Rebuild the ImGui style from scratch with the engine's dark theme and apply
// `ui_scale` to every style metric. Rebuilding from `ImGuiStyle()` first keeps
// the palette from drifting across repeated calls (e.g. UI-scale changes).
void ConfigureStyle(float ui_scale);

} // namespace Theme
