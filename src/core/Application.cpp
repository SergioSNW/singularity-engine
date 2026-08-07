#include "Application.h"
#include "Window.h"
#include "editor/EditorPanel.h"
#include "editor/SelectionState.h"
#include "editor/StatsPanel.h"
#include "editor/SceneHierarchyPanel.h"
#include "editor/InspectorPanel.h"
#include "editor/ViewportPanel.h"
#include "editor/GizmoController.h"
#include "editor/ScriptEditorPanel.h"
#include "editor/CommandPalette.h"
#include "editor/SettingsPanel.h"
#include "editor/Theme.h"

#include "Scene.h"
#include "SceneManager.h"
#include "SceneSerializer.h"
#include "PhysicsManager.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "EngineMath.h"
#include "script/ScriptEngine.h"
#include "editor/ContentBrowserPanel.h"
#include "editor/ConsolePanel.h"
#include "editor/InspectorPanel.h"
#include "core/Console.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

static const double TARGET_FRAME_TIME = 1.0 / 60.0;

// Viewport anti-aliasing factor. The off-screen 3D pass is rasterized by the
// SDL2 renderer, which offers no multisampling for render-target textures, so
// edges are anti-aliased by supersampling: the target is created at this
// multiple of the physical window resolution and ImGui downscales it to the
// viewport rect with linear filtering (2x factor = 4x samples per output
// pixel). Must stay >= 1.0; multiplied into both the target size and the
// gizmo's dpi_scale so screen-constant metrics keep their on-screen size.
static const float kViewportSupersample = 2.0f;

Application::Application()
    : m_window(nullptr)
    , m_running(false)
    , m_flying(false)
    , m_selection(nullptr)
    , m_viewport(nullptr)
    , m_scene(nullptr)
    , m_scene_manager(nullptr)
    , m_mesh_library(nullptr)
    , m_gizmo(nullptr)
    , m_script_engine(nullptr)
    , m_physics(nullptr)
    , m_script_editor(nullptr)
    , m_command_palette(nullptr)
    , m_settings_panel(nullptr)
    , m_content_browser(nullptr)
    , m_console_panel(nullptr)
    , m_inspector_panel(nullptr)
    , m_viewport_target(nullptr)
    , m_viewport_target_w(0)
    , m_viewport_target_h(0)
    , m_camera_scroll(0.0f)
    , m_ui_scale(1.0f)
    , m_applied_ui_scale(1.0f)
    , m_dpi_scale(1.0f)
    , m_fonts()
    , m_theme_colors()
    , m_recreate_viewport(false)
    , m_scene_path("assets/scenes/default.json")
    , m_scene_status()
    , m_mesh_error()
    , m_state(EngineState::Editor)
    , m_scene_snapshot()
    , m_save_as_open(false)
    , m_play_panel_saved(false)
    , m_script_editor_was_visible(true)
    , m_content_browser_was_visible(true)
    , m_console_was_visible(true)
    , m_inspector_was_visible(true)
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

    // The target is supersampled relative to the viewport rect, so it must be
    // sampled with linear filtering when ImGui draws it (the default is
    // nearest, which would drop texels on the downscale and reintroduce
    // aliasing / uneven pixels at fractional DPI).
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);

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

// Draw a world-space AABB as a wireframe box (12 edges), using the same
// projected-line path with near-plane clipping as the grid and mesh wireframes.
static void DrawWorldAABB(SDL_Renderer *renderer, const Mat4 &view_proj, float near_p,
                          int w, int h, const Vec3 &min, const Vec3 &max,
                          Uint8 r, Uint8 g, Uint8 b)
{
    const Vec3 c[8] = {
        { min.x, min.y, min.z }, { max.x, min.y, min.z },
        { max.x, max.y, min.z }, { min.x, max.y, min.z },
        { min.x, min.y, max.z }, { max.x, min.y, max.z },
        { max.x, max.y, max.z }, { min.x, max.y, max.z },
    };
    static const int EDGES[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // bottom face
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // top face
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // verticals
    };
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    for (int i = 0; i < 12; ++i)
        DrawProjectedLine(renderer, view_proj, near_p, w, h,
                          c[EDGES[i][0]], c[EDGES[i][1]]);
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

// --- Mesh rendering ---------------------------------------------------------
// All geometry (procedural cubes and loaded .obj assets) goes through the same
// path: transform the mesh's local triangle soup to world space, project to
// screen, shade by face normal, then depth-sort all triangles globally (one
// painter's pass across every entity) before rasterizing.

static inline Vec3 Mat4TransformPoint(const Mat4 &m, const Vec3 &p)
{
    float w;
    return Mat4MulVec3(m, p, w);
}

static bool ProjectToScreen(const Mat4 &view_proj, float near_p, int w, int h,
                            const Vec3 &p, int &sx, int &sy, float &depth)
{
    float w_out;
    Vec3 clip = Mat4MulVec3(view_proj, p, w_out);
    if (w_out < near_p)
        return false;
    sx = (int)((clip.x / w_out + 1.0f) * 0.5f * w);
    sy = (int)((1.0f - clip.y / w_out) * 0.5f * h);
    depth = w_out;
    return true;
}

// One screen-space shaded triangle, collected for the global painter pass.
// `texture` may be null (flat shading); when set, `r/g/b` act as the tint that
// SDL multiplies against the texture and `u0..v2` are its per-vertex UVs.
struct FillTri
{
    float depth;
    int x0, y0, x1, y1, x2, y2;
    float u0, v0, u1, v1, u2, v2;
    Uint8 r, g, b;
    SDL_Texture *texture = nullptr;
};

// Project + shade one mesh into screen-space FillTri entries. `color` is the
// RGBA albedo tint (already resolved from the entity's material asset when
// assigned); `uvs` (parallel to positions) and `texture` are used together to
// apply the diffuse map, otherwise flat normal shading is emitted.
static void EmitEntityTris(std::vector<FillTri> &tris, const Mesh &mesh,
                           const Mat4 &world, const Mat4 &view_proj, float near_p,
                           int w, int h, const float color[4],
                           SDL_Texture *texture, const std::vector<Vec2> *uvs)
{
    static const Vec3 LIGHT = { 0.45f, 0.78f, 0.44f };
    Uint8 base_r = (Uint8)std::min(255.0f, color[0] * 255.0f);
    Uint8 base_g = (Uint8)std::min(255.0f, color[1] * 255.0f);
    Uint8 base_b = (Uint8)std::min(255.0f, color[2] * 255.0f);

    const bool textured = texture != nullptr;
    const std::vector<Vec3> &pos = mesh.positions;
    for (size_t i = 0; i + 2 < pos.size(); i += 3)
    {
        Vec3 a = Mat4TransformPoint(world, pos[i]);
        Vec3 b = Mat4TransformPoint(world, pos[i + 1]);
        Vec3 c = Mat4TransformPoint(world, pos[i + 2]);

        int ax, ay, bx, by, cx, cy;
        float da, db, dc;
        if (!ProjectToScreen(view_proj, near_p, w, h, a, ax, ay, da) ||
            !ProjectToScreen(view_proj, near_p, w, h, b, bx, by, db) ||
            !ProjectToScreen(view_proj, near_p, w, h, c, cx, cy, dc))
            continue;

        // Directional shading from the world-space face normal. Applied to the
        // tint so textured triangles keep lighting on top of the map.
        Vec3 e1 = Vec3Sub(b, a);
        Vec3 e2 = Vec3Sub(c, a);
        Vec3 n = Vec3Normalize(Vec3Cross(e1, e2));
        float shade = 0.62f + 0.38f * std::max(0.0f, Vec3Dot(n, LIGHT));

        FillTri t;
        t.depth = (da + db + dc) / 3.0f;
        t.x0 = ax; t.y0 = ay;
        t.x1 = bx; t.y1 = by;
        t.x2 = cx; t.y2 = cy;
        t.u0 = t.v0 = t.u1 = t.v1 = t.u2 = t.v2 = 0.0f;
        t.r = (Uint8)(base_r * shade);
        t.g = (Uint8)(base_g * shade);
        t.b = (Uint8)(base_b * shade);
        t.texture = texture;
        if (textured)
        {
            const Vec2 &ta = (*uvs)[i];
            const Vec2 &tb = (*uvs)[i + 1];
            const Vec2 &tc = (*uvs)[i + 2];
            t.u0 = ta.u; t.v0 = ta.v;
            t.u1 = tb.u; t.v1 = tb.v;
            t.u2 = tc.u; t.v2 = tc.v;
        }
        tris.push_back(t);
    }
}

static void FlushTriBatch(SDL_Renderer *renderer, std::vector<SDL_Vertex> &verts,
                          SDL_Texture *texture)
{
    if (verts.empty())
        return;
    // Chunked so very large meshes stay well under any renderer vertex limit.
    static const size_t MAX_VERTS = 6000;
    size_t offset = 0;
    while (offset < verts.size())
    {
        size_t count = std::min(MAX_VERTS, verts.size() - offset);
        SDL_RenderGeometry(renderer, texture, verts.data() + offset, (int)count, nullptr, 0);
        offset += count;
    }
    verts.clear();
}

static void RenderMeshWireframe(SDL_Renderer *renderer, const Mat4 &view_proj,
                                float near_p, int w, int h, const Mat4 &world,
                                const Mesh &mesh, const float color[3], bool brighten)
{
    float gain = brighten ? 1.35f : 1.0f;
    Uint8 r = (Uint8)std::min(255.0f, color[0] * 255.0f * gain);
    Uint8 g = (Uint8)std::min(255.0f, color[1] * 255.0f * gain);
    Uint8 b = (Uint8)std::min(255.0f, color[2] * 255.0f * gain);
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);

    for (size_t i = 0; i + 1 < mesh.edge_lines.size(); i += 2)
    {
        Vec3 a = Mat4TransformPoint(world, mesh.edge_lines[i]);
        Vec3 b2 = Mat4TransformPoint(world, mesh.edge_lines[i + 1]);
        DrawProjectedLine(renderer, view_proj, near_p, w, h, a, b2);
    }
}

static void DrawTriangles(SDL_Renderer *renderer, std::vector<FillTri> &tris, int w, int h)
{
    // Painter's algorithm across all entities: farthest triangles first.
    std::stable_sort(tris.begin(), tris.end(),
        [](const FillTri &a, const FillTri &b) { return a.depth > b.depth; });

    std::vector<SDL_Vertex> verts;
    verts.reserve(tris.size() * 3);
    SDL_Texture *active_texture = nullptr;
    for (const FillTri &t : tris)
    {
        // Break the batch when the texture changes so SDL_RenderGeometry always
        // receives a coherent vertex set for one texture (or flat shading).
        if (t.texture != active_texture)
        {
            FlushTriBatch(renderer, verts, active_texture);
            active_texture = t.texture;
        }
        SDL_Color col = { t.r, t.g, t.b, 255 };
        verts.push_back({ { (float)t.x0, (float)t.y0 }, col, { t.u0, t.v0 } });
        verts.push_back({ { (float)t.x1, (float)t.y1 }, col, { t.u1, t.v1 } });
        verts.push_back({ { (float)t.x2, (float)t.y2 }, col, { t.u2, t.v2 } });
        if (verts.size() >= 6000)
            FlushTriBatch(renderer, verts, active_texture);
    }
    FlushTriBatch(renderer, verts, active_texture);
    tris.clear();
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
        Vec3 cam_pos{ 0.0f, 0.0f, 0.0f };
        float fov = 60.0f, pitch = 0.0f, yaw = 0.0f;
        float near_p = 0.1f, far_p = 100.0f;
        Mat4 view_proj;
        if (BuildViewProj(view_proj, cam_pos, fov, pitch, yaw, near_p, far_p))
        {
            // Ground-plane grid first so entities draw on top of it.
            RenderGroundGrid(renderer, view_proj, near_p, w, h);

            Entity *camera_entity = FindActiveCamera();

            // Refresh every entity's local AABB from its resolved mesh geometry
            // (Mesh::bounds_min/max). The mesh can change through the editor or
            // a scene load, so the component is recomputed each frame to stay a
            // true mirror of the geometry used for picking and collision.
            for (auto &entity_ptr : m_scene->GetEntities())
            {
                Entity &entity = *entity_ptr;
                std::string mesh_error;
                const Mesh *mesh = ResolveMesh(entity, mesh_error);
                if (mesh)
                {
                    entity.bounds.local_min = mesh->bounds_min;
                    entity.bounds.local_max = mesh->bounds_max;
                }
            }

            // --- Pass 1: solid fills, one global painter's pass ---
            std::vector<FillTri> tris;
            for (auto &entity_ptr : m_scene->GetEntities())
            {
                Entity &entity = *entity_ptr;
                if (&entity == camera_entity || !entity.material.active)
                    continue;

                std::string mesh_error;
                const Mesh *mesh = ResolveMesh(entity, mesh_error);
                if (!mesh_error.empty() && mesh_error != m_mesh_error)
                {
                    m_mesh_error = mesh_error;
                    m_scene_status = "Mesh load failed: " + mesh_error;
                }
                if (!mesh)
                    continue;

                Mat4 world = m_scene->ComputeWorldMatrix(entity);
                const float *tint = nullptr;
                const std::vector<Vec2> *uvs = nullptr;
                SDL_Texture *texture = ResolveEntityTexture(entity, *mesh, tint, uvs);
                EmitEntityTris(tris, *mesh, world, view_proj, near_p, w, h,
                               tint, texture, uvs);
            }
            DrawTriangles(renderer, tris, w, h);

            // --- Pass 2: wireframe overlay for every visible entity ---
            for (auto &entity_ptr : m_scene->GetEntities())
            {
                Entity &entity = *entity_ptr;
                if (&entity == camera_entity)
                    continue;

                std::string mesh_error;
                const Mesh *mesh = ResolveMesh(entity, mesh_error);
                if (!mesh)
                    continue;

                Mat4 world = m_scene->ComputeWorldMatrix(entity);
                RenderMeshWireframe(renderer, view_proj, near_p, w, h, world,
                                    *mesh, entity.material.color, true);
            }

            // --- Pass 3: selection outline, AABB bounds boxes, gizmo overlay
            // (editor only). Runs even with no selection so the hovered
            // entity's bounds box still draws. ---
            if (m_state == EngineState::Editor && m_selection && m_gizmo)
            {
                Entity *selected = (m_selection->entity_id >= 0)
                    ? m_scene->GetEntityById(m_selection->entity_id) : nullptr;

                // Selected entity: amber wireframe outline + white bounds box.
                if (selected && selected != camera_entity)
                {
                    std::string mesh_error;
                    const Mesh *mesh = ResolveMesh(*selected, mesh_error);
                    static const float OUTLINE[3] = { 1.0f, 0.65f, 0.2f }; // amber
                    if (mesh)
                    {
                        Mat4 world = m_scene->ComputeWorldMatrix(*selected);
                        RenderMeshWireframe(renderer, view_proj, near_p, w, h,
                                            world, *mesh, OUTLINE, false);
                    }
                    // Draw the bounds box in world space: the AABB component
                    // stores LOCAL bounds, so transform them into the entity's
                    // world frame before drawing.
                    Mat4 sel_world = m_scene->ComputeWorldMatrix(*selected);
                    Vec3 sel_wmin, sel_wmax;
                    TransformAABB(selected->bounds.local_min,
                                  selected->bounds.local_max, sel_world,
                                  sel_wmin, sel_wmax);
                    DrawWorldAABB(renderer, view_proj, near_p, w, h,
                                  sel_wmin, sel_wmax, 255, 255, 255);
                }

                // Hovered entity (ray/AABB hit under the cursor): light-blue
                // bounds box, distinct from the amber selection.
                int hover_id = m_gizmo->GetHoverEntity();
                if (hover_id >= 0 && hover_id != (selected ? selected->id : -1) &&
                    hover_id != (camera_entity ? camera_entity->id : -1))
                {
                    if (Entity *hover = m_scene->GetEntityById(hover_id))
                    {
                        Mat4 hover_world = m_scene->ComputeWorldMatrix(*hover);
                        Vec3 hover_wmin, hover_wmax;
                        TransformAABB(hover->bounds.local_min,
                                      hover->bounds.local_max, hover_world,
                                      hover_wmin, hover_wmax);
                        DrawWorldAABB(renderer, view_proj, near_p, w, h,
                                      hover_wmin, hover_wmax, 110, 180, 255);
                    }
                }

                // Physics collider volumes (editor aid): solid = green,
                // trigger = cyan. Drawn from the collider's own local box
                // (center +/- extents) transformed into the world frame.
                for (auto &entity_ptr : m_scene->GetEntities())
                {
                    Entity &collider_entity = *entity_ptr;
                    if (!collider_entity.collider.enabled)
                        continue;
                    if (&collider_entity == camera_entity)
                        continue;
                    const Vec3 clmin{
                        collider_entity.collider.center.x - collider_entity.collider.extents.x,
                        collider_entity.collider.center.y - collider_entity.collider.extents.y,
                        collider_entity.collider.center.z - collider_entity.collider.extents.z,
                    };
                    const Vec3 clmax{
                        collider_entity.collider.center.x + collider_entity.collider.extents.x,
                        collider_entity.collider.center.y + collider_entity.collider.extents.y,
                        collider_entity.collider.center.z + collider_entity.collider.extents.z,
                    };
                    Mat4 collider_world = m_scene->ComputeWorldMatrix(collider_entity);
                    Vec3 cwmin, cwmax;
                    TransformAABB(clmin, clmax, collider_world, cwmin, cwmax);
                    const bool is_trigger =
                        (collider_entity.collider.type == ColliderComponent::Type::Trigger);
                    DrawWorldAABB(renderer, view_proj, near_p, w, h,
                                  cwmin, cwmax,
                                  is_trigger ? 90 : 80,
                                  is_trigger ? 200 : 230,
                                  is_trigger ? 210 : 110);
                }

                GizmoFrame gf;
                gf.scene = m_scene;
                gf.selection = m_selection;
                gf.meshes = m_mesh_library;
                gf.active_camera_id = camera_entity ? camera_entity->id : -1;
                gf.vp_width = (float)w;
                gf.vp_height = (float)h;
                ImVec2 fb_scale = ImGui::GetIO().DisplayFramebufferScale;
                const float dpi = (fb_scale.x > 0.0f && fb_scale.y > 0.0f)
                    ? std::max(fb_scale.x, fb_scale.y) : 1.0f;
                gf.dpi_scale = dpi * kViewportSupersample;
                gf.hovered = m_viewport->IsHovered();
                gf.cam_pos = cam_pos;
                gf.cam_pitch = pitch;
                gf.cam_yaw = yaw;
                gf.cam_fov = fov;
                gf.near_p = near_p;
                gf.view_proj = view_proj;
                gf.dt = 0.0f;
                gf.snap_translation = m_snap.translation;
                gf.snap_rotation = m_snap.rotation;
                gf.snap_scale = m_snap.scale;
                gf.snap_active = m_snap.enabled || ImGui::GetIO().KeyCtrl;
                m_gizmo->Draw(renderer, gf);
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

bool Application::BuildViewProj(Mat4 &view_proj, Vec3 &cam_pos, float &fov,
                                float &pitch, float &yaw, float &near_p,
                                float &far_p)
{
    if (!m_scene || !m_viewport || m_viewport_target_w <= 0 || m_viewport_target_h <= 0)
        return false;

    Entity *camera_entity = FindActiveCamera();

    cam_pos = { 0.0f, 0.0f, 0.0f };
    fov   = 60.0f;
    pitch = 0.0f;
    yaw   = 0.0f;
    near_p = 0.1f;
    far_p  = 100.0f;

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

    float aspect = (float)m_viewport_target_w / (float)m_viewport_target_h;
    Mat4 proj = Mat4Perspective(fov, aspect, near_p, far_p);
    view_proj = Mat4Mul(proj, view);
    return true;
}

const Mesh *Application::ResolveMesh(const Entity &entity, std::string &error)
{
    if (!m_mesh_library)
        return nullptr;
    if (entity.mesh.path.empty())
        return m_mesh_library->GetBuiltinCube();
    const Mesh *mesh = m_mesh_library->GetOrLoad(entity.mesh.path, &error);
    if (mesh)
        return mesh;
    return m_mesh_library->GetBuiltinCube();
}

// Resolve the texture + tint that shade `entity`. A .mat asset assigned to the
// entity wins: its texture filename is looked up in the TextureLibrary and its
// color becomes the tint. Without an asset the entity's own texture_path and
// color are used. `out_uvs` is only non-null when a texture is active and the
// mesh carries a full UV set (otherwise the caller falls back to flat shading).
SDL_Texture *Application::ResolveEntityTexture(const Entity &entity, const Mesh &mesh,
                                               const float *&out_tint,
                                               const std::vector<Vec2> *&out_uvs)
{
    const Material *mat = nullptr;
    if (!entity.material.material_path.empty() && m_material_library)
        mat = m_material_library->Load(entity.material.material_path);

    std::string texture_key = entity.material.texture_path;
    if (mat && !mat->texture.empty())
        texture_key = mat->texture;

    std::string error;
    SDL_Texture *texture = nullptr;
    if (!texture_key.empty() && m_texture_library)
        texture = m_texture_library->GetTexture(texture_key, &error);

    out_tint = mat ? mat->color : entity.material.color;
    out_uvs = nullptr;
    if (texture && mesh.uvs.size() == mesh.positions.size())
        out_uvs = &mesh.uvs;
    return texture;
}

SDL_Texture *Application::ResolveEntityTexture(const Entity &entity)
{
    const float *tint = nullptr;
    const std::vector<Vec2> *uvs = nullptr;
    static const Mesh kEmptyMesh;  // mesh UVs never match an empty mesh
    return ResolveEntityTexture(entity, kEmptyMesh, tint, uvs);
}

void Application::SaveScene()
{
    if (!m_scene_manager)
        return;
    const std::string path = m_scene_path.empty() ? "assets/scenes/default.json" : m_scene_path;
    std::string error;
    if (m_scene_manager->SaveScene(path, &error))
    {
        m_scene_path = path;
        m_scene_status = "Scene saved to " + std::filesystem::absolute(path).string();
        ConsoleInfo("Scene saved: " + m_scene_path);
    }
    else
    {
        m_scene_status = "Save failed: " + error;
        ConsoleError("Scene save failed: " + error);
    }
}

void Application::OpenScene()
{
    if (!m_scene_manager)
        return;
    LoadSceneFile(m_scene_path.empty() ? "assets/scenes/default.json" : m_scene_path);
}

void Application::LoadSceneFile(const std::string &filepath)
{
    if (!m_scene_manager)
        return;
    if (m_state == EngineState::Play)
    {
        m_scene_status = "Stop play mode before switching scenes";
        return;
    }

    std::string error;
    if (m_scene_manager->LoadScene(filepath, &error))
    {
        m_scene_path = filepath;
        m_selection->entity_id = -1;
        m_selection->entity_name.clear();
        m_scene_status = "Scene loaded from " + std::filesystem::absolute(filepath).string();
        ConsoleInfo("Scene loaded: " + filepath + " (" +
                    std::to_string(m_scene->GetEntities().size()) + " entities)");
    }
    else
    {
        m_scene_status = "Open failed: " + error;
        ConsoleError("Scene load failed: " + filepath + " -> " + error);
    }
}

void Application::NewScene()
{
    if (!m_scene_manager)
        return;
    if (m_state == EngineState::Play)
    {
        m_scene_status = "Stop play mode before switching scenes";
        return;
    }

    std::string error;
    if (m_scene_manager->NewScene(&error))
    {
        m_scene_path.clear();
        m_selection->entity_id = -1;
        m_selection->entity_name.clear();
        m_scene_status = "New scene: " + m_scene_manager->ActiveName();
        ConsoleInfo("New scene created: " + m_scene_manager->ActiveName());
    }
    else
    {
        m_scene_status = "New scene failed: " + error;
        ConsoleError("New scene failed: " + error);
    }
}

void Application::OpenSaveAsModal()
{
    m_save_as_open = true;
    std::strncpy(m_save_as_name, m_scene_manager ? m_scene_manager->ActiveName().c_str() : "",
                 sizeof(m_save_as_name) - 1);
    m_save_as_name[sizeof(m_save_as_name) - 1] = '\0';
}

void Application::DrawSaveAsModal()
{
    if (m_save_as_open)
    {
        ImGui::OpenPopup("Save Scene As");
        m_save_as_open = false;
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Save the active scene as a map file.");
    ImGui::Separator();

    ImGui::InputText("File name", m_save_as_name, sizeof(m_save_as_name));
    ImGui::TextDisabled("Writes assets/scenes/<name>.json");

    ImGui::Separator();
    if (ImGui::Button("Save"))
    {
        // Sanitize the name: strip a stray .json suffix and path-hostile
        // characters so the file cannot escape assets/scenes/.
        std::string name = m_save_as_name;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
            name = name.substr(0, name.size() - 5);
        for (char &c : name)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';

        if (!name.empty())
        {
            const std::string path = "assets/scenes/" + name + ".json";
            std::string error;
            if (m_scene_manager && m_scene_manager->SaveScene(path, &error))
            {
                m_scene_path = path;
                m_scene_status = "Scene saved to " + std::filesystem::absolute(path).string();
                ConsoleInfo("Scene saved: " + m_scene_path);
            }
            else
            {
                m_scene_status = "Save failed: " + error;
                ConsoleError("Scene save failed: " + error);
            }
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void Application::EnterPlayMode()
{
    if (m_state == EngineState::Play)
        return;

    // Snapshot the whole scene graph in-memory (reuses the JSON serializer).
    // Any mutation made during play — flythrough camera moves, transform
    // edits, scene changes — is thrown away on Stop.
    m_scene_snapshot = SceneSerializer::SerializeScene(*m_scene);

    // Leave flight mode first so the captured mouse / hidden cursor never leak
    // into the play session (the Stop button needs a free cursor).
    if (m_flying)
    {
        m_flying = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    m_selection->entity_id = -1;
    m_selection->entity_name.clear();

    // Fresh physics state: no Enter/Exit edges may survive from a previous
    // play session into this one.
    if (m_physics)
        m_physics->Clear();

    // Bind every scripted entity and fire OnStart. Script load errors are
    // reported in the status line but play still runs for the scripts that
    // bound successfully.
    std::string script_errors;
    if (m_script_engine)
        m_script_engine->StartSession(*m_scene, script_errors);
    if (script_errors.empty())
        m_scene_status = "Play mode: scene snapshotted; Esc or Stop to exit";
    else
        m_scene_status = "Play mode: script errors -> " + script_errors;
    m_state = EngineState::Play;
    ConsoleInfo(script_errors.empty()
                    ? "Entered play mode (scene snapshotted)"
                    : "Entered play mode with script errors: " + script_errors);

    // Hide the non-essential editor windows so the game view is clean. The
    // pre-play visibility is snapshotted for a symmetrical restore on exit.
    SavePlayModePanelState();
}

void Application::ExitPlayMode()
{
    if (m_state != EngineState::Play)
        return;
    if (m_flying)
    {
        m_flying = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    // Release the script session BEFORE restoring the scene snapshot: each
    // script's Lua state holds pointers into the entities being torn down.
    if (m_script_engine)
        m_script_engine->StopSession();

    if (m_scene_snapshot.IsObject())
    {
        std::string error;
        if (SceneSerializer::DeserializeScene(*m_scene, m_scene_snapshot, &error))
        {
            m_scene_status = "Stopped: scene restored to pre-play snapshot";
            ConsoleInfo("Exited play mode: scene restored to pre-play snapshot");
        }
        else
            m_scene_status = "Stop failed: " + error;
    }
    else
    {
        m_scene_status = "Stopped: no snapshot to restore";
        ConsoleInfo("Exited play mode");
    }

    m_scene_snapshot = json::Value();
    m_selection->entity_id = -1;
    m_selection->entity_name.clear();
    m_state = EngineState::Editor;

    // Bring back the panels that were hidden for play, exactly as they were
    // before play started (hidden ones stay hidden, visible ones reappear).
    RestorePlayModePanelState();

    // Editor panel restoration. During play the viewport was force-undocked
    // (isolated fullscreen) and the dockspace + Hierarchy/Inspector/Stats were
    // never submitted, leaving their ImGui docking associations stale. Force
    // the dock layout to rebuild next frame: DockBuilder removes and re-creates
    // the node tree and explicitly re-docks every editor panel, so the full
    // editor UI deterministically comes back into view.
    m_viewport->SetIsolated(false);
    m_workspace_manager.RequestRebuild();
}

void Application::SavePlayModePanelState()
{
    m_play_panel_saved = true;
    m_script_editor_was_visible = m_script_editor ? m_script_editor->IsVisible() : false;
    m_content_browser_was_visible = m_content_browser ? m_content_browser->IsVisible() : false;
    m_console_was_visible = m_console_panel ? m_console_panel->IsVisible() : false;
    m_inspector_was_visible = m_inspector_panel ? m_inspector_panel->IsVisible() : false;

    if (m_script_editor)
        m_script_editor->SetVisible(false);
    if (m_content_browser)
        m_content_browser->SetVisible(false);
    if (m_console_panel)
        m_console_panel->SetVisible(false);
    if (m_inspector_panel)
        m_inspector_panel->SetVisible(false);
}

void Application::RestorePlayModePanelState()
{
    if (!m_play_panel_saved)
        return;
    m_play_panel_saved = false;

    if (m_script_editor)
        m_script_editor->SetVisible(m_script_editor_was_visible);
    if (m_content_browser)
        m_content_browser->SetVisible(m_content_browser_was_visible);
    if (m_console_panel)
        m_console_panel->SetVisible(m_console_was_visible);
    if (m_inspector_panel)
        m_inspector_panel->SetVisible(m_inspector_was_visible);
}

void Application::DuplicateSelection()
{
    if (m_state != EngineState::Editor)
        return;
    if (!m_selection || m_selection->entity_id < 0)
        return;

    Entity *source = m_scene->GetEntityById(m_selection->entity_id);
    if (!source)
        return;

    // Clone under the source's own parent so a child's duplicate stays a
    // sibling inside the same subtree; a root duplicate lands at the root.
    // The serializer assigns fresh ids/uuid, so the clone is fully independent.
    Entity *clone = SceneSerializer::DuplicateEntity(*m_scene, *source, source->parent);
    if (clone)
    {
        m_selection->entity_id = clone->id;
        m_selection->entity_name = clone->tag.tag;
        m_scene_status = "Duplicated '" + clone->tag.tag + "'";
    }
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
    // Redirect C stdout/stderr into the engine console before anything else
    // can write to a terminal: printf/std::cout/Lua-stdlib output now lands in
    // the ConsolePanel, so no external console window is needed. Failing the
    // redirect is non-fatal (direct Write() calls still work).
    Console::Instance().StartRedirect();

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

    // DPI-crisp text: rasterize every font at the display's framebuffer scale
    // (physical pixels per logical pixel) and fold that factor back out of
    // FontGlobalScale. The SDL2 renderer backend scales draw data by
    // io.DisplayFramebufferScale at present time; baking glyphs at the same
    // density keeps them exactly 1:1 with the framebuffer instead of scaled up.
    m_dpi_scale = Theme::ComputeDpiScale(m_window->GetNativeWindow(),
                                         m_window->GetNativeRenderer());
    Theme::LoadFonts(m_fonts, m_dpi_scale);
    ImGui::GetIO().FontGlobalScale = 1.0f / m_dpi_scale;

    // Restore a saved custom color scheme (editor_theme.json) before the first
    // ConfigureStyle so the whole palette derives from the user's tokens.
    m_theme_colors = Theme::DefaultColors();
    Theme::LoadThemeFromFile(m_theme_colors);

    Theme::ConfigureStyle(1.0f, m_theme_colors);
    ImGui_ImplSDL2_InitForSDLRenderer(
        m_window->GetNativeWindow(),
        m_window->GetNativeRenderer()
    );
    ImGui_ImplSDLRenderer2_Init(m_window->GetNativeRenderer());

    // Restore the workspace before the first frame so a saved custom layout
    // (or the remembered preset) is active immediately.
    m_workspace_manager.LoadFromFile();

    m_selection = new SelectionState();
    m_mesh_library = new MeshLibrary();
    m_material_library = new MaterialLibrary();
    m_texture_library = new TextureLibrary();
    // Textures upload through the engine renderer, so the library must know it
    // before the first Load(). Set right after renderer creation.
    m_texture_library->SetRenderer(m_window->GetNativeRenderer());
    m_gizmo = new GizmoController();
    m_script_engine = new ScriptEngine();
    m_physics = new PhysicsManager();

    // The SceneManager owns the active Scene. The object is allocated once and
    // rebuilt in place on every load, so the Scene* kept by every panel stays
    // valid across scene switches.
    m_scene_manager = new SceneManager();
    m_scene = m_scene_manager->GetScene();

    Entity &camera = m_scene->CreateEntity("Camera");
    camera.transform.position[1] = 2.0f;
    camera.transform.position[2] = 8.0f;
    camera.camera.pitch = -14.0f;
    m_scene->CreateEntity("Directional Light");
    Entity &cube = m_scene->CreateEntity("Cube Object");
    // Phase 19 demo: the cube carries a .mat asset that paints it with a
    // procedural checkerboard BMP; MaterialLibrary/TextureLibrary resolve the
    // material and texture on first render.
    cube.material.material_path = "Checker.mat";
    // Parented child: its local position is relative to the parent's transform,
    // demonstrating the WorldMatrix = ParentWorld * LocalMatrix pipeline.
    Entity &cube_child = m_scene->CreateEntity("Cube Child", &cube);
    cube_child.transform.position[0] = 1.5f;
    cube_child.transform.scale[0] = 0.5f;
    cube_child.transform.scale[1] = 0.5f;
    cube_child.transform.scale[2] = 0.5f;

    // Loaded-asset meshes: entities reference .obj files by name; MeshLibrary
    // resolves them under assets/meshes/ on first use.
    Entity &octahedron = m_scene->CreateEntity("Octahedron");
    octahedron.mesh.path = "octahedron.obj";
    octahedron.transform.position[1] = 1.5f;
    octahedron.transform.position[2] = 2.0f;
    octahedron.transform.scale[0] = 1.5f;
    octahedron.transform.scale[1] = 1.5f;
    octahedron.transform.scale[2] = 1.5f;
    octahedron.material.color[0] = 0.95f;
    octahedron.material.color[1] = 0.80f;
    octahedron.material.color[2] = 0.40f;

    // Demo gameplay script: spinning octahedron while in play mode. The file
    // is shipped under assets/scripts/ (copied next to the executable).
    octahedron.script.path = "assets/scripts/player.lua";

    Entity &icosahedron = m_scene->CreateEntity("Icosahedron");
    icosahedron.mesh.path = "icosahedron.obj";
    icosahedron.transform.position[0] = 3.0f;
    icosahedron.transform.position[1] = 1.1f;
    icosahedron.transform.position[2] = 1.0f;
    icosahedron.material.color[0] = 0.45f;
    icosahedron.material.color[1] = 0.85f;
    icosahedron.material.color[2] = 0.95f;

    Entity &pyramid = m_scene->CreateEntity("Pyramid");
    pyramid.mesh.path = "pyramid.obj";
    pyramid.transform.position[0] = -3.2f;
    pyramid.transform.position[1] = 0.75f;
    pyramid.transform.position[2] = 1.0f;
    pyramid.material.color[0] = 0.85f;
    pyramid.material.color[1] = 0.45f;
    pyramid.material.color[2] = 0.80f;

    // Phase 14 demo: the physics bridge. A solid Wall stops the script-driven
    // Bouncer (penetration-preventing separation), and a pass-through Trigger
    // Zone raises OnTriggerEnter/Exit on the Bouncer's way there. The Bouncer
    // is created AFTER the Wall so the solid resolution (which separates the
    // higher-id body) pushes the Bouncer, not the Wall.
    Entity &wall = m_scene->CreateEntity("Wall");
    wall.transform.position[0] = 6.0f;
    wall.transform.position[2] = -1.0f;
    wall.transform.scale[1] = 4.0f;
    wall.transform.scale[2] = 3.0f;
    wall.material.color[0] = 0.55f;
    wall.material.color[1] = 0.42f;
    wall.material.color[2] = 0.28f;
    wall.collider.enabled = true;

    Entity &bouncer = m_scene->CreateEntity("Bouncer");
    bouncer.transform.position[0] = -4.0f;
    bouncer.transform.position[2] = -1.0f;
    bouncer.material.color[0] = 0.92f;
    bouncer.material.color[1] = 0.25f;
    bouncer.material.color[2] = 0.25f;
    bouncer.collider.enabled = true;
    bouncer.script.path = "assets/scripts/bouncer.lua";

    Entity &trigger_zone = m_scene->CreateEntity("Trigger Zone");
    trigger_zone.transform.position[0] = 3.0f;
    trigger_zone.transform.position[2] = -1.0f;
    trigger_zone.transform.scale[1] = 2.0f;
    trigger_zone.transform.scale[2] = 3.0f;
    trigger_zone.material.color[0] = 0.30f;
    trigger_zone.material.color[1] = 0.75f;
    trigger_zone.material.color[2] = 0.80f;
    trigger_zone.collider.enabled = true;
    trigger_zone.collider.type = ColliderComponent::Type::Trigger;
    trigger_zone.script.path = "assets/scripts/trigger.lua";

    m_scene->Meta().name = "Default";

    m_viewport = new ViewportPanel();

    m_panels.push_back(
        std::make_shared<StatsPanel>(m_window)
    );
    m_panels.push_back(
        std::make_shared<SceneHierarchyPanel>(m_selection, m_scene)
    );
    m_inspector_panel = new InspectorPanel(m_selection, m_scene,
                                           m_material_library, m_texture_library);
    m_panels.push_back(std::shared_ptr<InspectorPanel>(m_inspector_panel));
    m_panels.push_back(std::shared_ptr<ViewportPanel>(m_viewport));

    // Script editor: sidebar over assets/scripts/ + dedicated floating code
    // window. Its reload callback hot-swaps the running play session after a
    // save so script edits apply without leaving play mode.
    m_script_editor = new ScriptEditorPanel(m_fonts.mono, [this]() -> bool {
        if (m_state != EngineState::Play)
            return false;
        std::string script_errors;
        m_script_engine->ReloadSession(*m_scene, script_errors);
        m_scene_status = script_errors.empty()
                             ? "Script session reloaded (OnStart re-ran)"
                             : "Reload errors -> " + script_errors;
        return true;
    });
    m_panels.push_back(std::shared_ptr<ScriptEditorPanel>(m_script_editor));

    // Live theme customizer + grid snapping config: owns no state itself — it
    // edits Application's token set (and SnapSettings) and asks for a
    // ConfigureStyle re-apply on every theme change.
    m_settings_panel = new SettingsPanel(&m_theme_colors, &m_snap, [this]() {
        Theme::ConfigureStyle(m_ui_scale, m_theme_colors);
    });
    m_panels.push_back(std::shared_ptr<SettingsPanel>(m_settings_panel));

    // Global command palette (Ctrl+Shift+P): the editor's core actions as a
    // fuzzy-searchable quick launcher. Registered after the panels above so
    // the callbacks can reach them.
    m_command_palette = new CommandPalette();
    m_panels.push_back(std::shared_ptr<CommandPalette>(m_command_palette));

    auto &cp = *m_command_palette;
    cp.Register({ "Open Script Editor", "View", "F4", [this]() {
        if (m_script_editor)
            m_script_editor->ToggleVisible();
    } });
    cp.Register({ "Toggle Editor Settings", "View", "", [this]() {
        if (m_settings_panel)
            m_settings_panel->ToggleVisible();
    } });
    cp.Register({ "Command Palette", "View", "Ctrl+Shift+P", [this]() {
        if (m_command_palette)
            m_command_palette->ToggleOpen();
    } });
    cp.Register({ "Reset UI Scale", "View", "", [this]() {
        m_ui_scale = 1.0f;
    } });
    cp.Register({ "Switch to Level Design Workspace", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            m_workspace_manager.ApplyWorkspace(WorkspaceManager::Workspace::LevelDesign));
    } });
    cp.Register({ "Switch to Scripting Workspace", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            m_workspace_manager.ApplyWorkspace(WorkspaceManager::Workspace::Scripting));
    } });
    cp.Register({ "Switch to Shading & Assets Workspace", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            m_workspace_manager.ApplyWorkspace(WorkspaceManager::Workspace::ShadingAndAssets));
    } });
    cp.Register({ "Reset View to Default Workspace", "Workspace", "", [this]() {
        m_workspace_manager.ResetToDefault();
        m_script_editor->RequestDockCodeWindow(0);
    } });
    cp.Register({ "Save Current Layout as Default", "Workspace", "", [this]() {
        m_workspace_manager.RequestSaveCurrent();
    } });
    cp.Register({ "Save Scene", "File", "Ctrl+S", [this]() { SaveScene(); } });
    cp.Register({ "Open Scene", "File", "Ctrl+O", [this]() { OpenScene(); } });
    cp.Register({ "New Scene", "File", "", [this]() { NewScene(); } });
    cp.Register({ "Save Scene As...", "File", "", [this]() { OpenSaveAsModal(); } });
    cp.Register({ "Enter Play Mode", "Transport", "", [this]() { EnterPlayMode(); } });

    // Content Browser: dockable asset manager over assets/ (scene/prefab
    // load & spawn, script open, folder/file ops). Its window is docked by
    // name in both workspace presets; the command toggles visibility so the
    // docked slot can be dismissed and restored.
    m_content_browser = new ContentBrowserPanel(m_scene_manager, m_script_editor);
    m_content_browser->on_load_scene = [this](const std::string &path) { LoadSceneFile(path); };
    m_panels.push_back(std::shared_ptr<ContentBrowserPanel>(m_content_browser));
    cp.Register({ "Toggle Content Browser", "View", "", [this]() {
        if (m_content_browser)
            m_content_browser->ToggleVisible();
    } });

    // Console: dockable log window fed by the shared Console sink (Lua print,
    // script exceptions, engine messages, and redirected stdout/stderr).
    m_console_panel = new ConsolePanel();
    m_panels.push_back(std::shared_ptr<ConsolePanel>(m_console_panel));
    cp.Register({ "Toggle Console", "View", "", [this]() {
        if (m_console_panel)
            m_console_panel->ToggleVisible();
    } });

    // Flush anything that printed during startup (SDL/ImGui chatter, test
    // output) so the first rendered frame already shows it.
    Console::Instance().DrainPipes();
    ConsoleInfo(std::string("Singularity Engine ") + title + " started");

    RecreateViewportTarget(800, 600);

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
            {
                // In play mode Esc exits the game view; in the editor it quits.
                if (m_state == EngineState::Play)
                    ExitPlayMode();
                else
                    m_running = false;
            }
            if (event.type == SDL_KEYDOWN && m_state == EngineState::Editor && m_gizmo)
            {
                // Gizmo mode hotkeys: 1 = translate, 2 = rotate, 3 = scale.
                if (event.key.keysym.sym == SDLK_1)
                    m_gizmo->SetMode(GizmoMode::Translate);
                else if (event.key.keysym.sym == SDLK_2)
                    m_gizmo->SetMode(GizmoMode::Rotate);
                else if (event.key.keysym.sym == SDLK_3)
                    m_gizmo->SetMode(GizmoMode::Scale);
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F4 && m_script_editor)
            {
                // F4 toggles the script editor in any mode (View menu equivalent).
                m_script_editor->ToggleVisible();
            }
            if (event.type == SDL_MOUSEWHEEL)
                m_camera_scroll += (float)event.wheel.preciseY;
            if (event.type == SDL_WINDOWEVENT)
            {
                // Keep the cached window size in sync so GetWidth/GetHeight and
                // the stats readout report the current dimensions.
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    m_window->OnResize(event.window.data1, event.window.data2);

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

        // Pull any redirected stdout/stderr bytes into the console every frame
        // (cheap: a single pipe peek when empty). The ConsolePanel also drains,
        // but doing it here keeps piped output flowing even while it is hidden.
        Console::Instance().DrainPipes();

        // Global command palette: Ctrl+Shift+P toggles it in editor mode.
        // IsKeyChordPressed reports the chord once per frame; the palette's
        // own OnImGuiRender draws the modal and handles navigation.
        if (m_state == EngineState::Editor && m_command_palette &&
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P))
        {
            m_command_palette->ToggleOpen();
        }

        // Quick duplication: Ctrl+D clones the selected entity (picked in the
        // Hierarchy or the viewport) as a sibling under its parent and selects
        // the clone. Skipped while a text field is focused so the chord can't
        // fire mid-typing (e.g. while naming a prefab).
        if (m_state == EngineState::Editor &&
            !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
        {
            DuplicateSelection();
        }

        // --- Main menu bar (transport controls + editor chrome) ---
        const bool playing = (m_state == EngineState::Play);

        if (ImGui::BeginMainMenuBar())
        {
            if (playing)
            {
                // Game view: only the Stop button and status remain visible.
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.18f, 0.18f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.25f, 0.25f, 1.00f));
                if (ImGui::Button(" Stop "))
                    ExitPlayMode();
                ImGui::PopStyleColor(2);
                ImGui::SameLine();
                ImGui::TextDisabled("PLAYING");
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.25f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.68f, 0.34f, 1.00f));
                if (ImGui::Button(" Play "))
                    EnterPlayMode();
                ImGui::PopStyleColor(2);
                ImGui::SameLine();

                // Gizmo mode selector (1/2/3 also work while hovering the editor).
                if (m_gizmo)
                {
                    const char *mode_names[3] = { "Move", "Rotate", "Scale" };
                    GizmoMode modes[3] = { GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale };
                    ImGui::TextUnformatted("Gizmo:");
                    ImGui::SameLine();
                    for (int i = 0; i < 3; ++i)
                    {
                        if (i > 0)
                            ImGui::SameLine();
                        const bool is_active = (m_gizmo->mode == modes[i]);
                        if (is_active)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.38f, 1.00f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.45f, 1.00f));
                        }
                        if (ImGui::Button(mode_names[i]))
                            m_gizmo->SetMode(modes[i]);
                        if (is_active)
                            ImGui::PopStyleColor(2);
                    }
                    ImGui::SameLine();

                    // Grid snap toggle. When on (or when Ctrl is held during a
                    // gizmo drag), translate/rotate/scale snap to the steps
                    // configured in the Editor Settings window.
                    const bool snap_on = m_snap.enabled;
                    if (snap_on)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.38f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.45f, 1.00f));
                    }
                    if (ImGui::Button(snap_on ? "Snap: ON" : "Snap: OFF"))
                        m_snap.enabled = !m_snap.enabled;
                    if (snap_on)
                        ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Toggle grid snapping. Hold Ctrl during a gizmo drag to "
                            "snap temporarily. Steps are set in Editor Settings.");
                    ImGui::SameLine();
                }

                // Workspace selector: switches the whole dock layout to the
                // arrangement best suited for the current task. Managed by
                // WorkspaceManager, which persists the active workspace.
                if (ImGui::BeginMenu("Workspace"))
                {
                    const WorkspaceManager::Workspace current = m_workspace_manager.GetWorkspace();
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::LevelDesign),
                            nullptr, current == WorkspaceManager::Workspace::LevelDesign))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            m_workspace_manager.ApplyWorkspace(WorkspaceManager::Workspace::LevelDesign));
                    }
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::Scripting),
                            nullptr, current == WorkspaceManager::Workspace::Scripting))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            m_workspace_manager.ApplyWorkspace(WorkspaceManager::Workspace::Scripting));
                    }
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::ShadingAndAssets),
                            nullptr, current == WorkspaceManager::Workspace::ShadingAndAssets))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            m_workspace_manager.ApplyWorkspace(WorkspaceManager::Workspace::ShadingAndAssets));
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset to Level Design"))
                    {
                        m_workspace_manager.ResetToDefault();
                        m_script_editor->RequestDockCodeWindow(0);
                    }
                    if (ImGui::MenuItem("Save Current Layout as Default", nullptr,
                                        m_workspace_manager.HasSavedLayout()))
                        m_workspace_manager.RequestSaveCurrent();
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                        SaveScene();
                    if (ImGui::MenuItem("Save Scene As..."))
                        OpenSaveAsModal();
                    if (ImGui::MenuItem("Open Scene", "Ctrl+O"))
                        OpenScene();
                    if (ImGui::MenuItem("New Scene"))
                        NewScene();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit"))
                        m_running = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                {
                    ImGui::SliderFloat("UI Scale", &m_ui_scale, 0.75f, 2.0f, "%.2fx");
                    if (ImGui::Button("Reset##UiScale"))
                        m_ui_scale = 1.0f;

                    ImGui::Separator();
                    if (m_script_editor)
                    {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Script Editor", "F4", m_script_editor->IsVisible()))
                            m_script_editor->ToggleVisible();
                    }

                    if (m_content_browser)
                    {
                        if (ImGui::MenuItem("Content Browser", nullptr,
                                            m_content_browser->IsVisible()))
                            m_content_browser->ToggleVisible();
                    }

                    if (m_console_panel)
                    {
                        if (ImGui::MenuItem("Console", nullptr,
                                            m_console_panel->IsVisible()))
                            m_console_panel->ToggleVisible();
                    }

                    if (m_settings_panel)
                    {
                        if (ImGui::MenuItem("Editor Settings", nullptr,
                                            m_settings_panel->IsVisible()))
                            m_settings_panel->ToggleVisible();
                    }

                    if (m_command_palette)
                    {
                        if (ImGui::MenuItem("Command Palette", "Ctrl+Shift+P"))
                            m_command_palette->ToggleOpen();
                    }
                    ImGui::EndMenu();
                }
            }

            // Right-aligned save/open status message.
            if (!m_scene_status.empty())
            {
                float status_w = ImGui::CalcTextSize(m_scene_status.c_str()).x + 20.0f;
                ImGui::SameLine(ImGui::GetWindowWidth() - status_w);
                ImGui::TextUnformatted(m_scene_status.c_str());
            }
            ImGui::EndMainMenuBar();
        }

        // Apply a changed UI scale: rebuild style metrics and font scale.
        // Font scale is the user zoom divided by the DPI factor (the fonts were
        // baked at dpi resolution), so logical layout stays consistent.
        if (m_ui_scale != m_applied_ui_scale)
        {
            Theme::ConfigureStyle(m_ui_scale, m_theme_colors);
            ImGui::GetIO().FontGlobalScale = m_ui_scale / m_dpi_scale;
            m_applied_ui_scale = m_ui_scale;
        }

        if (playing)
        {
            // Runtime viewport isolation: no dockspace, no editor panels, no
            // selection outlines. The viewport fills the window as a game view.
            // Every non-essential panel (script editor, content browser,
            // console, inspector) was hidden on EnterPlayMode and is restored
            // on exit, so the play session is a clean, uninterrupted view.
            m_viewport->SetIsolated(true);
            m_viewport->OnImGuiRender((float)dt);
        }
        else
        {
            m_viewport->SetIsolated(false);

            // Master dockspace: one transparent full-screen host window, so
            // every panel docks into a single unified workspace (see
            // WorkspaceManager). Rebuilds itself after play mode or workspace
            // changes.
            m_workspace_manager.DrawDockspace();

            for (auto &panel : m_panels)
                panel->OnImGuiRender((float)dt);

            DrawSaveAsModal();
        }

        ImGui::Render();

        // Persist a "Save Current Layout as Default" capture now that the frame
        // (and its window state) is fully serializable.
        m_workspace_manager.FinalizeSave();

        int vp_w = m_viewport ? m_viewport->GetWidth() : 0;
        int vp_h = m_viewport ? m_viewport->GetHeight() : 0;
        Uint32 win_flags = SDL_GetWindowFlags(m_window->GetNativeWindow());
        const bool minimized = (win_flags & SDL_WINDOW_MINIMIZED) != 0;

        // The viewport render target is supersampled to the window's *physical*
        // pixel size, not the ImGui logical size. On a high-DPI display the
        // renderer output is a multiple of the logical window size
        // (io.DisplayFramebufferScale, e.g. 2.0 on a 200% display): rendering
        // the 3D pass at logical resolution and letting the final present scale
        // it up produces a blurry, "upscaled" image. Creating the target at
        // physical resolution keeps the 3D geometry 1:1 with the framebuffer,
        // and the extra kViewportSupersample factor anti-aliases edges when
        // ImGui downscales it into the viewport rect (SDL2 renderer has no
        // MSAA for off-screen targets, so this is the AA strategy).
        ImVec2 fb_scale = ImGui::GetIO().DisplayFramebufferScale;
        const float dpi = (fb_scale.x > 0.0f && fb_scale.y > 0.0f)
            ? std::max(fb_scale.x, fb_scale.y) : 1.0f;
        int target_w = (int)std::ceil(vp_w * dpi * kViewportSupersample);
        int target_h = (int)std::ceil(vp_h * dpi * kViewportSupersample);

        // Recreate the off-screen target when it is missing, its size is stale,
        // or a window event invalidated its GPU resources. Never recreate while
        // minimized: the GPU may refuse to build targets for a hidden window.
        if (!minimized &&
            (m_recreate_viewport || !m_viewport_target ||
             target_w != m_viewport_target_w || target_h != m_viewport_target_h))
        {
            m_recreate_viewport = false;
            RecreateViewportTarget(target_w, target_h);
        }

        UpdateCameraControls((float)dt);

        // Gameplay scripts run only during play: OnUpdate(dt) fires for every
        // bound entity before the 3D pass renders the resulting transforms.
        // The physics step then detects solid/trigger overlaps at those new
        // positions, resolves solid penetration, and dispatches collision /
        // trigger Enter/Exit events to the script session.
        if (m_state == EngineState::Play)
        {
            if (m_script_engine)
                m_script_engine->UpdateSession(*m_scene, (float)dt);
            if (m_physics)
                m_physics->Step(*m_scene, *m_script_engine);
        }

        // Editor interaction: viewport picking + gizmo dragging. Skipped in
        // play mode and while the RMB fly camera is active (cursor captured).
        if (m_state == EngineState::Editor && !m_flying && m_gizmo && m_viewport)
        {
            Vec3 cam_pos;
            float fov, pitch, yaw, near_p, far_p;
            Mat4 view_proj;
            if (BuildViewProj(view_proj, cam_pos, fov, pitch, yaw, near_p, far_p))
            {
                GizmoFrame gf;
                gf.scene = m_scene;
                gf.selection = m_selection;
                gf.meshes = m_mesh_library;
                gf.active_camera_id = FindActiveCamera() ? FindActiveCamera()->id : -1;
                gf.vp_width = (float)m_viewport_target_w;
                gf.vp_height = (float)m_viewport_target_h;
                gf.dpi_scale = dpi * kViewportSupersample;
                gf.hovered = m_viewport->IsHovered();
                ImVec2 img_min = m_viewport->GetImageMin();
                ImVec2 img_size = m_viewport->GetImageSize();
                ImVec2 mouse = ImGui::GetMousePos();
                gf.mouse_x = (img_size.x > 1.0f)
                    ? (mouse.x - img_min.x) * (gf.vp_width / img_size.x) : 0.0f;
                gf.mouse_y = (img_size.y > 1.0f)
                    ? (mouse.y - img_min.y) * (gf.vp_height / img_size.y) : 0.0f;
                gf.cam_pos = cam_pos;
                gf.cam_pitch = pitch;
                gf.cam_yaw = yaw;
                gf.cam_fov = fov;
                gf.near_p = near_p;
                gf.view_proj = view_proj;
                gf.dt = (float)dt;
                // Grid snapping: persistent toggle OR hold Ctrl while dragging.
                gf.snap_translation = m_snap.translation;
                gf.snap_rotation = m_snap.rotation;
                gf.snap_scale = m_snap.scale;
                gf.snap_active = m_snap.enabled || ImGui::GetIO().KeyCtrl;
                m_gizmo->Update(gf);
            }
        }

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
    m_script_editor = nullptr;
    m_command_palette = nullptr;
    m_settings_panel = nullptr;
    m_content_browser = nullptr;
    m_console_panel = nullptr;
    m_inspector_panel = nullptr;

    // Tear the console pipes down before the window/SDL go away so no further
    // stdout traffic can target a closed pipe.
    Console::Instance().StopRedirect();

    if (m_viewport_target)
    {
        SDL_DestroyTexture(m_viewport_target);
        m_viewport_target = nullptr;
    }

    delete m_gizmo;
    m_gizmo = nullptr;

    // ScriptEngine's destructor tears down the Lua VM (and with it any
    // userdata pointing into entities), so release it before the scene.
    delete m_script_engine;
    m_script_engine = nullptr;

    delete m_mesh_library;
    m_mesh_library = nullptr;

    // GPU textures must go while the renderer still owns its device; the
    // library tears every SDL_Texture down before the window/SDL quit below.
    if (m_texture_library)
    {
        m_texture_library->DestroyAll();
        delete m_texture_library;
        m_texture_library = nullptr;
    }

    delete m_material_library;
    m_material_library = nullptr;

    delete m_selection;
    m_selection = nullptr;

    // m_scene aliases m_scene_manager->GetScene(); the manager owns it.
    delete m_scene_manager;
    m_scene_manager = nullptr;
    m_scene = nullptr;

    // Flush any pending layout capture while the ImGui context is still alive
    // (SaveIniSettingsToMemory needs it), then persist the preset state.
    m_workspace_manager.FinalizeSave();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    delete m_window;
    m_window = nullptr;

    SDL_Quit();
    m_running = false;
}
