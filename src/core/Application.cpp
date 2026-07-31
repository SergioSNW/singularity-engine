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

#include <algorithm>

static const double TARGET_FRAME_TIME = 1.0 / 60.0;

void ConfigureImGuiStyle(float ui_scale)
{
    // Rebuild the style from scratch so scaling never drifts: reset to the
    // built-in defaults, apply the dark theme, then our color overrides.
    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark();

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

    // Global UI zoom: scale every style metric (spacing, padding, rounding,
    // minimum window size, ...) so all widgets grow together.
    style.ScaleAllSizes(ui_scale);
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
    , m_ui_scale(1.0f)
    , m_applied_ui_scale(1.0f)
    , m_recreate_viewport(false)
{
}

Application::~Application()
{
    Shutdown();
}

void Application::RecreateViewportTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    SDL_Renderer *renderer = m_window->GetNativeRenderer();

    // Create the replacement first: if the GPU refuses a new target (e.g. the
    // window was just restored and resources are not ready yet), keep the old
    // target instead of leaving the viewport black.
    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width, height
    );
    if (!texture)
        return;

    if (m_viewport_target)
        SDL_DestroyTexture(m_viewport_target);

    m_viewport_target = texture;
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

// --- Cube mesh data (unit cube, corners at +-0.5 in local space) ---
static const Vec3 CUBE_CORNERS[8] = {
    { -0.5f, -0.5f, -0.5f },  // 0
    {  0.5f, -0.5f, -0.5f },  // 1
    {  0.5f,  0.5f, -0.5f },  // 2
    { -0.5f,  0.5f, -0.5f },  // 3
    { -0.5f, -0.5f,  0.5f },  // 4
    {  0.5f, -0.5f,  0.5f },  // 5
    {  0.5f,  0.5f,  0.5f },  // 6
    { -0.5f,  0.5f,  0.5f },  // 7
};

// 12 edges of the cube; each pair indexes CUBE_CORNERS.
static const int CUBE_EDGES[12][2] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },  // back  face (z = -0.5)
    { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },  // front face (z = +0.5)
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },  // depth connectors
};

// 6 faces, 4 corners each, wound counter-clockwise when viewed from outside
// so the cross product of the first three corners points outward.
static const int CUBE_FACES[6][4] = {
    { 4, 5, 6, 7 },  // front  (z = +0.5)
    { 1, 0, 3, 2 },  // back   (z = -0.5)
    { 2, 3, 7, 6 },  // top    (y = +0.5)
    { 1, 5, 4, 0 },  // bottom (y = -0.5)
    { 5, 1, 2, 6 },  // right  (x = +0.5)
    { 0, 4, 7, 3 },  // left   (x = -0.5)
};

static inline Vec3 Mat4TransformPoint(const Mat4 &m, const Vec3 &p)
{
    float w;
    return Mat4MulVec3(m, p, w);
}

static void RenderCubeWireframe(SDL_Renderer *renderer, const Mat4 &view_proj,
                                float near_p, int w, int h, const Mat4 &world,
                                const float color[3], bool brighten)
{
    float gain = brighten ? 1.35f : 1.0f;
    Uint8 r = (Uint8)std::min(255.0f, color[0] * 255.0f * gain);
    Uint8 g = (Uint8)std::min(255.0f, color[1] * 255.0f * gain);
    Uint8 b = (Uint8)std::min(255.0f, color[2] * 255.0f * gain);
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);

    for (int i = 0; i < 12; ++i)
    {
        Vec3 a = Mat4TransformPoint(world, CUBE_CORNERS[CUBE_EDGES[i][0]]);
        Vec3 b2 = Mat4TransformPoint(world, CUBE_CORNERS[CUBE_EDGES[i][1]]);
        DrawProjectedLine(renderer, view_proj, near_p, w, h, a, b2);
    }
}

static void RenderCubeSolid(SDL_Renderer *renderer, const Mat4 &view_proj,
                            float near_p, int w, int h, const Mat4 &world,
                            const float color[3])
{
    int sx[8], sy[8];
    float depth[8];
    bool behind = false;

    for (int i = 0; i < 8; ++i)
    {
        Vec3 corner = Mat4TransformPoint(world, CUBE_CORNERS[i]);
        float w_out;
        Vec3 clip = Mat4MulVec3(view_proj, corner, w_out);
        depth[i] = w_out;
        if (w_out < near_p)
            behind = true;
        sx[i] = (int)((clip.x / w_out + 1.0f) * 0.5f * w);
        sy[i] = (int)((1.0f - clip.y / w_out) * 0.5f * h);
    }

    // A face straddling the near plane would project to garbage; the wireframe
    // pass clips per-edge and carries the silhouette in that case.
    if (behind)
        return;

    // Painter's algorithm: draw faces farthest first, so nearer faces overlap.
    float face_depth[6];
    for (int f = 0; f < 6; ++f)
    {
        const int *idx = CUBE_FACES[f];
        face_depth[f] = (depth[idx[0]] + depth[idx[1]] + depth[idx[2]] + depth[idx[3]]) * 0.25f;
    }
    int order[6] = { 0, 1, 2, 3, 4, 5 };
    for (int i = 1; i < 6; ++i)
    {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && face_depth[order[j]] < face_depth[key])
        {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    // Directional shading: brightness follows the face normal against a fixed
    // light direction so the cube's planes read as distinct in 3D.
    static const Vec3 LIGHT = { 0.45f, 0.78f, 0.44f };

    Uint8 base_r = (Uint8)std::min(255.0f, color[0] * 255.0f);
    Uint8 base_g = (Uint8)std::min(255.0f, color[1] * 255.0f);
    Uint8 base_b = (Uint8)std::min(255.0f, color[2] * 255.0f);

    SDL_Vertex verts[36];
    int v = 0;
    for (int f = 0; f < 6; ++f)
    {
        const int *idx = CUBE_FACES[order[f]];
        Vec3 a = Mat4TransformPoint(world, CUBE_CORNERS[idx[0]]);
        Vec3 b = Mat4TransformPoint(world, CUBE_CORNERS[idx[1]]);
        Vec3 c = Mat4TransformPoint(world, CUBE_CORNERS[idx[2]]);
        Vec3 e1 = { b.x - a.x, b.y - a.y, b.z - a.z };
        Vec3 e2 = { c.x - a.x, c.y - a.y, c.z - a.z };
        Vec3 n = { e1.y * e2.z - e1.z * e2.y,
                   e1.z * e2.x - e1.x * e2.z,
                   e1.x * e2.y - e1.y * e2.x };
        float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-6f)
        {
            n.x /= len;  n.y /= len;  n.z /= len;
        }
        float shade = 0.62f + 0.38f * std::max(0.0f, n.x * LIGHT.x + n.y * LIGHT.y + n.z * LIGHT.z);

        SDL_Color col = {
            (Uint8)(base_r * shade),
            (Uint8)(base_g * shade),
            (Uint8)(base_b * shade),
            255
        };
        SDL_FPoint uv = { 0.0f, 0.0f };
        verts[v++] = { { (float)sx[idx[0]], (float)sy[idx[0]] }, col, uv };
        verts[v++] = { { (float)sx[idx[1]], (float)sy[idx[1]] }, col, uv };
        verts[v++] = { { (float)sx[idx[2]], (float)sy[idx[2]] }, col, uv };
        verts[v++] = { { (float)sx[idx[0]], (float)sy[idx[0]] }, col, uv };
        verts[v++] = { { (float)sx[idx[2]], (float)sy[idx[2]] }, col, uv };
        verts[v++] = { { (float)sx[idx[3]], (float)sy[idx[3]] }, col, uv };
    }

    SDL_RenderGeometry(renderer, nullptr, verts, 36, nullptr, 0);
}

void Application::RenderViewportTarget()
{
    if (!m_viewport_target || !m_viewport)
        return;

    SDL_Renderer *renderer = m_window->GetNativeRenderer();

    // If the GPU can no longer bind this texture (lost after minimize/restore),
    // flag it for recreation and skip the frame instead of rendering garbage.
    if (SDL_SetRenderTarget(renderer, m_viewport_target) != 0)
    {
        m_recreate_viewport = true;
        return;
    }

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
            Mat4 cam_world = m_scene->ComputeWorldMatrix(*camera_entity);
            cam_pos.x = cam_world.m[12];
            cam_pos.y = cam_world.m[13];
            cam_pos.z = cam_world.m[14];
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

        // Ground-plane grid first so entities draw on top of it.
        RenderGroundGrid(renderer, view_proj, near_p, w, h);

        // --- Render each entity as a 3D cube mesh ---
        for (auto &entity_ptr : m_scene->GetEntities())
        {
            Entity &entity = *entity_ptr;
            if (&entity == camera_entity)
                continue;

            Mat4 world = m_scene->ComputeWorldMatrix(entity);

            if (entity.material.active)
                RenderCubeSolid(renderer, view_proj, near_p, w, h, world, entity.material.color);

            RenderCubeWireframe(renderer, view_proj, near_p, w, h, world,
                                entity.material.color, true);
        }

        // --- Selection outline: wireframe bounding box around the selection ---
        if (m_selection && m_selection->entity_id >= 0)
        {
            Entity *selected = m_scene->GetEntityById(m_selection->entity_id);
            if (selected && selected != camera_entity)
            {
                static const float OUTLINE[3] = { 1.0f, 0.65f, 0.2f }; // amber
                Mat4 world = m_scene->ComputeWorldMatrix(*selected);
                RenderCubeWireframe(renderer, view_proj, near_p, w, h, world, OUTLINE, false);
            }
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
}

Entity *Application::FindActiveCamera()
{
    if (!m_scene)
        return nullptr;

    for (auto &e : m_scene->GetEntities())
        if (e->camera.primary)
            return e.get();

    for (auto &e : m_scene->GetEntities())
        if (e->camera.fov > 0.0f)
            return e.get();

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

    // Protect the editor layout: below this size the docked panels collapse
    // and the viewport becomes unusable.
    SDL_SetWindowMinimumSize(m_window->GetNativeWindow(), 1024, 768);

    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ConfigureImGuiStyle(1.0f);
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
    Entity &cube = m_scene->CreateEntity("Cube Object");
    // Parented child: its local position is relative to the parent's transform,
    // demonstrating the WorldMatrix = ParentWorld * LocalMatrix pipeline.
    Entity &cube_child = m_scene->CreateEntity("Cube Child", &cube);
    cube_child.transform.position[0] = 1.5f;
    cube_child.transform.scale[0] = 0.5f;
    cube_child.transform.scale[1] = 0.5f;
    cube_child.transform.scale[2] = 0.5f;

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
            if (event.type == SDL_WINDOWEVENT)
            {
                // Any of these can invalidate the off-screen render target's
                // GPU resources; force a fresh texture on the next frame.
                if (event.window.event == SDL_WINDOWEVENT_RESTORED ||
                    event.window.event == SDL_WINDOWEVENT_MAXIMIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    m_recreate_viewport = true;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- Main menu bar (global UI settings) ---
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("View"))
            {
                ImGui::SliderFloat("UI Scale", &m_ui_scale, 0.75f, 2.0f, "%.2fx");
                if (ImGui::Button("Reset##UiScale"))
                    m_ui_scale = 1.0f;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Apply a changed UI scale: rebuild style metrics and font scale.
        if (m_ui_scale != m_applied_ui_scale)
        {
            ConfigureImGuiStyle(m_ui_scale);
            ImGui::GetIO().FontGlobalScale = m_ui_scale;
            m_applied_ui_scale = m_ui_scale;
        }

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
        Uint32 win_flags = SDL_GetWindowFlags(m_window->GetNativeWindow());
        const bool minimized = (win_flags & SDL_WINDOW_MINIMIZED) != 0;

        // Recreate the off-screen target when it is missing, its size is stale,
        // or a window event invalidated its GPU resources. Never recreate while
        // minimized: the GPU may refuse to build targets for a hidden window.
        if (!minimized &&
            (m_recreate_viewport || !m_viewport_target ||
             vp_w != m_viewport_target_w || vp_h != m_viewport_target_h))
        {
            m_recreate_viewport = false;
            RecreateViewportTarget(vp_w, vp_h);
        }

        UpdateCameraControls((float)dt);

        // Skip the 3D render pass while minimized; the restore event already
        // queued a target recreation for the frame we come back.
        if (!minimized)
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
