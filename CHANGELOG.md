# Changelog

## [0.12.3-alpha] — 2026-08-04

### Changed

- The script editor now renders the Lua buffer in a dedicated floating `Script Editor: <file>` window (resizable, minimizable, dockable) instead of a fixed pane beside the sidebar. The title embeds the file name (dirty buffer flags it with `*`), and a top toolbar holds Save and Save & Reload buttons plus the last-action status line; `ImGui::GetContentRegionAvail()` makes the `TextEditor` buffer fill the remaining space on any resize.
- Opening a file in the sidebar focuses the dedicated code window. Because the ImGui window identity changes with the file name, the window's position/size are remembered and re-applied across file switches so the retitled window stays put.
- `ScriptEditorPanel` now exposes `IsVisible()`/`ToggleVisible()`; F4 and the View-menu item toggle the whole script-editing UI (hiding the sidebar also closes the code window, re-showing restores it when a file is loaded).
- Version bump from 0.12.1-alpha to 0.12.3-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.12.1-alpha] — 2026-08-04

### Added

- In-editor script IDE: a dockable `ScriptEditorPanel` (`src/editor/ScriptEditorPanel.{h,cpp}`) built on vendored [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) (MIT, `third_party/ImGuiColorTextEdit/`). The left sidebar scans `assets/scripts/` dynamically (sorted `.lua` list with a "New script" field and a Refresh button); the right side is a syntax-highlighting Lua buffer on a monospace font. Unsaved edits mark the filename with an asterisk `*`; switching files with a dirty buffer opens a Save/Discard/Cancel dialog; `Ctrl+S` (or Save) writes the file.
- "Save & Reload": writes the current script to disk and, when a play session is live, hot-swaps it through `ScriptEngine::ReloadSession` so `OnStart` re-runs against the new text without leaving play mode. In the editor it saves and reports "loads on next Play".
- `ScriptEngine::ReloadSession(scene, errors)`: tears down the current session (releases registry refs, closes the Lua VM) and starts a fresh one, re-reading every script file from disk.
- Script editor is reachable from the View menu (`F4` toggles) and stays visible during play mode (floating over the game view) for live editing; it docks to the bottom strip on first launch.

### Changed

- `Application` owns a `ScriptEditorPanel`, renders it during both Editor and Play states, and hooks its reload callback to `ReloadSession` only while playing.
- Vendored `ImGuiColorTextEdit` TextEditor (BalazsJako fork, ~ca2f9f14) and wired it into CMake (`target_sources` + `third_party` include dir). Lua language definition ships with the vendored widget.
- Verified script-path persistence with a standalone SceneSerializer round-trip test (serialize → deserialize keeps `ScriptComponent.path`) and the reload path with a standalone harness (edit file between sessions, `ReloadSession`, OnStart re-runs with new constants, new OnUpdate active, broken-script reload surfaces an error). The 18-check ScriptEngine harness and the `player.lua` spin check still pass.
- Version bump from 0.12.0-alpha to 0.12.1-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.12.0-alpha] — 2026-08-04

### Added

- Embedded Lua 5.4.7 into the build: CMake now fetches Lua via FetchContent (`GIT_TAG v5.4.7`), compiles it as C, and links it into the engine. The standard library is opened on session start, so gameplay scripts can use `math`, `string`, `print`, etc.
- `ScriptEngine` (`src/script/ScriptEngine.{h,cpp}`): creates one Lua VM per play session and binds every entity whose `ScriptComponent.path` is non-empty. Each script runs in its own `_ENV` preloaded with `entity` (name/id/transform), `transform` (position/rotation/scale), `self` (the environment), and `Vector3`. `OnStart()` is called once on bind; `OnUpdate(dt)` runs each frame during play. Live `Vector3` userdata points straight into the entity's transform, so `transform.position.x = 1` mutates in place, while `transform.position = Vector3(1,2,3)` writes the whole vector. Vector arithmetic (`+ - * /`, unary minus, equality, `tostring`) and `norm`/`length`/`dot`/`cross` methods are exposed.
- `ScriptComponent`: a per-entity component holding a `script.path`. Serialized as `"script": {"path": "assets/scripts/player.lua"}` and loaded back by the scene serializer; empty means no script.
- Editor support: the Inspector's new "Script" section lets you set the script path (Apply Script / Clear) and shows a hint that scripts run during play mode.
- Example script `assets/scripts/player.lua` (spins the bound entity around its local Y axis), attached to the Octahedron scene entity for a ready-to-run demo.

### Changed

- `Application` now owns a `ScriptEngine`: `EnterPlayMode` starts a session (binding all scripted entities and calling `OnStart`; per-entity load/run errors are collected into the editor status line), the play loop calls `UpdateSession(dt)` each frame, and `ExitPlayMode` stops the session before restoring the pre-play scene snapshot. Re-entering play reloads scripts from disk, giving natural reload semantics.
- Bind failures (missing file, Lua syntax/runtime errors) no longer kill play mode: the entity is skipped or continues running while the error is reported, and runtime `OnUpdate` errors are printed to stderr.
- Version bump from 0.11.0-alpha to 0.12.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified with a standalone ScriptEngine harness (live-view mutation, whole-vector assignment, OnStart/OnUpdate lifecycle, environment isolation between entities, session teardown + reload, missing-file error reporting — all pass) and a smoke test of the rebuilt engine; the example `player.lua` spins a bound entity at the expected rate.

## [0.11.0-alpha] — 2026-08-04

### Added

- `BoundsComponent`: each entity mirrors its resolved mesh's local-space AABB (`Mesh::bounds_min/max`) as `local_min/local_max`, refreshed every render frame so it always tracks the geometry used for picking and collision. The box is derived from the mesh, not serialized.
- `TransformAABB` in `EngineMath.h`: transforms all 8 local corners of a box by an entity's world matrix and returns the exact world-space AABB (valid under any affine transform).
- `RayAABB` in `EngineMath.h`: slab-method ray/box intersection returning `t_near`/`t_far`, handling parallel slabs, origin-inside boxes (`t_near = 0`), and behind-camera rejection; the direction need not be normalized.

### Changed

- Picking is now an exact raycast instead of the old screen-rectangle heuristic. `GizmoController::Pick` casts the un-projected cursor ray (`MakeRay`) against every entity's world AABB via `RayAABB` and selects the nearest `t_near` hit (or clears selection when nothing is hit). Overlapping boxes are resolved by true depth along the ray rather than by averaged projected-corner depths, which mis-picked whenever projections overlapped.
- Hover state: `GizmoController` now raycasts every frame while the viewport is hovered and reports the entity under the cursor through `GetHoverEntity()`, independent of selection.
- The editor overlays wireframe world bounds boxes in pass 3: white for the selected entity (beside its amber outline) and light-blue for the hovered entity. Boxes are transformed to world space with `TransformAABB` before drawing (drawing local coordinates directly misaligns rotated/translated entities).
- Verified with the fuzz harness (all 810 gizmo frames pass, now including hover/pick/deselect ray-cast scenarios) and a live smoke test of the rebuilt engine.
- Version bump from 0.10.4-alpha to 0.11.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.10.4-alpha] — 2026-08-03

### Changed

- Anti-aliased the 3D viewport via supersampling. The SDL2 renderer API exposes no MSAA for off-screen render-target textures (it only multisamples a window's backbuffer, too late to help the composited 3D pass), so the off-screen target is now created at `kViewportSupersample = 2.0` times the physical window resolution (`logical × DisplayFramebufferScale × 2`, i.e. 4 samples per output pixel) and ImGui downscales it into the viewport rect with linear filtering. Triangle edges, wireframes, the grid, and the gizmo itself are all rasterized at 4x texel density and smoothed on the downscale.
- `RecreateViewportTarget()` now calls `SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear)`. The target previously used the SDL default nearest sampling, which dropped texels on the downscale and produced uneven, aliased pixels at fractional DPI scales; linear sampling makes the physical→logical mapping smooth at any ratio.
- The gizmo's `dpi_scale` (in `Run()` and `RenderViewportTarget()`) is now `DisplayFramebufferScale × kViewportSupersample`, keeping all gizmo screen metrics (`AXIS_PX`, `HANDLE_PX`, `RING_PX`, `RING_TOL`, `CENTER_PX`) a constant on-screen size while hit-testing and drawing run at supersampled resolution. The cursor-to-target mapping is unchanged because both the target size and the gizmo scale grow by the same factor.
- Verified with the existing fuzz harness (all 810 gizmo frames pass; Z-ring sweep still yields |roll| ≈ 90) and a live smoke test of the rebuilt engine.
- Version bump from 0.10.3-alpha to 0.10.4-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.10.3-alpha] — 2026-08-03

### Added

- The rotate gizmo now has three full 3D orthogonal rotation rings (X red, Y green, Z blue) plus an outer, screen-facing trackball ring. Each ring is drawn as a projected 48-segment polyline with the far half dimmed (×0.30) so its 3D orientation reads at any angle, and the hovered/active ring is brightened.
- Ring drags rotate the entity about the actual axis of the grabbed ring: the mouse ray is intersected with the ring's plane, the cursor angle is measured against an orthonormal `RingBasis(u, v, n)` frame, the delta is wrapped to [-π, π], and the rotation is composed on top of the drag-start orientation via `ApplyRotationAboutAxis` (with non-finite euler rejection). The trackball ring rotates about the camera forward for a pure screen-space spin.
- Hit-testing uses the minimum distance from the cursor to the ring's projected polyline (`RingScreenDistance`), which is robust even when the ellipse collapses to a line at edge-on angles.

### Fixed

- Fixed a mirror bug in `GizmoController::CameraBasis`: `up` and `fwd` had their z components negated relative to the `RotX(-pitch) * RotY(-yaw)` view matrix, so cursor rays were mirrored about the camera plane once the camera pitched. It was invisible for the old flat screen-space rotate ring and masked for axis picking, but it made the new ring drags collapse (~6° of rotation for a 90° sweep). The vectors are now read directly from the view matrix rows (`right {cy,0,-sy}`, `up {sp·sy,cp,sp·cy}`, `fwd {cp·sy,-sp,cp·cy}`). Verified with a standalone ray/unproject probe and the fuzz harness's new Z-ring sweep test (now yields |roll| ≈ 90).

### Changed

- The 3D viewport render target is now created at *physical* pixel resolution instead of ImGui logical size: `Run()` derives the scale from `io.DisplayFramebufferScale` and sizes the off-screen texture accordingly, so on high-DPI displays the 3D pass is rendered 1:1 with the framebuffer instead of being bilinearly upscaled (blurry).
- Gizmo screen metrics (`AXIS_PX`, `HANDLE_PX`, `RING_PX`, `RING_TOL`) are now declared in logical points and multiplied by a new `GizmoFrame::dpi_scale` in the controller, keeping handles a constant on-screen size while hit-testing/drawing run in physical viewport pixels.
- Version bump from 0.10.2-alpha to 0.10.3-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.10.2-alpha] — 2026-08-01

### Fixed

- Fixed an immediate crash when switching the gizmo mode via the toolbar buttons (Move/Rotate/Scale). The active-button style push/pop in `Application.cpp` was asymmetric: the highlight was pushed based on the mode state *before* the click but popped based on the state *after* the click. Clicking a button whose mode differed from the current one therefore called `ImGui::PopStyleColor(2)` without a matching push, underflowing ImGui's style-color stack and aborting the process via `IM_ASSERT_USER_ERROR` ("Calling PopStyleColor() too many times!"). The push and pop now both use a single pre-click `is_active` flag, so they are always balanced.
- Switching gizmo modes (toolbar buttons or the 1/2/3 hotkeys) now goes through a new `GizmoController::SetMode()` that cancels any in-progress drag (`m_dragging = false`, `m_drag_axis = -1`), so a mid-drag mode change can never leave stale cached axis/center values that the next mode's hit-testing re-reads.

### Changed

- Version bump from 0.10.1-alpha to 0.10.2-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.10.1-alpha] — 2026-08-01

### Fixed

- Hardened the viewport transform gizmo's Rotate and Scale writeback in `GizmoController.cpp` so a corrupt (non-finite) matrix decomposition can never poison an entity's transform:
  - Rotate mode now rejects non-finite euler output from `Mat4ExtractEuler` before writing it back into `rotation`, instead of propagating NaN/Inf into the render pipeline.
  - Scale mode now guards the axis-radius denominator (`m_radius_world`), clamps the multiplier to a sane range, and skips the drag when the radius is degenerate — a zero/tiny radius at extreme zoom could previously produce an infinite scale factor.
  - The gizmo arm radius (`m_radius_world`) is now clamped to a positive minimum when it is derived from camera distance/FOV.
- Verified with a headless fuzz harness that compiles the real `GizmoController` against stub ImGui/SDL headers: 810+ frames across all three gizmo modes, every demo entity, degenerate camera/entity positions, mid-drag mode switches, a poisoned-rotation start state, and the Draw overlay produced no crash and no non-finite transform values.

### Changed

- Version bump from 0.10.0-alpha to 0.10.1-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.10.0-alpha] — 2026-08-01

### Added

- `src/core/Mesh.h` / `Mesh.cpp` — CPU-side triangle-soup mesh with deduplicated wireframe edges and a local AABB, plus `MeshLibrary`, a path-keyed asset cache. Loads Wavefront `.obj` assets (positions + faces, fan triangulation, negative relative indices) with a `assets/meshes/` path fallback and a procedural `__builtin_cube__` primitive preserved as the default.
- `MeshComponent` (per-entity `mesh.path`) with scene-serialization round-trip, and Inspector "Mesh" UI: asset combo of discovered `.obj` files, free-text path, Apply Path / Reset to Cube.
- `src/editor/GizmoController.{h,cpp}` — viewport transform gizmo with Move (axis + planar drags), Rotate (camera-view-axis ring drag), and Scale (axis drag), screen-constant size derived from FOV, and click-to-pick selection via projected-AABB + center-depth tiebreak.
- New sample assets `assets/meshes/octahedron.obj`, `pyramid.obj`, and `icosahedron.obj`, plus Octahedron / Icosahedron / Pyramid demo entities in the default scene.
- Gizmo mode hotkeys `1`/`2`/`3` and Move/Rotate/Scale menu-bar buttons (editor-only).
- `EngineMath` helpers: `Vec3` arithmetic/dot/cross/length, `Mat4RotateAxis` (Rodrigues), `Mat4RotateOnly`, `Mat4ExtractEuler` (inverse of the X-Y-Z TRS composition).

### Changed

- `RenderViewportTarget()` rewritten around a single global painter's pass: every entity's triangles are collected, depth-sorted once across the whole scene, and batched into chunked `SDL_RenderGeometry` calls; the per-entity local depth sort from Phase 9 is gone so arbitrary overlapping meshes sort correctly.
- Per-entity wireframe rendering generalized from the cube to any mesh via the deduplicated edge list.
- POST_BUILD custom command copies `assets/` next to the executable so OBJ files resolve at runtime.
- Version bump from 0.9.1-alpha to 0.10.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.9.1-alpha] — 2026-08-01

### Fixed

- Editor panels (Hierarchy, Inspector, Stats) now return after exiting Play mode. Play mode force-undocks the viewport and stops submitting the dockspace, which left the editor panels' docking associations stale. `ExitPlayMode()` now explicitly un-isolates the viewport and forces the dock layout to rebuild on the next editor frame, re-docking every editor panel deterministically.

### Changed

- Version bump from 0.9.0-alpha to 0.9.1-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.9.0-alpha] — 2026-08-01

### Added

- Play/Stop runtime state machine (`enum class EngineState { Editor, Play }`) driving a toolbar Play button and a red Stop button.
- In-memory scene snapshot on Enter → Play (reuses the JSON serializer) and full scene graph restoration on Stop → Editor, discarding all runtime mutations (moves, spawns, deletes, reparenting).
- Runtime viewport isolation: in play mode the dockspace and editor panels (Hierarchy, Inspector, Stats) are hidden and the viewport detaches to fill the whole window below the menu bar as a true game view.
- Selection outlines and gizmos are suppressed while playing.
- `Esc` exits play mode in-game (still quits the editor outside of play); Stop button force-releases fly-mode mouse capture.
- `ViewportPanel::SetIsolated()` switches the panel between docked and fullscreen game-view rendering.

### Changed

- Version bump from 0.8.0-alpha to 0.9.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.8.0-alpha] — 2026-08-01

### Added

- `src/core/Json.h` / `Json.cpp` — minimal self-contained JSON value type, recursive-descent parser, and compact/pretty writer (no third-party dependency, no exceptions).
- `src/core/SceneSerializer.h` / `.cpp` — serializes the full scene graph (name, UUID, parent links, local transform, material, camera) to and from a JSON scene file.
- Persistent entity identity: each `Entity` now carries a version-4 UUID generated at creation; scene files link parents by UUID, so hierarchy survives any array reordering on load.
- `Scene::Clear()` and a const `GetEntities()` accessor to support reloading.
- **File → Save Scene** / **File → Open Scene** menu items (default path `assets/scenes/default.json`, directory auto-created), plus **File → Exit**.
- Right-aligned status message in the menu bar reporting save/open results and errors.

### Changed

- Version bump from 0.7.0-alpha to 0.8.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.7.0-alpha] — 2026-08-01

### Added

- Resizable main window: `SDL_WINDOW_RESIZABLE` and `SDL_WINDOW_ALLOW_HIGHDPI` flags applied at `SDL_CreateWindow` time.
- `Window::OnResize()` keeps the cached client size in sync with `SDL_WINDOWEVENT_SIZE_CHANGED`.
- `StatsPanel` reads window dimensions live from the `Window` so the Resolution readout stays correct after resizing.

### Changed

- Version bump from 0.6.2-alpha to 0.7.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

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

## [0.5.2-alpha] — 2026-07-09

### Fixed

- Added `#include <imgui_internal.h>` for `DockBuilder` function declarations, fixing linker error on `DockBuilderRemoveNode`, `DockBuilderAddNode`, `DockBuilderSplitNode`, `DockBuilderDockWindow`, and `DockBuilderFinish`.

### Changed

- Removed `NoDockingSplit` and `NoUndocking` dockspace flags to allow users to freely undock, rearrange, and float panels after the initial layout loads.

## [0.6.0-alpha] — 2026-07-09

### Added

- `src/core/Components.h` — `TransformComponent`, `TagComponent`, `MaterialComponent` structs.
- `src/core/Entity.h` — lightweight `Entity` struct holding id + all components.
- `src/core/Scene.h` / `Scene.cpp` — `Scene` class managing a vector of entities with `CreateEntity()` and `GetEntityById()`.
- Scene pre-populated with Camera, Directional Light, and Cube Object.

### Changed

- `SceneHierarchyPanel` now iterates `Scene::GetEntities()` instead of hardcoded arrays.
- `InspectorPanel` reads and writes directly to the selected entity's `TransformComponent` and `MaterialComponent` — no more local mock variables.
- `Application` owns a `Scene*` and passes it to both panels.

## [0.6.1-alpha] — 2026-07-09

### Added

- Off-screen SDL render target (`SDL_TEXTUREACCESS_TARGET`) created and dynamically resized when `ViewportPanel` dimensions change.
- `RecreateViewportTarget()` destroys and recreates the texture on size mismatch.
- `RenderViewportTarget()` draws a dark background, grid lines, crosshair axes, and a cube outline to the off-screen texture each frame.
- `ViewportPanel::SetTexture()` receives the live texture so `ImGui::Image()` blits the off-screen scene directly.

## [0.6.2-alpha] — 2026-07-09

### Added

- `RenderViewportTarget()` now reads entity `TransformComponent` and `MaterialComponent` from the active `Scene`.
- Each entity is drawn as a filled + outlined rectangle at its position offset from viewport center, scaled by its `scale` values, colored by its `MaterialComponent::color`.
- Inactive entities (`material.active == false`) render as outlines only.
- Changes to position, scale, or color in the InspectorPanel update the viewport in real-time.
