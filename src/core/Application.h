#pragma once

class Window;

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
};
