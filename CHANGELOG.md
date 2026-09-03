# Changelog

## [0.50.0-alpha] — 2026-09-03

### Added

- **Scene transitions** (Stage 6): a new `Game.LoadScene(path)` Lua binding lets a script swap the active scene at runtime without ever leaving Play mode — e.g. walking into a level-exit trigger. The swap is wrapped in a brief fade-to-black-and-back (`src/core/SceneTransition.h`) rather than applied instantly: it reads as an intentional transition instead of a jarring pop, and it lets the actual scene load happen at the one safe point in the frame (right after `Scene::FlushPendingDestroyEntities`, once every per-frame system has finished walking the entity list) rather than mid-call from inside a trigger's `OnTriggerEnter`.
- **`Application::PerformSceneTransition`**: reuses `SceneManager::LoadScene` (the same call the editor's own Open Scene menu item uses, which reloads into the existing `Scene` object in place so every panel's cached `Scene*` stays valid) and mirrors `EnterPlayMode`'s session-start steps — stop the outgoing script session and audio, clear physics, load, start a fresh session, auto-play the new scene's audio. Health and score carry over between scenes (the player's running progress); the prompt banner and any Won/Lost status reset, since those described the level being left. A failed load (bad path) logs an error and still fades back in on whatever the scene was left in, rather than leaving the screen permanently black.
- **A new example script**, `assets/scripts/level_exit.lua`: attach to a Trigger-collider entity to make it a level exit, with the destination scene path as a single clearly-commented constant to edit (matching `goal_zone.lua`/`damage_zone.lua`'s existing "duplicate and edit" convention).
- **Two demo scenes**, `assets/scenes/level_1.scene` and `level_2.scene`, proving the feature end to end: Level 1's "Level Exit" trigger (`level_exit.lua`) loads Level 2; Level 2 reuses the existing `goal_zone.lua` unmodified on its own trigger, showing that a freshly-loaded scene's scripts bind and run exactly like a scene loaded any other way. Generated with real engine code (a temporary self-test building `Scene`/`SceneSerializer` output directly, the same technique Stage 4's procedural audio used) rather than hand-authored JSON.

### Fixed

- **A latent bug in `Scene::Clear()`**: an `entity:Destroy()` queued but not yet flushed on the scene being discarded (`m_pending_destroy`) survived the clear untouched. Since entity ids restart from 0 on every `Clear()`, a stale id here could silently delete an unrelated entity in whatever loads next, the first time the queue is flushed — a real risk once scene transitions made "destroy this pickup, then load the next level" a natural pattern to write in one script. `Clear()` now empties the pending-destroy queue along with the entity list.

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only). A temporary self-test in `Application::Init()` confirmed, against the real compiled engine: `SceneTransitionFadeAlpha` returns the expected values for both fade directions; both demo scenes save successfully; and a full transition cycle against the live scene (snapshotted first and restored after, so the test never permanently replaces the editor's own default scene) — requesting a load, advancing past the fade-out duration (confirmed the phase flips to FadingIn and the scene swap actually happened: the new scene's Player and Level Exit entities are present, entity count changed from 12 to 5), advancing past the fade-in duration (confirmed the phase returns to None) — passed in full. Separately launched the built exe with `--play assets/scenes/level_1.scene`: clean launch, no crash, no stderr output.

## [0.49.0-alpha] — 2026-09-02

### Added

- **Export/build pipeline** (Stage 5): a "Export Build..." command (File menu and Command Palette) packages the currently-open scene into a standalone, double-click-runnable folder — `game.scene` (the active scene, saved via the existing `SceneSerializer`), a full recursive copy of `assets/`, and a renamed copy of the running executable (`GetModuleFileNameA` + `std::filesystem::copy_file`). No new build target, no separate "player" binary, no stripped-down `Init()` path: the exported `.exe` is byte-identical to the editor binary — it behaves as a pure player purely because `game.scene` sits next to it.
- **Runtime mode**: a new `Application::InitRuntime(...)` calls the existing `Init()` unchanged, then runs a lean subset of `EnterPlayMode()`'s logic (no undo snapshot, no camera blend, no panel-visibility save/restore) against the loaded scene. A single new `bool m_runtime_mode` flag (orthogonal to `EngineState::Play`, which is otherwise reused entirely as-is) suppresses the main menu bar and makes Esc quit instead of returning to the editor — every other Play-mode gate already in place (isolated viewport, gameplay HUD, script session, physics) applies automatically with zero duplication.
- **CLI + auto-detection** (`main.cpp`): `--play <scene>` boots straight into that scene's Play session with no editor UI. With no arguments, a `game.scene` file found next to the executable (written by `ExportBuild`) is auto-detected and loaded the same way — so a shipped build's `.exe` needs no launch arguments or shortcut configuration at all. `main.cpp` also now calls `SDL_GetBasePath()` and normalizes the process's working directory to the executable's own folder before anything else runs, so relative `assets/...` paths resolve correctly regardless of how the binary was launched (double-click, shortcut with a different "Start in" target, or invoked from an arbitrary shell directory).

### Fixed

- `Application.cpp`'s new `#include <windows.h>` (needed for `GetModuleFileNameA`) was pulling in `min`/`max` macros ahead of `<algorithm>`, silently breaking every `std::min`/`std::max`/`std::clamp` call site in the file into malformed syntax (`C2059`/`C2589` errors at over a dozen unrelated lines). Fixed by defining `NOMINMAX` before the include, the standard fix for mixing `<windows.h>` with the STL.

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only). A temporary self-test in `Application::Init()` confirmed, against the real compiled engine: `SceneSerializer::SaveToFile` round-trips a scene to disk; `ExportBuild` produces a folder containing a readable `game.scene`, a full `assets/` copy, and a working renamed `.exe`. Separately launched the built editor exe directly (no crash, correct window title), launched it with `--play <saved scene>` (booted cleanly, no crash, no stderr output), and launched the *exported* build's own `.exe` with zero arguments from its own folder (auto-detected `game.scene`, booted cleanly, no crash, no stderr output) — exercising all three entry paths end to end.

## [0.48.0-alpha] — 2026-08-29

### Added

- **Movement audio** (Stage 4): jump and landing sound effects, plus material-aware footsteps while the player walks on the ground. `PlayerControllerComponent::ground_material_index` (new field) is recomputed every frame inside the headless `PlayerControllerUpdate` — matched by nearest-color against `kLandscapePaintPalette` when standing on a landscape vertex, or by exact `.mat` path when standing on a placed Solid entity — and read back in `Application::UpdatePlayerController` (which stays the only place that touches `AudioManager`, keeping `PlayerController.cpp` audio-free) to pick a footstep sample. Jump/landing/footstep triggers all reuse existing state rather than adding new dependencies: jump checks the same `grounded` value `PlayerControllerUpdate` itself uses to decide whether to actually launch; landing is a false→true edge on `grounded` across frames; footsteps run on a fixed cadence gated on the same WASD-input magnitude already computed for movement.
- **8 procedurally-synthesized sound effects** (`assets/audio/`): `footstep_{grass,stone,metal,dirt,default}.wav`, `jump.wav`, `land.wav`, `ambient_wind.wav`. Generated with a small Python script (filtered noise bursts with attack/decay envelopes for footsteps, a rising sine sweep for jump, a low-frequency thump for landing, slow-LFO-modulated filtered noise for a seamlessly-loopable ambient bed) since no stock SFX were available — plain 16-bit mono PCM WAV, confirmed to load and play through the engine's real SDL_mixer backend.
- **Ambient background audio**: the one-time startup demo scene gained an "Ambience" entity (`material.active = false`, so it never rasterizes) with `audio.auto_play = true` / `loop = true` — needed zero new C++, since `AudioComponent`'s existing auto-play/loop fields already do exactly this (started by `EnterPlayMode`, halted by `ExitPlayMode`'s `StopAll`).
- **Master Volume slider** (Environment & Shading panel, new "Audio" section): `AudioManager::SetMasterVolume`/`MasterVolume()` already existed but nothing in the editor called them. `EnvironmentSettings` gained a persisted `master_volume` field (round-trips through the `.env` JSON like every other setting in that panel) and `EnvironmentPanel` gained an optional `AudioManager*` (same pattern `InspectorPanel` already uses for its audio Preview buttons) so the slider pushes the new value live, the instant it moves — not just on Save.

### Fixed

- Two stale doc comments referencing the pre-fix (v0.46.0) state of the player/PhysicsManager relationship — one in `Components.h`, one in `Application.cpp` — still said the player "does not set collider.enabled on itself," which stopped being true when that became the default. Updated to describe the actual current behavior (collider enabled, but `PhysicsManager::ResolveSolid` skips moving it).

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only). A temporary self-test in `Application::Init()` confirmed, against the real compiled engine: a player standing on a landscape painted entirely Stone-colored gets `ground_material_index` matching Stone's palette entry; a player landing on an entity with `material.material_path = "Metal.mat"` gets Metal's index; and all 8 generated WAV files load and play successfully through a real `AudioManager`/SDL_mixer instance (`Play()` returning a valid channel, not -1, for every one), plus a master-volume round-trip. All passed with exact expected values, then removed. Editor launches and runs without crashing; the default scene now reports 12 entities (up from 11) with the new Ambience entity present.

## [0.47.0-alpha] — 2026-08-29

### Added

- **Deferred entity destruction from Lua** (`entity:Destroy()`): `Scene` gained `QueueDestroyEntity(id)`/`FlushPendingDestroyEntities()` — the former just records an id, the latter (called once, in `Application`'s Play-mode Update block, after `ScriptEngine::UpdateSession` and `PhysicsManager::Step` have both fully finished walking the entity list for the frame) is where the existing `Scene::DestroyEntity` actually runs. A new `g_play_scene` observer pointer (set in `ScriptEngine::StartSession`, cleared in `StopSession`, same pattern as `g_audio_manager`/`g_game_state`) lets the new `LuaEntityDestroy` binding reach the scene. This closes the gap flagged in the previous round: a script can now make a collectible really disappear (`collectible.lua` updated to call `entity:Destroy()` after scoring) rather than only tracking a "already collected" flag.

### Fixed

- **A pre-existing, previously-unnoticed bug in every Lua metatable's method-fallback path** (`ScriptEngine.cpp`, `RegisterVec3`/`RegisterTransform`/`RegisterPlayer`/`RegisterEntity`): each `__index` closure is supposed to capture the type's `methods` table as its upvalue, so a call like `someVector:length()` (a real `Vector3` method that predates this whole session) falls through to it when the explicit field checks don't match. All four registration functions instead wrote `lua_pushvalue(L, -2)` at the point the closure's upvalue gets pushed — at that point in the stack (`[mt, methods]`), `-2` is the **metatable**, not `methods`. Every method-table method on every bound Lua type — `Vector3:norm()/:length()/:dot()/:cross()` included — has silently been unreachable (`"attempt to call a nil value"`) since the binding was first written; nothing surfaced it because Transform's and Player's methods tables were empty (reserved, unused) and nobody had scripted a call to a Vector3 method before. Found while verifying `entity:Destroy()` (the first *new* method added to a previously-empty methods table), fixed by duplicating the top of the stack (`-1`, the actual `methods` table) instead of `-2` in all four functions.

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only). Two temporary self-tests in `Application::Init()`: one isolated the upvalue bug directly (`Vector3(3,4,0):length()` failed with the exact `"attempt to call a nil value (method 'length')"` error before the fix, ran clean after); the other drove a real `entity:Destroy()` call from inside an actual `PhysicsManager::Step()` overlap dispatch, confirming the target entity survives `Step()` itself (proving the deferral genuinely defers) and is gone only after the explicit flush. Both fully passed after the fixes, then removed. Also caught and fixed a second bug along the way: the first `entity:Destroy()` attempt used the wrong stack index in `RegisterEntity` (`lua_setfield(L, -1, ...)` instead of `-2`), which set a field on a non-table value and triggered Lua's default panic handler — an unprotected `abort()` — confirmed via a hung "Microsoft Visual C++ Runtime Library" crash dialog rather than a clean process exit.

## [0.46.0-alpha] — 2026-08-28

### Fixed

- **PhysicsManager double-resolved the player against solid geometry once a Trigger script needed it enabled** (`src/core/PhysicsManager.cpp`, `ResolveSolid`): `PlayerControllerComponent` was designed to own all of the player's own AABB collision (so it can slide/land correctly) by never setting `player.collider.enabled` — but that meant the player was invisible to `PhysicsManager`'s broad-phase entirely, so it could never dispatch `OnTriggerEnter`/`Exit` for it either. The user hit this directly: getting a goal-zone trigger script to fire required manually enabling the player's collider, which then let `PhysicsManager`'s generic solid/solid resolver *also* push the player out of every Wall it already handles itself. Fixed with a guard in `ResolveSolid`: skip position correction whenever either body has `player.enabled` set, while leaving overlap detection and Enter/Exit dispatch untouched — the player can now safely have `collider.enabled = true` (needed for trigger visibility) without ever being double-moved.

### Added

- **The player now ships with a collider by default**: both `Application::CreatePlayer()` and the one-time startup demo scene set `collider.enabled = true` / `type = Solid` with box extents matching the capsule's own `radius`/`height`, so a freshly-created (or default-scene) player works with Trigger Zones immediately — the manual toggle the user needed is no longer necessary.
- **A player entity in the default scene**: the startup demo scene (Camera/Directional Light/Wall/Bouncer/Trigger Zone/Landscape) now also spawns a ready-to-play Player capsule, positioned in the open area in front of the other demo content.
- **Jump** (Spacebar): `PlayerControllerComponent::jump_speed` (default 7.0, Inspector-editable, Lua-writable as `entity.player.jump_speed`), applied in `PlayerControllerUpdate` as a vertical velocity impulse when `jump_pressed` is true *and* the player was grounded as of the previous frame (edge-triggered on the caller's side via `Input::GetKeyDown`, not `GetKey`, so holding Space doesn't re-launch every grounded frame). Persisted through `SceneSerializer` and tracked by `CommandHistory`'s undo snapshot, matching every other authoring field on the component.
- **Two more ready-to-use example scripts**: `assets/scripts/collectible.lua` (`Game.AddScore(10)` once per pickup, via a local per-script flag since there's no Lua-facing way to destroy/hide an entity yet) and `damage_zone.lua` (`Game.SetHealth` -25, `Game.Lose()` at zero). All four example scripts (`goal_zone`, `hazard_zone`, `collectible`, `damage_zone`) now guard on `other.player.enabled` so a scene's other moving bodies (e.g. the demo scene's script-driven Bouncer) can't accidentally trip them.

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only). A temporary self-test in `Application::Init()` confirmed, against the real compiled code: a grounded player's `jump_pressed=true` call produces `velocity.y ≈ 6.67` (jump_speed minus one frame of gravity) with `grounded` cleared; and a player-enabled entity placed deep inside a Solid Wall's AABB is left at its exact starting position after a real `PhysicsManager::Step()` (previously it would have been pushed out). Both checks passed with exact expected values, then removed. Editor launches and runs without crashing; the default scene now reports 11 entities (up from 10) with the new Player present.

### Not implemented (scoped out, flagged for the user)

- A visual input-remapping panel ("select/create/edit" key bindings through the UI) does not exist — confirmed via a full search of `src/editor/`. WASD and the new Jump key are hardcoded scancode reads in `UpdatePlayerController`, matching the *existing* convention (WASD was already hardcoded, not routed through the `Input::RegisterAction`/`RegisterAxis` system, which itself has no persistence and is Lua-only today). Building a real rebinding editor is a legitimate, separable feature — recommended as a follow-up decision for the user rather than folded into this pass.

## [0.45.0-alpha] — 2026-08-28

### Added

- **Game state management & gameplay HUD** (Stage 3): a new `GameplayState` (`src/core/GameplayState.h`) — `health`, `score`, `prompt`, and a `Status` of `Playing`/`Won`/`Lost` — owned by `Application`, reset to defaults every `EnterPlayMode()`, and deliberately **not** serialized (it's Play-session state, not scene data) or given any built-in meaning (no hardcoded "health <= 0 loses" rule): a level's own script decides what the numbers mean and calls `Win()`/`Lose()` explicitly, keeping the HUD reusable across genres rather than assuming what kind of game is being built.
- **`Game.*` Lua bindings** (`src/script/ScriptEngine.cpp`): `SetHealth`/`GetHealth`, `SetScore`/`AddScore`/`GetScore`, `ShowPrompt`/`ClearPrompt`, `Win`/`Lose`, `GetStatus`. Registered the same way `Audio.*` already is (an observer pointer set once via `ScriptEngine::SetGameplayState`, degrading to a no-op when unset) — a Trigger Zone's `OnTriggerEnter` calling `Game.Win()` needs zero new C++ trigger-detection code, reusing the existing physics/scripting bridge entirely.
- **In-viewport gameplay HUD** (`Application::RenderGameplayHUD`): health/score box (top-left), a centered prompt banner (bottom) when `Game.ShowPrompt()` has set one, and a full win/lose screen (dimmed backdrop, large headline, a Restart hint) once the round ends. Wired through a new `ViewportPanel::on_gameplay_hud` callback — the mirror image of the existing editor-only `on_overlay` stats HUD, firing only in the *isolated* (Play) viewport instead of the docked editor one, so the two overlays can never show at the same time by construction.
- **Round lifecycle**: `UpdatePlayerController` now freezes all player input once `GameplayState::status` leaves `Playing` (no gravity/collision nudging the capsule around behind a "GAME OVER" banner); pressing **R** while won/lost cycles `ExitPlayMode()` + `EnterPlayMode()` for an instant restart, without leaving Play mode.
- **Two example scripts** (`assets/scripts/goal_zone.lua`, `hazard_zone.lua`): drop either path into any Trigger-collider entity's Inspector Script field to get a working win or lose condition with zero Lua required.

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only), no CMake generator conflict this round. Functionally verified through the real compiled `ScriptEngine` + `GameplayState`, not just a compile check: a temporary self-test in `Application::Init()` ran a script calling `SetHealth`/`AddScore` (twice)/`ShowPrompt`/`Win`, then read the resulting struct back in C++. All 4 checks passed with exact expected values (`health=50`, `score=15`, `prompt='Hello'`, `status=won`), then the self-test was removed. Editor launches and runs without crashing with the HUD wired into the real Play-mode viewport.

## [0.44.0-alpha] — 2026-08-28

### Added

- **`entity.player` Lua binding** (`src/script/ScriptEngine.cpp`: `LuaPlayer`, `PushPlayer`, `LuaPlayerIndex`/`NewIndex`, `RegisterPlayer`): exposes `PlayerControllerComponent` to gameplay scripts, closing the one real gap identified in an audit of the existing (and already extensive) Lua scripting system before this work started — a request to rebuild `ScriptComponent`/lifecycle hooks/`entity.transform`/`OnTriggerEnter`/Inspector script field/scene serialization from scratch was set aside once it became clear all of that already exists (Lua 5.4 VM, `OnStart`/`OnUpdate`/`OnCollisionEnter`/`Exit`/`OnTriggerEnter`/`Exit` hooks, a live `entity.transform.position/.rotation/.scale`, an Inspector "Script" section, JSON round-trip, and a full in-editor Lua IDE with hot-reload) — none of it reached the player controller added in the previous session, since that component didn't exist yet when the scripting bridge was built. `entity.player.velocity` follows the exact same live-view convention as `transform.position` (a `Vector3` userdata backed directly by the component's memory, so `entity.player.velocity.y = 10` mutates the real value, not a copy); `enabled`/`radius`/`height`/`move_speed` are plain read-write; `grounded` is read-only (it's `PlayerControllerUpdate`'s own per-frame output — a script attempting to write it gets a clear `luaL_error`, not a silent no-op).

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only). Functionally verified through the real compiled `ScriptEngine`, not just a compile check: a temporary self-test in `Application::Init()` ran an actual scripted entity through `StartSession()` — a script writing `entity.player.velocity`/`.move_speed`/`.enabled` in `OnStart`, and a second script attempting to write `entity.player.grounded` — then read the resulting C++-side component state back directly. All 4 checks passed with exact expected values (`velocity=(1,2,3)`, `move_speed=4` from `radius(0.4) * 10`, `enabled=true`, and the read-only write correctly rejected with `"player: 'grounded' is read-only or unknown"`), then the self-test was removed.
- Also includes a repeat of the CMake generator clean-reconfigure from the previous fix — `build/debug`'s cache drifted back to Ninja again mid-session (via `CMakePresets.json`), consistent with something external (most likely VS Code's CMake Tools extension) periodically reconfiguring the same directory in the background. Worth checking if this keeps recurring.

## [0.43.1-alpha] — 2026-08-28

### Fixed

- **Paint Mode had no brush cursor feedback** (`Application::UpdatePaintMode`, `Application.cpp`): the landscape brush-ring cursor (`m_landscape_brush_valid`/`m_landscape_brush_center`, drawn by `DrawLandscapeBrushCursor`) was only ever set by `UpdateLandscapeBrush` — the sculpt path — never by `UpdatePaintMode`. Painting had no on-screen indicator of the brush's position or radius at all, and the cursor-draw dispatch checked `IsLandscapeSculptMode()` specifically, so outside the Landscape workspace it fell through to drawing the **transform gizmo** instead, over whatever entity happened to be selected — a jarring, unrelated widget appearing while trying to paint. Reported by the user as the brush placement feeling "uncalibrated."
- **Paint Mode's raycast only ran while the mouse was held** (same function): `UpdatePaintMode` gated its entire body — including the raycast that would have positioned a cursor — behind `lmb` (left mouse button down), unlike `UpdateLandscapeBrush`, which raycasts on every hovered frame and only gates the *actual sculpt/paint application* behind the mouse button. Restructured to match: the landscape raycast and cursor position now update every frame the viewport is hovered, and only the color blend / material swap requires the button held.
- Combined fix: the cursor-draw dispatch now also fires for `m_paint_mode` (reusing the exact same ring as sculpt, set by whichever of the two functions is actually running that frame — dispatch priority already guarantees only one ever runs), and the gizmo is correctly suppressed while painting, matching sculpt's existing behavior.

### Investigated, not reproduced

- User-reported: painted terrain colors appearing to "go away" when switching between the Landscape and Level Design workspaces. Read through `SyncWorkspaceSideEffects`/`ApplyWorkspace` and found nothing that touches `LandscapeComponent::colors` or forces a mesh rebuild from stale data — workspace switching only changes panel visibility. The leading suspect is the transform-gizmo-popping-up bug above: in Level Design workspace `IsLandscapeSculptMode()` is unconditionally false, so pre-fix, switching there while Paint Mode was active would suddenly draw the full transform gizmo over the selected entity, which plausibly reads as "something about my paint just changed." Flagged to the user to retest specifically after this fix rather than assumed fixed.

## [0.43.0-alpha] — 2026-08-28

### Added

- **Player capsule character controller** (Stage 2): `PlayerControllerComponent` (`Components.h`/`Entity.h` — `velocity`, `radius`, `height`, `move_speed`, `grounded`) plus a new headless logic module `src/core/PlayerController.{h,cpp}` (mirroring `Landscape.cpp`'s pure-logic, no-input/no-rendering style so it can run without the editor, as the in-app self-test used to verify it did). `PlayerControllerUpdate` applies gravity (20 units/sec², terminal-velocity clamped), snaps horizontal velocity to the caller-supplied move direction (no acceleration/friction modeling — a "basic" brief), resolves per-axis AABB collision against every other entity's enabled Solid `ColliderComponent` (X/Z first, then Y — one generic resolver correctly handles Walls blocking, Floors/Ramps supporting, and Ramps as an acknowledged box approximation, without per-type special-casing), then snaps to the nearest landscape surface below the resolved position via the existing `LandscapeWorldToLocal`/`LandscapeSampleHeightLocal`. Deliberately independent of `PhysicsManager`'s generic solid/solid resolver (which pushes whichever body has the higher entity id — not appropriate for player movement).
- **Capsule mesh primitive** (`kBuiltinCapsulePath`, `Mesh.{h,cpp}`): a proper hemisphere-capped cylinder (12 slices, 4 rings/cap, base-pivoted at y=0 like Wall/Floor/Ramp), registered alongside the other builtin primitives and drag-droppable from the Content Browser's Primitives folder. Winding verified by direct cross-product derivation (documented inline) rather than assumed, matching the divergence-theorem verification already applied to `BuildRampMesh`.
- **`Application::CreatePlayer()`**: spawns a Player entity (Capsule mesh, `player.enabled = true`, defaults matching the capsule's own built size) in front of the editor camera, mirroring `CreateLandscape()`'s convention exactly (undo-pushed, selected, toasted). Reachable via the Command Palette as **Create Player** (Create group), next to **Create New Material**.
- **Play-mode camera follow** (`Application::UpdatePlayerCameraFollow`): while in Play mode, the active gameplay camera entity's position is re-pointed at the player's position plus a fixed, yaw-relative offset (5 units behind, 2.2 above) every frame, replacing whatever static position it was authored at. Only ever touches position, never yaw/pitch/fov; `ExitPlayMode`'s existing scene-snapshot restore puts the camera back afterward. WASD movement direction is built from that same active camera's yaw (falling back to the editor camera's yaw if the scene has no camera entity), reusing `UpdateCameraControls`'s existing yaw-only forward/right basis.
- **Player Controller Inspector section** (`InspectorPanel.cpp`): Enabled/Radius/Height/Move Speed, following the same `ComponentHeader`/`BeginEditSession`/`CommitEdit` pattern as every other component section, plus a live read-only Grounded/Velocity readout while enabled.
- **Persistence**: `SceneSerializer` and `CommandHistory`'s undo snapshot both gained matching fields for the component's authoring properties (`enabled`/`radius`/`height`/`move_speed`); `velocity`/`grounded` are deliberately excluded from both (runtime state, reset naturally on Play re-entry, not something a scene save or an undo should freeze in place).

### Verified

- Clean MSVC rebuild (benign `LNK4044 /static` + `M_PI` warnings only). The standalone g++ smoke-test harness used earlier this session for isolated logic verification could not link in this environment (`ld` exits 116 with no diagnostic on any output path tried, consistent with the AV interference seen during the landscape-painting work) — so verification instead ran the exact shipped object code: a temporary self-test in `Application::Init()` exercised `PlayerControllerUpdate` against three synthetic in-memory scenes (gravity + landscape ground-snap, horizontal Wall collision, landing on a suspended Floor), wrote PASS/FAIL to a file, was confirmed 7/7 passing with exact expected values, then was removed. Editor launches and runs without crashing with the feature wired into the real per-frame Play-mode update loop.

### Added

- **Editor Working Light** (`EnvironmentSettings::editor_fill_light_enabled`/`editor_fill_light_intensity`, `Environment.{h,cpp}`, `ShadeVertex`/`EmitEntityTris`/`RenderScenePass` in `Application.cpp`): a flat, unshadowed ambient fill applied on top of the existing per-light shading so terrain valleys, primitive undersides, and grazing-angle faces stay readable while sculpting, placing, and painting, regardless of camera angle or whether the scene has any lights configured yet. Unlike a light's own `ambient` floor (`Components.h`), the fill is added outside the per-light loop, so `DirectionalShadowFactor` never attenuates it — a face fully in a directional-light's shadow still gets the fill. Applied wherever `RenderScenePass`/`RenderMaterialPreview` run in Editor state only (`m_state == EngineState::Editor`); always 0 in Play mode, so gameplay lighting is judged on its own, unaffected by the editing aid. Exposed as a new **Editor Working Light** section in the Environment & Shading panel (`EnvironmentPanel.cpp`) — an on/off toggle plus a 0–1 intensity slider, persisted to the `.env` asset like every other environment setting. Default: on, intensity `0.35`.

## [0.41.0-alpha] — 2026-08-28

### Added

- **Landscape material painting** (`src/core/Landscape.{h,cpp}`, `Application::UpdatePaintMode`): a new toolbar **Paint Mode**, independent of the pre-existing Landscape-panel sculpt brush, that blends a selected material into the terrain's per-vertex color layer (`LandscapeComponent::colors`) or swaps the flat material outright on a placed primitive (Wall/Floor/Ramp/Cube). Raycasts through the same camera-basis math as `UpdateAssetPlacement`/`UpdateLandscapeBrush` (`RaycastAnyLandscape`, and the new `RaycastAnyEntity` — ray-vs-AABB over every non-landscape entity, factored out for this feature and shared with placement's drop-position resolution).
- **Shared paint material palette** (`Landscape.h`: `PaintMaterialPreset`, `kLandscapePaintPalette`, `kLandscapePaintPaletteCount`): one canonical four-swatch list — Grass, Stone, Metal, Dirt — with deliberately saturated, mutually distinct colors (so a stroke reads immediately without a bound texture) plus a matching `.mat` asset each (`assets/materials/{Grass,Stone,Metal,Dirt}.mat`, the last with a genuine PBR split: metallic 0.9 / roughness 0.25, tinted steel-blue rather than neutral gray so it's visually unmistakable from the others). Both the toolbar's Paint Mode and the Landscape panel's own Paint mode read this one list, replacing what were previously two independent, differently-named preset sets (Grass/Rock/Dirt/Snow/Sand vs. Grass/Stone/Metal/Dirt).
- **Lower sculpt tool** (`SculptTool::Lower`, `Landscape.cpp`): terrain sculpting now has an explicit Raise/Lower pair instead of Raise-only (there was previously no UI path to push the surface down).
- **Brush falloff profile** (`BrushFalloffProfile::{Smooth,Sharp}`, `LandscapeBrushSettings::falloff_profile`): the brush edge can now ease in with the existing cubic Smoothstep curve or fall off at a constant linear rate (`LinearFalloff`) for a harder, more deliberate edge — useful for cliffs or a crisp boundary between two paint layers. Threaded through both `UpdateLandscapeBrush` and `UpdatePaintMode`.
- **Landscape panel Mode switch** (`src/editor/LandscapePanel.cpp`): the panel now leads with an explicit **Sculpt / Paint** toggle instead of folding Paint into the height-tool radio group. Sculpt shows the four height brushes (Raise/Lower/Flatten/Smooth); Paint shows the shared material palette as color-swatch + `Selectable` rows. The toggle writes the exact same `bool`/`int` state the toolbar's Paint button and material combo read, so the panel and the toolbar can never disagree about whether painting is active or which material is selected. Brush Size/Strength/Falloff/Edge-shape controls are shared by both modes and shown once, below the mode-specific section.

### Changed

- **Default brush strength** (`LandscapeBrushSettings::strength`): raised from `0.5` to `1.5` (still adjustable down to `0.01`) so a brief, deliberate hold reads as an immediate visible change instead of a slow multi-second fade-in — the previous default made a quick click nearly imperceptible.
- **Toolbar toggle-button contrast** (`Application.cpp`, `DrawViewportToolbar`, main toolbar): the Render-mode pills (Lit/Wireframe/Unlit), both Snap toggles, the Gizmo mode selector, Place, and Paint all previously highlighted their active state with an ad-hoc, low-contrast literal color (`0.30, 0.30, 0.38`) duplicated at six separate call sites — never the theme's own accent. All six now use the existing `Theme::PushPrimaryButtonColor()`/`PopPrimaryButtonColor()` helper (the theme's real indigo accent, already used elsewhere for primary actions), giving every "this is active" state one consistent, clearly legible, theme-aware treatment.

### Fixed

- **Paint Mode toolbar crash (`abort()` / Debug Error)**: the Place and Paint toggle buttons read the live mode flag both before (to push a highlight color) and after (to pop it) drawing the button, but the button's own click handler flips that flag in between — so clicking a button to turn a mode **on** pushed nothing but then popped 2 colors that were never pushed, hitting ImGui's `EndFrame()` color-stack-balance assertion. Fixed by capturing each flag into a local `const bool` before the button, matching the (already-correct) pattern the adjacent Snap toggle used.
- **Paint Mode silently doing nothing (routing conflict)**: the per-frame viewport dispatch checked `IsLandscapeSculptMode()` before the toolbar's `m_paint_mode` flag. `IsLandscapeSculptMode()` goes true ambiently the moment a landscape exists and is targeted (`CreateLandscape` auto-targets the new terrain) and stays true for the whole time the Landscape workspace is active — it does not reflect an explicit user choice the way the Paint toolbar toggle does. Every click while Paint Mode was on was silently routed to the old sculpt brush instead (default tool Raise, which moves height, not color). Fixed by checking `m_paint_mode` first in the dispatch chain.

### Verified

- Clean MSVC rebuild (only the pre-existing benign `LNK4044 /static` + `M_PI` warnings). Launched via PowerShell, ran without crashing, `se_diagnostics.txt` shows a stable frame rate. Landscape painting confirmed visually working end-to-end after the routing fix (user-verified: status-bar diagnostic showed the per-vertex color converging on the selected preset while holding the brush over terrain).

## [0.40.0-alpha] — 2026-08-21

### Added

- **Professional dark-slate ImGui theme** (`Theme::ConfigureStyle`): replaced the warm-charcoal base palette with a cooler dark-slate scheme (inspired by UE / DCC tools). Refined six user-editable tokens — `window_bg`, `child_bg`, `popup_bg`, `frame_bg`, `text`, `accent` — now derive a cohesive indigo-blue accent. All derived colors (hover, active, tints, tabs, scrollbars, docking) update automatically when the accent token is edited live.
- **Bilinear bloom sampling** (`EnvironmentFX::PostProcess`): the half-res bloom buffer is now sampled with bilinear interpolation instead of nearest-neighbor integer division, producing a smooth, filmic glow without hard block boundaries.
- **Presentation layer guidelines** (`docs/Singularity_Architecture_Textbook.md`): added Phase 40 section covering the visual polish sprint: theme system, viewport overlay defaults, bloom rendering, and renderer null-safety.

### Changed

- **Viewport overlay defaults** (`ViewportOverlaySettings`): `colliders` and `bounds` are now `false` by default so the 3D geometry is not visually overpowered by diagnostic overlays. Grid remains visible; light gizmos and transform gizmo remain on.
- **Softer ground grid** (`Application::RenderGroundGrid`): minor grid lines reduced from solid `(45,45,55)` to semi-transparent `(38,38,48,180)`, and world axes softened to `(180,60,60,200)` / `(60,90,200,200)` for a clean, non-intrusive look.
- **Reduced bloom blowout** (continuation of hotfix): bloom strength lowered to `0.2`, threshold raised to `0.92`, sun intensity reduced to `0.9` — the sky and lighting are no longer washed out.
- **SDL_RenderReadPixels bypass** (continuation of hotfix): post-processing readback is skipped entirely in Unlit / Wireframe modes and when bloom is off with all grade params at neutral defaults, eliminating ~55 ms/frame in lightweight editing modes.
- **Renderer null-safety guards** (`Application.cpp`): added `!renderer` checks to `DrawProjectedLine`, `DrawWorldAABB`, `RenderGroundGrid`, `FlushTriBatch`, `RenderMeshWireframe`, `DrawTriangles`, `RenderScenePass`, `RenderEditorOverlay`; `!entity_ptr` checks in all entity iteration loops.

### Fixed

- **Bloom block-boundary artifacts**: nearest-neighbor sampling of the half-res bloom buffer produced visible block boundaries on gradients and sky. Bilinear interpolation eliminates these artifacts.
- **Washed-out white sky / over-exposure**: the bloom strength (0.7) and sun intensity (1.2) caused sky and sun pixels to smear bright white across the entire viewport via gaussian blur. Values tuned to produce a subtle, filmic glow.

### Verified

- Clean MSVC rebuild succeeds (benign `LNK4044 /static` + `M_PI` warnings only). Smoke test: process stable for ~56 seconds, diagnostics file produced, 17-18 FPS in Lit mode (PostProcess active), Unlit/Wireframe modes bypass the ~55 ms readback.

## [0.39.0-alpha] — 2026-08-20

### Added

- **Mode-based panel isolation** (`Application::SyncWorkspaceSideEffects`): every workspace mode now explicitly controls the visibility of every editor panel. The five profiles are:
  - **Level Design**: Viewport, Hierarchy, Inspector, Content Browser.
  - **Landscape**: Viewport, Landscape Panel, Inspector, Hierarchy.
  - **Shading & Assets**: Material Preview, Material Editor, Environment Panel, Content Browser.
  - **Sequencing**: Timeline Panel, Viewport, Inspector, Hierarchy.
  - **Scripting**: Script Editor (tabbed mini-IDE), Console Panel, Content Browser.
  All panels outside the active mode are hidden, preventing cross-workspace UI spillover. The function is the single source of truth for panel visibility — individual panels never override it.
- **Visibility controls on editor panels** (`SceneHierarchyPanel`, `LandscapePanel`, `TimelinePanel`, `StatsPanel`): these panels now carry `m_visible`, `SetVisible(bool)`, `IsVisible()`, and `ToggleVisible()`, matching the existing contract on `ViewportPanel`, `InspectorPanel`, `ContentBrowserPanel`, `ScriptEditorPanel`, `ConsolePanel`, `MaterialPanel`, `MaterialPreviewPanel`, and `EnvironmentPanel`. Each panel's `OnImGuiRender` early-returns when invisible, skipping the `ImGui::Begin()`/`End()` pair entirely.

### Fixed

- **Mid-frame dock-tree crash**: `WorkspaceManager::ApplyWorkspace()` previously called `RebuildLayout()` synchronously from menu-bar callbacks during the ImGui frame. `DockBuilderRemoveNode()` would destroy dock nodes while earlier ImGui windows still referenced them, causing stale-pointer crashes. The rebuild is now deferred: `ApplyWorkspace()` sets `m_needs_rebuild = true` and `DrawDockspace()` consumes it at the top of the next frame, before any panel is submitted.
- **Startup segfault (exit 139)**: `m_material_panel`, `m_viewport_layout_panel`, and `m_profiler_panel` were missing from the `Application` constructor initializer list. They contained garbage pointer values when `SyncWorkspaceSideEffects()` was called during `Init()`, and the null-guard dereferenced the garbage. All three are now initialized to `nullptr`.
- **Stale `m_code_window_node` return**: when `ApplyWorkspace()` defers the rebuild, `m_code_window_node` still holds the previous layout's value. The caller (`ScriptEditorPanel::RequestDockCodeWindow`) would store this stale ID and attempt to dock to a node that gets destroyed in the next `RebuildLayout()`. Fixed by returning 0 (floating) when the rebuild is deferred.

### Verified

- Clean MSVC rebuild succeeds (only the benign `LNK4044 /static` warning); editor smoke run stays alive for 14 seconds with an empty log and no stray file edits on disk. Startup segfault confirmed fixed — process starts, runs, and exits cleanly on SIGTERM.

## [0.38.0-alpha] — 2026-08-20

### Added

- **PBR material schema** (`src/core/Material.{h,cpp}`): `Material` graduates from tint + texture + (unused) shininess into an explicit channel layout — **Albedo** (`color` RGBA tint, `texture` albedo map, `albedo_multiplier`), a **Normal** slot (`normal_texture`/`normal_strength`, documented slot-only: the CPU rasterizer shades flat), and per-channel scalars + optional texture-map slots + multipliers for **Metallic**, **Roughness** and **Ambient Occlusion** (`metallic`/`metallic_texture`/`metallic_multiplier`, `roughness`/`roughness_texture`/`roughness_multiplier`, `ao`/`ao_texture`/`ao_multiplier`). The `.mat` JSON round-trip writes/reads every field with backward-compatible defaults (old files keep working; `shininess` stays serialized as the superseded legacy knob). A tiny `MaterialShading` struct carries the resolved scalars into the renderer, and `MaterialLibrary::LiveUpdate(filename, material)` refreshes every cached copy **in memory** (no disk write) so live edits re-shade the scene and preview immediately — `Save` still persists the file.
- **Material shading core** (`src/render/MaterialCore.h`, pure header, no SDL/`<filesystem>`): `pbr::SpecularPower` (roughness 0 → 257-power mirror, 1 → dead matte), `pbr::DielectricF0` (dielectrics reflect ~4%, metals their base albedo, lerped by `metallic`), `pbr::AmbientFloor` (AO-dimmed ambient), `pbr::BlinnPhong` (NdotH) and `pbr::SpecularWeight` — verified by a standalone g++ harness (25 checks, all pass).
- **Software PBR shading** (`src/core/Application.cpp`): `EmitEntityTris` now takes `const MaterialShading &` and shades per light with **albedo_multiplier** (scales the albedo), **AO** (dims the ambient floor via `pbr::AmbientFloor`), and a cheap **Blinn-Phong specular** term tinted by `DielectricF0` (guard-vectors for a camera sitting on the centroid and opposing light/view). `RenderScenePass` resolves each entity's shading through the new `Application::ResolveEntityShading`, and its light-gathering loop is factored into a reusable `GatherSceneLights(Scene*)`.
- **Material Editor PBR rework** (`src/editor/MaterialPanel.{h,cpp}`): collapsible **Albedo / Normal / Metallic / Roughness / Ambient Occlusion** sections — texture-slot combos (None + every `assets/textures/` asset) per channel, channel multipliers (0–2), and the albedo map live preview — with every edit pushed live through `MaterialLibrary::LiveUpdate` and the existing Save/Revert persist/reload pair. The bottom inline "New Material" block becomes a **New Material…** button that opens the **Create New Material wizard** modal (file name + albedo tint + metallic + roughness → `Create` + select); the wizard is also reachable via the new Command Palette entry **Create New Material** (Create group) which shows the panel first.
- **Material Preview viewport** (`src/editor/MaterialPreviewPanel.{h,cpp}` + `Application::RecreateMaterialPreview`/`RenderMaterialPreview`): a dedicated interactive preview of the active material — a procedural **UV sphere or cylinder** (full UV sets so albedo maps wrap correctly) rendered each frame under the **active Environment & Shading settings** (sky behind, per-triangle fog, full post chain) and the **scene's directional lights** (key-light fallback when the scene has none). The panel orbits the test mesh (drag to yaw/pitch, scroll to dolly, **Auto-rotate** + **Reset**, test-mesh combo) through the same texture-provider pattern as the Inspector camera preview; the Application skips the software render entirely when the window is docked-inactive (`FrameActive`) for thermal efficiency. The panel reads the Material Editor's selected material (live copy), so slider edits are visible on the test mesh instantly.
- **Workspace & wiring**: the Shading & Assets workspace now splits its center column into the main viewport (top) and the **Material Preview** strip (bottom) under the same environment lighting; "Material Preview" is also docked as a back-tab in the Development Zone. "Toggle Material Preview" joins the **View menu** and **Command Palette** (View); the preview panel is folded into the play-mode save/restore and torn down on Shutdown. `CMakeLists.txt` adds `src/editor/MaterialPreviewPanel.cpp` and bumps the version to `0.38.0`.

### Verified

- Pure PBR math (`SpecularPower` endpoints/clamping, `DielectricF0` dielectric/metal/midpoint, `AmbientFloor`, `BlinnPhong` facing/back-facing, `SpecularWeight`) passes the standalone g++ harness (no `<filesystem>`, per the known toolchain linker defect). Clean MSVC rebuild succeeds (only the benign `LNK4044 /static` warning); editor smoke run stays alive with the PBR panel, wizard, preview viewport and scene shading active, an empty log, and no stray file edits on disk.

## [0.37.0-alpha] — 2026-08-19

### Added

- **Environment settings asset** (`src/core/Environment.{h,cpp}`): an `EnvironmentSettings` struct owning all three environment blocks — **Sky** (enabled, top/horizon/sun colors, sun intensity/glow/disk/yaw/pitch, star intensity), **Fog** (enabled, color, density, height falloff, start distance) and **Post** (enabled, working scale, bloom threshold/strength/radius, exposure, gamma, saturation, contrast, temperature, ACES toggle) — round-tripped through the engine's `json::Value` serializer as a `.env` file (JSON, `"type": "environment"`), with `LoadEnvironmentAsset`/`SaveEnvironmentAsset` mirroring the `.pmat` helpers (directory created on demand). The asset is **global editor state** (like the theme — no undo): `Application` owns the one `EnvironmentSettings m_environment`, writes `assets/environment/default.env` with the defaults on first launch, and surfaces parse/save errors on the console.
- **Procedural skybox** (`src/render/EnvironmentFX.{h,cpp}`): a streaming RGBA8888 texture the size of each viewport region, filled with the zenith→horizon gradient (per-ray `env::SkyGradient`, darkening below the horizon as earth shadow), a deterministic sparse **star hash** (`env::StarHash`/`env::SkyStars`, 0 disables) and a **screen-space sun disk + glow** projected from `sky_sun_yaw/pitch` (pure arithmetic smoothstep falloff). The texture is cached across frames and rebuilt only when the camera basis (position/fwd/fov), region size or sky settings change (`SignatureFromSettings`). Drawn behind the geometry in every camera entry region of `RenderViewportTarget` and in the Inspector camera preview (`RenderCameraPreview`).
- **Exponential height fog** (`src/render/EnvironmentCore.h`): `env::HeightFog(density, height_falloff, fog_start, dist, cam_y, world_y)` — fog grows below the camera (`exp(-falloff·(worldY−camY))`, valleys haze up, summits stay clear), returns 0 before `fog_start`, and is applied **per-triangle during rasterization** in `EmitEntityTris` by blending the shaded tint toward the fog color in tint space (textured surfaces fog too, since SDL multiplies vertex tint by the texture sample). `RenderScenePass` now takes the camera position for this.
- **CPU post-processing chain** (`EnvironmentFX::PostProcess`): reads the supersampled target region at `working = region × post_scale`, and runs — all in software, only while enabled — **bloom** (threshold bright extraction, half-res box downsample, separable gaussian blur, added pre-exposure via `post_bloom_strength`), then **exposure / temperature / saturation / contrast**, an **ACES filmic tone map** (Narkowicz fit) and a **12-bit gamma LUT** (`RebuildLUT`, no per-pixel `pow`), blitting the graded result back over the region. Runs **before** the editor overlay so selection bounds and the gizmo stay crisp and ungraded; the camera preview deliberately skips post.
- **Environment & Shading panel** (`src/editor/EnvironmentPanel.{h,cpp}`): collapsible **Sky / Fog / Post-Processing** sections editing the live `EnvironmentSettings` immediately (each slider applies next frame, no undo), a **Reload**/**Save** asset pair with a status/error readout, and a **Material Editor** shortcut that focuses the docked `MaterialPanel`. Docked **behind** the Material Editor in the Shading & Assets primary zone and into the Development Zone and mat_bottom tab groups; "Toggle Environment & Shading" joins the **View menu** and **Command Palette** (View group); folded into the play-mode save/restore.
- **Wiring & versioning**: `Application` owns the `EnvironmentSettings` + `EnvironmentFX` (created beside the libraries, `m_fx.Destroy()` on Shutdown) and the `EnvironmentPanel`; `CMakeLists.txt` adds `src/core/Environment.cpp`, `src/render/EnvironmentFX.cpp` and `src/editor/EnvironmentPanel.cpp` and bumps the version to `0.37.0`.

## [0.36.0-alpha] — 2026-08-18

### Added

- **Physics material assets** (`src/core/PhysicsMaterial.{h,cpp}`): a `PhysicsMaterial` struct — `name`, `friction` (0..1 tangential grip), `restitution` (0..1 bounciness) — round-tripped through the engine's `json::Value` serializer as a `.pmat` file (JSON: `{ "name", "friction", "restitution" }`). `CombinePhysicsMaterials(a, b, &friction, &restitution)` fixes the pair-combination contract a future velocity-based solver must obey: **restitution = max** (the bouncier body wins), **friction = geometric mean** `sqrt(fa*fb)` (a slippery surface wins without ever exceeding either input; `Default<->Default` reproduces the Default material exactly). `PhysicsMaterialLibrary` mirrors `MaterialLibrary` — a cache keyed by caller path, `Load` resolving bare filenames against `assets/physics/` and parsing on first touch, an always-present `"__default__"` entry (0.5 / 0.1), and `Create`/`Save` writing through the same JSON path (creating the directory on demand) while refreshing every cached copy.
- **Collision layer matrix** (`src/core/CollisionMatrix.h`): a fixed **16×16 symmetric** grid of named layers (`rows[16]` bitmasks + `names[16]`, "Default"/"Player"/"Environment"/"Projectile"/…/"Custom"), with `SetPair(a, b, on)` flipping both symmetric entries, a **user-controllable diagonal** (self-collision), `LayersInteract(maskA, maskB)` — true when *any* layer of A is allowed to collide with *any* layer of B — and `ResetAll`. It starts **all-on** and colliders default to the Default layer, so pre-Phase-36 scenes are unchanged by construction. The matrix is **scene state**: it lives on the `Scene`, serializes in the scene file's root `"collision_matrix"` block (`layer_i: { name, mask }`), and — like the theme — has no undo transaction; the physics step reads it live every frame.
- **Physics step gating** (`src/core/PhysicsManager.{h,cpp}`): after the AABB broad-phase test, `Step` skips pairs whose layers don't interact — rejected pairs are pass-through **entirely** (no solid separation, no trigger events), so disabled pairs can't fire stale overlap callbacks either.
- **Collision Matrix panel** (`src/editor/CollisionMatrixPanel.{h,cpp}`): a 17-column table (layer-label column + one per layer) with **inline-editable** row labels (write-through on Enter/release, re-synced from the matrix while idle), symmetric pair checkboxes with layer-naming tooltips, a "Reset All Pairs" action, and a hint tying it to the Inspector's Layer membership control. It edits the `Scene` matrix directly and docks **first** inside the Development Zone and Shading & Assets tab groups so it is one tab away in every workspace; "Toggle Collision Matrix" joins the **View menu** and the **Command Palette** (View group), and the panel is folded into the play-mode save/restore.
- **Inspector Collider section** (`src/editor/InspectorPanel.{h,cpp}`): a **Layers** membership combo (checkboxes over the matrix's live layer names, joined preview, committed as one "Edit Collider Layers" undo step), a **Physics Material** combo ("Default" + every `assets/physics/.pmat`, with a resolved Friction/Restitution readout, null-guarded against a missing library), and **New Physics Material** inline creation (Friction/Restitution sliders + filename box; `Create` writes the `.pmat`, appending the extension when omitted, and assigns it in the same transaction). Collider Reset restores `layers = 1u` and clears the material.
- **Wiring & versioning**: `ColliderComponent` gains `layers` (membership bitmask, default `1u`) and `physics_material` (empty = library Default); `SceneSerializer` round-trips both collider fields and the matrix block; `CommandHistory` snapshots the new fields so layer/material edits undo cleanly; `Application` owns the `PhysicsMaterialLibrary` (created beside the other libraries, passed to the Inspector) and the `CollisionMatrixPanel`; `CMakeLists.txt` adds `src/core/PhysicsMaterial.cpp` and `src/editor/CollisionMatrixPanel.cpp` and bumps the version to `0.36.0`.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 36 chapter ("Physics Materials & Collision Layer Matrix") covering the material model and combination contract, the matrix semantics, the physics-step gate, the panel, the Inspector authoring flow, and serialization/wiring.

### Verified

- Matrix math (symmetry, diagonal control, multi-layer `LayersInteract`, all-on defaults, `ResetAll`) and the combination rules (max restitution, geometric-mean friction, Default identity) pass a standalone harness. (The g++ scratch toolchain on this machine crashes in the linker on `<filesystem>` — a toolchain defect — so the harness covered the pure-math headers and the `.pmat`/filesystem paths are exercised through the MSVC build.) Clean rebuild succeeds; editor smoke run stays alive with the Collision Matrix panel, layer/material wiring, an empty log, and no stray file edits on disk.

## [0.35.0-alpha] — 2026-08-17

### Added

- **Animation component** (`src/core/Animation.h`, `src/core/Components.h`, `src/core/Entity.h`): a new `AnimationComponent` with three transform-property tracks — `position`, `rotation`, `scale` — each an `AnimationTrack` (time-sorted `std::vector<AnimationKeyframe { time, value[3] }>`, Euler **degrees** for rotation), plus per-entity `loop` and `duration` (mirrors the longest key time). Empty tracks are inert, so the component rides every `Entity` with zero cost until keys are recorded.
- **Anim core module** (`src/core/Animation.{h,cpp}`): `SetKeyframe`/`RemoveKeyframe`/`KeyAt` (time-epsilon match, sorted insert), `TrackDuration`, and the samplers — `SampleValue` (linear interpolation, clamped at the edges, `fmod`-wrapped when looping) and `SampleRotation` (spherical interpolation in quaternion space with shortest-arc handling and nlerp fallback). Rotation converts via a verified Euler→quat (`qx⊗qy⊗qz`, matching the renderer's Rx·Ry·Rz order) and its exact inverse; landing on a keyframe time reproduces that key's stored Euler **verbatim**, so recorded poses never re-express through the classic ±180° ambiguity. `Apply` writes a pose but only overwrites properties that carry keys.
- **Sequencing workspace** (`src/core/WorkspaceManager.{h,cpp}`): a new `Workspace::Timeline` preset ("Sequencing") lays out the Hierarchy on the left, the **Timeline** panel center-stage (replacing the viewport, which the Application hides via a new `ViewportPanel` visibility flag), the Inspector + Editor Settings right rail, and the Development Zone + Stats bottom strip; round-trips through the layout save/load ("timeline" key).
- **TimelinePanel** (`src/editor/TimelinePanel.{h,cpp}`): transport row (**Play/Pause**, **Stop**, a scrub slider over the global duration, a Duration drag and a Loop checkbox) plus one **lane** per transform property for the selected entity — keyframe diamonds at their times, a playhead line, a hover crosshair, **click-to-scrub**, **right-click a diamond to remove** the key, and a "+" record button per lane.
- **Inspector keyframe toggles** (`src/editor/InspectorPanel.cpp`): each Transform row (Position/Rotation/Scale) gains a "●" record button that samples the property at the current playhead through the shared `TimelineBridge`; the dot glows amber while a key sits exactly at the playhead on that track.
- **Playback + transport wiring** (`src/core/Application.{h,cpp}`): the Application owns `TimelineState` and the `TimelineBridge`; `ApplyTimeline(dt)` runs in the editor Update stage — advancing the clock while playing (wrap per Loop, clamp-and-stop at the end) and writing sampled poses to **every** animated entity, gated on a dirty flag so a paused timeline never stomps gizmo/Inspector edits. `PlayPauseTimeline`/`StopTimeline`/`ScrubTimeline` drive the transport; `SetTimelineKeyframe`/`RemoveTimelineKeyframe` record/remove keys in a single **undo transaction** ("Set Keyframe"/"Remove Keyframe") and stretch the global duration when a key lands past its edge. A new `ApplyWorkspace`/`ResetWorkspaceDefault` wrapper applies workspace **side effects** (viewport hidden in Sequencing, timeline playback stopped when leaving it); the gizmo interaction is additionally gated off while the timeline plays. "Switch to Sequencing Workspace" joins the **Command Palette** (Workspace) and the **Workspace menu**; "Play Timeline"/"Stop Timeline" join the palette's Transport group.
- **Serialization & undo**: `SceneSerializer` round-trips an `"animation"` object (loop/duration + per-track `{ time, value:[x,y,z] }` key arrays, emitted only when keys exist, re-sorted + duration-recomputed on load); `CommandHistory` snapshots the animation fields so key record/remove undoes cleanly like any other property edit.
- **Wiring & versioning**: `CMakeLists.txt` adds `src/core/Animation.cpp` and `src/editor/TimelinePanel.cpp` and bumps the project version to `0.35.0`.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 35 chapter ("Animation & Timeline Foundation") covering the component, the sampling math (LERP/SLERP + Euler/quat convention), the Sequencing workspace, the timeline panel, and the playback/undo/serialization story.

### Verified

- Clean rebuild succeeds. Editor smoke run stays alive with the timeline panel, sequencing workspace, bridge and playback wiring, an empty log, and no stray file edits on disk.

## [0.34.0-alpha] — 2026-08-16

### Added

- **Procedural landscape component** (`src/core/Components.h`, `src/core/Entity.h`): a new `LandscapeComponent { enabled=false, resolution=64, size=40, base_height=0, heights (std::vector<float>, row-major), shared_ptr<Mesh> mesh, mesh_dirty }` — `resolution × resolution` quads spanning `size` world units centered on the entity, with per-vertex heights. The mesh is runtime-only (never serialized); `enabled` defaults to **false** so ordinary entities are untouched.
- **Landscape module** (`src/core/Landscape.{h,cpp}`): `LandscapeInitialize` zero-fills the heightfield, `LandscapeRebuildMesh` generates the grid (triangle winding so normals face +Y, sparse surface-riding wireframe `edge_lines` every `res/8` grid line, AABB from min/max height), and the sculpt kernels — **Raise** (smoothstep-falloff elevation), **Smooth** (relax toward the 4-neighbor average), and **Flatten** (blend toward the bilinear-sampled height at the brush center) — operate in the landscape's **local** grid space, converted via `LandscapeWorldToLocal` (affine 4×4 inverse, so rotated/scaled terrains sculpt correctly) with the world radius scaled by `LandscapeWorldScale`. `LandscapeSampleHeightLocal` bilinearly samples heights for flattening; `LandscapeRaycast` does a local slab-AABB slab test → cell-scale marching → 12-iteration bisection for the viewport pick.
- **Landscape Mode workspace** (`src/core/WorkspaceManager.{h,cpp}`): a new `Workspace::Landscape` preset ("Landscape Mode") lays out a right-side rail with the Landscape panel on top and Inspector/Settings below, the Hierarchy on the left, and the Development Zone + Stats on the bottom (Script Editor stays floating), and round-trips through the layout save/load ("landscape" key).
- **Sculpt viewport override** (`src/core/Application.cpp`): while the workspace is Landscape Mode and the brush target has `landscape.enabled`, the transform gizmo is replaced by `UpdateLandscapeBrush` — a camera-basis pick ray (same math as `ComputeDropWorldPos`) ray-casts the terrain each frame, stores the hit, and while LMB is held stamps the brush (`strength × dt` per frame) inside a single **"Sculpt Landscape"** `BeginEntityEdit`/`EndEntityEdit` transaction per stroke; the overlay draws a projected brush-sphere cursor (outer + inner cap ring, depth pole, center cross) instead of the gizmo.
- **LandscapePanel** (`src/editor/LandscapePanel.{h,cpp}`): brush **Size** (0.5–20), **Strength** (0.01–2) and **Falloff** (0–1) sliders, a Raise / Smooth / Flatten tool palette, the target entity combo with a "+" Create Landscape action, and an empty-state Create button — it edits the shared `LandscapeBrushSettings` and routes creation through the Application (spawn + undo + selection + workspace switch).
- **Creation + wiring**: a new `Application::CreateLandscape` spawns the "Landscape" entity (green-tinted, 64×64 × 40 m, placed ~6 m in front of the editor camera), pushes a `PushSpawn` undo record, selects it, arms the brush target and toasts "Created 'Landscape'". "Create Landscape" entries land in the **Command Palette** (Create) and the **viewport right-click menu** and switch to Landscape Mode after spawning; "Switch to Landscape Mode" joins the palette and Workspace menu. The per-entity AABB refresh loop, the three render passes and the selection outline now resolve meshes through a new `ResolveEntityMesh` (landscape mesh if enabled, else the mesh library), with load failures still surfaced to the status bar. `CommandHistory` snapshots the landscape fields (including a copy of the heights vector) so sculpt strokes and spawn/delete undo cleanly; `SceneSerializer` round-trips `enabled/resolution/size/base_height` + the heights array and rebuilds the mesh on load.
- **Wiring & versioning**: `CMakeLists.txt` adds `src/core/Landscape.cpp` and `src/editor/LandscapePanel.cpp` and bumps the project version to `0.34.0`.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 34 chapter ("Landscape & Topology Design Suite") covering the heightfield component, the mesh builder, the sculpt kernels, the ray-cast pick, the workspace viewport override and the undo/serialization story.

### Verified

- Clean rebuild succeeds. Editor smoke run stays alive with the landscape workspace, panel, brush cursor and sculpt transaction wiring, an empty log, and no stray file edits on disk.

## [0.33.0-alpha] — 2026-08-15

### Added

- **True workspace layouts** (`src/core/WorkspaceManager.{h,cpp}`): the built-in workspaces now describe canonical dock layouts for the unified **Script Editor** mini-IDE — the Scripting workspace hosts the whole IDE (browser sidebar + tab bar + code pane in one docked window) in a taller bottom strip beside the Development Zone tabs, and the Level Design workspace docks it in the bottom-right beside the Stats/Content Browser/Console group. `ApplyWorkspace` returns the dock node reserved for the IDE (0 = floating, as in Shading & Assets), and a new **Reset to Workspace Default** action rebuilds the *active* workspace's canonical layout and forgets any captured custom layout (returning the IDE node to route). The old split sidebar + floating code-window model is gone.
- **Tabbed mini-IDE** (`src/editor/ScriptEditorPanel.{h,cpp}`): the single dockable "Script Editor" window now keeps multiple `.lua` scripts open as tabs — every tab owns its own `TextEditor` buffer, undo stack, and canonical saved-on-disk baseline, so switching tabs never loses edits. Dirty tabs carry a `*` (amber) and per-tab `x` close buttons confirm unsaved changes (Save / Discard / Cancel); a toolbar offers Save / Save & Reload / Auto-save and a **Float / Dock to Workspace** toggle that pops the whole window out of the dock or routes it back through the Application's redock callback. Ctrl+S, auto-save-on-blur, and the external-edit disk watcher all carry over, scoped to the active tab. The Content Browser's `.lua` double-click still opens files through `RequestOpen`.
- **Mini-IDE theme + gutter fill** (`third_party/ImGuiColorTextEdit/TextEditor.{h,cpp}`): a new `PaletteIndex::LineNumberFill` paints a contrasting gutter strip behind the line numbers (added to the dark/light/retro palettes), and the IDE applies its own accent-tuned palette (deep navy-black background, accent current-line, `#82AAFF` known identifiers).
- **Wiring & versioning**: `Application::Init` builds the `ScriptEditorPanel` with a redock callback that re-applies the current workspace, and all three reset entries (Workspace menu, View menu, command palette) now route the returned IDE node. `CMakeLists.txt` bumps the project version to `0.33.0`.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 33 chapter ("True Workspace Layouts, Tabbed Mini-IDE & Theme") covering the state-driven workspace presets, the reset-to-workspace action, the tabbed IDE, the Float/Dock toggle, and the mini-theme.

### Verified

- Clean rebuild succeeds. Editor smoke run stays alive with the workspace presets, tabbed IDE, Float/Dock toggle and gutter-filled mini-theme wired, an empty log, and no stray file edits on disk.

## [0.32.0-alpha] — 2026-08-14

### Added

- **ScriptEngine REPL** (`src/script/ScriptEngine.{h,cpp}`): a new `Execute(scene, code, error)` method evaluates live Lua snippets against the active scene in a persistent, isolated scratchpad VM. The state is created lazily on first use and survives play sessions, so REPL definitions persist for the whole editor run. The scratchpad `_ENV` is chained to the engine API (Vector3 / Audio / print / the stdlib resolve without polluting any play session's globals) and exposes a `scene` table with `count()` / `get(i)` (1-based) / `find(name)` / `name`; entities are the same `Singe.Entity` userdata gameplay scripts use, so snippets can read and mutate transforms in place. `print()` output and any chunk return values (`=> ...`, one `tostring()` per value) route to the Console sink; compile and runtime errors are returned and logged as Error.
- **ConsolePanel REPL command line** (`src/editor/ConsolePanel.{h,cpp}`): the console gains a Lua input row under the log — Enter runs the snippet through `on_execute` (wired by the Application to `ScriptEngine::Execute`), Up/Down walk the input history, Escape clears the line, and the field keeps focus so multi-line tinkering stays in place.
- **ScriptEditorPanel real-time editing hooks** (`src/editor/ScriptEditorPanel.{h,cpp}`): an **Auto-save** toggle (default on) writes the buffer the instant the code window loses focus — and, when a play session is live, hot-reloads it through the existing `ReloadSession` callback. A disk watcher compares the open file's mtime against the last open/save: an external edit (e.g. another tool) is adopted and the live session reloaded, and a dirty buffer is never clobbered (surfaced in the status line instead).
- **Wiring & versioning**: `Application::Init` binds the console's `on_execute` to `ScriptEngine::Execute` against the active scene; `CMakeLists.txt` bumps the project version to `0.32.0`.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 32 chapter ("Integrated Lua Scripting IDE") covering the REPL VM, the scene bindings, the console command line, and the editor's real-time save/hot-reload hooks.

### Verified

- Clean rebuild succeeds. Editor smoke run stays alive with the REPL state + console command line + auto-save/external-reload hooks wired and an empty log.

## [0.31.0-alpha] — 2026-08-14

### Added

- **AssetCatalog taxonomy** (`src/core/AssetCatalog.h`, pure header-only, no SDL/ImGui): extension-based asset classification mirroring `AssetImporter` (including the `<name>.prefab.json` prefab convention and `.wav/.ogg` audio), category-chip filtering (`All` / `Meshes` / `Materials` / `Textures` / `Audio` / `Prefabs`, folders always passing so navigation survives an active filter), case-insensitive name search matching, and breadcrumb path splitting — one shared taxonomy for the browser UI and the OS importer.
- **ThumbnailCache render module** (`src/render/ThumbnailCache.{h,cpp}`): lazy off-screen preview generation with an in-memory cache keyed by asset path. `.obj` meshes render into a 96×96 `SDL_TEXTUREACCESS_TARGET` texture with a bounds-framing orbit camera (perspective, `Mat4LookAt`/`Mat4Perspective`), flat shading from the engine's default directional light, painter's-algorithm depth-sorted fills via `SDL_RenderGeometry`, and the engine's wireframe edge pass; `.mat` materials render a diffuse-color swatch with a border; image assets borrow the already-decoded `TextureLibrary` GPU texture (never owned). The render target is saved/restored around generation so the panel's thumbnails never disturb the ImGui blit; `Shutdown()` releases owned textures while the renderer is still alive in `Application::Shutdown` (panels are torn down first).
- **ContentBrowserPanel rework** (Phase 31): the file area switches between the responsive **grid** and a compact **list view** (small preview, name, type label, human-readable file size). A **Thumb** slider scales the preview cells (48–192 px); a live **search** box filters item names case-insensitively; **category chips** (All/Meshes/Materials/Textures/Audio/Prefabs) hide files that aren't of the requested kind while keeping folders visible for navigation; **breadcrumbs** split the current path into clickable segments that jump straight to that folder. Mesh/material items draw real off-screen thumbnails; image assets keep their aspect-fit preview; everything else falls back to the colored per-type badge. Audio (.wav/.ogg) is now classified and labeled in both views.
- **Wiring & versioning**: `Application::Init` passes the SDL renderer + `MeshLibrary` into `ContentBrowserPanel`; `CMakeLists.txt` adds `src/render/ThumbnailCache.cpp` and bumps the project version to `0.31.0`.
- **Documentation**: `docs/Singularity_Architecture_Textbook.md` gains a Phase 31 chapter ("Advanced Content Browser & Thumbnail Generator") covering the taxonomy, the off-screen thumbnail pipeline, the grid/list views with search + chips + breadcrumbs, and the harness.

### Verified

- New `phase31_asset_catalog_test` harness (links standalone, pure Core): extension classification across every supported kind (including `.prefab.json` vs plain `.json`, audio, unknown extensions), category-chip pass/fail logic (folders always pass), case-insensitive search matching, and breadcrumb segmentation — all checks pass. Clean rebuild succeeds; editor smoke run stays alive with the thumbnail cache + new views wired and an empty log.

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
