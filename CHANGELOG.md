# Changelog

## [0.30.0-alpha] — 2026-08-14

### Added

- **Profiler telemetry core** (`src/core/Profiler.h`, pure header-only, no SDL/ImGui dependency): a 120-frame rolling `Series` ring buffer per stage (Update / Render / UI / Physics) plus the frame total, each keeping running sum/max for O(1) latest/avg/peak reads. `StartFrame` / `BeginStage` / `EndStage` / `EndFrame` bracket the phases; `RecordResources(entities, draw_calls, memory_bytes)` snapshots live entity count, 3D draw calls, and resident memory (latest values readable even while paused); `SetPaused` freezes the buffers for frame inspection; `Clear` drops everything.
- **Run-loop instrumentation** (`Application::Run`): the Update stage wraps gameplay script updates + editor gizmo/camera interaction (the physics step runs nested as its own stage, play-mode only), the Render stage wraps the off-screen 3D pass (`RenderViewportTarget`) + Inspector camera preview, and the UI stage spans ImGui's NewFrame→Render plus the final SDL blit + Present. `RenderScenePass` / `RenderEditorOverlay` thread an `int &draw_calls` tally through every `DrawProjectedLine` / `FlushTriBatch` call, so the draw-call count reflects real `SDL_RenderDrawLine` / `SDL_RenderGeometry` load (resets each frame).
- **ProfilerPanel** (`src/editor/ProfilerPanel.{h,cpp}`): a dockable `EditorPanel` that plots frame time and each stage's ms trend (`ImGui::PlotLines`), with latest/avg/peak readouts per stage and entities / draw calls / memory MB resource plots. A **Pause/Resume** button freezes the buffers (with a "FROZEN" indicator), **Clear** empties the history. Toggled from the View menu ("Profiler") and the command palette (`Toggle Profiler`, `Pause/Resume Profiler`, `Clear Profiler Data`).
- **Memory estimates**: `MeshLibrary::ResidentBytes()` (map nodes + vector storage for positions/edge lines/UVs) and `TextureLibrary::ResidentBytes()` (w×h×4 per cached GPU texture), summed with the live entity count each frame.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 30 chapter ("Real-Time Performance Profiler UI") covering the pure telemetry core, run-loop stage instrumentation, draw-call tallying, the panel, and the harness.

### Verified

- New `phase30_profiler_test` harness (links standalone, pure Core): fresh-profiler state, one frame with a measurable Update stage + resource snapshot, ring-buffer wrap (150 frames → fixed 120-sample window, oldest/newest ordering), Pause freeze / Resume behavior, and Clear — 41/41 checks pass. Clean rebuild succeeds; editor smoke run stays alive with the Profiler wired and an empty log.

## [0.29.0-alpha] — 2026-08-13

### Added

- **Viewport header toolbar** (`Application::DrawViewportToolbar`, wired into the Viewport window through new `ViewportPanel::on_toolbar` / `on_overlay` callbacks): a docked header bar inside the 3D viewport — it belongs to the window, so it rides along with the Viewport across every workspace preset and custom layout. Row 1 holds segmented **Lit / Wireframe / Unlit** render-mode buttons and visibility checkboxes for the ground grid, physics colliders, light gizmos, bounding boxes, and the transform gizmo; row 2 holds the grid-snap quick toggle plus compact Translation/Rotation/Scale snap-increment inputs. The mode + overlay state lives in one pure struct, `ViewportOverlaySettings` (`src/core/ViewportOverlaySettings.h`), edited by the toolbar, the View menu, and the command palette and read by the render passes.
- **Render modes**: `RenderScenePass` (shared by the multi-viewport render and the Inspector camera preview) now branches on the mode — **Lit** is the classic lit fills + wireframe pass; **Wireframe** skips solid fills and draws only the mesh `edge_lines` pass; **Unlit** skips the light-gather loop so surfaces fall back to flat albedo and drops the wireframe pass. The ground grid stays independent and honors its own toggle.
- **Overlay toggles**: `RenderEditorOverlay` gates selection/hover bounds boxes, collider volumes, and the transform gizmo behind the matching toggles, and gains a **light gizmo** — for every active light a "sun" cross at the entity's world position plus an arrow along its direction, so the mesh-less default light entity is findable in the scene.
- **Viewport stats HUD** (`Application::DrawViewportHud`, drawn on top of the 3D image): a translucent corner readout of the active render mode, smoothed FPS, and the editor camera position. Editor-only — the overlay callback never fires in the isolated play view.
- **Menu & palette integration**: the View menu gains a "Render Mode" submenu plus the six overlay toggles; the command palette gains `Set Render Mode: Lit/Wireframe/Unlit` and `Toggle Grid / Colliders / Light Gizmos / Bounding Boxes / Transform Gizmo / Viewport HUD`.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 29 chapter ("Viewport Overlays & Gizmo Toggle Toolbar") covering the shared settings struct, render-mode gating, the light gizmo, the HUD, and the harness.

### Verified

- New `phase29_viewport_overlay_test` harness: settings defaults (Lit + all toggles on, HUD off), render-mode label mapping, the `Lit → Wireframe → Unlit → Lit` cycle, independent overlay flag flips, and `SnapSettings` step defaults/edits the toolbar writes — 29/29 checks pass. Clean rebuild succeeds; editor smoke run stays alive with the toolbar + HUD wired and an empty log.

## [0.28.0-alpha] — 2026-08-13

### Added

- **Status bar**: the bottom of every workspace is reserved with a 24 px strip (`WorkspaceManager::SetBottomBarHeight` shrinks the dock host/nodes; no persistence change — the strip rides along with the existing layout save). It shows frame time and an EMA-smoothed FPS, entity count, audio channels in use (`AudioManager::ActiveChannelCount`), enabled viewports (`CameraManager::EnabledCount`), and the current workspace name (`WorkspaceManager::WorkspaceName`). Toggleable from the View menu ("Status Bar").
- **Toast notifications** (`src/core/ToastManager.h` / `.cpp`): a new pure, headless-testable overlay manager. `Push(text, now_ms, lifetime_ms = 3500)` appends timed entries; repeating the *newest* message refreshes it in place; the visible list caps at 5 with oldest eviction; `Update(now_ms)` prunes expired toasts; `NewestFade` drives a 400 ms linear fade-out on the newest entry. The editor pushes toasts on save/load/new/play/stop/duplicate/delete and import batches, drawn top-right (`Application::DrawToasts`).
- **Viewport RMB context menu**: a quick right-click on the viewport (under the fly-mode drag threshold) opens a "Viewport Context" popup — Rename (inline undoable modal via `BeginEntityEdit`/`EndEntityEdit`), Duplicate, Delete, and Create (Empty Entity, Cube `Checker.mat`, Octahedron `octahedron.obj`, Directional Light, Camera, spawned at the editor camera). Dragging past the threshold still enters fly navigation.
- **Command palette refinements**: the palette toggle is now `Ctrl+P` (F1 and `Ctrl+Shift+P` remain), and it gained Save Scene, Open Scene, New Scene, Save Scene As…, Enter/Stop Play Mode, Duplicate/Delete Selected, and the Create Entity commands.
- **Hierarchy inline rename**: `SceneHierarchyPanel::DrawRenameRow` gives the selected entity an editable rename row — Enter or focus loss commits (undoable), Esc cancels, one-shot keyboard focus — plus a "Rename…" context-menu item.
- **Content Browser Duplicate**: context-menu "Duplicate" copies a file or folder as `<stem>_copy` (numeric suffixes `_copy_2`… on collision), recursive for folders, then refreshes the tree and selects the copy.
- **Workspace menu consolidation**: "Reset to Default Layout" and "Save Current Layout as Default" round out the workspace layer.

### Verified

- New `phase28_ui_consolidation_test` harness: toast empty/reject, stacking order and lifetimes, newest-repeat refresh in place vs. older-repeat stacking, MaxToasts cap with oldest eviction, `Update` pruning at exact expiry, `NewestFade` 1.0/linear/0 behavior, `CameraManager::EnabledCount` counting/flip/clear, and `WorkspaceManager::WorkspaceName` mapping (including unknown → "Level Design") — 40/40 checks pass. Clean rebuild succeeds; editor smoke run stays alive with the status bar, toasts, and context menu wired and an empty log.

## [0.27.0-alpha] — 2026-08-13

### Added

- **CameraManager / multi-viewport rendering**: the engine's single-camera assumption is replaced by `CameraManager` (`src/core/CameraManager.h` / `.cpp`) — a pure, headless-testable stack of camera entries. Each entry couples a camera *source* (the free-fly editor camera or a scene entity's `CameraComponent`) with a *viewport layout definition*: a normalized rect (x/y/w/h), a z-order (higher renders on top, stable for ties), an enabled flag, and a primary flag. The viewport renderer is now a multi-pass renderer: it walks the stack bottom-up by z-order and draws each enabled entry into its own region of the shared supersampled target, so split-screen / multi-camera scenes render in a single pass pipeline. Rendering, lights gathering, and wireframe were factored into a shared `RenderScenePass`; the editor overlays (selection, bounds boxes, colliders, gizmo) render only in the primary entry.
- **Primary entry / input ownership**: exactly one entry is primary (falling back to the topmost *enabled* entry when the primary is disabled or removed). It owns editor input: gizmo picking and mesh-drop targets resolve against its pixel rect instead of the whole texture, and its source camera's entity is hidden from its own pass so the camera never sees itself.
- **Viewport Layout panel** (`src/editor/ViewportLayoutPanel.h` / `.cpp`): docks into the development zone of every workspace (Material Editor → Console → History → Viewport Layout → Content Browser) and is reachable via the View menu and the command palette ("Toggle Viewport Layout"). It edits the camera stack live — label, source combo (Editor Camera / Scene Entity + entity id), X/Y/Width/Height normalized rect, Z order, Enabled, Primary radio, per-entry Remove, Add Entry (defaults to a right-half split), and Reset to Single Viewport.
- **Camera Preview in the Inspector**: the selected entity's `CameraComponent` is rendered into a small off-screen target each editor frame and shown live in the Camera section (with a primary readout). It reuses the shared scene pass (grid, lights, fills, wireframe) without any editor overlay.

### Verified

- New `phase27_camera_manager_test` harness: single-viewport defaults, normalized-rect → pixel conversion (rounding, off-screen/degenerate rejection, target clamping), stable z-ordered draw order, primary invariants (SetPrimary demotes all others; removing the primary promotes the topmost enabled entry; disabled primary falls back), empty/all-disabled handling — 46/46 checks pass. Clean rebuild succeeds; editor smoke run stays alive on the default single-viewport layout with an empty log.

## [0.26.0-alpha] — 2026-08-12

### Added

- **Audio & sound effects subsystem**: the engine now plays sound through SDL_mixer 2.8.2, fetched and built statically like SDL2 with only the dependency-free codecs enabled (WAV native + OGG Vorbis via SDL_mixer's bundled stb_vorbis). `AudioManager` (`src/core/AudioManager.h` / `.cpp`) opens a 44.1 kHz stereo device on a 16-channel pool, lazily loads and caches samples, and plays/stopped them via `Play(path, volume, loop)` / `Stop(path)` / `StopAll()` with a master-volume gain. It is optional at runtime: if the audio device can't open, it logs one error and every call degrades to a silent no-op.
- **AudioComponent**: entities carry `path`, `loop`, `volume`, `auto_play` — `auto_play` fires the sample once when play mode starts, so ambient/looping sounds need no script. The Inspector gained an Audio section (path input, volume slider, loop / auto-play checkboxes, Preview Play / Preview Stop buttons) with every edit undoable like the other components.
- **Lua `Audio` bridge**: gameplay scripts call `Audio.Play(path, volume?, loop?)` (returns the channel id or -1) and `Audio.Stop(path)`; the bindings degrade to a silent no-op when no manager is attached.
- **Serialization / undo / import**: scenes persist an `"audio"` block per entity (legacy scenes load with defaults), CommandHistory snapshots the four audio fields (capture/apply/no-op-compare), and the drag-drop AssetImporter routes `.wav`/`.ogg` into `assets/audio/`.
- **Demo audio asset**: `assets/audio/beep.wav` (generated 0.35 s fade-in/out sine), wired onto the Bouncer with `auto_play`, and `bouncer.lua`'s `OnCollisionEnter` triggers another beep via `Audio.Play(...)`.

### Verified

- New `phase26_audio_test` harness: component defaults, audio JSON round-trip alongside script/light, legacy-scene defaults, undo/redo of audio edits, no-op transaction detection, and `.wav`/`.ogg` import classification — 58/58 checks pass. Clean rebuild succeeds with SDL_mixer static + stb_vorbis; editor smoke run stays alive with the mixer open and demo audio wired.

## [0.25.0-alpha] — 2026-08-10

### Added

- **Free-Fly Editor Camera**: the editor now renders the viewport through a dedicated `EditorCamera` (position/pitch/yaw/fov in `src/core/EditorCamera.h`) that is independent from the scene's gameplay camera entities. Fly Mode activates by holding right-click while hovering the viewport — the OS cursor is captured and ImGui ignores the mouse for the session — then WASD (or arrows) strafes horizontally, Q/E rise and lower vertically, and mouse movement looks around (yaw/pitch, pitch clamped to ±89°). Scroll-wheel zoom adjusts the editor camera's FOV (10°–120°). Fly mode is editor-only: the isolated game view never captures the cursor, so the Stop button stays clickable during play.
- **Editor Settings sliders**: the Editor Settings panel gained a "Viewport Navigation" section with **Fly Speed** (1–40 world-units/sec) and **Rotation Sensitivity** (0.05–1.0 deg/pixel) sliders backing `EditorCameraSettings`, consumed by `UpdateCameraControls` (session-only, like the grid-snap steps).
- **Smooth Play/Stop camera transition**: toggling Play Mode now blends the view between the editor camera and the active gameplay camera entity over `kCameraTransitionDuration` (0.6 s). The blend is driven by the pure `CameraBlend` function — smoothstep easing, position/pitch/yaw/fov interpolation, yaw traveling the shortest arc and wrapping back into (-180, 180]. Entering play eases from the editor camera to the gameplay camera; stopping restores the scene snapshot and eases back to the editor camera. A toggle mid-blend starts from whatever the viewport currently shows, so the motion stays continuous.
- **Play-mode camera separation**: during play the viewport follows the gameplay camera entity (`camera.primary`), so scripts that move a camera drive the game view; the free-fly pose is untouched and preserved for the return trip.

### Fixed

- The viewport could previously be dragged into Fly Mode during play (the isolated view is hovered and the cursor is free), hijacking the view mid-session; Fly Mode is now gated to the editor state.
- Editor scroll-zoom previously wrote into the scene's camera entity (persisted into saved maps); it now tunes the editor camera only, leaving gameplay camera FOV untouched.

### Verified

- New `phase25_camera_test` harness: `CameraBlend` endpoints, midpoint, smoothstep easing (t=0.25 → 15.625%, t=0.75 → 84.375%), out-of-range clamping, yaw shortest-arc both directions, output yaw wrapping, identical-pose stability, and settings defaults within slider ranges — 42/42 checks pass. Clean rebuild succeeds; editor smoke run stays alive with the fly camera + transition wiring active.

## [0.24.0-alpha] — 2026-08-09

### Added

- **Directional lighting component**: entities carry a `DirectionalLightComponent` (`active`, `color`, `intensity`, `direction`, `ambient`, plus directional-shadow attenuation params `shadow_strength` / `shadow_bias` / `shadow_distance`). The forward-shading pass in `Application::EmitEntityTris` now shades each triangle from the scene's active lights — diffuse from the world-space face normal toward the light, an ambient floor, and the light's color/intensity — replacing the hardcoded light vector. With no active light the surfaces render at flat albedo.
- **Directional shadow attenuation**: per-triangle shadow ray against the world AABBs of the other visible mesh entities; occlusion darkens by `shadow_strength` and fades out as the blocker moves toward `shadow_distance`. Since the engine rasterizes on the CPU (SDL2, no z-buffer), the task's "shadow mapping *or* directional shadow attenuation parameters" route is implemented.
- **Default light guarantee**: `CreateDirectionalLightEntity` builds a lit, mesh-less "Directional Light" entity and `EnsureActiveLight` guarantees at least one active light after the default map setup, `New Scene`, and every `LoadScene` — freshly opened maps are never left in the dark.
- **Inspector & Hierarchy light management**: the Inspector edits the Directional Light component (color, intensity, direction, ambient, shadow params) through the same undo transactions as every other component; the Hierarchy's Scene Root context menu adds new `Add Directional Light` entities (undoable, auto-selected).
- **Level Design workspace refinement**: the Hierarchy now owns the full-height left rail (room to manage entities and light sources), with the Stats panel moved into the bottom "Development Zone" tab group alongside Material Editor / Console / History / Content Browser and the Script Editor sidebar.

### Fixed

- The wireframe overlay pass now honors `material.active`, so hidden-mesh entities (lights, disabled materials) no longer draw a wireframe in the viewport.
- Legacy scene files without a `light` block load cleanly (the component stays off, so old entities are not accidentally lights).

### Verified

- New `phase24_light_test` harness: light entity creation, `EnsureActiveLight` idempotence + inactive-only fallback, full light-field serialization round-trip, legacy-file compat, undo/redo of light edits, and `NewScene` camera+light composition — 43/43 checks pass. Prior regression harnesses (prefab-drop, undo, import, phase19) pass from the repo root; clean rebuild and editor smoke run stay alive.

## [0.23.1-alpha] — 2026-08-09

### Fixed

- **Prefab/mesh drop-instantiation crash**: dropping an asset onto the editor no longer crashes when the spawn reallocates `Scene`'s entity vector mid-iteration. `SceneHierarchyPanel` now defers entity-creating mesh drops queued on tree rows (`PendingMeshSpawn` queue + `FlushPendingSpawns`, flushed after the `GetEntities()` range-for completes) instead of calling `CreateEntity` inside `DrawEntityNode`; Scene Root / detach-zone drops were already iteration-safe and stay immediate.
- **Robust drop payload processing**: every drop target (viewport `on_drop`, hierarchy rows, Scene Root, detach zone) now null/size-guards `type`/`payload`/`Data`/`DataSize` before reading, the viewport dispatcher no longer indexes `type[1]` on a short type string, `m_selection` is guarded, and `ProcessExternalDrops` skips empty queue entries — `.dat`/binary/unknown drops degrade to a status message instead of a crash.
- **Safe entity tree instantiation**: the recursive-descent JSON parser (`Json.cpp`) gains a `kMaxJsonDepth` nesting cap and `SceneSerializer::EntityTreeFromJson` a `kMaxTreeDepth` cap, so a hostile or hand-edited prefab nested thousands of levels deep is rejected cleanly ("nesting too deep") instead of overflowing the call stack.
- Version bump from 0.23.0-alpha to 0.23.1-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

### Verified

- Headless prefab-drop harness (`phase23.1_prefab_drop_test.cpp`) re-run against the fixed tree/parser: valid prefab spawn + drop-position write + `PushSpawn`/undo/redo, scene-mislabeled-as-prefab rejection, binary `.dat` rejection, malformed JSON and missing-root rejection, 64-deep tree spawn/depth/undo/redo, 40k-deep hostile JSON clean rejection, over-cap tree instantiation capped without crashing, prefab-under-parent parenting, and mesh-spawn undo/redo — all 22 checks pass. Prior phase regression harnesses (undo, material, import, phase19) pass from the repo root. Clean rebuild succeeds and the editor smoke run stays alive.

## [0.23.0-alpha] — 2026-08-09

### Added

- **OS file-drop ingestion** (`src/core/AssetImporter.{h,cpp}`): a dependency-free filesystem module (`ClassifyDir`/`Import`) that maps dropped files to `assets/` sub-folders by extension (`.obj`→`meshes`, `.mat`→`materials`, images→`textures`, `.lua`→`scripts`, `*.prefab.json`→`prefabs`, other `.json`→`scenes`, case-insensitive), normalizes target folders, resolves name collisions with `_1`/`_2` suffixes, and rejects the bare `assets/` root as a target. `Application` queues `SDL_DROPFILE` events each frame, routes them after the frame into the Content Browser's browsed folder (or by classification elsewhere), refreshes the browser, and flashes a transient "Imported N file(s)" overlay — new assets appear without a manual Refresh. Older SDL builds re-enable drops via `SDL_HINT_DROPFILES` (guarded, since SDL 2.30.x drops it).
- **Viewport drag-drop spawning**: the viewport now registers a custom drop target (`BeginDragDropTargetCustom`) over its image rect and accepts `PREFAB`, `MESH`, `MATERIAL` and `TEXTURE` payloads. Prefabs/meshes drop at the cursor's `y=0` ground point (new `SpawnMeshEntity`, `ComputeDropWorldPos` ray helpers matching the gizmo controller's camera basis) — spawned, selected, and undoable via `PushSpawn`; materials/textures assign to the current selection as undo transactions. OS-dropped mesh files that land over the viewport also spawn as entities.
- **Hierarchy/Content Browser drop extensions**: entity rows, the Scene Root header, and empty tree space accept `MESH`/`MATERIAL`/`TEXTURE` payloads — mesh assets instantiate as children (`InstantiateMeshAsset`), materials/textures assign to the target entity (undo-aware). Content Browser mesh assets drag with a `"MESH"` payload (alongside the existing `"PREFAB"`).

### Changed

- Version bump from 0.22.0-alpha to 0.23.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

### Fixed

- (none)

- Verified with a headless AssetImporter harness (pure filesystem, no engine deps): classification for every extension plus case-insensitivity, import into every sub-folder, `_1`/`_2` collision suffixes, nested targets, `assets/` root rejection, and missing-source rejection — all 23 checks pass. The engine rebuilds clean and the smoke run stays alive without crashing.

## [0.22.0-alpha] — 2026-08-08

### Added

- **Custom editor font**: `assets/fonts/Roboto-Regular.ttf` and `Roboto-Medium.ttf` are bundled and preferred by `Theme::LoadFonts` (`src/editor/Theme.cpp`) via a new `LoadFont` helper with a fallback chain (`assets/fonts/Roboto-*.ttf` → Segoe UI → Arial), so the editor renders in a modern sans-serif face with the exact same high-DPI pipeline as before. The mono font is unchanged.
- **Global Undo/Redo** (`src/core/CommandHistory.{h,cpp}`): a Command-pattern history shared by every editor panel that mutates the scene.
  - `Command` base + concrete commands: `EntityStateCommand` (whole-entity before/after snapshots), `DeleteEntityCommand` (subtree delete, restored byte-for-byte via `SceneSerializer::SerializeEntityTree`/`SpawnEntityTree`, live-id tracked through undo/redo cycles), and `SpawnEntityCommand` (undoable duplicate/asset spawn).
  - `CommandHistory` exposes `Execute` (apply now + record), `Push` (register an already-applied action), `Undo`/`Redo`/`Clear`, `PushSpawn`, `ExecuteDelete`, and `BeginEntityEdit`/`EndEntityEdit` transaction pairs (a `Begin` supersedes any dangling session; `End` pushes nothing when the entity state is unchanged). Stacks are capped at 100 steps.
  - `SceneSerializer` gains the public tree helpers `SerializeEntityTree` (components + children + root uuid) and `SpawnEntityTree` (re-materialize under a parent, restoring the uuid).
- **Wiring**: gizmo drags are undo transactions (new `on_drag_start`/`on_drag_end` callbacks on `GizmoController`, `IsDragging()` guard); `Ctrl+Z`/`Ctrl+Y` (and `Ctrl+Shift+Z`) undo/redo in editor mode, text-input- and drag-guarded; **Edit** menu (Undo/Redo/Duplicate Selected/Clear Undo History) and View → **History**; `Ctrl+D` duplicates, Hierarchy create/duplicate/delete/re-parent/prefab-spawn, and Inspector property edits (transform drags, color, collider, camera, mesh/script/material/parent combos, drag-drop assigns, Reset actions) all route through history.
- **History panel** (`src/editor/HistoryPanel.{h,cpp}`): read-only view of the undo/redo stacks with Undo/Redo/Clear buttons, docked as a tab in the development zone of every workspace (Material Editor → Console → History → Content Browser). Hidden during play mode like the other editor panels.
- **Content Browser thumbnails**: texture assets render a live aspect-correct image preview in their grid cell (`ImGui::Image` via the SDL2 backend), `.mat` assets show a rounded swatch of their diffuse color, and the cell height grows to 64px to fit; other types keep the colored per-type badge.
- **Workspace dropdown modernization**: the Workspace menu is grouped into **Workspace Presets** (with an active-workspace checkmark) and **Layout** sections with section headers and separators.

### Changed

- Inspector/SceneHierarchy/ContentBrowser constructors take the shared `CommandHistory*` (and `MaterialLibrary*`/`TextureLibrary*` for the Content Browser); `InspectorPanel` commits one undo step per user gesture via `IsItemDeactivatedAfterEdit`, not one per frame.
- WorkspaceManager docks the History panel in the development-zone tab group; Material Editor stays first so Content Browser remains the active tab.
- Version bump from 0.21.0-alpha to 0.22.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified with a headless undo/redo harness (Scene + SceneSerializer + Json + CommandHistory, no window): property-edit push/undo/redo, no-op edit pushes nothing, delete→undo re-spawns with state, undo/redo cycle stability, subtree delete/restore with re-parented children, duplicate spawn undo/redo, tree round-trip with uuid preservation, and Clear — all 25 checks pass. The engine rebuilds clean and the smoke run stays alive without crashing.

## [0.21.0-alpha] — 2026-08-08

### Added

- **`MaterialPanel`** (`src/editor/MaterialPanel.{h,cpp}`): a dedicated dockable **Material Editor** for inspecting, creating and modifying `.mat` assets. A two-pane layout — scrollable asset list on the left, property editor on the right — exposes the **diffuse color tint** (`ColorEdit4`), **texture slot** selection (None + every `assets/textures/` image), a **live `ImGui::Image` preview** with dimensions, and a shininess knob. Dirty edits are marked, then persisted by a primary **Save Material** button (with Revert); **Create** builds a fresh white `.mat` from a typed file name.
- **`MaterialLibrary::Save(filename, material)`** (`src/core/Material.{h,cpp}`): rewrites `assets/materials/<filename>` and refreshes every cached copy (both the bare-filename and prefixed keys) so meshes re-tint on the next rendered frame — no restart or asset reload needed. Reloading through a fresh library instance returns the edited values, so the .mat file stays the single source of truth.
- **Workspace integration**: the Material Editor is docked as the **primary authoring zone** of the **Shading & Assets** workspace (full right rail, with Inspector + Editor Settings + Content Browser tabbed beneath it); in **Level Design** and **Scripting** it joins the bottom development-zone tab group. It also gets a **Toggle Material Editor** command-palette entry and a View-menu item, and is hidden during play mode like the other editor panels.

### Changed

- **UI modernization pass** in `Theme::ConfigureStyle` (`src/editor/Theme.cpp`), giving the editor a breathable, professional studio feel:
  - **Metrics**: window padding 10→16, frame padding 7×5→8×6, item spacing 8×6→10×8, indent 18→22, scrollbar 13→14.
  - **Rounding** softened: window 8→12, child 6→8, frame 4→6, popup 8→12, scrollbar 8→10, tab 4→8; the selected-tab overline grows to 3px.
  - **Borders** toned down to hairline status: window/child/tab borders go to 0, relying on contrast and rounding instead of hard outlines.
  - **Palette**: default accent becomes a clearer studio blue (0x5B7CFA → 0x4D8DFF); button hover/active warm toward the accent, tab hover/selected tints and selection-header tints are strengthened, and borders/separators are softened.
- **`Theme::PushPrimaryButtonColor()` / `PopPrimaryButtonColor()`**: a reusable accent-styled button palette for primary actions (Save, Create), derived from the last configured accent token.

### Fixed

- (none)

- Version bump from 0.20.0-alpha to 0.21.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.20.0-alpha] — 2026-08-07

### Added

- **`WorkspaceManager`** (`src/core/WorkspaceManager.{h,cpp}`, renamed from `LayoutManager`): owns the full-screen DockBuilder node tree and three task-oriented workspaces — **Level Design** (default), **Scripting**, and **Shading & Assets** — switched via `ApplyWorkspace()`. `WorkspaceName()` provides the human-readable labels.
- **Workspace selector dropdown** in the main menu bar (replacing View → Layout): radio-checked items for the three workspaces, plus **Reset to Level Design** and **Save Current Layout as Default**. The command palette's `Layout` category is renamed **Workspace** and gains a "Switch to Shading & Assets Workspace" entry.
- **Tab grouping**: related panels now share tabbed dock nodes. The right-hand rail tabs Inspector over Editor Settings; the bottom zone tabs Content Browser over Console; the Shading & Assets workspace tabs Content Browser into the right rail and Console over Stats in the bottom zone. DockBuilder focuses the last-docked window, so tab order is deterministic.

### Changed

- `LayoutManager`/`Preset` renamed to `WorkspaceManager`/`Workspace`; `ApplyPreset` → `ApplyWorkspace`, `GetPreset` → `GetWorkspace`. Application's member is now `m_workspace_manager`.
- Workspace layouts reorganized for real-estate efficiency: Level Design moves the Console into the bottom zone (tabbed with Content Browser) and tabs Editor Settings into the right rail; Stats fills the left rail under Hierarchy.
- `editor_layout.json` stores the active mode under a new `workspace` key (`"level_design" | "scripting" | "shading_assets"`); loading falls back to the legacy `preset` key so v0.18/v0.19 files migrate.

### Fixed

- (none)

- Version bump from 0.19.0-alpha to 0.20.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.19.0-alpha] — 2026-08-07

### Added

- **Material assets (`.mat`)**: `Material` in `src/core/Material.h` (RGBA albedo tint, optional texture filename, `shininess` knob) with `MaterialToJson`/`MaterialFromJson` round-tripping through the engine's own `json::Value`. `MaterialLibrary` caches `.mat` files under `assets/materials/` (mirroring `MeshLibrary`: caller-path keys, `assets/materials/` fallback, a default material, and `Create()` writing new assets with on-demand directory creation).
- **stb_image integration**: `stb_image.h` v2.30 vendored under `third_party/stb/`, compiled once in `src/core/Texture.cpp`. `TextureLibrary` decodes BMP/PNG/JPG/etc. to RGBA8 and uploads cached `SDL_Texture`s (`SDL_CreateTextureFromSurface`, linear filtering, alpha blending); the library holds the renderer via `SetRenderer()` and destroys all GPU textures in `Application::Shutdown` before SDL teardown.
- **Mesh UVs**: `Mesh::uvs` parallel to `positions`. The built-in cube gets a unit quad per face; the OBJ loader parses `vt` records, `v/vt` face tokens, negative indices, and flips v to SDL's top-left origin. Meshes with any UV-less face keep empty uvs and fall back to flat shading.
- **Textured rendering**: `FillTri` carries per-vertex UVs + `SDL_Texture*`; `Application::ResolveEntityTexture` resolves an assigned `.mat` asset (its texture + tint) over the entity's own `texture_path`/`color`, and `DrawTriangles` batches by texture. Lighting shade still multiplies the tint so textures stay lit.
- **Editor wiring**: the Inspector's Material section gains a Material Asset combo, a **New Material** creator (seeds from the current albedo), a Texture combo with a live `ImGui::Image` preview, and drag-drop assignment; the Content Browser classifies `.mat` and image files as new `FileKind::Material`/`FileKind::Texture` (new badges, drag payloads `MATERIAL`/`TEXTURE`, status lines on double-click).
- **Persistence**: `MaterialComponent` gains `material_path`/`texture_path`, serialized under the existing `"material"` object; old scenes load unchanged.
- **Sample assets**: `assets/textures/checkerboard.bmp` (procedural 32×32 checker) and `assets/materials/Checker.mat`; the demo scene's cube references `Checker.mat`.

### Changed

- `Mesh` struct, `MaterialComponent`, and `EngineMath` gain the new `Vec2` UV type; OBJ face parsing now tracks position+UV pairs per corner.
- `Application::Init` creates `MaterialLibrary`/`TextureLibrary`; `Shutdown` tears the texture library down before the renderer/window quit.
- InspectorPanel and ContentBrowserPanel constructors take the new library pointers.

### Fixed

- (none)

- Version bump from 0.18.0-alpha to 0.19.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.

## [0.18.0-alpha] — 2026-08-07

### Added

- **Grid snapping for gizmo drags**: translate (axis and planar), rotate (ring and trackball), and scale drags in `GizmoController` now snap their final result to a configurable step. Snapping the final value (rather than the incremental delta) keeps relative input feel natural. Snapping is active when the setting toggle is on **or** Ctrl is held during the drag (`snap_active = enabled || Ctrl`).
- **`SnapSettings` struct** in `GizmoController.h` (`enabled`, `translation = 0.5`, `rotation = 15.0°`, `scale = 0.1`), owned by Application and wired into every `GizmoFrame` it builds. A `Snap: ON/OFF` toolbar button in the viewport flips the toggle (highlighted when on, tooltip documents the Ctrl override).
- **Editor Settings window**: the theme customizer is renamed to Editor Settings and gains a **Grid & Snapping** section — a `Snap to grid` checkbox plus `DragFloat`s for the translation/rotation/scale steps. Snap steps are session-only (not persisted); the theme tokens keep round-tripping through `editor_theme.json`.
- **Quick duplication**: `SceneSerializer::DuplicateEntity` clones an entity and its whole subtree as a sibling with fresh ids/uuids, implemented as an in-memory `EntityTreeToJson` → `EntityTreeFromJson` round-trip through the prefab machinery. Wired as **Ctrl+D** (Application, editor mode, text-input-guarded), a Hierarchy context-menu item, and a Hierarchy toolbar **Duplicate** button; the clone is selected so a follow-up Ctrl+D duplicates the newest copy.

### Changed

- `SettingsPanel` constructor now takes the live `SnapSettings*` alongside the theme colors; its class doc and window title drop "Theme" for "Editor Settings".
- Version bump from 0.17.0-alpha to 0.18.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified: a headless harness (Scene + SceneSerializer + Json, no window) covers duplication of a 3-level subtree — fresh ids/uuids, correct parenting, preserved components, identical world-matrix folding, and independence from the original — all 16 checks pass. The engine rebuilds clean and the smoke run (`timeout 5 ./build/debug/singularity-engine.exe`) exits with code 124 (clean timeout, no crash).

## [0.17.0-alpha] — 2026-08-06

### Added

- **Drag-and-drop re-parenting in the Hierarchy panel**: every entity row is now a drag source (payload type `"ENTITY"` carrying the entity id). Dropping a row onto another entity parents it there; dropping onto **Scene Root** or into empty tree space detaches it to the scene root. Dropping onto the dragged entity itself or onto one of its descendants is not offered as a target — that would form a cycle. A status line reports each reparent/detach (`Parented 'X' to 'Y'`, `Detached 'X' to scene root`). The existing prefab spawn drop (`"PREFAB"`) now lives on the Scene Root header, accepting both payload types in one target.

### Changed

- **`Scene::SetParent` robustness**: invalid reparent targets are rejected **before** the child is unlinked from its old parent. Previously a rejected reparent (self-parent, reparent into the entity's own subtree, or an unknown target id) had already stripped the child from its parent's `children` list, silently detaching it to the root. Now a rejected reparent leaves the hierarchy untouched — a no-op. The Inspector's Parent combo and the serializer are unaffected (they only ever pass valid targets); the drag-and-drop path is what benefits.
- The scene graph's core was already in place (parent/child links on `Entity`, recursive `ComputeWorldMatrix = ParentWorld * Local`, recursive deletion, cycle prevention in `SetParent`, tree-indented Hierarchy panel); this phase hardens it and makes the editor interaction complete. Parent/children intentionally stay on `Entity` as stable pointers (addresses are fixed by `unique_ptr` ownership) rather than moving into `TransformComponent` as ids — ids would require a scene lookup on every world-matrix query and risk dangling references across scene loads.
- Version bump from 0.16.1-alpha to 0.17.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified with a headless harness (Scene + EngineMath, no window) covering world-matrix folding through 1 and 2 parent levels, reparent/unparent bookkeeping, rejection of cycle/self/unknown reparents without detaching, and recursive deletion with no dangling child pointers — all 15 checks pass. Engine smoke run exits with code 124 (clean timeout, no crash).

## [0.16.1-alpha] — 2026-08-06

### Added

- **Play Mode isolation**: entering play mode now hides the non-essential editor windows — Script Editor, Content Browser, Console, and Inspector — so the isolated viewport is a clean game view with zero editor chrome. `Application::SavePlayModePanelState()` snapshots each panel's pre-play visibility and hides it; `ExitPlayMode` calls `RestorePlayModePanelState()` to put every panel back exactly as it was (hidden panels stay hidden, visible panels reappear) before the dock layout rebuild. The Hierarchy, Stats, and Settings windows are intentionally left alone during play.
- `InspectorPanel` gained a visibility API (`IsVisible`/`SetVisible`/`ToggleVisible`) matching the other panels, and its window now closes with the dock title-bar X (the `&m_visible` flag on `ImGui::Begin`).

### Changed

- The play-mode render branch no longer draws the Script Editor's floating code window over the game view; during play the viewport is the only thing rendered.
- **Window scaling**: the primary window grows from 1280×720 to **1580×1020** (+300px in each dimension) in `src/core/main.cpp` for a roomier dockspace canvas.
- Version bump from 0.16.0-alpha to 0.16.1-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified: the engine builds clean and the smoke run (`timeout 5 ./build/debug/singularity-engine.exe`) exits with code 124 (clean timeout, no crash).

## [0.16.0-alpha] — 2026-08-06

### Added

- **Engine Console** (`src/core/Console.{h,cpp}`): a shared, severity-tagged log sink. `Console::Write(level, text)` appends an entry; `ConsoleInfo`/`ConsoleWarning`/`ConsoleError` are the convenience wrappers used across the engine. The buffer is capped (oldest rows drop) and is fully cleared by `Console::Clear()`.
- **Console redirection** — external terminal windows are no longer needed:
  - **Lua `print()`** is replaced at VM setup (after `luaL_openlibs`) with a handler that `tostring()`s each argument tab-separated and funnels the line into the Console as Info. Scripts resolve `print` through their `_ENV -> engine API -> _G` chain, so one override reaches every scripted environment.
  - **ScriptEngine errors** (bind failures, Lua runtime exceptions from `OnUpdate` and the collision/trigger hooks) route to the Console as Error. A persistent runtime exception is logged once instead of every frame (`m_last_error_logged` dedupe).
  - **C `stdout`/`stderr`** (printf, `std::cout`, stray `fprintf`) are captured through two OS pipes created in `Application::Init`: `CreatePipe`, `SetStdHandle`, and `_dup2` wire both CRT fds into their own pipe, `setvbuf` disables buffering, and `Console::DrainPipes()` peeks + reads available bytes each frame on the UI thread (no blocking, no threads, no locks). Severity is approximated by stream (stdout -> Info, stderr -> Error); CRLF and partial-line buffering are handled. A redirect failure is non-fatal (direct writes still work).
- **Console Panel** (`src/editor/ConsolePanel.{h,cpp}`): dockable window rendering the console buffer with color-coded rows — Info white (theme text), Warning yellow, Error red. A **Clear** button empties the buffer; an **Auto-scroll** checkbox keeps the newest rows pinned in view (default on). Toggleable from the View menu and the Command Palette ("Toggle Console").
- **Engine lifecycle logging**: the Application now reports scene loads/saves/new-scenes (with entity counts), save failures, and play-mode enter/exit as Console rows, so the editor narrates its own operations.
- **Development Zone workspace**: both layout presets reserve a bottom region for asset + script work. Default = Content Browser | Script Editor | Stats side by side; Scripting = script sidebar | docked code window | Content Browser over Stats. The **Console** docks under the Hierarchy on the left rail in both presets, replacing the Content Browser's old left-rail slot.

### Changed

- `ScriptEngine` no longer writes to `stderr`; all diagnostics go through the Console. The `LayoutManager` preset node trees were restructured (Hierarchy over Console; Content Browser + Script Editor + Stats Development Zone; code window keeps its Scripting-slot docking).
- Version bump from 0.15.0-alpha to 0.16.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified headless (Scene + ScriptEngine + Console, no window): severity routing and Clear; Lua `print()` (chunk-level, `OnStart`, tab-separated args) routed as Info; an `OnUpdate` runtime error routed as Error exactly once; `StartRedirect` captures `printf` as Info and `fprintf(stderr)` as Error and splits multi-line output into rows. All 15 checks pass.

## [0.15.0-alpha] — 2026-08-05

### Added

- **`SceneManager`** (`src/core/SceneManager.{h,cpp}`): owns the engine's single active `Scene` and all file-backed transitions between maps. The Scene object is allocated once and rebuilt *in place* on every load, so every subsystem holding a `Scene*` (panels, gizmo, physics, script session) keeps a valid pointer across a scene switch. `LoadScene(path)` replaces the contents and tracks the active path/name; `SaveScene(path)` stamps map metadata; `NewScene()` starts from a blank map with a default camera. `Application` now routes `SaveScene`/`OpenScene`/`New Scene`/`Save Scene As` through it.
- **Map metadata**: scene files carry a `"meta"` block (`name`, `author`, `created`) that round-trips through `SceneSerializer`. `SceneManager` stamps the name from the file stem and the ISO-8601 creation date on first save, so files are self-describing.
- **Prefabs**: `SceneSerializer::SavePrefab(entity, path)` writes a single entity tree (root + `"children"`) as a `{ "prefab": true, "name", "root" }` `.prefab.json` document; `LoadPrefab(scene, path, parent)` instantiates it with **fresh UUIDs** (and fresh runtime ids) on every spawn, either at the scene root or under a chosen parent; `IsPrefabFile(path)` tells prefabs from scene files. Prefabs reuse the same per-entity component encoding as scene files.
- **Content Browser** (`src/editor/ContentBrowserPanel.{h,cpp}`): a dockable asset-management panel over `assets/` — a recursive folder tree on the left and a responsive file grid on the right, each item with a colored per-type badge (scene / prefab / script / mesh / folder). Double-click acts on the asset: folders navigate, `.json` scenes load as the active map, `.json` prefabs instantiate into the active scene, `.lua` opens in the Script Editor. A toolbar and per-item context menus provide create-folder, inline rename, and delete-with-confirmation (blocked outside `assets/`). The window is docked by name in both workspace presets (Hierarchy over Content Browser on the left rail).
- **Prefab authoring & instantiation in the editor**: the Hierarchy context menu gains "Save as Prefab..." (modal names the file under `assets/prefabs/`), a "Spawn Prefab..." button opens a picker of every prefab under `assets/`, and prefabs can be **dragged from the Content Browser onto the Hierarchy window** (drag payload `"PREFAB"`) to spawn an instance.
- **Scene management UI**: the File menu gains "New Scene" and "Save Scene As..." (a modal writes `assets/scenes/<name>.json` with path-sanitized names); the Command Palette gains "New Scene", "Save Scene As...", and "Toggle Content Browser". Sample assets ship under `assets/prefabs/Crate.prefab.json`.

### Changed

- `Application` owns a `SceneManager *m_scene_manager`; `m_scene` now aliases `m_scene_manager->GetScene()` and is no longer deleted separately. `ScriptEditorPanel::RequestOpen` became public so the Content Browser can open `.lua` files (the dirty-buffer dialog still runs first). The default scene's meta name is `"Default"`.
- Version bump from 0.14.0-alpha to 0.15.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified with a headless smoke test of the new pure logic (Scene + SceneSerializer + SceneManager, no window): scene save/load preserves entity count, hierarchy (via UUID), components, and metadata; `SaveScene` stamps name/date; `NewScene` resets to a camera-only map; `LoadScene` replaces contents and tracks path/name; prefabs save, classify, and instantiate with fresh UUIDs and preserved subtree/components/transforms.

## [0.14.0-alpha] — 2026-08-05

### Added

- **Physics-Scripting Bridge**: a new `ColliderComponent` (`src/core/Components.h`) gives every entity an axis-aligned box physics volume defined in local space by `center +/- extents` (defaults `{0,0,0}` / `{0.5,0.5,0.5}`, matching the cube primitive). It is **disabled by default** so only explicitly-colliding entities participate; `type` selects `Solid` (blocks other solids) or `Trigger` (pass-through ghost volume that raises events only). The component is serialized into scene files as a `"collider"` object (`enabled`, `type`, `center`, `extents`) by `SceneSerializer`.
- **`PhysicsManager`** (`src/core/PhysicsManager.{h,cpp}`): an AABB physics step run by `Application`'s play loop *after* the scripts' `OnUpdate(dt)`, so collisions reflect the transforms scripts just produced. Each frame it computes the world-space box of every enabled collider (rotation/scale/parenting applied via `TransformAABB`) and runs a broad-phase O(n²) pair test. Solid vs Solid pairs are **penetration-prevented**: the higher-id body is separated along the minimum-penetration axis (only unparented bodies are moved; parented ones report events only). Trigger-involved pairs are pass-through and never separated.
- **Edge-tracked events**: a per-pair overlap map inside `PhysicsManager` distinguishes fresh overlaps from continuing ones, so each pair fires `OnCollisionEnter`/`OnTriggerEnter` exactly once when it starts overlapping and `OnCollisionExit`/`OnTriggerExit` exactly once when it separates — never every frame. Trigger semantics: only the trigger volume raises the trigger hook (a solid overlapping a trigger is pass-through and hears nothing); a solid-solid pair notifies both bodies.
- **Lua collision/trigger hooks** in `ScriptEngine`: `OnCollisionEnter(other)`, `OnCollisionExit(other)`, `OnTriggerEnter(other)`, `OnTriggerExit(other)`. The four hooks are captured as registry refs during `BindEntity` (like `OnStart`/`OnUpdate`) and fired via the new `ScriptEngine::DispatchEvent(entity_id, event, other)`, which passes the other entity as a Lua `Entity` handle (`other.name`, `other.id`, `other.transform`). Hook runtime errors are reported like script errors. A reload (`Save & Reload`) re-binds them along with the rest of the session.
- **Inspector Collider section**: Enabled checkbox, Solid/Trigger combo, `Center` and `Extents` drags, and a Reset action — consistent with the other component headers.
- **Editor collider visualization**: the viewport draws every enabled collider's world AABB as a wireframe box in editor mode — green for Solid, cyan for Trigger — so volumes are visible without entering play.
- **Demo scene & scripts**: a `Wall` (solid) and `Trigger Zone` (cyan trigger) added to the default scene with a script-driven `Bouncer` cube (`assets/scripts/bouncer.lua`) that drives +X, crosses the trigger (printing one Enter and one Exit), and presses against the wall (one CollisionEnter, never penetrating); `assets/scripts/trigger.lua` prints zone entries/exits.

### Changed

- `Entity` gained a `ColliderComponent collider` member; `Application` owns a `PhysicsManager` and calls `Clear()` on entering play mode so no Enter/Exit edges leak between sessions.
- Version bump from 0.13.1-alpha to 0.14.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified with a headless smoke test of the physics bridge (Scene + ScriptEngine + PhysicsManager at 60 fps, no window): the bouncer stops at the wall face (`x == 5.0`), and each of TriggerEnter / TriggerExit / CollisionEnter prints exactly once.

## [0.13.1-alpha] — 2026-08-05

### Added

- Global **Command Palette** (`src/editor/CommandPalette.{h,cpp}`): a modal quick-launcher toggled with `Ctrl+Shift+P` (or View → Command Palette). It fuzzy-matches the editor's core actions across their category/label/shortcut text using a subsequence scorer that rewards prefixes, word boundaries, camel humps and consecutive runs; multi-word queries (e.g. "open editor") must match every token. Up/Down navigate, Enter or a click runs the command, Esc dismisses. The filter box grabs keyboard focus the instant the palette opens, so it is fully usable from the keyboard. Registered actions include Open Script Editor (F4), Toggle Theme Customizer, Reset UI Scale, Switch to Default/Scripting Workspace, Reset View to Default Workspace, Save Current Layout as Default, Save/Open Scene, and Enter Play Mode.
- **Live Theme Customizer** (`src/editor/SettingsPanel.{h,cpp}`): `Theme::Colors` exposes six editable color tokens (window/panel/popup/frame backgrounds, text, accent). `Theme::ConfigureStyle` now derives the *entire* style palette from those tokens (borders, hovers, tabs, scrollbars, focus rings, docking previews), so editing one token re-skins the editor coherently. The View → Theme Customizer window live-applies color edits, and Save Theme / Reset to Default persist or restore via `Theme::SaveThemeToFile`/`LoadThemeFromFile` (`editor_theme.json`, gitignored, loaded at startup before the first style pass).

### Changed

- `InspectorPanel` refactored into clean, consistently spaced collapsible component headers with subtle "ghost" action buttons on the header row (Transform Reset, Material Reset, Mesh Reset to Cube, Script Clear, Camera Reset) plus an identity header (tag rename + entity id). With no entity selected it shows a minimal centered placeholder instead of empty controls.
- `Theme::ConfigureStyle(float ui_scale)` became `Theme::ConfigureStyle(float ui_scale, const Colors &colors)`; `Application` owns the live `Theme::Colors` token set, restores a saved scheme at `Init`, and re-applies the style with the current tokens on UI-scale changes and live edits.
- Version bump from 0.13.0-alpha to 0.13.1-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified with a smoke test of the rebuilt engine: the app runs stably with the new panels registered, no ImGui assertions, and `editor_theme.json` is only written on an explicit Save.

## [0.13.0-alpha] — 2026-08-04

### Added

- New `Theme` module (`src/editor/Theme.{h,cpp}`): a single custom dark style replaces ImGui's stock grey with a professional palette — warm charcoal neutrals (`#1B1D23` window, `#22252C` popups, `#C9CDD6` text), an indigo accent (`#5B7CFA`) used for selection, tabs, drag grabs, focus rings and docking previews, refined rounding (window 8 / child 6 / frame 4 / tab 4), a 2px `TabBarOverlineSize` highlight on the selected tab, centered title bars, and a taller monospace-friendly scrollbar. `style.ScaleAllSizes(ui_scale)` keeps the existing UI-scale slider working.
- DPI-crisp font pipeline: `Theme::ComputeDpiScale` derives the framebuffer scale from `SDL_GetRendererOutputSize` / `SDL_GetWindowSize` (the same inputs the ImGui SDL2 backend uses), and `Theme::LoadFonts` bakes Segoe UI / Segoe UI Semibold / Cascadia Mono (with Arial / Consolas / Courier fallbacks) at `size * dpi`. `FontGlobalScale = ui_scale / dpi` folds the DPI factor back out, so glyphs are 1:1 with the framebuffer and text/borders no longer get scaled up and blurred on high-DPI displays. The script editor's mono font now comes from this shared set instead of loading its own Consolas.
- New `LayoutManager` (`src/core/LayoutManager.{h,cpp}`): a master full-screen transparent `DockSpace` host window covers the viewport work area, so every panel docks into one unified workspace. `View → Layout` offers two built-in presets — **Default** (Hierarchy | Viewport | Inspector over a bottom strip of Script Editor + Stats, code window free-floating) and **Scripting** (taller bottom strip: Script sidebar | docked code window | Stats) — plus **Reset to Default Workspace** and **Save Current Layout as Default**. The script editor's `Script Editor: <file>` window participates in the workspace via `SetNextWindowDockID` (it can be docked or undocked by a preset). A captured custom layout is serialized to `editor_layout.json` (gitignored) and restored on the next launch; otherwise the stored preset is rebuilt deterministically each start.

### Changed

- `Application` now owns a `Theme::Fonts` set and a `LayoutManager`; `Init` computes the DPI scale, loads the theme fonts, applies `ConfigureStyle`, and restores the workspace before the first frame. The old inline `ConfigureImGuiStyle` / `SetupDockingLayout` free functions were removed in favor of the new modules. Exiting play mode still force-rebuilds the dock layout so the editor deterministically returns after the viewport's fullscreen isolation.
- Version bump from 0.12.3-alpha to 0.13.0-alpha across `main.cpp` title, README, architecture doc, and CMake project version.
- Verified with a smoke test of the rebuilt engine: the Default workspace docks all panels into the unified dock, the script IDE opens its floating code window, and the app runs stably with no ImGui assertions.

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
