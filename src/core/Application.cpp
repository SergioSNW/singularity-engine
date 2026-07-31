#include "Application.h"
#include "Window.h"
#include "editor/EditorPanel.h"
#include "editor/SelectionState.h"
#include "editor/StatsPanel.h"
#include "editor/SceneHierarchyPanel.h"
#include "editor/InspectorPanel.h"
#include "editor/ViewportPanel.h"

#include "Scene.h"
#include "EngineMath.h"

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
    , m_flying(false)
    , m_layout_initialized(false)
    , m_selection(nullptr)
    , m_viewport(nullptr)
    , m_scene(nullptr)
    , m_viewport_target(nullptr)
    , m_viewport_target_w(0)
    , m_viewport_target_h(0)
    , m_camera_scroll(0.0f)
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

static void DrawProjectedLine(SDL_Renderer *renderer, const Mat4 &view_proj,
                              float near_p, int w, int h, Vec3 a, Vec3 b)
{
    // For the perspective matrix, w_out == -z_view (depth in front of camera).
    float wa, wb;
    Mat4MulVec3(view_proj, a, wa);
    Mat4MulVec3(view_proj, b, wb);

    if (wa < near_p && wb < near_p)
        return; // segment entirely behind / inside the near plane

    // Clip the segment against the near plane. The clip parameter t is the
    // same in view and world space (affine transform), so interpolate the
    // world endpoints directly.
    if (wa < near_p)
    {
        float t = (near_p - wa) / (wb - wa);
        a.x += (b.x - a.x) * t;
        a.y += (b.y - a.y) * t;
        a.z += (b.z - a.z) * t;
    }
    else if (wb < near_p)
    {
        float t = (near_p - wb) / (wa - wb);
        b.x += (a.x - b.x) * t;
        b.y += (a.y - b.y) * t;
        b.z += (a.z - b.z) * t;
    }

    Vec3 ca = Mat4MulVec3(view_proj, a, wa);
    Vec3 cb = Mat4MulVec3(view_proj, b, wb);

    int ax = (int)((ca.x / wa + 1.0f) * 0.5f * w);
    int ay = (int)((1.0f - ca.y / wa) * 0.5f * h);
    int bx = (int)((cb.x / wb + 1.0f) * 0.5f * w);
    int by = (int)((1.0f - cb.y / wb) * 0.5f * h);

    SDL_RenderDrawLine(renderer, ax, ay, bx, by);
}

static void RenderGroundGrid(SDL_Renderer *renderer, const Mat4 &view_proj,
                             float near_p, int w, int h)
{
    const float extent = 20.0f;

    // Minor grid lines on the y=0 (XZ) plane; the axis lines are drawn below.
    SDL_SetRenderDrawColor(renderer, 45, 45, 55, 255);
    for (int k = -(int)extent; k <= (int)extent; ++k)
    {
        if (k == 0)
            continue;
        DrawProjectedLine(renderer, view_proj, near_p, w, h,
                          { (float)k, 0.0f, -extent }, { (float)k, 0.0f, extent });
        DrawProjectedLine(renderer, view_proj, near_p, w, h,
                          { -extent, 0.0f, (float)k }, { extent, 0.0f, (float)k });
    }

    // Highlighted world axes: X = red, Z = blue.
    SDL_SetRenderDrawColor(renderer, 220, 70, 70, 255);
    DrawProjectedLine(renderer, view_proj, near_p, w, h,
                      { -extent, 0.0f, 0.0f }, { extent, 0.0f, 0.0f });
    SDL_SetRenderDrawColor(renderer, 70, 110, 230, 255);
    DrawProjectedLine(renderer, view_proj, near_p, w, h,
                      { 0.0f, 0.0f, -extent }, { 0.0f, 0.0f, extent });
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

    if (m_scene)
    {
        // --- Locate active camera ---
        Entity *camera_entity = FindActiveCamera();

        // --- Build perspective view-projection from active camera ---
        Vec3 cam_pos = { 0.0f, 0.0f, 0.0f };
        float fov    = 60.0f;
        float pitch  = 0.0f;
        float yaw    = 0.0f;
        float near_p = 0.1f;
        float far_p  = 100.0f;

        if (camera_entity)
        {
            cam_pos.x = camera_entity->transform.position[0];
            cam_pos.y = camera_entity->transform.position[1];
            cam_pos.z = camera_entity->transform.position[2];
            if (camera_entity->camera.fov > 1.0f)
                fov = camera_entity->camera.fov;
            pitch = camera_entity->camera.pitch;
            yaw   = camera_entity->camera.yaw;
            near_p = camera_entity->camera.near_plane;
            far_p  = camera_entity->camera.far_plane;
        }

        // View = RotX(-pitch) * RotY(-yaw) * Translate(-cam_pos): the inverse
        // of the camera's world orientation. Yaw first about world up, then
        // pitch about the camera's local right axis, so roll stays locked to
        // zero for any yaw/pitch combination.
        Mat4 view = Mat4Mul(
            Mat4RotateX(-pitch),
            Mat4Mul(Mat4RotateY(-yaw), Mat4Translate(-cam_pos.x, -cam_pos.y, -cam_pos.z))
        );

        float aspect = (float)w / (float)h;
        Mat4 proj = Mat4Perspective(fov, aspect, near_p, far_p);
        Mat4 view_proj = Mat4Mul(proj, view);

        // Focal length in pixels: screen height / (2 * tan(fov/2)).
        float focal = (float)h / (2.0f * std::tan(fov * 3.1415926535f / 360.0f));

        // Ground-plane grid first so entities draw on top of it.
        RenderGroundGrid(renderer, view_proj, near_p, w, h);

        // --- Render each entity ---
        for (auto &entity : m_scene->GetEntities())
        {
            if (&entity == camera_entity)
                continue;

            float r = entity.material.color[0];
            float g = entity.material.color[1];
            float b = entity.material.color[2];

            Uint8 ur = (Uint8)(r * 255.0f);
            Uint8 ug = (Uint8)(g * 255.0f);
            Uint8 ub = (Uint8)(b * 255.0f);

            Vec3 world_pos = {
                entity.transform.position[0],
                entity.transform.position[1],
                entity.transform.position[2]
            };

            float w_out;
            Vec3 clip = Mat4MulVec3(view_proj, world_pos, w_out);

            // For the perspective matrix, w_out == -z_view (positive depth in
            // front of the camera). Cull anything behind or inside the near plane.
            if (w_out < near_p)
                continue;

            float ndc_x = clip.x / w_out;
            float ndc_y = clip.y / w_out;

            int sx = (int)((ndc_x + 1.0f) * 0.5f * w);
            int sy = (int)((1.0f - ndc_y) * 0.5f * h);

            // Perspective foreshortening: apparent size shrinks with depth.
            int sw = (int)(entity.transform.scale[0] * focal / w_out);
            int sh = (int)(entity.transform.scale[1] * focal / w_out);
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;

            if (entity.material.active)
            {
                SDL_SetRenderDrawColor(renderer, ur, ug, ub, 200);
                SDL_Rect fill = { sx - sw / 2, sy - sh / 2, sw, sh };
                SDL_RenderFillRect(renderer, &fill);
            }

            SDL_SetRenderDrawColor(renderer, ur, ug, ub, 255);
            SDL_Rect outline = { sx - sw / 2, sy - sh / 2, sw, sh };
            SDL_RenderDrawRect(renderer, &outline);
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
}

Entity *Application::FindActiveCamera()
{
    if (!m_scene)
        return nullptr;

    for (auto &e : m_scene->GetEntities())
        if (e.camera.primary)
            return &e;

    for (auto &e : m_scene->GetEntities())
        if (e.camera.fov > 0.0f)
            return &e;

    return nullptr;
}

void Application::UpdateCameraControls(float dt)
{
    if (!m_scene || !m_viewport)
        return;

    Entity *cam = FindActiveCamera();
    if (!cam)
        return;

    // Consume the accumulators every frame so no stale motion leaks into a
    // later navigation session (drag deltas, scroll deltas).
    int rel_x = 0, rel_y = 0;
    SDL_GetRelativeMouseState(&rel_x, &rel_y);
    const float scroll = m_camera_scroll;
    m_camera_scroll = 0.0f;

    const bool over_viewport = m_viewport->IsHovered();
    const bool rmb_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);

    // Enter fly mode: RMB pressed while hovering the viewport. The OS cursor
    // is captured (relative mode) for unlimited rotation and ImGui is told to
    // ignore the mouse so the hidden cursor never triggers a panel.
    if (!m_flying && over_viewport && rmb_down)
    {
        m_flying = true;
        SDL_SetRelativeMouseMode(SDL_TRUE);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
        SDL_GetRelativeMouseState(nullptr, nullptr); // drain pre-capture motion
    }

    // Exit fly mode as soon as RMB is released; the normal cursor returns.
    if (m_flying && !rmb_down)
    {
        m_flying = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    if (!m_flying)
    {
        // Hover-only action: scroll wheel zoom.
        if (over_viewport && scroll != 0.0f)
        {
            cam->camera.fov *= std::pow(0.9f, scroll);
            if (cam->camera.fov < 10.0f)  cam->camera.fov = 10.0f;
            if (cam->camera.fov > 120.0f) cam->camera.fov = 120.0f;
        }
        return;
    }

    const float pitch = cam->camera.pitch;
    const float yaw   = cam->camera.yaw;
    const float fov   = (cam->camera.fov > 1.0f) ? cam->camera.fov : 60.0f;

    // --- Keyboard movement (WASD / arrows) ---
    // Horizontal forward/right from yaw only; pitch only tilts the view.
    float fx = -std::sin(yaw * 3.1415926535f / 180.0f);
    float fz = -std::cos(yaw * 3.1415926535f / 180.0f);
    float fwd_len = std::sqrt(fx * fx + fz * fz);
    if (fwd_len > 1e-6f)
    {
        fx /= fwd_len;
        fz /= fwd_len;
    }
    const float rx = -fz;  // right = cross(forward, world_up)
    const float rz =  fx;

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const float move_speed = fov * 0.1f; // world-units per second
    float mv_f = 0.0f, mv_r = 0.0f, mv_u = 0.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    mv_f += 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  mv_f -= 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  mv_r -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) mv_r += 1.0f;
    if (keys[SDL_SCANCODE_E]) mv_u += 1.0f;
    if (keys[SDL_SCANCODE_Q]) mv_u -= 1.0f;

    if (mv_f != 0.0f || mv_r != 0.0f || mv_u != 0.0f)
    {
        cam->transform.position[0] += (fx * mv_f + rx * mv_r) * move_speed * dt;
        cam->transform.position[1] += mv_u * move_speed * dt;
        cam->transform.position[2] += (fz * mv_f + rz * mv_r) * move_speed * dt;
    }

    // --- Mouse look (pitch/yaw), FPS-style ---
    cam->camera.yaw   -= rel_x * 0.2f;
    cam->camera.pitch -= rel_y * 0.2f;
    if (cam->camera.pitch >  89.0f) cam->camera.pitch =  89.0f;
    if (cam->camera.pitch < -89.0f) cam->camera.pitch = -89.0f;

    // --- Scroll wheel zoom (fov = vertical field of view in degrees) ---
    if (scroll != 0.0f)
    {
        cam->camera.fov *= std::pow(0.9f, scroll);
        if (cam->camera.fov < 10.0f)  cam->camera.fov = 10.0f;
        if (cam->camera.fov > 120.0f) cam->camera.fov = 120.0f;
    }
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
    Entity &camera = m_scene->CreateEntity("Camera");
    camera.transform.position[1] = 2.0f;
    camera.transform.position[2] = 8.0f;
    camera.camera.pitch = -14.0f;
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
            if (event.type == SDL_MOUSEWHEEL)
                m_camera_scroll += (float)event.wheel.preciseY;
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

        UpdateCameraControls((float)dt);

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
