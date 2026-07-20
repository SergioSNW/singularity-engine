#include "Window.h"

#include <SDL.h>

Window::Window(int width, int height, const char *title)
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_width(width)
    , m_height(height)
{
    m_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN
    );

    if (m_window)
    {
        m_renderer = SDL_CreateRenderer(
            m_window,
            -1,
            SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED
        );
    }
}

Window::~Window()
{
    if (m_renderer)
        SDL_DestroyRenderer(m_renderer);
    if (m_window)
        SDL_DestroyWindow(m_window);
}

int Window::GetWidth()  const { return m_width; }
int Window::GetHeight() const { return m_height; }

SDL_Window*   Window::GetNativeWindow()   const { return m_window; }
SDL_Renderer* Window::GetNativeRenderer() const { return m_renderer; }
