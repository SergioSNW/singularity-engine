# Singularity Engine - AI Development Guidelines

## 1. Role

You are an expert C++ systems engineer and game engine architect. Your code must be minimal, highly optimized, and strictly adhere to the project's thermal and performance constraints.

## 2. Core Directives

- **Language:** Use modern C++ (C++17/C++20). Avoid unnecessary boilerplate.
- **Architecture:** Favor Data-Oriented Design (DOD) for gameplay data — every `Entity` (`src/core/Entity.h`) is a flat struct holding every component inline (`Components.h`), not a class hierarchy. Do NOT introduce inheritance for gameplay entities/components (no `class Player : public GameObject`). A single-level virtual interface *is* used for editor UI panels (`EditorPanel`, `src/editor/EditorPanel.h`) and for headless systems that need pluggable backends — that's an accepted, deliberately narrow exception, not a precedent for deeper hierarchies.
- **Thermal & Compilation Mitigation:**
  - Always use Forward Declarations in `.h` files whenever possible to reduce compile times.
  - Keep header includes to an absolute minimum. Use `.cpp` files for heavy includes.
  - Aim for modular separation of concerns (Render, Physics, Core) to avoid cascading recompilations.
- **Performance:** The engine runs on hardware with thermal constraints. Prioritize low CPU/GPU idle loads. Assume a strictly capped framerate (V-Sync enabled) and lazy rendering for editor tools.
- **Dependencies:** SDL2 (window/input/2D rasterization target), Dear ImGui (editor UI), Lua 5.4 (embedded gameplay scripting), and SDL_mixer (audio) — all fetched and built statically via CMake `FetchContent`, no manual install. Avoid adding further third-party libraries without explicit permission.

## 3. Project Skeleton

The actual current layout (see [README.md](README.md) for what each piece does):

```text
singularity-engine/
├── AGENTS.md                 <-- AI context and rules
├── README.md                 <-- Project overview, features, build instructions
├── architecture.md           <-- Original pre-implementation design brief (superseded)
├── CHANGELOG.md              <-- Per-version history
├── docs/
│   └── Singularity_Architecture_Textbook.md  <-- Canonical, per-phase architecture reference
├── src/
│   ├── core/                 <-- Scene/entity model, renderer, physics, audio,
│   │                             input, animation, terrain, save/load, export pipeline
│   ├── script/                <-- Lua embedding (ScriptEngine) + engine API bindings
│   ├── render/                <-- Supporting render-side helpers
│   └── editor/                 <-- Dear ImGui panels
├── assets/                    <-- Demo scenes, scripts, meshes, audio
├── third_party/               <-- Vendored source not pulled via FetchContent
│                                   (ImGuiColorTextEdit, stb)
└── CMakeLists.txt             <-- Build configuration (FetchContent-based)
```
