# Singularity Engine

A minimal, thermally-efficient 2D game engine and editor written in C++20.

## Version

v0.28.0-alpha — Final UI & Workspace Consolidation

## Dependencies

- [SDL2](https://github.com/libsdl-org/SDL) (2.30.x) — windowing, input, 2D rendering
- [SDL_mixer](https://github.com/libsdl-org/SDL_mixer) (2.8.x) — audio playback (WAV + stb_vorbis OGG)
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
    │   ├── CameraManager.h/.cpp (multi-viewport camera stack)
    │   ├── ToastManager.h/.cpp  (editor toast notifications)
    │   ├── Window.h/.cpp
    ├── render/        SDL2 rendering wrappers (future)
    └── editor/        Dear ImGui editor panels
        ├── EditorPanel.h            (base interface)
        ├── StatsPanel.h/.cpp        (diagnostics)
        ├── SceneHierarchyPanel.h/.cpp (scene tree)
        ├── InspectorPanel.h/.cpp    (property editor)
        └── ViewportLayoutPanel.h/.cpp (camera layout editor)
```

## License

MIT
