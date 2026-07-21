cmake_minimum_required(VERSION 3.16)
project(singularity-engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-2.30.9
  GIT_SHALLOW TRUE
  OVERRIDE_FIND_PACKAGE
)

set(SDL_STATIC ON)
set(SDL_SHARED OFF)
set(SDL_TEST OFF)
set(SDL_VULKAN OFF)
set(SDL_METAL OFF)
set(SDL_X11 OFF)
set(SDL_WAYLAND OFF)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL2)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

add_executable(singularity-engine)

target_sources(singularity-engine PRIVATE
  src/core/main.cpp
  src/core/Window.cpp
  src/core/Application.cpp
  src/editor/StatsPanel.cpp
  src/editor/SceneHierarchyPanel.cpp
  src/editor/InspectorPanel.cpp
  src/editor/ViewportPanel.cpp
  ${IMGUI_SOURCES}
)

target_link_libraries(singularity-engine PRIVATE
  SDL2::SDL2
  SDL2::SDL2main
)

target_include_directories(singularity-engine PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_include_directories(singularity-engine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(singularity-engine PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
