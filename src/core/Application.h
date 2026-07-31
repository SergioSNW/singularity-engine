#pragma once

#include <memory>
#include <string>
#include <vector>

struct SDL_Texture;
class Window;
class EditorPanel;
struct SelectionState;
class ViewportPanel;
class Scene;
struct Entity;

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
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};
