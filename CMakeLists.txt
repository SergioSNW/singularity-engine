cmake_minimum_required(VERSION 3.16)
project(singularity-engine VERSION 0.27.0 LANGUAGES C CXX)

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

# SDL_mixer 2.8.2: the audio playback backend for the Phase 26 sound
# subsystem. Built statically (BUILD_SHARED_LIBS OFF) into the engine
# executable, like SDL2 itself. Only codecs with no external dependencies are
# enabled: WAV (SDL's native decoder, SDL2MIXER_WAV) and OGG Vorbis via the
# stb_vorbis source bundled inside SDL_mixer (SDL2MIXER_VORBIS=STB). Every
# other codec is switched off, so no system audio libraries are needed to
# build or run the engine.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VENDORED OFF)
set(SDL2MIXER_SAMPLES OFF)
set(SDL2MIXER_INSTALL OFF)
set(SDL2MIXER_CMD OFF)
set(SDL2MIXER_DEPS_SHARED OFF)
set(SDL2MIXER_WAV ON)
set(SDL2MIXER_WAVE ON)
set(SDL2MIXER_VORBIS "STB")
set(SDL2MIXER_FLAC OFF)
set(SDL2MIXER_MOD OFF)
set(SDL2MIXER_MP3 OFF)
set(SDL2MIXER_MIDI OFF)
set(SDL2MIXER_OPUS OFF)
set(SDL2MIXER_GME OFF)
set(SDL2MIXER_WAVPACK OFF)

FetchContent_Declare(
  SDL_mixer
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
  GIT_TAG release-2.8.2
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL_mixer)

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
  src/core/SceneManager.cpp
  src/core/PhysicsManager.cpp
  src/core/Mesh.cpp
  src/core/Material.cpp
  src/core/Texture.cpp
  src/core/WorkspaceManager.cpp
  src/core/Console.cpp
  src/core/CommandHistory.cpp
  src/core/AssetImporter.cpp
  src/core/AudioManager.cpp
  src/core/CameraManager.cpp
  src/script/ScriptEngine.cpp
  src/editor/StatsPanel.cpp
  src/editor/SceneHierarchyPanel.cpp
  src/editor/InspectorPanel.cpp
  src/editor/ViewportPanel.cpp
  src/editor/GizmoController.cpp
  src/editor/ScriptEditorPanel.cpp
  src/editor/CommandPalette.cpp
  src/editor/SettingsPanel.cpp
  src/editor/ContentBrowserPanel.cpp
  src/editor/ConsolePanel.cpp
  src/editor/MaterialPanel.cpp
  src/editor/HistoryPanel.cpp
  src/editor/ViewportLayoutPanel.cpp
  src/editor/Theme.cpp
  third_party/ImGuiColorTextEdit/TextEditor.cpp
  ${IMGUI_SOURCES}
  ${LUA_SOURCES}
)

target_link_libraries(singularity-engine PRIVATE
  SDL2::SDL2
  SDL2::SDL2main
  SDL2_mixer::SDL2_mixer-static
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
