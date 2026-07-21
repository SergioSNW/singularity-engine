# Singularity Engine

A minimal, thermally-efficient 2D game engine and editor written in C++20.

## Version

v0.5.0-alpha — Selection, Viewport, Custom Theme

## Dependencies

- [SDL2](https://github.com/libsdl-org/SDL) (2.30.x) — windowing, input, 2D rendering
- Auto-resolved via CMake FetchContent — no manual install required.

## Build

```bash
cmake -B build -G Ninja
cmake --build build
./build/singularity-engine
```

## Project Structure

```
src/
├── core/          Game loop and ECS logic
│   ├── main.cpp
│   ├── Application.h/.cpp
│   ├── Window.h/.cpp
├── render/        SDL2 rendering wrappers (future)
└── editor/        Dear ImGui editor panels
    ├── EditorPanel.h            (base interface)
    ├── StatsPanel.h/.cpp        (diagnostics)
    ├── SceneHierarchyPanel.h/.cpp (scene tree)
    └── InspectorPanel.h/.cpp    (property editor)
```

## License

MIT
