# Singularity Engine — Architecture Textbook

## Table of Contents

1. [Development Environment Setup](#1-development-environment-setup)
2. [The SDL2 Core Loop and Thermal-Aware Frame Pacing](#2-the-sdl2-core-loop-and-thermal-aware-frame-pacing)
3. [Dear ImGui Integration and Lifecycle Binding](#3-dear-imgui-integration-and-lifecycle-binding)
4. [Modular Class Architecture — Structural Reasoning](#4-modular-class-architecture--structural-reasoning)
5. [Phase 29 — Viewport Overlays & Gizmo Toggle Toolbar](#5-phase-29--viewport-overlays--gizmo-toggle-toolbar)
6. [Phase 30 — Real-Time Performance Profiler UI](#6-phase-30--real-time-performance-profiler-ui)
7. [Phase 31 — Advanced Content Browser & Thumbnail Generator](#7-phase-31--advanced-content-browser--thumbnail-generator)
8. [Phase 32 — Integrated Lua Scripting IDE](#8-phase-32--integrated-lua-scripting-ide)
9. [Phase 33 — True Workspace Layouts, Tabbed Mini-IDE & Theme](#9-phase-33--true-workspace-layouts-tabbed-mini-ide--theme)
10. [Phase 34 — Landscape & Topology Design Suite](#10-phase-34--landscape--topology-design-suite)
11. [Phase 35 — Animation & Timeline Foundation](#11-phase-35--animation--timeline-foundation)
12. [Phase 36 — Physics Materials & Collision Layer Matrix](#12-phase-36--physics-materials--collision-layer-matrix)
13. [Phase 37 — Post-Processing & Environment Lighting Stack](#13-phase-37--post-processing--environment-lighting-stack)
14. [Phase 38 — Dedicated Material Authoring & Shading Mode](#14-phase-38--dedicated-material-authoring--shading-mode)
15. [Phase 39 — Mode-based Panel Isolation & Crash Fix](#15-phase-39--mode-based-panel-isolation--crash-fix)

---

## 1. Development Environment Setup

### 1.1 Why WSL2 + CMake + Ninja?

The engine targets Windows as its primary development platform, but compiles inside **WSL2 (Windows Subsystem for Linux)**. This decision was driven by two factors:

- **POSIX fidelity:** WSL2 provides a real Linux kernel inside a lightweight VM, giving us access to the full GNU toolchain, `fork()`, `mmap()`, and other POSIX primitives that Windows-native toolchains abstract away.
- **Thermal isolation:** Builds run in the WSL2 VM, which has its own CPU scheduling and I/O throttling. Heavy compilation spikes are partially absorbed by the VM layer, reducing thermal impact on the host.

**CMake** was chosen as the meta-build system because it is the de-facto standard for cross-platform C++ projects. It generates build files for Ninja (or Make, or Visual Studio) from a single `CMakeLists.txt`.

**Ninja** is used instead of Make because:
- It is designed for incremental builds — it only recompiles exactly what changed.
- It parallelizes compilation across all CPU cores by default (`-j` auto-detected).
- Build start-up time is near-zero (no recursive Make overhead).

### 1.2 FetchContent — Zero-Install Dependencies

Rather than requiring the developer to manually install SDL2, Dear ImGui, or any future library, the project uses CMake's `FetchContent` module to download and build dependencies at configure time. This means:

- **Reproducibility:** Every clone gets the exact same library versions (pinned by Git tag).
- **Zero friction:** A new developer runs `cmake -B build` and everything "just works."
- **No system-wide pollution:** Libraries are built statically and linked directly into the engine binary.

The current dependency graph:

```
singularity-engine
 ├── SDL2 (2.30.9)         — windowing, input, 2D rendering
 └── Dear ImGui (v1.91.0) — editor UI framework
      └── backends
           ├── imgui_impl_sdl2.cpp
           └── imgui_impl_sdlrenderer2.cpp
```

---

## 2. The SDL2 Core Loop and Thermal-Aware Frame Pacing

### 2.1 The Anatomy of a Game Loop

Every game engine revolves around a **game loop** — an infinite cycle that processes input, updates state, and renders a frame. The naive version looks like this:

```
while (running) {
    processInput();
    update();
    render();
}
```

This has a fatal flaw: on modern hardware, it will spin as fast as the CPU/GPU can go, consuming maximum power and generating maximum heat. For a thermal-constrained development environment (the engine's primary design constraint), this is unacceptable.

### 2.2 The V-Sync Foundation

Vertical Synchronization (**V-Sync**) synchronizes the engine's frame buffer swap with the monitor's refresh cycle. When enabled via `SDL_RENDERER_PRESENTVSYNC`, the `SDL_RenderPresent()` call blocks until the next vertical blanking interval. This alone limits the frame rate to the monitor's refresh rate (typically 60 Hz), which is a coarse thermal mitigation.

However, V-Sync alone is not sufficient for two reasons:
1. **Frame time spikes:** If a frame takes 14 ms, V-Sync will wait ~2 ms to hit the next 16.67 ms boundary — but the CPU has already done all its work and could be put to sleep.
2. **Lack of a secondary cap:** If V-Sync is disabled or fails, the loop must have a software fallback.

### 2.3 Delta Time Calculation

Delta time is the elapsed wall-clock time between two consecutive frames, measured in seconds. It is the fundamental unit of temporal measurement in the engine:

```cpp
Uint64 freq = SDL_GetPerformanceFrequency();          // ticks per second
Uint64 counter = SDL_GetPerformanceCounter();          // current tick
double dt = (double)(counter - prev_counter) / freq;  // seconds
```

- `SDL_GetPerformanceFrequency()` returns the high-resolution timer's frequency (usually the TSC or HPET on x86-64).
- `SDL_GetPerformanceCounter()` returns the current tick count.
- Dividing the difference by the frequency yields sub-microsecond-precision delta time.

### 2.4 The Frame Pacing Equation

The target frame time for 60 FPS is:

```
T_target = 1 / 60 ≈ 0.01667 seconds ≈ 16.67 ms
```

The engine computes how much time is left after processing the frame and deliberately sleeps for the remainder:

```cpp
if (dt < TARGET_FRAME_TIME) {
    double delay_ms = (TARGET_FRAME_TIME - dt) * 1000.0;
    SDL_Delay((Uint32)delay_ms);
}
```

This is superior to a fixed `SDL_Delay(16)` because:
- **No drift:** If a frame takes 11 ms, we delay 5 ms. If it takes 16 ms, we delay 0 ms. The sum always lands at ~16.67 ms.
- **No busy-waiting:** `SDL_Delay` yields the CPU timeslice to the operating system, allowing the core to enter a low-power C-state. On thermally constrained hardware, this can drop package temperature by 10–15 °C during idle periods.
- **Deterministic headroom:** The engine always knows how much time it has left, which will be critical for future subsystems (physics tick budgeting, audio mixing, network sync).

### 2.5 The Complete Thermal Mitigation Stack

```
┌─────────────────────────────────────────────────────┐
│                 Game Loop (60 FPS)                   │
├─────────────────────────────────────────────────────┤
│  1. Query performance counter → compute dt          │
│  2. Poll all pending SDL events                     │
│  3. Dispatch events to ImGui                        │
│  4. Begin ImGui frame (NewFrame triple-call)        │
│  5. Run game logic / editor UI                      │
│  6. Render ImGui draw data                          │
│  7. SDL_RenderPresent (blocks on V-Sync)            │
│  8. If frame finished early, SDL_Delay the rest     │
│  9. Repeat                                          │
└─────────────────────────────────────────────────────┘
                                                          Thermal benefit
    V-Sync (layer 1)      ─── synchronizes with monitor  ─── prevents GPU spin
    SDL_Delay (layer 2)   ─── yields to OS scheduler     ─── lets CPU sleep
    Result                ─── near-idle when no work      ─── stable ~40-45 °C
```

---

## 3. Dear ImGui Integration and Lifecycle Binding

### 3.1 Why Dear ImGui?

Dear ImGui is an **immediate-mode GUI** library. Unlike retained-mode UI frameworks (Qt, wxWidgets), ImGui has no persistent widget tree — every frame you declare what UI should exist, and ImGui computes the rest. This aligns with the engine's minimalism because:

- **Zero state management:** No UI object graph to serialize, deserialize, or debug.
- **Single-file integration:** The core library is 4 source files + 2 backend files.
- **Thermal-friendly:** ImGui's `NewFrame`-`Render` cycle is O(n) in the number of visible widgets. No hidden layout passes, no style recalculation.

### 3.2 The ImGui Lifecycle

The lifecycle has exactly three phases:

#### Phase 1 — Initialization (once)

```cpp
ImGui::CreateContext();                                    // allocates internal state
ImGui::StyleColorsDark();                                  // applies theme
ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);       // binds SDL2 backend
ImGui_ImplSDLRenderer2_Init(renderer);                     // binds SDL_Renderer2 backend
```

Each `Init` call registers callbacks and allocates GPU resources (vertex buffers, texture atlas) within the respective backend.

#### Phase 2 — Per-Frame (every iteration)

```cpp
ImGui_ImplSDLRenderer2_NewFrame();      // resets SDL_Renderer2 backend state
ImGui_ImplSDL2_NewFrame();              // polls SDL input state (mouse, keyboard)
ImGui::NewFrame();                      // begins the ImGui frame — all ImGui:: calls must follow
```

The order is strict:
1. Backend `NewFrame` calls **must** come first — they prepare platform state.
2. `ImGui::NewFrame()` starts the frame and **must** be called before any `ImGui::Begin` / widget calls.
3. After `ImGui::Render()`, no widget calls are allowed until the next `NewFrame`.

#### Phase 3 — Render (per frame)

```cpp
ImGui::Render();                                            // generates draw lists
SDL_RenderClear(renderer);                                  // clear to background color
ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());// issues SDL_RenderDraw calls
SDL_RenderPresent(renderer);                                // swap buffers
```

#### Phase 4 — Shutdown (once)

```cpp
ImGui_ImplSDLRenderer2_Shutdown();
ImGui_ImplSDL2_Shutdown();
ImGui::DestroyContext();
```

### 3.3 Event Routing

SDL2 events must be forwarded to ImGui so that it can detect mouse clicks, keyboard input, text input, and window focus changes:

```cpp
while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);  // let ImGui inspect first
    if (event.type == SDL_QUIT) ...       // then handle engine events
    if (event.type == SDL_KEYDOWN &&
        event.key.keysym.sym == SDLK_ESCAPE) ...
}
```

`ImGui_ImplSDL2_ProcessEvent` returns `void` — it inspects the event internally and modifies ImGui's input state. It does **not** consume the event, so engine-level handling continues normally.

---

## 4. Modular Class Architecture — Structural Reasoning

### 4.1 Motivation for the Refactor

In the initial implementation, everything lived in `main.cpp`:

```
main.cpp (108 lines)
 ├── SDL2 init
 ├── SDL2 window + renderer creation
 ├── ImGui init
 ├── game loop (delta time, events, ImGui, render, pacing)
 ├── ImGui shutdown
 └── SDL2 shutdown
```

This is a **procedural monolith**. While functional, it violates three principles from the engine's design document:
1. **No separation of concerns** — windowing, application logic, and rendering are interleaved.
2. **No testability** — you cannot create a Window without also creating an ImGui context.
3. **No reusability** — if the render subsystem later splits into a separate static library, everything must be untangled manually.

### 4.2 The New Architecture

```
┌──────────────┐     ┌──────────────────┐
│   main.cpp   │ ──> │   Application    │
│ (entrypoint) │     │  (owns lifecycle)│
└──────────────┘     └────────┬─────────┘
                              │ owns
                              ▼
                      ┌──────────────┐
                      │    Window    │
                      │ (owns SDL2)  │
                      └──────────────┘
```

### 4.3 Class Responsibilities

#### `Window` (`Window.h` / `Window.cpp`)

**Responsibility:** Manage the lifetime of `SDL_Window*` and `SDL_Renderer*`.

- Constructor allocates both objects.
- Destructor destroys them in reverse order (renderer first, then window).
- Accessors (`GetWidth`, `GetHeight`, `GetNativeWindow`, `GetNativeRenderer`) provide read-only access to internals without exposing ownership.
- The class is **non-copyable** (by convention — deleted copy ops are added as the codebase matures).

Why not inline SDL2 in `Application`? Because the Window abstraction can later be swapped:
- A `HeadlessWindow` subclass for automated testing.
- A `VulkanWindow` subclass when the engine adds Vulkan rendering.
- Each variant provides the same interface but constructs different native handles.

#### `Application` (`Application.h` / `Application.cpp`)

**Responsibility:** Orchestrate the engine's lifecycle.

- `Init()` — SDL_Init, create Window, init ImGui. Returns `bool` so the caller can handle failure without exceptions.
- `Run()` — the main loop. Encapsulates delta time, event polling, ImGui frame management, rendering, and frame pacing.
- `Shutdown()` — ImGui teardown, Window destruction, SDL_Quit.
- `~Application()` — calls `Shutdown()` as a safety net.

The separation of `Init` from the constructor follows the **two-phase initialization** pattern:
1. Constructor constructs a valid (but inactive) object.
2. `Init()` performs operations that can fail (resource allocation).
3. If `Init` fails, the destructor safely cleans up partial state.

This avoids the need for exceptions (which are disabled by the project's `-fno-exceptions` convention) while keeping RAII semantics.

#### `main.cpp`

**Responsibility:** One thing — bootstrap.

```cpp
int main(int argc, char *argv[]) {
    Application app;
    if (!app.Init(1280, 720, "Singularity Engine v0.1.0-alpha"))
        return 1;
    app.Run();
    return 0;
}
```

This is the **entry point as factory** pattern. `main` does not know about SDL2, ImGui, window dimensions, or frame timing. It simply constructs the application, initializes it, and runs it. If the engine later needs command-line argument parsing, that logic lives in `Application::Init`, not in `main`.

### 4.4 Thermal Mitigation — Preserved and Verified

The refactor moves the thermal-mitigation logic from `main.cpp` into `Application::Run()`, but the algorithm is unchanged:

| Mechanism | Original `main.cpp` | `Application::Run()` |
|---|---|---|
| Delta time via `SDL_GetPerformanceCounter` | Line 55–57 | Line 41–43 |
| `SDL_PollEvent` + ImGui forwarding | Lines 60–67 | Lines 46–53 |
| ImGui NewFrame triple-call | Lines 69–71 | Lines 55–57 |
| `SDL_RenderClear` → `ImGui::Render` → `SDL_RenderPresent` | Lines 87–90 | Lines 75–79 |
| Frame-aware `SDL_Delay` | Lines 92–96 | Lines 81–84 |

All thermal constraints from `AGENTS.md` are honored:
- V-Sync is forced at renderer creation (`SDL_RENDERER_PRESENTVSYNC`).
- A secondary 60 FPS cap is applied via adaptive `SDL_Delay`.
- No busy-waiting — `SDL_Delay` yields the CPU.
- ImGui only renders visible widgets (the demo window and the stats window are both lightweight).

### 4.5 Forward Declaration Discipline

Per the engine's build-time thermal rules:

- `Window.h` forward-declares `SDL_Window` and `SDL_Renderer` instead of including `<SDL.h>`.
- `Application.h` forward-declares `Window` instead of including `Window.h`. (Though in this iteration we include `Window.h` in the `.cpp` — this is acceptable because the `.h` only uses a pointer, which can be forward-declared.)

This ensures that a change to `<SDL.h>` or `Window.h` does not cascade into a recompilation of every file that includes `Application.h`.

### 4.6 Future-Proofing

The current architecture is deliberately simple — three files, two classes, one executable. But it is designed to scale:

- **Render subsystem:** When `src/render/` gains its own classes (e.g., `Texture`, `SpriteBatch`, `Camera`), they will be constructed inside `Application::Init` and used inside `Application::Run()`, without touching `main.cpp`.
- **Editor subsystem:** When `src/editor/` gains panels (Scene View, Inspector, Asset Browser), they will be registered as ImGui callbacks in `Application::Init`. The main loop will not change.
- **ECS integration:** When an ECS library (EnTT) is added, the system update loop will slot into `Application::Run()` between the ImGui `NewFrame` and `Render` calls, keyed off the computed `dt`.

The architecture is not final — it is a foundation that can be reshaped as the engine grows. But every reshape will follow the same rule: **main.cpp never grows beyond 10 lines**.

---

## 5. Phase 29 — Viewport Overlays & Gizmo Toggle Toolbar

The editor ships with a **docked header toolbar inside the 3D Viewport window**, the
one place every rendering/viewing decision already happens. It belongs to the window,
not the dockspace, so it rides along with the Viewport across every workspace preset
and user layout — no `WorkspaceManager` changes were needed.

### 5.1 One Struct, Three Consumers

A single pure struct owns all of it:

```cpp
struct ViewportOverlaySettings {
    ViewportRenderMode render_mode;   // Lit | Wireframe | Unlit
    bool grid, colliders, light_gizmos, bounds, gizmo, hud;
};
```

Three editor surfaces edit one instance (`Application::m_overlay`), and the render
passes read the same instance every frame:

1. **The header toolbar** (`Application::DrawViewportToolbar`, wired into
   `ViewportPanel::on_toolbar`) — segmented Lit/Wireframe/Unlit buttons, checkbox
   toggles for Grid / Colliders / Light Gizmos / Bounds / Gizmo / HUD, and a second
   row with the grid-snap toggle plus compact Translation/Rotation/Scale increment
   inputs (writing straight into `SnapSettings m_snap`, the exact values the gizmo
   math snaps against — no duplicated state).
2. **The View menu** — a "Render Mode" submenu plus the same six toggle items.
3. **The command palette** — `Set Render Mode: Lit/Wireframe/Unlit` and
   `Toggle Grid / Colliders / Light Gizmos / Bounding Boxes / Transform Gizmo / Viewport HUD`.

### 5.2 Render Modes Gate the Shared Pass

`RenderScenePass` — the single per-entry body shared by the multi-viewport render
and the Inspector camera preview — now branches on the mode:

| Mode | Solid fills | Lighting | Wireframe pass |
|------|-------------|----------|----------------|
| Lit | yes | scene lights | yes |
| Wireframe | **no** | — | yes |
| Unlit | yes | **flat albedo** | **no** |

Unlit simply skips the light-gather loop (`use_lighting`), so `EmitEntityTris` falls
back to its flat-albedo path with no shading code change; Wireframe skips Pass 1
fills entirely and keeps only the mesh `edge_lines` pass. The ground grid stays
independent and honors its own `grid` toggle.

### 5.3 Overlay Toggles and the New Light Gizmo

`RenderEditorOverlay` (selection/hover bounds, colliders, gizmo) gates its sections
behind `bounds`, `colliders`, and `gizmo`. It also gained a **light gizmo**: the
default directional-light entity is mesh-less, so without a marker it is invisible
in the scene. For every active light it draws a small "sun" cross at the entity's
world position plus an arrow along the light's direction, so lights stay findable
without turning the viewport into a debug wireframe.

### 5.4 The Stats HUD

`Application::DrawViewportHud` (wired as `ViewportPanel::on_overlay`, drawn after
the 3D image so it overlays the top-left corner) shows the active render mode, the
smoothed FPS, and the editor camera position. It is editor-only: the overlay
callback never fires in the isolated play view, keeping the game view clean.

### 5.5 Testability

`ViewportOverlaySettings` lives in `src/core/ViewportOverlaySettings.h` — no SDL/ImGui
dependency — and `SnapSettings` (`GizmoController.h`) only needs `EngineMath.h`, so
the `phase29_viewport_overlay_test` harness links standalone and verifies defaults,
label mapping, the `Lit → Wireframe → Unlit → Lit` cycle, independent flag flips, and
the snap-step defaults the toolbar edits (29/29 checks).

---

*End of textbook section covering versions v0.1.0-alpha through the architecture refactor, the v0.30.0-alpha real-time performance profiler UI, and the v0.31.0-alpha advanced content browser & thumbnail generator.*

## 6. Phase 30 — Real-Time Performance Profiler UI

Optimizing an editor without measurement is guesswork. Phase 30 adds a lightweight,
always-on telemetry core and a dockable panel that plots it — cheap enough to run
every frame on thermally-constrained hardware, and pure enough to unit-test standalone.

### 6.1 The Pure Core: `Profiler` (`src/core/Profiler.h`)

No SDL, no ImGui, just `std::array` / `std::chrono` — the same "pure Core" pattern as
`ToastManager` and `ViewportOverlaySettings`. Per frame it records:

- **Four stage series** (`Update`, `Render`, `UI`, `Physics`) plus the **frame total**,
  each a fixed 120-sample rolling `Series` ring buffer that keeps a running sum and max
  so latest / average / peak are O(1) reads. `StartFrame()` opens the frame clock and
  zeroes the accumulators; `BeginStage`/`EndStage` bracket each phase (repeatable —
  the accumulator sums disjoint spans); `EndFrame()` commits one sample per stage.
- **A resource snapshot** — entity count, 3D draw calls, resident memory bytes —
  recorded via `RecordResources(...)` and kept as both a readable "latest" and a
  rolling trend series.
- **Pause / Clear** — `SetPaused(true)` makes every recorder a no-op, so the history
  freezes mid-scroll for frame-by-frame inspection; `Clear()` drops all samples.

```cpp
Profiler p;
p.StartFrame();
p.BeginStage(Profiler::Update);   // gameplay + editor interaction
p.EndStage(Profiler::Update);
p.RecordResources(entities, draw_calls, mem);
p.EndFrame();
```

### 6.2 Run-Loop Instrumentation (`Application::Run`)

Each loop iteration calls `m_profiler.StartFrame()` up front, then brackets every
phase so the four stages are mutually exclusive and together span the frame:

| Stage | Bracket |
|-------|---------|
| **UI** | ImGui `NewFrame` → `ImGui::Render` (editor chrome, panels, menus), **plus** the final `SDL_RenderClear` / `RenderDrawData` / `SDL_RenderPresent` blit (two disjoint spans, summed by the accumulator) |
| **Update** | `UpdateCameraControls` + play-mode script `UpdateSession` + editor gizmo/picking interaction |
| **Physics** | `PhysicsManager::Step` (nested inside Update; play-mode only) |
| **Render** | `RenderViewportTarget` (off-screen 3D pass) + `RenderCameraPreview` |

Draw calls are counted, not guessed: `RenderScenePass` / `RenderEditorOverlay` now
thread an `int &draw_calls` tally through every `DrawProjectedLine` (one per
`SDL_RenderDrawLine`) and `FlushTriBatch` (one per `SDL_RenderGeometry` chunk), reset
to zero each frame. Memory is an order-of-magnitude estimate from new
`MeshLibrary::ResidentBytes()` (map nodes + vector storage) and
`TextureLibrary::ResidentBytes()` (w×h×4 per cached GPU texture), plus the live entity
count.

### 6.3 The Panel: `ProfilerPanel` (`src/editor/ProfilerPanel.{h,cpp}`)

A standard `EditorPanel` (View-menu toggle + command palette). It plots the rolling
series with `ImGui::PlotLines` — frame time and each stage in milliseconds (seconds
scaled ×1000), plus raw entities / draw calls / memory-MB trends — with a
latest/avg/peak text row per graph. The header row offers **Pause/Resume** (freezes
the buffers, shows a "FROZEN" tag), **Clear**, and the recorded frame count.

### 6.4 Testability

The `phase30_profiler_test` harness links standalone against `Profiler.h` and verifies
fresh state, a measurable Update stage + resource snapshot, ring-buffer wrap (150
frames keep a fixed 120-sample window with correct oldest/newest ordering), Pause
freezing vs. Resume, and Clear (41/41 checks). The engine smoke run stays alive with
the profiler wired and an empty log.

---

## 7. Phase 31 — Advanced Content Browser & Thumbnail Generator

The Content Browser began as a flat grid of colored badges. Phase 31 turns it into a
real browsing surface: live previews for the assets that can be previewed, a compact
list view for dense folders, and the standard power tools — search, category chips,
thumbnail sizing, and breadcrumb navigation. Every piece follows the project's
"pure Core + thin editor" split: the taxonomy that decides what a file is lives in a
SDL/ImGui-free header, the pixel work lives in a render module, and the panel only
wires the two together.

### 7.1 The Pure Taxonomy: `AssetCatalog` (`src/core/AssetCatalog.h`)

Before Phase 31, the browser and the OS importer each classified files by extension
in their own way, and neither knew about audio assets. `AssetCatalog` is the single
source of truth:

- **Classification** — `ClassifyAsset(path)` maps extensions to an `AssetKind`
  (`Scene` for `.json`, `Prefab` for the `<name>.prefab.json` convention, `Script`
  for `.lua`, `Mesh` for `.obj`, `Material` for `.mat`, `Texture` for image formats,
  `Audio` for `.wav/.ogg`, `Other` otherwise). The Content Browser still decides
  scene-vs-prefab from the JSON content (`SceneSerializer::IsPrefabFile`), then
  delegates everything else to this module so the UI and the importer agree.
- **Search** — `NameMatches(name, query)` is a case-insensitive substring match
  against the item's leaf name; an empty query matches everything.
- **Chips** — `AssetFilter` (`All` / `Meshes` / `Materials` / `Textures` / `Audio` /
  `Prefabs`) with `AssetPassesFilter(kind, filter)`, which always lets `Folder`
  through so a category filter never strands the user without navigation.
- **Breadcrumbs** — `BreadcrumbSegments(path)` splits `assets/meshes/props` into the
  clickable cumulative prefixes `["assets", "assets/meshes", "assets/meshes/props"]`.

The file is header-only and dependency-free, which is what makes the
`phase31_asset_catalog_test` harness link standalone.

### 7.2 The Renderer: `ThumbnailCache` (`src/render/ThumbnailCache.{h,cpp}`)

This is the first file in the `render/` layer of the skeleton — SDL2-drawing logic
kept out of `core/` and `editor/`. The cache generates and stores previews lazily:

| Asset kind | Thumbnail |
|------------|-----------|
| **Mesh (.obj)** | Off-screen 96×96 `SDL_TEXTUREACCESS_TARGET` texture; a bounds-framing orbit camera (`Mat4LookAt` + `Mat4Perspective`, 45° FOV, yaw 45° / pitch 22°, distance scaled to the mesh radius) projects the triangle soup with the engine's own `ProjectToScreen` math. Faces are flat-shaded with the default directional light (dir `{0,-1,0}`, ambient 0.35), painter-sorted by depth, rasterized via `SDL_RenderGeometry`, then the deduplicated `edge_lines` are stroked on top in a brighter tint — the same fills+wireframe look as the viewport at thumbnail scale. |
| **Material (.mat)** | A swatch texture cleared to the diffuse RGBA color with a subtle inner border so it reads as a card. |
| **Image** | The TextureLibrary's already-decoded GPU texture, borrowed — never owned by the cache. |

Thumbnails are cached in a `std::map<std::string, Entry>` where `Entry` tracks
ownership: mesh/material textures are created here and destroyed in `Shutdown()`;
image handles belong to the TextureLibrary. Because generation happens during the
panel's ImGui frame (when the renderer target is the window), each pass saves and
restores the current render target around `SDL_SetRenderTarget` so the ImGui blit is
never disturbed. `Application::Shutdown` clears panels before destroying libraries
and the window, so the cache's SDL textures are released while the renderer is alive.

### 7.3 The Panel: Search, Chips, Views, and Breadcrumbs (`ContentBrowserPanel`)

`ContentBrowserPanel` gains a `ThumbnailCache` (constructed with the SDL renderer +
`MeshLibrary` + the material/texture libraries it already had) and a view state:
`m_list_view`, `m_thumb_scale` (48–192 px), `m_search`, and `m_filter`. The toolbar
is now two rows:

- **Row 1** — Up button, **breadcrumbs** (each cumulative path segment is a small
  button that jumps straight to that folder), item count, Refresh, New Folder.
- **Row 2** — a **Grid/List** view toggle, a **Thumb** slider, a live **search**
  box, and the **category chips**. `PassesFilter` applies search to item names and
  the chip to files (folders always pass); when both are empty of matches the area
  says so instead of the generic "Empty folder" hint.

The grid keeps its responsive column layout but the cell size follows
`m_thumb_scale`; meshes and materials draw their off-screen thumbnails, images keep
their aspect-fit preview, and everything else falls back to the per-type badge
(`BadgeColor`, which now also knows `Audio`). The new **list view** (`DrawListRow`)
renders a full-width selectable per item with a small preview, the name, the type
label, and a human-readable file size (`HumanSize`), while preserving every existing
interaction: selection, double-click open, context menus (rename/duplicate/delete),
and the `PREFAB` / `MESH` / `MATERIAL` / `TEXTURE` drag payloads.

### 7.4 Testability and Verification

The `phase31_asset_catalog_test` harness links standalone against `AssetCatalog.h`
and verifies extension classification (meshes, materials, images, audio, scenes,
prefabs via the `.prefab.json` convention, unknowns), chip pass/fail logic with
folders always visible, case-insensitive search matching, and breadcrumb
segmentation. The engine rebuilds clean and the smoke run stays alive with the
thumbnail cache, list view, search, chips, and breadcrumbs wired.

---

## 8. Phase 32 — Integrated Lua Scripting IDE

The script subsystem before Phase 32 was two disjoint halves: a code editor that
wrote `.lua` files and a runtime that bound them on play. Phase 32 closes the loop
with an interactive loop of its own — a Lua REPL in the console, and real-time
editing hooks that push saved edits straight into a running play session.

### 8.1 The REPL Engine: `ScriptEngine::Execute` (`src/script/ScriptEngine.{h,cpp}`)

The REPL is not a second binding path but a *second Lua state* owned by the engine,
created lazily on the first snippet and kept alive for the whole editor run:

- **Persistent scratchpad** — definitions survive play sessions, so `function` and
  `global` assignments made in the console are still there after Stop/Start. The
  state is independent of `m_lua` (the play VM), so snippets can never corrupt a
  live session's environment table.
- **Chained `_ENV`** — the scratchpad environment is a fresh table whose metatable
  resolves through the same `Singe.EngineApi` registry table gameplay scripts use,
  which in turn chains to `_G`. `Vector3`, `Audio`, `print` and the full stdlib are
  visible; nothing written in the REPL leaks into any entity's environment.
- **`scene` bindings** — a `scene` table is bound to the active scene each call:
  `count()`, `get(i)` (1-based), `find(name)`, and `name`. Entities are the exact
  `Singe.Entity` userdata gameplay scripts see, so `scene.find("Cube").transform.position.y = 5`
  mutates the real transform in place. The pointer is rebound on every `Execute`,
  mirroring the `g_audio_manager` observer pattern, so the REPL always addresses
  the current scene and never a torn-down play session.
- **Output routing** — `print()` funnels into the Console sink through the same
  `LuaPrint` handler as play sessions; a successful chunk's return values are echoed
  as `=> value` (one `tostring()` per value, mirroring stdlib semantics) so
  `return 2+2` behaves like a classic REPL; compile and runtime errors are returned
  to the caller and logged as Error.

Compilation uses `luaL_loadbuffer` + `lua_setupvalue(1)` to install the scratchpad
environment before `lua_pcall(LUA_MULTRET)`, and the stack is left clean after every
path (success and error) — the same discipline the entity binding path uses.

### 8.2 The Console Command Line: `ConsolePanel` (`src/editor/ConsolePanel.{h,cpp}`)

The log pane gains a REPL row beneath it. Enter submits the line through a
`std::function<void(const std::string&)> on_execute` hook, Up/Down walk a per-run
input history (indexing `[0..size]`, `size` meaning "editing fresh"), Escape clears
the current line, and `SetKeyboardFocusHere(-1)` keeps the field focused after a
submit so the developer can keep typing. `Application::Init` wires the hook to
`m_script_engine->Execute(*m_scene, code, error)`; a REPL error surfaces in the
engine status line while the Console sink itself carries the full output.

### 8.3 Real-Time Save and Hot Reload: `ScriptEditorPanel` (`src/editor/ScriptEditorPanel.{h,cpp}`)

The existing Save / Save & Reload path was manual. Two hooks make it automatic:

- **Auto-save on blur** — an **Auto-save** checkbox (default on) tracks whether the
  code window held focus; the moment it loses it, a dirty buffer is written and the
  existing `ReloadSession` callback is fired, so leaving the editor while in play
  mode applies the change to the live session immediately.
- **Disk watcher** — the panel records the open file's mtime on every open and
  save. Each frame it stats the file again; a strictly-newer mtime with differing
  content means an external edit (another tool, a git checkout). If the buffer is
  clean the new text is adopted and the live session hot-reloaded; if it is dirty
  the change is *not* clobbered — it is surfaced in the status line instead.

Both hooks reuse `ReloadCallback`, which the Application gates on `EngineState::Play`,
so outside play mode saving is a no-op reload (the session binds fresh on the next
Enter Play Mode).

### 8.4 Testability and Verification

`ScriptEngine::Execute` shares the exact userdata, metatable, and print plumbing
the play session uses, so the verified bindings carry over. The engine rebuilds
clean; the editor smoke run stays alive with the REPL state, console command line,
and the auto-save/external-reload hooks wired and an empty log.

---

## 9. Phase 33 — True Workspace Layouts, Tabbed Mini-IDE & Theme

Phase 33 completes the editor shell: workspace presets become real, state-driven
dock layouts that own the whole script-authoring surface, and the script editor
becomes a multi-file tabbed mini-IDE with its own visual theme.

### 9.1 State-Driven Workspace Layouts (`src/core/WorkspaceManager.{h,cpp}`)

Up to Phase 32 the workspaces were a hybrid: the Script Editor sidebar docked into
the tree while the actual code window floated by name. Since the mini-IDE is now a
*single* window titled `"Script Editor"` (browser sidebar, tab bar, and code pane
all inside one `ImGui::Begin("Script Editor", ...)`), the workspace layouts can
own the entire authoring surface deterministically:

- **Level Design** docks the mini-IDE in the bottom-right, beside the bottom-left
  "Development Zone" (Stats + Material Editor / Console / History / Viewport Layout
  / Content Browser tabs), with Hierarchy over Stats on the left and the
  Inspector/Editor Settings right rail.
- **Scripting** raises the bottom strip to 38% and gives the mini-IDE the wide
  left slot (66%) with the Development Zone tabs beside it, so the whole IDE is
  part of the unified dock.
- **Shading & Assets** maximizes the viewport; the mini-IDE is left floating
  (`m_code_window_node = 0`).

`ApplyWorkspace(ws)` rebuilds the tree with `DockBuilder`, sets
`m_code_window_node` to the slot reserved for the mini-IDE, persists the active
workspace to `editor_layout.json`, and returns that node — which the Application
routes straight into `ScriptEditorPanel::RequestDockCodeWindow(node)` so the
window lands exactly in its workspace slot.

The new **Reset to Workspace Default** action (Workspace menu, View menu, and
command palette) calls `ResetToWorkspaceDefault()`: it clears any captured custom
layout and re-applies the *active* workspace's canonical layout, returning the
IDE node to route. It supersedes the old "Reset to Level Design", which always
jumped back to the Level Design preset regardless of the current workspace.

### 9.2 The Tabbed Mini-IDE (`src/editor/ScriptEditorPanel.{h,cpp}`)

The single `ScriptEditorPanel` window now hosts multiple open `.lua` scripts:

- **Tabs** — every tab owns a `Tab { path, TextEditor *editor, saved_text,
  last_write_time }`. Because each tab has its own `TextEditor`, undo stack, and
  canonical saved baseline, switching tabs never loses edits. The custom tab strip
  draws dirty tabs with a `*` prefix in amber and a per-tab `x` close button;
  closing a dirty tab opens a modal (Save / Discard / Cancel) that saves then
  closes, discards, or aborts.
- **Toolbar** — Save and Save & Reload (both gated on dirtiness), the Auto-save
  toggle, and the **Float / Dock to Workspace** toggle. "Float" pops the whole
  window out of the dock (`RequestDockCodeWindow(0)`); "Dock to Workspace"
  fires a `RedockCallback` that re-applies the current workspace in the
  Application and routes the returned IDE node back, so the window lands in its
  canonical slot.
- **Real-time hooks carry over, scoped to the active tab** — Ctrl+S, auto-save on
  window blur, and the disk watcher all operate on `m_active_tab`; a dirty buffer
  is still never clobbered by an external edit.

The Content Browser still opens `.lua` assets through the unchanged
`RequestOpen(path)`.

### 9.3 Mini-IDE Theme and the Line-Number Gutter (`third_party/ImGuiColorTextEdit`)

The vendored TextEditor stores palettes as `0xAABBGGRR` and blends every
`PaletteIndex` from `0` to `Max`. A new `PaletteIndex::LineNumberFill` (inserted
between `Breakpoint` and `LineNumber`) paints a contrasting gutter strip behind
the line numbers — drawn before the (semi-transparent) current-line highlight so
the gutter keeps its tone on every row. The index was added to all three static
palettes (dark `0xff1a1d22`, light `0xffe8e8ec`, retro `0xff000000`), so any
consumer compiling against the vendored header stays consistent.

The mini-IDE applies its own `MakeEditorPalette()`: a deep navy-black background
(`#12141B`), the engine accent (`#5B7CFA`) tinted current-line fill/edge, a
darker gutter with muted line numbers, `#82AAFF` known identifiers, and a curated
keyword/number/string set — giving the code pane a distinct identity from the
panels around it.

### 9.4 Testability and Verification

The engine rebuilds clean (the new `LineNumberFill` index flows through the
existing palette-blend loop). The editor smoke run stays alive with the workspace
presets, the tabbed IDE, the Float/Dock toggle, and the gutter-filled mini-theme
wired, an empty log, and no stray file edits on disk.

---

## 10. Phase 34 — Landscape & Topology Design Suite

Phase 34 gives the engine its first procedural terrain: a heightfield component,
a mesh builder, three sculpt kernels, a dedicated workspace whose viewport
replaces the transform gizmo with a live brush cursor, and full undo/serialization
support for the painted heights. Everything is editor-driven — landscapes are
authoring-only entities whose mesh is regenerated on demand.

### 10.1 The Heightfield Component (`src/core/Components.h`)

`LandscapeComponent` stores the terrain as data, not geometry:

- `enabled` — defaults to **false**, so an ordinary entity is never mistaken for
  a landscape (unlike a mesh path, "no terrain" must be the safe default).
- `resolution` / `size` — `resolution × resolution` quads spanning `size` world
  units, centered on the entity origin (local x/z in `[-size/2, +size/2]`).
- `base_height` — the y baseline the surface is painted on top of.
- `heights` — `(resolution+1)²` floats, row-major, the only editable state.
- `mesh` — a runtime-only `shared_ptr<Mesh>` generated from the heights; it is
  never serialized, and rebuilt whenever `mesh_dirty` is set.

### 10.2 Mesh Building and the Render Path (`src/core/Landscape.cpp`)

`LandscapeRebuildMesh` triangulates the grid with winding chosen so face normals
point +Y, adds a sparse **edge_lines** wireframe (every `res/8` grid line, riding
the actual surface heights) so the terrain reads through the editor's wireframe
pass, and derives `bounds_min/max` from the min/max height for picking. In the
Application's per-entity AABB refresh loop a dirty landscape is rebuilt **before**
its bounds are read, so rendering, picking, and gizmo ray-casts always see fresh
geometry. All render passes and the selection outline resolve meshes through a new
`ResolveEntityMesh`: landscape mesh if enabled, otherwise the mesh library
(`ResolveMesh`), with load failures still surfaced to the status bar.

### 10.3 Sculpt Kernels in Local Space

The brush hits the terrain in **world** space (ray-cast) but sculpts in the
landscape's **local** grid space:

- `LandscapeWorldToLocal` inverts the entity world matrix via the affine 4×4
  inverse, so rotated and non-uniformly scaled landscapes still sculpt correctly.
- The world-space brush radius is divided by `LandscapeWorldScale` (the average
  column scale) to get a radius in grid cells.
- **Raise** raises vertices with a `1 - smoothstep(r_cells - fade_in, r_cells, d)`
  falloff; **Smooth** blends each vertex toward its 4-neighbor average; **Flatten**
  blends toward the bilinear-sampled height at the brush center (`LandscapeSampleHeightLocal`).
- Each frame stamps `strength × dt` and marks the mesh dirty.

### 10.4 The Landscape Workspace and Viewport Override

A new `Workspace::Landscape` ("Landscape Mode") lays out a right rail (Landscape
panel on top, Inspector/Settings below), Hierarchy left, Development Zone + Stats
bottom, with the Script Editor floating, and round-trips via "landscape" in the
layout save/load. When the workspace is Landscape Mode **and** the brush target
entity has `landscape.enabled`, `UpdateLandscapeBrush` replaces `m_gizmo->Update`:
it builds the same camera-basis pick ray as `ComputeDropWorldPos`, ray-casts the
terrain (slab test → march → bisection), stores the hit, and while LMB is held
stamps the brush inside a single **"Sculpt Landscape"** undo transaction per
stroke. `RenderEditorOverlay` swaps the gizmo draw for `DrawLandscapeBrushCursor`:
a projected outer + inner-cap ring pair, a depth pole, and a center cross.

### 10.5 The LandscapePanel

`LandscapePanel` edits the shared `LandscapeBrushSettings` (tool, size, strength,
falloff, target id): size/strength/falloff sliders, a Raise / Smooth / Flatten
tool palette, a target combo with a "+" Create Landscape action, and an empty-state
create button. Creation routes through `Application::CreateLandscape`, which spawns
a green-tinted 64×64×40 m terrain ~6 m in front of the editor camera, pushes a
spawn undo record, selects it, and arms the brush.

### 10.6 Undo and Serialization

`CommandHistory::EntitySnapshot` now captures the landscape fields including a
copy of the heights vector, so each paint stroke (and spawn/delete) undoes
cleanly. `SceneSerializer` writes `enabled/resolution/size/base_height` and the
heights array, and on load drops the generated mesh and marks it dirty so the
next frame rebuilds it.

### 10.7 Testability and Verification

The engine rebuilds clean with both new translation units (`src/core/Landscape.cpp`,
`src/editor/LandscapePanel.cpp`) in the explicit CMake source list. The editor smoke
run stays alive with the landscape workspace, panel, brush cursor and sculpt
transaction wiring, an empty log, and no stray file edits on disk.

---

## 11. Phase 35 — Animation & Timeline Foundation

Phase 35 gives the engine its first **keyframe animation** pipeline: a data-only
`AnimationComponent` on `Entity`, a lean `Anim` sampling core, and a dedicated
**Sequencing** workspace whose track-based `TimelinePanel` replaces the viewport
as the authoring surface. Playback is driven by a single Application-owned global
clock shared with the Inspector through a `TimelineBridge`.

### 11.1 The Data Model (`src/core/Animation.h`)

Everything animation lives in one dependency-free header (no SDL/ImGui):

- **`AnimationKeyframe`** — `time` (seconds on the global clock) + `value[3]`
  (position, Euler-rotation in **degrees**, or scale). `operator==` is inline so
  key vectors compare by value in the undo diff.
- **`AnimationTrack`** — a time-sorted `std::vector<AnimationKeyframe>`. Ordering
  is maintained by `Anim::SetKeyframe`; the samplers assume ascending times.
- **`AnimationComponent`** — the three property tracks plus `loop` and `duration`
  (which mirrors `TrackDuration` = the longest key time across the tracks). It
  rides every `Entity` (`Entity.h`) and is **inert while empty**, so the cost is
  one object per entity, not one pipeline.
- **`TimelineState`** — the Application-owned clock: `time`, `duration`,
  `playing`, `loop`.
- **`TimelineBridge`** — the editor contract. The TimelinePanel and the
  Inspector's keyframe toggles read `state` and fire `on_play_pause` /
  `on_stop` / `on_scrub` / `on_set_keyframe` / `on_remove_keyframe` back into
  the Application, which owns the undo transactions and scene mutations.

### 11.2 Sampling: LERP, SLERP and the Euler Convention

`Anim::Apply` writes a pose and only overwrites properties that carry keys.

- **Position/Scale** use `SampleValue`: linear interpolation between the
  bracketing pair, first/last value held before the first and after the last
  key, and `fmod`-wrapped time when the track loops.
- **Rotation** uses `SampleRotation` — SLERP in quaternion space so orientations
  rotate through the shortest arc. The convention is the crux: the renderer
  applies Euler rotations as composite **Rx·Ry·Rz** (X, then Y, then Z), and the
  matching unit quaternion is the half-angle product **q = qx ⊗ qy ⊗ qz**. The
  engine stores its matrices transposed from the textbook standard, but that
  cancels for interpolation: keys travel Euler → quat → SLERP → quat → Euler, so
  only the **inverse pair** matters. `QuatToEuler` extracts
  `y = asin(R02)`, `x = atan2(-R12, R22)`, `z = atan2(-R01, R00)` from the
  standard matrix (gimbal lock folds yaw into roll). This was verified
  numerically with a standalone g++ harness over 200k random Euler triples.
- **Endpoint fidelity**: Euler angles are ambiguous (±180° representations of
  the same orientation). Landing *exactly* on a keyframe time therefore
  reproduces that key's stored Euler **verbatim**, so a pose recorded in the
  Inspector is never re-expressed as a visually different angle set mid-scrub.

### 11.3 The Sequencing Workspace (`src/core/WorkspaceManager.{h,cpp}`)

`Workspace::Timeline` ("Sequencing", layout key `"timeline"`) replaces the
center-stage viewport node with the **Timeline** window; the Hierarchy stays on
the left, the Inspector + Editor Settings on the right rail (the Inspector's
keyframe toggles pair with the lanes), and the Development Zone + Stats along
the bottom. Because the viewport is no longer part of the layout, the
Application **hides** it through a new `ViewportPanel` visibility flag
(`SetVisible`/`IsVisible` + early return in `OnImGuiRender`) and routes every
workspace switch through an `ApplyWorkspace` wrapper that applies the side
effects — viewport visible outside Sequencing, timeline playback stopped when
leaving it (and forced visible + re-synced around isolated play mode).

### 11.4 The TimelinePanel (`src/editor/TimelinePanel.{h,cpp}`)

The panel owns no scene state — it renders the shared clock and fires bridge
callbacks:

- **Transport**: Play/Pause toggle (restarts from 0 when parked at the end),
  Stop (rewind), a scrub slider over `[0, duration]`, a Duration drag and a Loop
  checkbox.
- **Lanes**: one row per transform property for the selected entity — label +
  key count + a "+" record button, then a strip drawn with the window draw list:
  dim background, a playhead line at `time/duration`, keyframe diamonds per key,
  and a hover crosshair. Left-click scrubs to the mouse time; right-clicking a
  diamond removes that key (bridge → undoable `RemoveTimelineKeyframe`).
- The playhead and scrub slider always reflect `TimelineState::time`, so the
  Inspector toggles and the panel stay in lockstep.

### 11.5 Playback, Recording and Undo (`src/core/Application.{h,cpp}`)

- `ApplyTimeline(dt)` runs in the editor **Update** stage: while `playing` the
  clock advances (wrap per Loop, otherwise clamp at `duration` and stop), and
  the sampled pose is written to **every** entity carrying keys. It is gated on
  `m_timeline_dirty` — set by play, scrub and record — so a paused timeline never
  stomps gizmo/Inspector edits. The gizmo interaction block is additionally
  gated off while the timeline plays or the viewport is hidden.
- `SetTimelineKeyframe`/`RemoveTimelineKeyframe` read the selected entity's
  current transform, mutate the track inside a single **"Set Keyframe"** /
  **"Remove Keyframe"** undo transaction, refresh `animation.duration`, and
  stretch the global `m_timeline.duration` when a key lands past its edge.
- `CommandHistory::EntitySnapshot` captures the loop flag, duration and the
  three key vectors (with the field-by-field diff comparison), so key edits undo
  like any other property edit. `SceneSerializer` emits an `"animation"` object
  only when keys exist (`{ loop, duration, position/rotation/scale: [{time,
  value:[x,y,z]}, …] }`) and re-sorts + recomputes duration on load.

### 11.6 Testability and Verification

The sampling math (Euler↔quat round-trip, SLERP midpoints, endpoint fidelity)
was verified with a standalone g++ harness before wiring, since no unit-test
framework is part of the tree. The engine rebuilds clean with the two new
translation units (`src/core/Animation.cpp`, `src/editor/TimelinePanel.cpp`) in
the explicit CMake source list. The editor smoke run stays alive with the
sequencing workspace, timeline panel, bridge and playback wiring, an empty log,
and no stray file edits on disk.

## 12. Phase 36 — Physics Materials & Collision Layer Matrix

Phase 36 turns physics from a single uniform interaction into a **tunable
material-and-layer system**. Up to now every overlapping collider pair resolved
identically: solid bodies blocked each other, triggers fired events, and there
was no way to say "projectiles ignore each other" or "this surface is bouncy".
Two additions close that gap. A **physics material** (.pmat asset) gives each
collider an authored friction/restitution pair. A **collision layer matrix**
gives each collider a membership bitmask over 16 named layers, and the physics
step only resolves pairs whose layers the matrix allows to interact. Both are
authorable from the editor, serialize with the scene, and (for the material) ride
the undo system.

### 12.1 The Physics Material (`src/core/PhysicsMaterial.{h,cpp}`)

A `PhysicsMaterial` is deliberately small — three fields, all of which appear in
the `.pmat` file:

```json
{
  "name": "Bouncy Rubber",
  "friction": 0.35,
  "restitution": 0.9
}
```

`friction` (0..1) is tangential grip: 0 is ice, 1 is maximum traction.
`restitution` (0..1) is bounciness: 0 is perfectly inelastic, 1 perfectly
elastic. Both are **authored data**, not solver inputs: the Phase 36 resolver is
still kinematic and script-driven, so the step does not yet consume friction or
restitution. They exist now so scene content is authored with them before a
future velocity-based solver arrives, and the **combination contract** is fixed
up front in `CombinePhysicsMaterials` (the rule every future solver must obey):

- `restitution = max(a.restitution, b.restitution)` — bounciness is dominated
  by the more elastic body.
- `friction = sqrt(a.friction * b.friction)` — the geometric mean, so a
  slippery surface always wins against a grippy one but the result never
  exceeds either input. `Default<->Default` is exactly the Default material
  (0.5 / 0.1).

The file format is the engine's own `json::Value` serializer
(`PhysicsMaterialToJson`/`PhysicsMaterialFromJson`), so assets round-trip
byte-for-byte and stay hand-editable. `PhysicsMaterialLibrary` mirrors
`MaterialLibrary`: a cache keyed by caller path, a `Load` that resolves a bare
filename against `assets/physics/` and parses on first touch, an always-present
`"__default__"` entry (friction 0.5, restitution 0.1), and `Create`/`Save` that
write through the same JSON path (creating the directory on demand) while
refreshing every cached copy of the asset. A collider with an empty
`physics_material` string uses the library default; the Inspector's combo
presents "Default" plus every `.pmat` currently in `assets/physics/`.

### 12.2 The Collision Layer Matrix (`src/core/CollisionMatrix.h`)

The matrix is a fixed **16×16** symmetric grid over named layers:

```cpp
struct CollisionMatrix {
    std::uint16_t rows[16];       // rows[i] = bitmask of layers i collides with
    std::string  names[16];
};
```

Layer 0 is "Default", with sensible authored names for the rest (Player,
Environment, Projectile, Enemy, Pickup, Water, Vehicle, … up to "Custom"). Every
`ColliderComponent` carries a **membership bitmask** — `unsigned int layers`
(default `1u`, meaning the Default layer alone) — and one body may belong to
several layers at once. `SetPair(a, b, on)` flips both symmetric entries (and,
unlike some engines, the **diagonal is user-controllable** — uncheck it so
projectiles pass through each other). `LayersInteract(a_mask, b_mask)` is the
per-pair query:

```text
LayersInteract(maskA, maskB)  ->  exists i in maskA, j in maskB with Collides(i, j)
```

A fresh matrix starts **all-on**, and every collider defaults to the Default
layer, so scenes that never touch layers behave byte-for-byte as before Phase 36
(backward compatible by construction). The matrix is **scene state**, not an
entity property: it lives on the `Scene`, ships in the scene file's
`"collision_matrix"` root block (one `layer_i` object per layer, `{ name,
mask }`), and — like the theme — has **no undo transaction**; the physics step
reads it every frame, so changes apply instantly.

### 12.3 The Physics Step (`src/core/PhysicsManager.cpp`)

`Step` iterates body pairs as before, and after the broad-phase AABB overlap
test inserts the Phase 36 gate:

```cpp
if (!scene.collision_matrix.LayersInteract(
        a.entity->collider.layers, b.entity->collider.layers))
    continue;
```

A rejected pair is skipped **entirely**: no solid separation and no trigger
events — the pair behaves as pure pass-through. This makes layer disabling a
cheap, complete off-switch rather than a filtering quirk; the pair never enters
the trigger/solid bookkeeping, so it can't fire stale overlap callbacks either.

### 12.4 The Collision Matrix Panel (`src/editor/CollisionMatrixPanel.{h,cpp}`)

The panel is a 17-column table (a layer-label column plus one column per layer).
Row labels are **inline-editable** — typing writes through to the matrix on
Enter / widget-release, and the buffer re-syncs from the matrix while idle, so a
scene load or "Reset All Pairs" always shows the fresh names. Pair cells are
symmetric checkboxes (toggling `(i, j)` flips the `(j, i)` entry too), with
tooltips naming the two layers (and "self-collision" on the diagonal). "Reset
All Pairs" re-enables every pair including the diagonal. The panel edits the
`Scene`'s matrix directly, so nothing has to invalidate caches or re-bake
anything — the next physics step reads the new bits. Like the other shared
editor panels it docks **first** inside the Development Zone and the
Shading & Assets right-rail tab groups, so it is one tab away in every
workspace without stealing focus, and joins the **View menu** and the Command
Palette ("Toggle Collision Matrix", View group).

### 12.5 The Inspector (`src/editor/InspectorPanel.{h,cpp}`)

The Collider section grows three Phase 36 controls:

- **Layers** — a combo popup of the 16 matrix layer names with checkboxes for
  membership (the preview joins the enabled names, "None" when empty); the
  bitmask commits through `CommitEdit("Edit Collider Layers", …)` as a single
  undo step.
- **Physics Material** — a combo of "Default" plus every `assets/physics/.pmat`
  (with a resolved Friction/Restitution readout under it), committing through
  `CommitEdit("Assign Physics Material", …)`. The readout is null-guarded: if
  the library is missing the panel simply degrades.
- **New Physics Material** — an inline Friction/Restitution slider pair plus a
  filename box; "Create" writes the `.pmat` (appending the extension when
  omitted) via `PhysicsMaterialLibrary::Create` and assigns it to the collider
  in the same transaction.

The Collider Reset also restores `layers = 1u` and clears the material string,
and `CommandHistory` snapshots both new fields, so layer/material edits undo and
redo cleanly like any other property change.

### 12.6 Serialization and Wiring

`SceneSerializer` writes each collider's `"layers"` (unsigned) and
`"physics_material"` (string) fields and a root `"collision_matrix"` block
(`layer_i: { name, mask }`); on load the block restores names and pair bitmasks
(missing block → the fresh all-on defaults). `Application` owns a single
`PhysicsMaterialLibrary` created beside the other libraries, passes it to the
Inspector, owns the `CollisionMatrixPanel`, registers its palette command and
View-menu toggle, and folds it into the play-mode panel save/restore. 
`CMakeLists.txt` gains `src/core/PhysicsMaterial.cpp` and
`src/editor/CollisionMatrixPanel.cpp` and bumps the version to `0.36.0`.

### 12.7 Testability and Verification

The matrix math (symmetry of `SetPair`, diagonal control, `LayersInteract` over
multi-layer masks, all-on defaults, `ResetAll`) and the combination rules
(restitution = max, geometric-mean friction, Default identity) were verified
with a standalone harness — the g++ scratch toolchain on this machine crashes in
the linker on `<filesystem>` (a toolchain defect, not an engine one), so the
scratch test covered the pure-math headers while the `.pmat`/filesystem paths
are exercised through the real MSVC build. The engine rebuilds clean with the
two new translation units in the explicit CMake source list. The editor smoke
run stays alive with the Collision Matrix panel, layer/material wiring, an empty
log, and no stray file edits on disk.

---

## 13. Phase 37 — Post-Processing & Environment Lighting Stack

The viewport output is no longer raw rasterized color: a full **environment
lighting stack** now sits between the painter's algorithm and the pixels the
user sees. The stack is global editor state (like the theme — deliberately **not**
part of the undo history), lives in a single serializable `EnvironmentSettings`
struct, and is split across three independent passes that Application wires
together each frame:

| Block | Where | Cost model |
|---|---|---|
| **Sky** (procedural skybox) | `EnvironmentFX::DrawSky` | cached texture, rebuilt only on pose/settings/region change |
| **Fog** (exponential height fog) | `env::HeightFog` in `EmitEntityTris` | per-triangle color blend, ~free |
| **Post** (bloom → grade → ACES → gamma) | `EnvironmentFX::PostProcess` | CPU loop at `working = region × post_scale` |

### 13.1 Why a CPU stack (and where it fits)

The engine has no GPU shaders — everything is CPU software rasterization into an
RGBA8888 `SDL_TEXTUREACCESS_TARGET` texture, blitted to screen by the SDL2
renderer (with 2× supersampling for AA, `kViewportSupersample`). A hardware
post/fog stack would need an OpenGL/Shader pipeline that does not exist, so both
features are implemented the way the rest of the engine is: **arithmetic on
buffers the engine already has**. The fog runs during triangle emission, where
the per-tri centroid and world Y are already computed. The sky and post runs are
texture-sized CPU passes over the same RGBA8888 buffer the rasterizer fills —
no new render path, no new dependency, and every cost scales linearly with the
working resolution the user can dial down (`post_scale`).

### 13.2 The settings asset (`src/core/Environment.{h,cpp}`)

`EnvironmentSettings` is a plain data struct with three blocks. **Sky** — the two
gradient stops (zenith `sky_color_top`, horizon `sky_color_horizon`), the sun
(color/intensity/glow radius/disk radius + `sky_sun_yaw`/`sky_sun_pitch` world
direction, and a `sky_star_intensity` knob). **Fog** — `fog_color`, `fog_density`,
`fog_height_falloff`, `fog_start`. **Post** — `post_scale`, the bloom trio
(threshold/strength/radius), exposure/gamma/saturation/contrast/temperature, and
the ACES toggle. Serialization mirrors the Phase 36 `.pmat` pattern exactly
(`json::Value`, `WritePretty`, per-key defaults on read) as `assets/environment/
default.env` with a `"type": "environment"` discriminator. Application owns the
single instance, writes the file with defaults on first launch, and reports
parse/save errors to the console. Edits apply **next frame** (the render reads
`m_environment` live) and never enter the undo stack — same philosophy as the
theme and viewport overlay settings.

### 13.3 Sky: procedural skybox with per-pose caching

`EnvironmentFX::DrawSky(renderer, env, cam_pos, fwd, right, up, fov, x, y, w, h)`
renders one region of the current render target. The camera basis comes from a
file-local `CameraBasis(pose)` helper that mirrors the view construction in
`BuildViewProjFromPose` (RotX(-pitch)·RotY(-yaw)): `forward = -Z` view axis,
`right`/`up` complete the right-handed frame. For each pixel the view ray is
`normalize(fwd + right·(px·tan(fov/2)·aspect) − up·(py·tan(fov/2)))`, then:

- **Gradient** — `env::SkyGradient`: blend horizon→top by `pow(up_component, 0.5)`;
  below the horizon the sky falls to 0.85× the horizon color (earth shadow).
- **Stars** — `env::SkyStars`: a deterministic integer-cell hash
  (`StarHash(x·2048, y·2048, z·2048)`) with a sparse threshold, so the pattern is
  stable while the camera moves without any RNG state.
- **Sun** — the sun direction is projected once into screen space
  (`(s_rgt/s_fwd)/(tan(fov/2)·aspect)`, etc.); per pixel the disk and glow are
  pure-arithmetic smoothstep falloffs on the screen-space distance
  (`1 − SmoothStep(...)`), added as `sun_color × intensity × (disk + 0.45·glow)`.

The texture is only rebuilt when `SignatureFromSettings` (FNV over the camera
basis + settings floats + region size) changes — while flying, the sky rebuilds
every frame (unavoidable, it is a function of the view), but when the view is
still it is one `SDL_RenderCopy` per frame. The Inspector camera preview reuses
the same cached pass with its own pose.

### 13.4 Fog: per-triangle exponential height fog

`env::HeightFog(density, height_falloff, fog_start, dist, cam_y, world_y)`
returns `1 − exp(−density·(dist − fog_start)·exp(−height_falloff·(world_y − cam_y)))`,
clamped to [0,1], 0 before `fog_start`. The sign convention matters: a triangle
**below** the camera (`world_y < cam_y`) sees `extinction > 1` and saturates to
full fog quickly, while a summit (`world_y > cam_y`) stays clear — valleys fill
with haze, hilltops stay crisp. `EmitEntityTris` now receives the camera position
(threaded through `RenderScenePass`, whose call sites pass `pose.position`) and
blends each triangle's shaded tint toward `fog_color` in **tint space**: for
textured triangles this still fogs correctly, because SDL multiplies the vertex
tint by the texture sample, so the fog-colored tint darkens and shifts the
textured surface too (a documented approximation — the fog is per-triangle, not
per-pixel).

### 13.5 Post: a CPU bloom + color-grade chain

`EnvironmentFX::PostProcess` runs after the scene pass but **before** the editor
overlay, so selection bounds and the gizmo stay crisp and ungraded. Pipeline:

1. `SDL_RenderReadPixels` of the region → box downsample to
   `working = region × post_scale` as linear RGB floats (`m_lin`).
2. **Bloom** — a bright pass (`lum − threshold`, clamped, normalized by lum)
   box-downsampled to half working res, separable gaussian blur (precomputed
   weights, `m_tmp` ping-pong), kept at half res and sampled **nearest** during
   the apply loop.
3. **Grade** — per-pixel `env::PostProcess`: add `strength × bloom` (pre-exposure,
   so highlights survive tone mapping), then exposure → temperature lerp → satur
   ation (luma mix) → contrast (0.5 pivot).
4. **Tone map + gamma** — ACES (Narkowicz fit) then `x^(1/γ)` collapsed into a
   **12-bit LUT** (`m_lut[4096]`, rebuilt only when γ/tonemap change), so the
   per-pixel loop is one table lookup per channel — no `powf`/`expf` anywhere in
   the hot path.
5. `SDL_UpdateTexture` on a streaming work texture, `SDL_RenderCopy` scaled back
   over the region (the SDL2 renderer's linear filtering does the upscale).

Post is **opt-in**: when `post_enabled` is false the pass returns without doing
work, keeping the editor at its previous idle cost; the working scale is the
thermal knob (0.5 → quarter the pixels of the supersampled target).

### 13.6 Workspace integration

The new **Environment & Shading panel** (`src/editor/EnvironmentPanel.{h,cpp}`)
edits the live settings through collapsible Sky / Fog / Post-Processing sections,
has Reload/Save against the `.env` asset with a console + inline error readout,
and a **Material Editor** shortcut that focuses the docked MaterialPanel. In the
**Shading & Assets** workspace it docks into the primary zone *behind* the
Material Editor (DockBuilder focuses the last-docked window, so order = active
tab) and again into the mat_bottom tab group; it is also first in the shared
Development Zone tab groups. `WorkspaceManager` gains `kEnvironmentWindow`; the
panel joins the View menu, the Command Palette (View group), and the play-mode
save/restore like every other panel.

### 13.7 Testability and Verification

The pure math (`env::Aces` monotonicity/bounds, `env::HeightFog` start-distance
gating and valley/summit asymmetry, `env::SunDirection` unit length,
`env::SkyGradient` stop behavior, `env::StarHash` determinism/sparseness, and
the full `env::PostProcess` chain) is verified by a standalone harness on the
g++ scratch toolchain — the file intentionally has **no** `<filesystem>`/SDL so
it avoids the toolchain's known linker defect, while the `.env` asset paths and
the SDL passes run through the real MSVC build. The engine rebuilds clean with
the three new translation units, and the editor smoke run stays alive with the
sky, fog and post passes active, an empty log, and no stray file edits on disk.

## 14. Phase 38 — Dedicated Material Authoring & Shading Mode

Phase 38 turns the `.mat` asset from a color tint plus a single texture slot
into a structured, authorable PBR material, and gives the editor a dedicated
place to see it: a **Material Preview viewport** that renders a test mesh under
the live environment lighting, reacting to every slider tick the moment it
happens.

### 14.1 The material schema (`src/core/Material.{h,cpp}`)

The `Material` struct now mirrors the channels a real PBR pipeline expects:

- **Albedo** — the RGBA tint (`color`), the albedo map (`texture`, resolved
  under `assets/textures/`), and `albedo_multiplier` (0–2) that scales the
  final albedo in the shade loop.
- **Normal** — `normal_texture` + `normal_strength`. Explicitly a **slot only**:
  the software rasterizer shades per-triangle from face normals, so this pair
  serializes and validates but has no CPU shading effect (documented in the
  struct so a future per-pixel pipeline consumes it unchanged).
- **Metallic / Roughness / AO** — each is a scalar (`metallic` 0–1, `roughness`
  0–1, `ao` 0–1) plus an optional **texture-map slot** (`*_texture`) and a
  **channel multiplier** (`*_multiplier`, 0–2). The map slots are likewise
  consumed by the scalar values in the software renderer but ready for a
  texture pipeline.

The `.mat` JSON round-trip writes and reads every field with defaults that keep
legacy files byte-compatible: an old file simply loads `metallic=0`,
`roughness=0.5`, `ao=1`, all multipliers at 1, and the pre-v0.38 `shininess`
knob stays serialized (superseded by `roughness`, never deleted).

`MaterialShading` is the renderer-facing view of a material: just the four
scalars the shade loop needs (`metallic`, `roughness`, `ao`,
`albedo_multiplier`), produced by `MaterialShading::FromMaterial`. `Application`
resolves it per entity through `ResolveEntityShading`, mirroring
`ResolveEntityTexture`'s material resolution (a `.mat` path on the entity wins;
no asset → neutral defaults).

### 14.2 The shading core (`src/render/MaterialCore.h`)

A pure, dependency-free header keeps the metallic/roughness math unit-testable:

- `pbr::SpecularPower(roughness)` — `1 + 256·(1−r)²`: a mirror (r→0) gets a
  257-power highlight, a matte surface (r→1) falls to power 1.
- `pbr::DielectricF0(metallic, albedo)` — `0.04 + metallic·(albedo−0.04)`:
  dielectrics reflect ~4% at normal incidence, metals reflect their albedo.
- `pbr::AmbientFloor(ambient, ao)` — the AO-dimmed ambient floor.
- `pbr::BlinnPhong(ndh, power)` and `pbr::SpecularWeight(roughness)`.

These are exercised by a standalone g++ harness in the scratch toolchain — the
file deliberately has no `<filesystem>`/SDL so it sidesteps the known linker
defect.

### 14.3 Software PBR shading (`src/core/Application.cpp`)

`EmitEntityTris` takes a `const MaterialShading &` and, per light, builds:

```
ambient = pbr::AmbientFloor(l.ambient, ao)
diffuse = max(0, n·(−l.dir)) · l.intensity
factor  = ambient + (1 − ambient)·diffuse·shadow
spec    = pbr::BlinnPhong(n·h, pbr::SpecularPower(roughness)) · intensity
          · pbr::SpecularWeight(roughness)
          · DielectricF0(metallic, albedo_channel)
```

The albedo multiplier scales `color×255` before the `Uint8` base tint is
derived, so it applies to textured and flat surfaces alike. The view and
half-angle vectors are guarded against degenerate lengths (camera on the
centroid, light exactly behind the view). `roughness=1` zeroes the specular
term entirely, keeping old scenes visually close to their pre-v0.38 flat-shaded
look while adding a subtle sheen at the new defaults.

`RenderScenePass`'s light-gathering loop is factored into `GatherSceneLights`,
which the preview reuses with a key-light fallback.

### 14.4 Live authoring (`MaterialLibrary::LiveUpdate`)

Edits in the Material Editor must appear instantly in both the scene and the
preview. `MaterialLibrary::LiveUpdate(filename, material)` mirrors `Save`'s key
bookkeeping (the bare-filename and `assets/materials/`-prefixed cache entries)
but stays **in memory** — every slider tick writes the working copy into the
cache, the next frame's `RenderScenePass` shades with it, and the user
explicitly commits with **Save Material**. This is the difference between an
edit-then-save workflow and a live material authoring session.

### 14.5 The Material Editor rework (`src/editor/MaterialPanel.{h,cpp}`)

The two-pane editor keeps its asset list, but the property pane is reorganized
into collapsible **Albedo / Normal / Metallic / Roughness / Ambient Occlusion**
sections. Each channel exposes its scalar slider, its texture-slot combo (None
+ every `assets/textures/` asset), and its multiplier. Every control marks the
working copy dirty and calls `PushLive` → `LiveUpdate`, so the scene re-shades
on the next frame without touching the file; **Save Material** persists and
**Revert** reloads the file copy.

The bottom inline "New Material" box is replaced by a **New Material…** button
that opens a modal **wizard** — file name, albedo tint, metallic, roughness —
which calls `MaterialLibrary::Create` and selects the new asset. The wizard is
also reachable from the Command Palette (**Create New Material**, Create group),
which first shows the panel.

### 14.6 The Material Preview viewport (`MaterialPreviewPanel` + Application)

The preview is the phase's centerpiece: a procedural **UV sphere** or
**cylinder** (generated once as function-local statics in the Application, each
with a full UV set so albedo maps wrap correctly) rendered into an off-screen
RGBA8888 target every frame:

1. An **orbit pose** is built from the panel's yaw/pitch/distance (looking at
   the origin; `CameraBasis`-consistent framing).
2. The **environment stack** runs first: `EnvironmentFX::DrawSky` behind the
   geometry, per-triangle height fog inside `EmitEntityTris`, and the full
   `PostProcess` chain (bloom + grade + ACES + gamma LUT) on top — the exact
   lighting the scene viewport uses.
3. **Lights** come from `GatherSceneLights` with a key-light fallback so the
   preview is always readable even in an unlit scene.
4. The **active material** is the Material Editor's selection read through the
   (live) library cache, so every slider edit is visible the next frame.

The panel draws the texture through the same `std::function<void*(int,int)>`
provider pattern as the Inspector's camera preview, and overlays a transparent
drag/zoom layer: drag orbits, the mouse wheel dollies (clamped 1–8), an
**Auto-rotate** toggle spins the framing at 25°/s, and **Reset** returns to the
default. For thermal efficiency the Application skips the software render
entirely when the window is a docked-inactive tab (`FrameActive()`), and the
panel is folded into the play-mode hide/restore so the game view stays clean.

### 14.7 Workspace & wiring

The Shading & Assets workspace splits its center column: the main viewport on
top and the **Material Preview** strip beneath it, both under the same
environment lighting — the scene on the left, the authored material on the
right, reacting in lockstep. "Material Preview" also docks as a back-tab in the
Development Zone of every workspace, "Toggle Material Preview" joins the View
menu and Command Palette, and the new source joins `CMakeLists.txt` with the
version bumped to `0.38.0`.

### 14.8 Verification

The pure math is harness-verified on the g++ scratch toolchain; the `.mat`
JSON, the library `LiveUpdate`, the panel/wizard UI, the preview render pass
and the workspace docking run through the real MSVC build — a clean rebuild, an
editor smoke run that stays alive with the PBR panel, wizard, preview viewport
and scene shading active, an empty log, and no stray file edits on disk.

## 15. Phase 39 — Mode-based Panel Isolation & Crash Fix

### 15.1 The Cross-Workspace UI Spillover Problem

Before Phase 39, the five workspace modes (Level Design, Landscape, Shading &
Assets, Sequencing, Scripting) controlled only the **dock layout** — which panels
docked where. But every panel was always *rendered*, regardless of the active
workspace. Switching to the Scripting workspace would still show the Landscape
panel floating behind the IDE; switching to Shading & Assets would still render
the Timeline. This cross-contamination made the workspace distinction feel
illusory.

### 15.2 Strict Per-Mode Panel Visibility

The fix is a single source of truth: `Application::SyncWorkspaceSideEffects(Workspace)`.

Every workspace mode now explicitly controls the visibility of every editor panel.
The five profiles are:

| Panel | Level Design | Landscape | Shading & Assets | Sequencing | Scripting |
|---|---|---|---|---|---|
| Viewport | ✓ | ✓ | — | ✓ | — |
| Hierarchy | ✓ | ✓ | — | ✓ | — |
| Inspector | ✓ | ✓ | — | ✓ | — |
| Content Browser | ✓ | — | ✓ | — | ✓ |
| Landscape Panel | — | ✓ | — | — | — |
| Material Editor | — | — | ✓ | — | — |
| Material Preview | — | — | ✓ | — | — |
| Environment Panel | — | — | ✓ | — | — |
| Timeline Panel | — | — | — | ✓ | — |
| Script Editor | — | — | — | — | ✓ |
| Console Panel | — | — | — | — | ✓ |

Panels outside the active mode are hidden. The function is called on every
workspace switch (from the menu bar, Command Palette, and workspace toolbar
buttons) and during `Init()` to set the initial visibility based on the
saved/default workspace. Individual panels never override this — the function
is the single source of truth.

### 15.3 Visibility Controls on Editor Panels

Each panel that participates in mode-based isolation now carries `m_visible`,
`SetVisible(bool)`, `IsVisible()`, and `ToggleVisible()`. These were already
present on `ViewportPanel`, `InspectorPanel`, `ContentBrowserPanel`,
`ScriptEditorPanel`, `ConsolePanel`, `MaterialPanel`, `MaterialPreviewPanel`,
and `EnvironmentPanel`. Phase 39 adds them to `SceneHierarchyPanel`,
`LandscapePanel`, `TimelinePanel`, and `StatsPanel`.

Every panel's `OnImGuiRender` now early-returns when invisible, skipping the
`ImGui::Begin()`/`End()` pair entirely. This avoids both the CPU cost of
building the ImGui draw lists and the visual artifact of a floating window
appearing outside the dock layout.

### 15.4 The Mid-Frame Dock-Tree Crash

The original `WorkspaceManager::ApplyWorkspace()` called `RebuildLayout()`
synchronously from menu-bar callbacks. This function calls
`DockBuilderRemoveNode()` to tear down the old dock tree and recreate it. But
these callbacks execute *during* the ImGui frame — specifically inside
`ImGui::BeginMainMenuBar()` — when earlier ImGui windows still reference the
old dock nodes. Destroying those nodes mid-frame causes stale-pointer crashes.

The fix defers the rebuild: `ApplyWorkspace()` now sets `m_needs_rebuild = true`
and returns immediately. `DrawDockspace()` runs at the top of the render loop
*before* any panel is submitted, which is the safe point to tear down and
re-create the tree. This ensures no panel holds a reference to a destroyed dock
node at the time of destruction.

### 15.5 The Stale Code-Window Node Return

When the rebuild is deferred, `m_code_window_node` still holds the value from
the *previous* workspace's layout. The caller — `ScriptEditorPanel::RequestDockCodeWindow`
— would store this stale ID and attempt to dock to a node that gets destroyed in
the next `RebuildLayout()`. The fix is to return 0 (floating) when the rebuild
is deferred, so the Script Editor docks to root for one frame. On the next
workspace switch, `m_code_window_node` will have been set by the preceding
`RebuildLayout()` and carries the correct value.

### 15.6 The Startup Segfault

Three panel pointers — `m_material_panel`, `m_viewport_layout_panel`, and
`m_profiler_panel` — were missing from the `Application` constructor initializer
list. They contained garbage pointer values when `SyncWorkspaceSideEffects()`
was called during `Init()`. The null guard (`if (m_material_panel)`) evaluated
the garbage as truthy, and the `SetVisible()` call dereferenced a wild pointer,
producing a SIGSEGV (exit code 139) before the engine even rendered its first
frame. The fix is to initialize all three to `nullptr`.

### 15.7 Verification

The pure crash fix is validated by a clean MSVC rebuild (only the benign
`LNK4044 /static` warning) and an editor smoke run: the process starts, runs
for 14 seconds with an empty log and no stray file edits on disk, and exits
cleanly on SIGTERM. The five workspace profiles are verified by switching
between all modes and confirming that each shows only its required panels.

---

## Phase 40 — Visual & UI Polish Sprint

Phase 40 elevates the engine's look and feel to professional standards. The work
spans four areas: a custom ImGui dark-slate theme, cleaner viewport defaults,
bilinear bloom sampling, and renderer null-safety hardening.

### 16.1 Custom Professional ImGui Theme

The editor's visual identity is defined in `Theme::ConfigureStyle()`
(`src/editor/Theme.cpp`). Six user-editable color tokens — `window_bg`,
`child_bg`, `popup_bg`, `frame_bg`, `text`, `accent` — drive every derived
color in the style through `Lerp` / `Darken` / `Lighten` / `Over` helpers. This
means a live edit to the accent token re-skins selection highlights, active tabs,
scrollbar grabs, and focus rings together, instead of leaving orphaned stock
colors behind.

The default palette was upgraded from warm charcoal to a cooler dark-slate scheme
inspired by professional DCC tools and Unreal Engine:

| Token | Old | New | Description |
|-------|-----|-----|-------------|
| `window_bg` | `#1B1D23` | `#191B20` | Cool slate window surface |
| `child_bg` | `#1F2128` | `#1E1F24` | Panel recesses |
| `popup_bg` | `#22252C` | `#222328` | Dropdown menus |
| `frame_bg` | `#24272E` | `#26272C` | Input frames, buttons |
| `text` | `#C9CDD6` | `#CCD1DB` | Slightly brighter primary text |
| `accent` | `#4D8DFF` | `#4480F5` | Cooler indigo-blue accent |

Style metrics (rounding, padding, spacing) were already tuned in Phase 37 and
remain unchanged. The palette persists to `editor_theme.json` via
`Theme::SaveThemeToFile()` and is restored on startup, so custom color schemes
survive restarts.

### 16.2 Clean Viewport Defaults

The viewport overlay system (`ViewportOverlaySettings`) controls what diagnostic
aids are drawn over the 3D scene. The defaults were tuned so that a fresh scene
opens with a clean, professional look that doesn't visually overpower geometry:

| Overlay | Old Default | New Default | Rationale |
|---------|-------------|-------------|-----------|
| `grid` | `true` | `true` | Essential for spatial orientation |
| `colliders` | `true` | `false` | Heavy volumes; enable on demand |
| `light_gizmos` | `true` | `true` | Lightweight; useful for lighting |
| `bounds` | `true` | `false` | Enable when selecting entities |
| `gizmo` | `true` | `true` | Required for transform editing |

The ground-grid line colors were softened from solid `(45, 45, 55)` to
semi-transparent `(38, 38, 48, 180)`, and the world axes from saturated
`(220, 70, 70)` / `(70, 110, 230)` to muted `(180, 60, 60, 200)` /
`(60, 90, 200, 200)` with reduced alpha.

### 16.3 Bilinear Bloom Sampling

The post-processing bloom pass reads from a half-resolution bright-pass buffer.
Previously, the buffer was sampled using nearest-neighbor integer division
(`i / 2`, `j / 2`), which produced visible block boundaries on gradients and
smooth sky surfaces.

The sampling was upgraded to bilinear interpolation: for each working-resolution
pixel, the fractional position in the half-res buffer is computed, the four
nearest texels are fetched, and the result is blended by the sub-pixel weights
`(1-fx)*(1-fy)`, `fx*(1-fy)`, `(1-fx)*fy`, `fx*fy`. This produces a smooth,
filmic glow with no hard boundaries.

Note: the engine's 3D geometry already uses GPU-side bilinear texture filtering
via `SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear)` set at texture upload
in `Texture.cpp`. The bloom upgrade brings the post-processing pipeline to the
same quality level.

### 16.4 Renderer Null-Safety

Every rendering function that receives an `SDL_Renderer*` was audited and given
an early-return null guard. Entity iteration loops in `RenderScenePass` and
`RenderEditorOverlay` now check `!entity_ptr` before dereferencing, preventing
crashes from any race or stale state. This was part of the Mode-switch crash
hardening carried over from the hotfix cycle.

### 16.5 Verification

Clean MSVC rebuild succeeds (benign `LNK4044 /static` + `M_PI` warnings only).
Smoke test: process stable for ~56 seconds, diagnostics file produced. Lit mode
runs at 17-18 FPS (PostProcess active). Unlit / Wireframe modes bypass the
~55 ms `SDL_RenderReadPixels` readback, pushing effective frame time well under
30 ms.

## Phase 42 — Surface & Material Painting

Phase 42 adds the engine's second landscape brush: painting. Where the
existing Raise/Lower/Flatten/Smooth brushes edit `LandscapeComponent::heights`,
Paint edits `LandscapeComponent::colors` — the same per-vertex buffer
`LandscapeRebuildMesh` was already copying into `Mesh::colors`, and
`EmitEntityTris` was already reading as the Gouraud-shaded albedo whenever a
mesh carries vertex colors (see Phase 34/37). No renderer changes were needed
to make painting *visible*; the work was entirely in getting a brush stroke
to reach that buffer correctly, and in presenting sculpting and painting as
two clearly distinct modes rather than two items buried in one tool list.

### 42.1 Two Paint Entry Points, One Palette

Painting can be driven from two places: a new viewport toolbar **Paint Mode**
button (next to the existing Snap/Gizmo-mode/Place toggles), and the
Landscape panel's own **Mode: Sculpt / Paint** switch. Both write the exact
same `Application` state — `m_paint_mode` (bool) and `m_paint_material_index`
(int) — so neither can show "active" while the other silently disagrees.

The material choices themselves — Grass, Stone, Metal, Dirt — live in exactly
one place: `kLandscapePaintPalette` in `Landscape.h`. Each entry pairs a
saturated, mutually distinct RGB swatch (so a stroke reads immediately with
no bound texture) with a `.mat` asset filename under `assets/materials/`.
Metal is the one preset with a non-trivial PBR split (metallic 0.9,
roughness 0.25) and a deliberately blue-tinted steel color, so it never reads
as "another gray" next to Stone. Before this phase, the Landscape panel had
its own independent five-preset list (Grass/Rock/Dirt/Snow/Sand) that the new
toolbar feature would otherwise have duplicated with slightly different
names and values; both now read from the one table.

### 42.2 UpdatePaintMode and the Shared Raycast Helpers

`Application::UpdatePaintMode(gf, dt)` follows the same per-frame viewport
override pattern as `UpdateLandscapeBrush` and `UpdateAssetPlacement`: while
the left mouse button is held over the viewport, it builds a pick ray from
the same camera-basis math (`ComputeDropWorldPos`'s convention), then tries
two raycasts in order:

1. **`RaycastAnyLandscape`** — every landscape entity in the scene, nearest
   hit wins. This is a genuinely new shared helper: the logic already existed
   inline inside `ComputeDropWorldPos` (for drop-position resolution), and
   rather than write a third near-identical copy for painting, it was
   extracted once and both call sites now share it.
2. **`RaycastAnyEntity`** — ray-vs-AABB (`RayAABB` + `TransformAABB`) over
   every non-landscape entity with a resolvable mesh. New in this phase.

A landscape hit runs a continuous blend through the existing
`LandscapeSculpt(..., SculptTool::Paint, ...)` — the same function the
Landscape panel's brush already called, reusing its radius/strength/falloff.
An entity hit is a discrete, one-shot swap of `material.color` and
`material.material_path`, wrapped in one `CommandHistory` undo transaction
per entity per stroke (re-touching the same entity while the button is still
held is a no-op, not a re-trigger).

### 42.3 Brush Expansion: Lower and Falloff Profile

Two additions to `LandscapeBrushSettings` apply to every sculpt/paint stroke,
not just painting:

- **`SculptTool::Lower`** — previously there was no way to push the surface
  down through the UI; only `Raise` existed, always adding to height.
- **`BrushFalloffProfile`** (`Smooth` / `Sharp`) — the brush's edge
  transition can now ease in with the existing cubic Smoothstep curve, or
  fall off at a constant linear rate (`LinearFalloff`, new in `Landscape.cpp`)
  for a harder, more deliberate edge. Both curves share the same `falloff`
  slider (fade-band width in `[0, r_cells]`); the profile only changes the
  *shape* of the transition within that band, not its size.

### 42.4 Two Bugs Found Getting Here

Two defects surfaced during this phase, both instructive:

**A `PushStyleColor`/`PopStyleColor` imbalance.** The Place and Paint toolbar
buttons read the live mode flag both before drawing the button (to decide
whether to push a highlight color) and after (to decide whether to pop it) —
but the button's own click handler flips that flag in between. Clicking a
button to turn a mode **on** therefore pushed nothing, then popped 2 colors
that were never pushed, tripping ImGui's `EndFrame()` color-stack assertion
and aborting the process. The fix is the same pattern the adjacent Snap
toggle already used correctly: capture the flag into a local `const bool`
*before* the button, and push/pop based on that captured value.

**A dispatch-priority conflict.** The per-frame viewport override chain
checked `IsLandscapeSculptMode()` ahead of `m_paint_mode`. That function
returns true ambiently whenever a landscape exists and is targeted (creating
one auto-targets it) and the Landscape workspace is active — not because the
user asked for sculpting, just because the conditions happen to be true. Paint
Mode is an explicit toolbar toggle. With the old ordering, turning Paint Mode
on while a landscape was targeted did nothing visible: every click was still
routed to the old sculpt brush (default tool Raise, height not color). The
fix reorders the chain so the explicit toggle wins.

### 42.5 Toolbar Accent Consistency

While investigating the button crash, six separate toolbar toggle states
(Render-mode pills, both Snap toggles, Gizmo mode selector, Place, Paint)
turned out to share one copy-pasted, low-contrast literal color
(`ImVec4(0.30, 0.30, 0.38, 1.0)`) for their "active" highlight — never the
theme's own accent color, despite `Theme::PushPrimaryButtonColor()` already
existing and being used correctly elsewhere (Load Heightmap, Save Material).
All six now use that helper, so every "this control is active" state in the
editor reads from one consistent, theme-aware accent instead of six
hand-picked near-duplicates.

### 42.6 Verification

Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only, both
pre-existing and unrelated). Process launches and runs without crashing
across every change in this phase. Painting confirmed visually working
end-to-end by direct user testing after the dispatch-order fix.

## Phase 43 — Editor Working Light

Phase 43 adds a second, independent ambient source that exists purely to keep
the editor readable, addressing a gap the existing lighting model always had:
with a directional light active, a shadowed or grazing-angle face renders at
roughly that light's own `ambient` value (`Components.h`, default `0.10`) —
close to black, and with *no* active light, everything skips shading entirely
and renders at flat full brightness (see Phase 34/37). The dark case is the
one that hurts editing: inspecting sculpted terrain, placed primitives, or a
freshly painted material stroke from an angle that happens to face away from
the sun.

### 43.1 Where It Sits in the Shading Model

`ShadeVertex` (`Application.cpp`) computes, per light, `factor = ambient +
(1 - ambient) * diffuse * shadow` and sums `factor * light.color` across every
active light. The Editor Working Light is not another entry in that loop —
it is a flat value seeded into `out_r/g/b` *before* the loop runs:

```cpp
out_r = out_g = out_b = pbr::AmbientFloor(fill_intensity, ao);
for (const RenderLight &l : lights) { ... }   // adds on top, as before
out_r = std::min(out_r, 1.0f);                // final clamp, unchanged
```

This placement is the whole design: because it sits outside the per-light
loop, `DirectionalShadowFactor` (which only ever attenuates a *light's*
diffuse contribution) has no opportunity to touch it. A face in full shadow
still gets `fill_intensity` (dimmed by the material's AO, same as a light's
own ambient floor, via the same `pbr::AmbientFloor` helper) — the fill isn't
a light that can be shadowed, it's closer to how a bounce-light or a studio
fill card works: it just always contributes.

### 43.2 Editor-Only, By Construction

The fill's actual value is resolved once, in `RenderScenePass` (and
independently in `RenderMaterialPreview`, which has its own light list):

```cpp
const float fill_intensity =
    (m_state == EngineState::Editor && m_environment.editor_fill_light_enabled)
        ? std::clamp(m_environment.editor_fill_light_intensity, 0.0f, 1.0f)
        : 0.0f;
```

Both are member functions with direct access to `m_state`, so the gate costs
nothing extra to wire up. In Play mode this is always `0.0f` — the fill never
reaches `EmitEntityTris`/`ShadeVertex` at all, so gameplay lighting is exactly
what the scene's actual lights produce, unaffected by the editing aid. There
is deliberately no separate "game view" render path to gate here: Editor and
Play share the same `RenderScenePass`, differing only in camera source and
overlay visibility, so one `m_state` check at the top of the function is
sufficient.

### 43.3 Settings and Persistence

Two new fields on `EnvironmentSettings` (`Environment.h`):
`editor_fill_light_enabled` (bool, default `true`) and
`editor_fill_light_intensity` (float, default `0.35`, UI range `0..1`).
Both round-trip through the existing `.env` JSON serializer
(`EnvironmentSettingsToJson`/`EnvironmentSettingsFromJson`) alongside sky,
fog, and post-processing, and are missing-key-safe: an older `.env` file
without them simply keeps the struct defaults on load, so this did not
require touching `assets/environment/default.env`.

The Environment & Shading panel (`EnvironmentPanel.cpp`) gained a new
**Editor Working Light** collapsing section, following the same
`DrawSectionHeader`/`DrawSlider` pattern as Sky/Fog/Post-Processing above it:
a checkbox to enable/disable, and — only while enabled — an intensity slider.

### 43.4 Verification

Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only, both
pre-existing). Process launches and runs without crashing with the fill wired
into both render call sites (main viewport and Material Preview).

## Phase 44 — Player Capsule Character Controller (Stage 2)

Phase 44 is the engine's first player-controlled character: a capsule that
walks, falls, and collides, driven by WASD and gravity rather than the
editor's fly-camera or a Lua script. It deliberately does not extend
`PhysicsManager` (Phase 14's generic AABB solid/solid resolver) -- that
system pushes whichever body has the *higher entity id* out of a collision,
a rule that makes sense for two inanimate props but not for "the player
should never be the one that gets shoved." The controller is its own
self-contained module instead.

### 44.1 `PlayerControllerComponent` and the Headless Module

`PlayerControllerComponent` (`Components.h`) is a plain data component like
every other one in this engine (`enabled` gate, in-class defaults):
`velocity` (world-space, persists across frames so falling behaves like
falling and not "instant terminal velocity every frame"), `radius`/`height`
(an upright capsule shape used purely for collision math, independent of
whatever mesh the entity renders -- the same decoupling `ColliderComponent`'s
own `extents` already has from mesh bounds), `move_speed`, and `grounded`.

The actual physics lives in `src/core/PlayerController.{h,cpp}`, a new module
following `Landscape.cpp`'s established shape: pure logic, no ImGui, no
`Application` state, no rendering -- just `Entity&`/`Scene&`/math in, mutated
transform/component out. `Application::UpdatePlayerController` is the only
caller, and its whole job is translating *editor/input* concerns (which keys
are down, which camera's yaw defines "forward") into the one value the
headless function actually needs: a world-space desired horizontal velocity.

### 44.2 One Resolver, Three Collider Types

The brief asked for collision against "Walls, Floors, Ramps" -- three shapes
that behave differently (block, support, and approximately-support). Rather
than write per-type logic, `ResolveEntityCollisions` (in the module's
anonymous namespace) resolves the player's AABB against every enabled Solid
collider **one axis at a time**: X, then Z, then Y, each tested against a
trial position that only changes along that one axis. This single generic
rule produces all three behaviors for free:

- A **Wall** in the player's path is caught on the X or Z pass. Only that
  axis clamps; the player's Y (falling, standing, whatever it was doing)
  is untouched, so a wall blocks walking without also arresting a fall.
- A **Floor** the player is falling onto is caught on the Y pass: the trial
  Y position overlaps the floor's box, `next.y < pos.y` (moving downward)
  identifies this as "landing on top" rather than "hitting a ceiling," and
  the player is clamped to the floor's top face with `grounded` set. This is
  not a special "floor" code path -- it is the exact same per-axis overlap
  test as the wall case, just on a different axis with a different sign of
  motion.
- A **Ramp** gets the same box treatment as a Wall, since `ColliderComponent`
  has no slope shape anywhere in this engine. This is an explicit,
  acknowledged approximation ("basic AABB collision," per the brief) --
  walking up a ramp will feel like a staircase of box collisions, not a true
  slope walk, until `ColliderComponent` grows a shape beyond box extents.

### 44.3 Landscape Ground Snap as the Floor of Last Resort

After entity collision resolves, the controller checks every landscape in
the scene via the same `LandscapeWorldToLocal`/`LandscapeSampleHeightLocal`
pair the sculpt/paint brushes already use (Phase 34/42), takes the highest
applicable ground height across all of them, and clamps the player up to it
if they're at or below it. This runs *after* entity collision specifically
so a Floor entity placed above the terrain (a platform) is respected --
landscape snap only ever raises the player, never overrides a surface
they're already legitimately standing on.

### 44.4 Play-Mode Camera Binding

`CaptureGameplayCamera` (Phase 27) already resolves the Play-mode view from
a dedicated scene Camera entity's own `transform`/`camera.yaw`/`camera.pitch`
-- not from the free-fly editor camera, which the brief's phrasing suggested
was the pre-existing behavior but, per direct inspection, was not: Play mode
was already static at wherever the Camera entity had been authored. Stage 2
makes that position dynamic: `Application::UpdatePlayerCameraFollow`
re-points the active camera entity's `transform.position` at the player's
position plus a fixed offset (5 units behind, 2.2 above), rotated by the
camera's own yaw so it trails consistently as the camera looks around. It
never touches yaw/pitch/fov, and only runs in Play state; `ExitPlayMode`'s
existing full-scene-snapshot restore (Phase 16.1) puts the camera back at
its authored position with no extra bookkeeping needed here.

WASD movement direction is built from that same active camera's yaw
(falling back to the editor camera's yaw if the scene has no camera entity
at all), reusing the exact yaw-only forward/right basis
`UpdateCameraControls`'s fly-cam movement already established -- one basis
formula, two consumers.

A known rough edge, disclosed rather than silently shipped: `EnterPlayMode`
captures a single fixed camera-transition blend target via
`BeginCameraTransition` at the moment Play starts, before the follow camera
has moved anything. Since the follow logic keeps repositioning the camera
entity throughout that ~0.6s blend, the transition can end with a small pop
once `GetActiveCameraPose` switches from the blend to calling
`CaptureGameplayCamera` directly. Fixing it properly would mean making the
transition's blend target itself follow the player during the blend, a
change to the transition system this phase didn't make.

### 44.5 Verification

The standalone g++ smoke-test harness used earlier this session for
isolated logic checks (the landscape heightmap loader, `BuildRampMesh`'s
winding) could not link in this environment on any of several attempts --
`ld` exits with code 116 and no diagnostic text at all, on freshly-produced
unsigned executables regardless of output location, consistent with the AV
interference already seen once this session. Rather than fight the
toolchain, verification ran the *actual shipped object code* instead: a
temporary self-test call in `Application::Init()` exercised
`PlayerControllerUpdate` against three synthetic, in-memory `Scene`/`Entity`
objects (gravity + landscape ground-snap; horizontal Wall collision, with a
ground-plane bug in the *test* itself caught and fixed along the way when
the first run showed the player falling through open space and phasing past
the wall entirely; landing atop a Floor suspended in open air), wrote
PASS/FAIL plus the exact numeric results to a file, confirmed all seven
checks passing with values matching hand-computed expectations exactly
(e.g. the wall test's player stopping at x=4.1, precisely `wall_face - radius`),
and was then removed. The editor launches and runs without crashing with the
controller wired into the real per-frame Play-mode update loop.

## Phase 45 — Paint Brush Cursor Feedback (Hotfix)

A small but user-visible gap in Phase 42's Paint Mode: there was no on-screen
indicator of where a paint stroke would land or how large the brush was.
Root cause was two related oversights in `Application::UpdatePaintMode`:

1. **The shared cursor state was never written.** The landscape brush-ring
   cursor (`m_landscape_brush_valid`/`m_landscape_brush_center`, drawn by
   `DrawLandscapeBrushCursor`) is shared infrastructure — Phase 44's Paint
   Mode was always meant to reuse it rather than invent a second cursor
   system, and the render-side dispatch already checks a single flag to
   decide gizmo vs. brush-ring. But only `UpdateLandscapeBrush` (the sculpt
   path) ever *wrote* to those two fields. Paint strokes ran against stale
   or default cursor state, and the dispatch condition itself
   (`IsLandscapeSculptMode()`) doesn't even consider Paint Mode, so outside
   the Landscape workspace it fell through to drawing the **transform
   gizmo** instead — an unrelated, distracting widget appearing over the
   selected entity while the user was trying to paint.
2. **The raycast itself was gated behind the mouse button.** `UpdatePaintMode`
   bundled "where does the ray hit" together with "apply the stroke" behind
   one `lmb`-gated early return, unlike `UpdateLandscapeBrush`, which
   raycasts on every hovered frame regardless of the mouse button and only
   gates the *sculpt application* behind it. Cursor feedback requires the
   former to run continuously; the fix separates them, matching the sculpt
   brush's structure.

Both fixed in `UpdatePaintMode`: the landscape raycast branch now sets
`m_landscape_brush_valid = true` / `m_landscape_brush_center = hit` on every
hovered frame (moving the `lmb` check to *after* the cursor update, gating
only the `LandscapeSculpt` call and the entity color-swap), and the
render-side dispatch condition became `IsLandscapeSculptMode() || m_paint_mode`
instead of just the former.

A second report — painted colors appearing to revert when switching between
the Landscape and Level Design workspaces — was investigated but not
reproduced in code: `SyncWorkspaceSideEffects` only toggles panel
visibility, nothing touches `LandscapeComponent::colors` or forces a stale
mesh rebuild. The leading theory is that the gizmo-popping-up bug above was
the actual cause of that report too (switching to Level Design while Paint
Mode was active would suddenly show the transform gizmo, which reasonably
reads as "something about my paint just changed" even though the underlying
data never moved) — flagged to the user to retest specifically now that the
gizmo no longer appears during Paint Mode, rather than assumed fixed without
evidence.

*End of textbook section covering versions v0.1.0-alpha through the architecture
refactor, the v0.30.0-alpha real-time performance profiler UI, the v0.31.0-alpha
advanced content browser & thumbnail generator, the v0.40.0-alpha visual &
UI polish sprint, the v0.41.0-alpha surface & material painting system, the
v0.42.0-alpha editor working light, the v0.43.0-alpha player capsule
character controller, and the v0.43.1-alpha paint brush cursor hotfix.*

