cmake_minimum_required(VERSION 3.16)
project(singularity-engine VERSION 0.13.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)

set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF)
set(SDL_METAL OFF)
set(SDL_X11 OFF)
set(SDL_WAYLAND OFF)

FetchContent_Declare(
  SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-2.30.9
  GIT_SHALLOW TRUE
  OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
  GIT_SHALLOW TRUE
)

# Lua 5.4: the embedded scripting runtime for per-entity gameplay scripts.
# Its C sources are compiled straight into the engine executable (no shared
# library). The ScriptEngine in src/script/ drives it with the raw C API.
FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.7
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL2)
FetchContent_MakeAvailable(imgui)
FetchContent_MakeAvailable(lua)

# Lua core library: every .c except the standalone interpreter (lua.c), the
# bytecode compiler (luac.c), the one-lua amalgamation (onelua.c), and the test
# scaffolding (ltests.c).
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luaoc|luac|onelua|ltests)\.c$")

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
  src/core/Scene.cpp
  src/core/Json.cpp
  src/core/SceneSerializer.cpp
  src/core/Mesh.cpp
  src/core/LayoutManager.cpp
  src/script/ScriptEngine.cpp
  src/editor/StatsPanel.cpp
  src/editor/SceneHierarchyPanel.cpp
  src/editor/InspectorPanel.cpp
  src/editor/ViewportPanel.cpp
  src/editor/GizmoController.cpp
  src/editor/ScriptEditorPanel.cpp
  src/editor/Theme.cpp
  third_party/ImGuiColorTextEdit/TextEditor.cpp
  ${IMGUI_SOURCES}
  ${LUA_SOURCES}
)

target_link_libraries(singularity-engine PRIVATE
  SDL2::SDL2
  SDL2::SDL2main
)

target_include_directories(singularity-engine PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${lua_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(singularity-engine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

set_target_properties(singularity-engine PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  ENABLE_EXPORTS OFF
)

target_compile_definitions(singularity-engine PRIVATE _USE_MATH_DEFINES _CRT_DECLARE_NONSTDC_NAMES=1)
target_link_options(singularity-engine PRIVATE -static)

# Keep runtime assets (mesh library, default scenes) next to the executable so
# relative "assets/..." paths resolve regardless of the working directory.
add_custom_command(TARGET singularity-engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:singularity-engine>/assets
)
