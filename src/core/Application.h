#pragma once

#include <memory>
#include <vector>

class Window;
class EditorPanel;

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
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};
