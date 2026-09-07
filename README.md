# Singularity Engine

A 3D game engine and editor, written from scratch in C++20, that renders
entirely on the CPU — no OpenGL, Vulkan, or Direct3D anywhere in the
pipeline. Every stage a GPU normally owns (vertex transforms, projection,
triangle rasterization, depth sorting, shading) is engine code; the only
thing SDL2 is asked to do is blit the finished triangles to the screen.

[![Build](https://github.com/SergioSNW/singularity-engine/actions/workflows/build.yml/badge.svg)](https://github.com/SergioSNW/singularity-engine/actions/workflows/build.yml)

**v0.54.0-alpha** — [full version history in CHANGELOG.md](CHANGELOG.md)

## What this is (and isn't)

Singularity Engine is a portfolio project and a real, working editor +
runtime: a docked Dear ImGui workspace for building scenes, an embedded
Lua 5.4 scripting layer for gameplay, and an export pipeline that turns
a scene into a standalone double-click-able build. It's aimed at retro-
styled, moderate-complexity 3D games — the kind of scope a software
rasterizer can comfortably push in real time — not at competing with
Unity or Unreal. Every system in it (physics, audio, terrain, animation,
scripting, UI) was built to be *just capable enough* to ship a small game
end to end, not to be a general-purpose AAA engine.

## Highlights

- **CPU software rasterizer** — a full 3D pipeline (world/view/projection
  transforms, backface culling, per-triangle depth sorting, directional
  lighting with soft ray-cast shadows) built on top of SDL2's 2D
  `SDL_RenderGeometry`, with zero GPU 3D API dependency.
- **A real editor** — docking Dear ImGui workspace with a scene hierarchy,
  inspector, content browser with thumbnail generation, material/texture
  authoring, an integrated Lua script IDE, a real-time performance
  profiler, undo/redo history, and saved custom workspace layouts.
- **Procedural terrain** — sculptable heightfield landscapes (raise,
  lower, smooth, flatten) with per-vertex material painting, imported
  heightmaps, and a shared paint palette used by both terrain and
  placed-object materials.
- **Embedded Lua scripting** — every entity can carry a script with
  `OnStart`/`OnUpdate`/`OnTriggerEnter`/`OnCollisionEnter`/`OnGUI`
  lifecycle hooks, bound against the real engine API: `Game.*` (health,
  score, win/lose, scene loading), `UI.*` (script-drawn menus and HUDs),
  `Audio.*`, `Input.*`, and direct `Vector3`/`Transform`/`Entity`
  manipulation.
- **A working gameplay loop** — a WASD + gravity character controller
  with AABB and landscape collision, a physics layer with solid/trigger
  colliders and a collision matrix, material-aware footstep audio, a
  gameplay HUD (health/score/prompt/win-lose), scene-to-scene transitions
  with a fade, and script-driven UI for building menus on top of all of
  it.
- **Ships as a real build** — "Export Build" packages the current scene,
  every asset it needs, and a copy of the engine into a folder whose
  `.exe` boots straight into the game, no editor, no arguments.

## Try it

Three scenes under `assets/scenes/` demonstrate the full loop end to end:

```
main_menu.scene  →  level_1.scene  →  level_2.scene
  (Start button)      (walk into        (walk into
   drawn by            the exit          the goal
   UI.* + OnGUI)        trigger)          trigger)
```

Open `main_menu.scene` in the editor and hit Play, or launch a built exe
straight into it:

```bash
singularity-engine.exe --play assets/scenes/main_menu.scene
```

The scripts behind all three (`assets/scripts/main_menu.lua`,
`level_exit.lua`, `goal_zone.lua`) are short and meant to be read as
usage examples, along with `collectible.lua`, `damage_zone.lua`,
`hazard_zone.lua`, and `bouncer.lua`.

## Building

No manual dependency installation — SDL2, SDL_mixer, Dear ImGui (docking
branch), and Lua 5.4 are all fetched and built statically via CMake's
`FetchContent` the first time you configure.

**Windows (Visual Studio 2022):**

```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
.\build\Debug\singularity-engine.exe
```

**Ninja (Windows, Linux, or macOS):**

```bash
cmake -B build -G Ninja
cmake --build build
./build/singularity-engine
```

Requires a C++20 compiler and CMake 3.16+. The first configure downloads
and builds all four dependencies, so it's noticeably slower than every
build after it.

## Project structure

```
src/
├── core/     Scene/entity model, the software renderer, physics,
│             audio, input, animation, terrain, save/load, the
│             export/runtime pipeline — everything not editor-only.
├── script/   The Lua 5.4 embedding (ScriptEngine) and the engine-API
│             bindings scripts see (Game.*, UI.*, Audio.*, Input.*).
├── editor/   Dear ImGui panels — hierarchy, inspector, content
│             browser, material/terrain/timeline/environment editors,
│             the script IDE, command palette, profiler.
└── render/   Supporting render-side helpers (environment FX, thumbnail
              generation).

assets/
├── scenes/   Demo scenes (main_menu → level_1 → level_2).
├── scripts/  Example Lua gameplay scripts.
├── meshes/   .obj mesh assets.
└── audio/    Procedurally-generated sound effects.

docs/
└── Singularity_Architecture_Textbook.md   The canonical, per-phase
                                            engineering log: why each
                                            system is built the way it
                                            is, not just what it does.
```

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — every shipped version, what changed and why.
- [docs/Singularity_Architecture_Textbook.md](docs/Singularity_Architecture_Textbook.md) —
  the full architecture history, one numbered phase per milestone,
  written as design rationale rather than a feature list.

## License

[MIT](LICENSE)
