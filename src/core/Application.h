#pragma once

#include <memory>
#include <vector>

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
    Window *m_window;
    bool m_running;
    bool m_layout_initialized;
    SelectionState *m_selection;
    ViewportPanel *m_viewport;
    Scene *m_scene;
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};
