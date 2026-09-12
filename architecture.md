# Architecture

> **Current version:** v0.55.0-alpha — see [CHANGELOG.md](CHANGELOG.md) for the
> full version history and [README.md](README.md) for the current feature set.

This file was originally written as a pre-implementation design brief —
a plan for what the engine would be before any of it existed. Most of
that plan changed once real constraints showed up (the ECS became plain
data structs directly on each `Entity`, not a library like `entt`; the
renderer ended up doing a full custom 3D pipeline rather than staying
2D; precompiled headers were never needed). Rather than deleting that
history or leaving it to quietly go stale next to the real
implementation, it's kept below as the original brief, clearly marked as
superseded.

**For how the engine actually works today**, see:

- [README.md](README.md) — current feature set, build instructions, and
  what the project is (and isn't) aiming to be.
- [docs/Singularity_Architecture_Textbook.md](docs/Singularity_Architecture_Textbook.md) —
  the canonical, actively-maintained architecture reference: one numbered
  phase per milestone, written as design rationale (why a system is built
  the way it is) rather than a changelog of what shipped.

---

## Original design brief (superseded — kept for history)

### Overview

Singularity Engine was originally scoped as a 2D video game engine for
PC, written in C++, designed with a focus on minimalism, resource
efficiency, and thermal control on hardware with limited cooling. The
implementation grew well past 2D — the renderer is a full software 3D
pipeline (world/view/projection transforms, backface culling, per-
triangle depth sorting, directional lighting with soft shadows), and
"minimalism" came to mean *no GPU 3D API dependency*, not a small
feature set.

### Original design principles

- **Thermal efficiency by design:** a low processing footprint to avoid
  overheating during long editor sessions.
- **Minimized compile times:** fast incremental builds.
- **Clean architecture:** decoupled core subsystems (rendering, physics,
  core logic) for maintainability and isolated testing.

### Originally proposed technology stack

- **Language:** C++17/20.
- **Windowing & input:** SDL2 or SFML.
- **Editor interface:** Dear ImGui.
- **Build system:** CMake + Ninja.
- **Data architecture:** an ECS library (e.g. `entt`) for cache locality
  and modularity.

What was actually built: SDL2 (windowing, input, and the 2D
`SDL_RenderGeometry` call every rasterized triangle ultimately goes
through), Dear ImGui (docking branch), CMake with `FetchContent` for
zero-install dependencies (also fetching Lua 5.4 and SDL_mixer, neither
of which were part of the original plan), and a hand-rolled ECS — each
`Entity` is a plain struct holding every component inline, no external
library.

### Thermal/performance rules that did carry through

- The editor targets V-Sync-capped frame pacing rather than an unlocked
  loop.
- Headers stay light where practical (forward declarations over heavy
  includes) to keep incremental builds fast — informal discipline rather
  than a hard rule enforced by tooling.
- Precompiled headers and a static-library-per-subsystem split were
  planned but never implemented; a single executable target with
  `FetchContent`-managed dependencies turned out to be simple enough that
  neither was needed at this project's size.
