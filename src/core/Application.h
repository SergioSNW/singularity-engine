#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Json.h"
#include "editor/Theme.h"
#include "core/LayoutManager.h"

struct SDL_Texture;
struct Mat4;
struct Vec3;
struct Mesh;
class Window;
class EditorPanel;
struct SelectionState;
class ViewportPanel;
class Scene;
class MeshLibrary;
class GizmoController;
class ScriptEngine;
class PhysicsManager;
class ScriptEditorPanel;
class CommandPalette;
class SettingsPanel;
struct Entity;

// Editor runtime state machine. Play mode isolates the viewport as a full-window
// game view and snapshots the scene so Stop restores it exactly.
enum class EngineState
{
    Editor,
    Play,
};

class Application
{
public:
    Application();
    ~Application();

    bool Init(int width, int height, const char *title);
    void Run();
    void Shutdown();

private:
    void RecreateViewportTarget(int width, int height);
    void RenderViewportTarget();
    void UpdateCameraControls(float dt);
    Entity *FindActiveCamera();
    bool BuildViewProj(Mat4 &view_proj, Vec3 &cam_pos, float &fov, float &pitch,
                       float &yaw, float &near_p, float &far_p);
    const Mesh *ResolveMesh(const Entity &entity, std::string &error);
    void SaveScene();
    void OpenScene();
    void EnterPlayMode();
    void ExitPlayMode();

    Window *m_window;
    bool m_running;
    bool m_flying;
    SelectionState *m_selection;
    ViewportPanel *m_viewport;
    Scene *m_scene;
    MeshLibrary *m_mesh_library;
    GizmoController *m_gizmo;
    ScriptEngine *m_script_engine;
    PhysicsManager *m_physics;
    ScriptEditorPanel *m_script_editor;
    CommandPalette *m_command_palette;
    SettingsPanel *m_settings_panel;
    SDL_Texture *m_viewport_target;
    int m_viewport_target_w;
    int m_viewport_target_h;
    float m_camera_scroll;
    float m_ui_scale;
    float m_applied_ui_scale;
    float m_dpi_scale;
    Theme::Fonts m_fonts;
    Theme::Colors m_theme_colors;
    LayoutManager m_layout;
    bool m_recreate_viewport;
    std::string m_scene_path;
    std::string m_scene_status;
    std::string m_mesh_error;
    EngineState m_state;
    json::Value m_scene_snapshot;   // pre-play backup; restored on Stop
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};
