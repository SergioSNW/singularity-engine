#include "Application.h"
#include "Window.h"
#include "editor/EditorPanel.h"
#include "editor/SelectionState.h"
#include "editor/StatsPanel.h"
#include "editor/SceneHierarchyPanel.h"
#include "editor/InspectorPanel.h"
#include "editor/ViewportPanel.h"

#include <SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>

static const double TARGET_FRAME_TIME = 1.0 / 60.0;

void ConfigureImGuiStyle()
{
    ImGuiStyle &style = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowRounding = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg]          = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_TitleBg]           = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive]     = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg]         = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_Header]            = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered]     = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive]      = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    style.Colors[ImGuiCol_Button]            = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered]     = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive]      = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    style.Colors[ImGuiCol_FrameBg]           = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive]     = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_Tab]               = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_TabHovered]        = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_TabActive]         = ImVec4(0.20f, 0.20f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocusedActive]= ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
}

Application::Application()
    : m_window(nullptr)
    , m_running(false)
    , m_selection(nullptr)
{
}

Application::~Application()
{
    Shutdown();
}

bool Application::Init(int width, int height, const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return false;

    m_window = new Window(width, height, title);

    if (!m_window->GetNativeWindow() || !m_window->GetNativeRenderer())
        return false;

    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ConfigureImGuiStyle();
    ImGui_ImplSDL2_InitForSDLRenderer(
        m_window->GetNativeWindow(),
        m_window->GetNativeRenderer()
    );
    ImGui_ImplSDLRenderer2_Init(m_window->GetNativeRenderer());

    m_selection = new SelectionState();

    m_panels.push_back(
        std::make_shared<StatsPanel>(m_window->GetWidth(), m_window->GetHeight())
    );
    m_panels.push_back(
        std::make_shared<SceneHierarchyPanel>(m_selection)
    );
    m_panels.push_back(
        std::make_shared<InspectorPanel>(m_selection)
    );
    m_panels.push_back(
        std::make_shared<ViewportPanel>()
    );

    m_running = true;
    return true;
}

void Application::Run()
{
    if (!m_running)
        return;

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev_counter = SDL_GetPerformanceCounter();

    while (m_running)
    {
        Uint64 curr_counter = SDL_GetPerformanceCounter();
        double dt = (double)(curr_counter - prev_counter) / (double)freq;
        prev_counter = curr_counter;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                m_running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                m_running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        for (auto &panel : m_panels)
            panel->OnImGuiRender((float)dt);

        ImGui::Render();

        SDL_Renderer *renderer = m_window->GetNativeRenderer();
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        if (dt < TARGET_FRAME_TIME)
        {
            double delay_ms = (TARGET_FRAME_TIME - dt) * 1000.0;
            SDL_Delay((Uint32)delay_ms);
        }
    }
}

void Application::Shutdown()
{
    if (!m_window)
        return;

    m_panels.clear();

    delete m_selection;
    m_selection = nullptr;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    delete m_window;
    m_window = nullptr;

    SDL_Quit();
    m_running = false;
}
