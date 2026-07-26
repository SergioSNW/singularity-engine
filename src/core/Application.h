#pragma once

#include <memory>
#include <vector>

struct SDL_Texture;
class Window;
class EditorPanel;
struct SelectionState;
class ViewportPanel;
class Scene;

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

    Window *m_window;
    bool m_running;
    bool m_layout_initialized;
    SelectionState *m_selection;
    ViewportPanel *m_viewport;
    Scene *m_scene;
    SDL_Texture *m_viewport_target;
    int m_viewport_target_w;
    int m_viewport_target_h;
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};
