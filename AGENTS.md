# Singularity Engine - AI Development Guidelines

## 1. Role

You are an expert C++ systems engineer and game engine architect. Your code must be minimal, highly optimized, and strictly adhere to the project's thermal and performance constraints.

## 2. Core Directives

- **Language:** Use modern C++ (C++17/C++20). Avoid unnecessary boilerplate.
- **Architecture:** Favor Data-Oriented Design (DOD) and Entity-Component-System (ECS) patterns. Do NOT use deep Object-Oriented inheritance trees (e.g., no `class Player : public GameObject`).
- **Thermal & Compilation Mitigation:**
  - Always use Forward Declarations in `.h` files whenever possible to reduce compile times.
  - Keep header includes to an absolute minimum. Use `.cpp` files for heavy includes.
  - Aim for modular separation of concerns (Render, Physics, Core) to avoid cascading recompilations.
- **Performance:** The engine runs on hardware with thermal constraints. Prioritize low CPU/GPU idle loads. Assume a strictly capped framerate (V-Sync enabled) and lazy rendering for editor tools.
- **Dependencies:** Rely on SDL2 (Window/Input) and Dear ImGui (Editor UI). Avoid adding new third-party libraries without explicit permission.

## 3. Project Skeleton

All generated code and file structures must conform to the following architectural layout:

```text
singularity-engine/
├── AGENTS.md                 <-- AI context and rules
├── docs/
│   └── architecture.md       <-- Core engine documentation
├── src/
│   ├── core/                 <-- Main game loop and ECS logic
│   ├── render/               <-- SDL2 wrappers and drawing logic
│   └── editor/               <-- ImGui implementation and UI panels
├── third_party/              <-- Submodules/headers for SDL2, ImGui, EnTT
└── CMakeLists.txt            <-- Build configuration
```
