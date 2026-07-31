#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Json.h"

struct SDL_Texture;
class Window;
class EditorPanel;
struct SelectionState;
class ViewportPanel;
class Scene;
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
    void SaveScene();
    void OpenScene();
    void EnterPlayMode();
    void ExitPlayMode();

    Window *m_window;
    bool m_running;
    bool m_flying;
    bool m_layout_initialized;
    SelectionState *m_selection;
    ViewportPanel *m_viewport;
    Scene *m_scene;
    SDL_Texture *m_viewport_target;
    int m_viewport_target_w;
    int m_viewport_target_h;
    float m_camera_scroll;
    float m_ui_scale;
    float m_applied_ui_scale;
    bool m_recreate_viewport;
    std::string m_scene_path;
    std::string m_scene_status;
    EngineState m_state;
    json::Value m_scene_snapshot;   // pre-play backup; restored on Stop
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};
