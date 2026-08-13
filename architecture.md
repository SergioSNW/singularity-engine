# Singularity Engine

> **Current version:** v0.29.0-alpha — Viewport Overlays & Gizmo Toggle Toolbar: the 3D Viewport gains a docked header toolbar (drawn by the Application inside the Viewport window through new `ViewportPanel::on_toolbar` / `on_overlay` callbacks, so it rides the window across every workspace) with segmented **Lit / Wireframe / Unlit** render-mode buttons and visibility toggles for the ground grid, physics colliders, light gizmos, bounding boxes, and transform gizmo, plus an optional on-viewport stats HUD (render mode, smoothed FPS, camera position — editor-only, never in the isolated play view). The mode + overlay state lives in one pure, headless-testable struct — `ViewportOverlaySettings` (`src/core/ViewportOverlaySettings.h`) — edited by the toolbar, the View menu, and the command palette, and read by the render passes: `RenderScenePass` gates solid fills (skipped in Wireframe), the light loop (skipped in Unlit → flat albedo), and the wireframe pass (skipped in Unlit); `RenderEditorOverlay` gates its bounds/collider/gizmo sections and gains a new light-gizmo indicator (sun cross + direction arrow for each active light, so mesh-less light entities stay findable). The toolbar's grid-snap quick toggle and T/R/S increment inputs write straight into the existing `SnapSettings m_snap` consumed by the gizmo math — no duplicated state. A second toolbar row hosts the snap controls; the full step sliders remain in Editor Settings. The status bar, toasts, workspace persistence, multi-viewport camera stack, lighting, and audio from v0.24.0–v0.28.0 are unchanged.
> [CHANGELOG](../CHANGELOG.md)

## Architecture and Technical Feasibility Document

### 1. Overview

**Singularity Engine** is a 2D video game engine for PC and its respective editor, written in C++. It is designed with a radical focus on minimalism, resource efficiency, and thermal control, allowing for agile and stable development even on hardware with severe cooling and power constraints.

### 2. Design Principles

- **Thermal Efficiency by Design:** Prioritizing a low processing footprint to prevent system overheating during prolonged editor use.
- **Minimized Compile Times:** Code structuring oriented towards ultra-fast incremental builds, drastically reducing CPU stress spikes.
- **Clean Architecture:** Absolute decoupling of core subsystems (Rendering, Physics, Business Core) to facilitate scalability, maintenance, and isolated testing.

### 3. Proposed Technology Stack

- **Language:** C++ (C++17 or C++20 standard to leverage modern features without bloating compile times).
- **Windowing & Input:** SDL2 or SFML (lightweight, proven libraries with excellent native 2D performance).
- **Editor Interface:** Dear ImGui (industry-standard tool, extremely lightweight, and easy to integrate).
- **Build System:** CMake + Ninja (to maximize compilation speed and manage dependencies efficiently).
- **Data Architecture:** Entity-Component-System (ECS), such as `entt`, to guarantee cache data locality (maximum performance) and total modularity in game logic.

### 4. Thermal Mitigation and Performance Strategy

Given that the target development environment is highly sensitive to heat, Singularity Engine will implement the following low-level architectural rules:

#### A. GPU Control (The Editor)

- **Mandatory FPS Capping:** The editor will never run with an unlocked framerate. Vertical Synchronization (V-Sync) will be forced at the swap chain level, with a secondary capper at 60 FPS in the main loop.
- **Lazy Rendering:** While the engine is in edit mode and not in _Play_ mode, the editor interface will minimize draw calls. If the user does not interact (mouse movement, drag and drop), the main loop will go to sleep, reducing GPU usage to practically zero.

#### B. CPU Control (Compilation Process)

- **Forward Declarations:** Strict prohibition of including heavy `.h` files inside other headers. Forward declarations will be used to isolate dependencies and prevent a single change from triggering a recompilation of half the project.
- **Precompiled Headers (PCH):** Use of precompiled headers for all third-party code (the C++ STL, SDL, ImGui), so the compiler does not have to reprocess them during every development iteration.
- **Library Modularity:** Separation of the engine into static libraries (`.lib` / `.a`). A change in the audio subsystem must not trigger the re-linking of the graphics subsystem.

### 5. Workflow

1.  **Fast Iteration:** CPU control rules ensure that when modifying the C++ logic of an entity, the incremental compilation takes just 2 to 5 seconds (a negligible heat spike).
2.  **Safe Prolonged Sessions:** By combining lightweight 2D rendering with strict frame control, the computer chassis will maintain stable operating temperatures.
3.  **Clean Prototyping:** Utilizing an ECS architecture allows for the creation of complex gameplay systems without generating the "spaghetti" code that typically slows down compilation in engines based on deep object-oriented inheritance.
