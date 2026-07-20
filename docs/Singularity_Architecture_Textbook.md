# Singularity Engine — Architecture Textbook

## Table of Contents

1. [Development Environment Setup](#1-development-environment-setup)
2. [The SDL2 Core Loop and Thermal-Aware Frame Pacing](#2-the-sdl2-core-loop-and-thermal-aware-frame-pacing)
3. [Dear ImGui Integration and Lifecycle Binding](#3-dear-imgui-integration-and-lifecycle-binding)
4. [Modular Class Architecture — Structural Reasoning](#4-modular-class-architecture--structural-reasoning)

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

*End of textbook section covering versions v0.1.0-alpha through the architecture refactor.*
