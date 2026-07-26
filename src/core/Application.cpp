#include "Application.h"
#include "Window.h"
#include "editor/EditorPanel.h"
#include "editor/SelectionState.h"
#include "editor/StatsPanel.h"
#include "editor/SceneHierarchyPanel.h"
#include "editor/InspectorPanel.h"
#include "editor/ViewportPanel.h"

#include "Scene.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
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
    , m_layout_initialized(false)
    , m_selection(nullptr)
    , m_viewport(nullptr)
    , m_scene(nullptr)
    , m_viewport_target(nullptr)
    , m_viewport_target_w(0)
    , m_viewport_target_h(0)
{
}

Application::~Application()
{
    Shutdown();
}

void Application::RecreateViewportTarget(int width, int height)
{
    SDL_Renderer *renderer = m_window->GetNativeRenderer();

    if (m_viewport_target)
    {
        SDL_DestroyTexture(m_viewport_target);
        m_viewport_target = nullptr;
    }

    if (width <= 0 || height <= 0)
        return;

    m_viewport_target = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width, height
    );

    m_viewport_target_w = width;
    m_viewport_target_h = height;

    if (m_viewport)
        m_viewport->SetTexture(m_viewport_target);
}

void Application::RenderViewportTarget()
{
    if (!m_viewport_target || !m_viewport)
        return;

    SDL_Renderer *renderer = m_window->GetNativeRenderer();

    SDL_SetRenderTarget(renderer, m_viewport_target);

    SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
    SDL_RenderClear(renderer);

    int w = m_viewport_target_w;
    int h = m_viewport_target_h;

    SDL_SetRenderDrawColor(renderer, 40, 40, 50, 255);
    int grid_step = 40;
    for (int x = 0; x <= w; x += grid_step)
        SDL_RenderDrawLine(renderer, x, 0, x, h);
    for (int y = 0; y <= h; y += grid_step)
        SDL_RenderDrawLine(renderer, 0, y, w, y);

    int cx = w / 2;
    int cy = h / 2;
    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
    SDL_RenderDrawLine(renderer, cx, 0, cx, h);
    SDL_RenderDrawLine(renderer, 0, cy, w, cy);

    if (m_scene)
    {
        for (auto &entity : m_scene->GetEntities())
        {
            float r = entity.material.color[0];
            float g = entity.material.color[1];
            float b = entity.material.color[2];

            Uint8 ur = (Uint8)(r * 255.0f);
            Uint8 ug = (Uint8)(g * 255.0f);
            Uint8 ub = (Uint8)(b * 255.0f);

            if (entity.material.active)
            {
                SDL_SetRenderDrawColor(renderer, ur, ug, ub, 200);
                SDL_Rect fill = {
                    cx + (int)entity.transform.position[0] - (int)entity.transform.scale[0] / 2,
                    cy + (int)entity.transform.position[1] - (int)entity.transform.scale[1] / 2,
                    (int)entity.transform.scale[0],
                    (int)entity.transform.scale[1]
                };
                SDL_RenderFillRect(renderer, &fill);
            }

            SDL_SetRenderDrawColor(renderer, ur, ug, ub, 255);
            SDL_Rect outline = {
                cx + (int)entity.transform.position[0] - (int)entity.transform.scale[0] / 2,
                cy + (int)entity.transform.position[1] - (int)entity.transform.scale[1] / 2,
                (int)entity.transform.scale[0],
                (int)entity.transform.scale[1]
            };
            SDL_RenderDrawRect(renderer, &outline);
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
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

    m_scene = new Scene();
    m_scene->CreateEntity("Camera");
    m_scene->CreateEntity("Directional Light");
    m_scene->CreateEntity("Cube Object");

    m_viewport = new ViewportPanel();

    m_panels.push_back(
        std::make_shared<StatsPanel>(m_window->GetWidth(), m_window->GetHeight())
    );
    m_panels.push_back(
        std::make_shared<SceneHierarchyPanel>(m_selection, m_scene)
    );
    m_panels.push_back(
        std::make_shared<InspectorPanel>(m_selection, m_scene)
    );
    m_panels.push_back(std::shared_ptr<ViewportPanel>(m_viewport));

    RecreateViewportTarget(800, 600);

    m_running = true;
    return true;
}

void SetupDockingLayout()
{
    ImGuiID dockspace_id = ImGui::GetID("MainDockspace");

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID top, bottom;
    ImGuiID left, center, right;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, 0.80f, &top, &bottom);
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.25f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Singularity Engine Stats", bottom);

    ImGui::DockBuilderFinish(dockspace_id);
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

        if (!m_layout_initialized)
        {
            SetupDockingLayout();
            m_layout_initialized = true;
        }

        ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
        ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), dockspace_flags);

        for (auto &panel : m_panels)
            panel->OnImGuiRender((float)dt);

        ImGui::Render();

        int vp_w = m_viewport ? m_viewport->GetWidth() : 0;
        int vp_h = m_viewport ? m_viewport->GetHeight() : 0;
        if (vp_w != m_viewport_target_w || vp_h != m_viewport_target_h)
            RecreateViewportTarget(vp_w, vp_h);

        RenderViewportTarget();

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
    m_viewport = nullptr;

    if (m_viewport_target)
    {
        SDL_DestroyTexture(m_viewport_target);
        m_viewport_target = nullptr;
    }

    delete m_selection;
    m_selection = nullptr;

    delete m_scene;
    m_scene = nullptr;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    delete m_window;
    m_window = nullptr;

    SDL_Quit();
    m_running = false;
}
