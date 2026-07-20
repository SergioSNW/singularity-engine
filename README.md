# Singularity Engine

A minimal, thermally-efficient 2D game engine and editor written in C++20.

## Version

v0.3.1-alpha — Modular Editor Panels

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
├── core/      Game loop and ECS logic
├── render/    SDL2 rendering wrappers
└── editor/    Dear ImGui editor panels
```

## License

MIT
