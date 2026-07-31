#pragma once

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;

class Window
{
public:
    Window(int width, int height, const char *title);
    ~Window();

    int GetWidth()  const;
    int GetHeight() const;

    // Keep the cached client size in sync with the OS-reported window size.
    void OnResize(int width, int height);

    SDL_Window*   GetNativeWindow()   const;
    SDL_Renderer* GetNativeRenderer() const;

private:
    SDL_Window   *m_window;
    SDL_Renderer *m_renderer;
    int m_width;
    int m_height;
};
