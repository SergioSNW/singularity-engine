# Changelog

## [0.1.0-alpha] — 2026-07-09

### Added

- CMake build system with FetchContent-based SDL2 resolution.
- `src/core/main.cpp` — SDL2 window (1280x720), V-Sync'd render loop.
- README with build instructions and project structure.

### Changed

- Game loop now calculates delta time via `SDL_GetPerformanceCounter`.
- Escape key closes the window alongside the close button.
- Frame-aware pacing: `SDL_Delay` only the remainder to hit 60 FPS target, preventing drift and busy-waiting.
- Integrated Dear ImGui (v1.91.0) with SDL2 + SDL_Renderer2 backends.
- Engine Stats diagnostic window showing Delta Time and FPS.
- ImGui demo window enabled for visual confirmation.

## [0.2.0-alpha] — 2026-07-09

### Added

- `src/core/Window.h` / `Window.cpp` — class encapsulating SDL_Window and SDL_Renderer creation and destruction.
- `src/core/Application.h` / `Application.cpp` — class managing the ImGui lifecycle and the thermal-mitigated game loop.
- `docs/Singularity_Architecture_Textbook.md` — comprehensive architecture documentation covering the environment setup, frame pacing math, ImGui integration, and structural design decisions.

### Changed

- `src/core/main.cpp` refactored to a minimal entry point (instantiates `Application`, calls `Init` + `Run`).
- `CMakeLists.txt` updated to compile `Window.cpp` and `Application.cpp`.

## [0.3.0-alpha] — 2026-07-09

### Added

- `src/editor/EditorPanel.h` — pure virtual interface for all editor panels.
- `src/editor/StatsPanel.h` / `StatsPanel.cpp` — first panel implementation, containing the diagnostic stats window and ImGui demo window.
- `CMakeLists.txt` updated to compile `StatsPanel.cpp` and include `src/editor/` in the include path.

### Changed

- `Application` now holds a `std::vector<std::shared_ptr<EditorPanel>>` collection.
- ImGui stats UI logic moved out of `Application::Run()` into `StatsPanel::OnImGuiRender()`.
- `Application::Init()` instantiates and registers `StatsPanel`.
- `Application::Run()` iterates the panel collection each frame.

## [0.3.1-alpha] — 2026-07-09

### Fixed

- `ImGui_ImplSDLRenderer2_RenderDrawData` call missing `SDL_Renderer*` second argument in `Application::Run()`.
- Missing `#include <SDL.h>` in `main.cpp` causing `SDL_main` macro not to be applied, leading to linker error on Windows.
- Removed `ImGui::ShowDemoWindow()` call from `StatsPanel` (requires `imgui_demo.cpp` which is not compiled).

## [0.4.0-alpha] — 2026-07-09

### Added

- ImGui docking enabled (`ImGuiConfigFlags_DockingEnable`) in `Application::Init()`.
- Root dockspace covering the main viewport (`DockSpaceOverViewport`) created each frame before panel rendering.
- All existing panels (StatsPanel) are now dockable into the central dockspace.

## [0.4.1-alpha] — 2026-07-09

### Added

- `src/editor/SceneHierarchyPanel.h` / `.cpp` — new panel inheriting `EditorPanel`, renders a "Hierarchy" window with a mock scene tree (Scene Root, Camera, Directional Light, Cube Object).
- SceneHierarchyPanel registered in `Application::Init()` alongside StatsPanel.

## [0.4.2-alpha] — 2026-07-09

### Added

- `src/editor/InspectorPanel.h` / `.cpp` — new panel inheriting `EditorPanel`, renders an "Inspector" window with mock Transform (Position/Rotation/Scale drag floats), Material (color picker), and Active checkbox controls.
- InspectorPanel registered in `Application::Init()` alongside existing panels.

## [0.5.0-alpha] — 2026-07-09

### Added

- `src/editor/SelectionState.h` — shared struct (`entity_id`, `entity_name`) owned by `Application` and passed to panels.
- `src/editor/ViewportPanel.h` / `.cpp` — stub panel rendering a "Viewport" window with a dummy drawable region.
- Shared selection between `SceneHierarchyPanel` and `InspectorPanel`: clicking an entity in the hierarchy updates the selection; Inspector shows properties conditionally based on selection.
- Custom ImGui theme via `ConfigureImGuiStyle()` — rounded corners, dark palette with accent highlights, applied after `StyleColorsDark()`. 
- ViewportPanel registered in `Application::Init()`.

## [0.5.1-alpha] — 2026-07-09

### Added

- Dockspace locking: `ImGuiDockNodeFlags_NoDockingSplit` and `NoUndocking` prevent accidental panel undocking or node splitting.
- Automatic docked layout on first frame: Hierarchy (left 25%), Viewport (center 50%), Inspector (right 25%), Stats (bottom 20%).
- `ViewportPanel` now tracks its pixel dimensions (`GetWidth()`/`GetHeight()`) and accepts an `SDL_Texture*` for live rendering via `ImGui::Image()`.
- `ImGuiWindowFlags_NoCollapse` applied to StatsPanel, SceneHierarchyPanel, and InspectorPanel to prevent collapsing.
